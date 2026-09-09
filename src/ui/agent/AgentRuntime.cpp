#include "ui/agent/AgentRuntime.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <utility>

namespace {

using modbuslens::agent::AgentRunLocalError;
using modbuslens::agent::AgentToolName;
using modbuslens::agent::kMaxAgentQuestionChars;
using modbuslens::agent::kMaxAgentToolRounds;
using modbuslens::agent::kMaxAgentTotalToolCalls;

QString localErrorMessage(modbuslens::agent::AgentRunLocalError code)
{
    switch (code) {
    case AgentRunLocalError::InvalidQuestion: return QStringLiteral("问题为空或超过 %1 字符").arg(kMaxAgentQuestionChars);
    case AgentRunLocalError::MalformedToolCall: return QStringLiteral("工具调用格式无效");
    case AgentRunLocalError::UnknownTool: return QStringLiteral("未知工具");
    case AgentRunLocalError::InvalidArguments: return QStringLiteral("工具参数无效");
    case AgentRunLocalError::TransactionNotFound: return QStringLiteral("事务不存在");
    case AgentRunLocalError::ToolRoundLimitExceeded: return QStringLiteral("已达到工具轮次上限");
    case AgentRunLocalError::ToolCallLimitExceeded: return QStringLiteral("已达到工具调用总数上限");
    }
    return QStringLiteral("未知错误");
}

} // namespace

namespace modbuslens::agent {

AgentRunFailure AgentRunFailure::fromLocal(AgentRunLocalError code, QString message)
{
    return AgentRunFailure{.providerError = false, .localError = code,
                           .providerCode = AiDiagnosisErrorCode::NetworkError,
                           .message = std::move(message)};
}

AgentRunFailure AgentRunFailure::fromProvider(AiDiagnosisErrorCode code, QString message)
{
    return AgentRunFailure{.providerError = true,
                           .localError = AgentRunLocalError::InvalidQuestion,
                           .providerCode = code, .message = std::move(message)};
}

} // namespace modbuslens::agent

AgentRuntime::AgentRuntime(ModelScopeAgentClient* client, QObject* parent)
    : QObject(parent), client_(client)
{
    connect(client_, &ModelScopeAgentClient::roundSucceeded,
            this, &AgentRuntime::handleRoundSucceeded);
    connect(client_, &ModelScopeAgentClient::roundFailed,
            this, &AgentRuntime::handleRoundFailed);
}

void AgentRuntime::start(const modbuslens::agent::AgentRunRequest& request)
{
    // User question is UNTRUSTED free text: reject empty/whitespace/overlong
    // verbatim (never truncate-and-send, never tokenize).
    const QString question = request.userQuestion;
    if (question.trimmed().isEmpty() || question.size() > kMaxAgentQuestionChars) {
        emit runFailed(request.runGeneration,
                       modbuslens::agent::AgentRunFailure::fromLocal(
                           AgentRunLocalError::InvalidQuestion,
                           localErrorMessage(AgentRunLocalError::InvalidQuestion)));
        return;
    }
    if (!client_->isConfigured()) {
        emit runFailed(request.runGeneration,
                       modbuslens::agent::AgentRunFailure::fromProvider(
                           AiDiagnosisErrorCode::NotConfigured,
                           QStringLiteral("ModelScope Agent 客户端未配置")));
        return;
    }

    // Same-batch newer run SUPERSEDES any in-flight run: invalidate + abort
    // the old one silently (it was replaced, not cancelled by the user).
    if (isBusy()) {
        ++currentAgentGeneration_;
        client_->cancel(AiAbortReason::SupersededRequest);
    }

    prompt_ = modbuslens::agent::buildAgentPrompt();
    context_ = request.context;
    // The context's own revision is the ONE source of batch identity — a
    // second request-level revision field cannot exist (removed by review).
    capturedBatchRevision_ = request.context.capturedBatchRevision;
    runGeneration_ = request.runGeneration;
    currentBatchRevision_ = request.context.capturedBatchRevision;
    currentAgentGeneration_ = request.runGeneration;
    toolRounds_ = 0;
    totalToolCalls_ = 0;

    messages_ = QJsonArray{
        QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                    {QStringLiteral("content"), prompt_.systemInstructions}},
        QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                    {QStringLiteral("content"), question}},
    };
    state_ = State::WaitingForModel;
    sendNextRequest();
}

void AgentRuntime::cancel()
{
    if (!isBusy()) {
        return;
    }
    const std::uint64_t cancelledRun = runGeneration_;
    ++currentAgentGeneration_; // invalidate: late deliveries go stale
    state_ = State::Idle;
    client_->cancel(AiAbortReason::UserCancel);
    emit runCancelled(cancelledRun);
}

bool AgentRuntime::isBusy() const
{
    return state_ == State::WaitingForModel || state_ == State::ExecutingTools;
}

void AgentRuntime::setCurrentBatchRevision(std::uint64_t revision)
{
    currentBatchRevision_ = revision;
}

void AgentRuntime::setCurrentAgentGeneration(std::uint64_t generation)
{
    currentAgentGeneration_ = generation;
}

bool AgentRuntime::isStale(std::uint64_t runGeneration) const
{
    return runGeneration != runGeneration_
        || runGeneration != currentAgentGeneration_
        || capturedBatchRevision_ != currentBatchRevision_;
}

void AgentRuntime::failLocal(modbuslens::agent::AgentRunLocalError code, QString message)
{
    state_ = State::Failed;
    emit runFailed(runGeneration_,
                   modbuslens::agent::AgentRunFailure::fromLocal(code, std::move(message)));
}

void AgentRuntime::failProvider(AiDiagnosisErrorCode code, QString message)
{
    state_ = State::Failed;
    emit runFailed(runGeneration_,
                   modbuslens::agent::AgentRunFailure::fromProvider(code, std::move(message)));
}

void AgentRuntime::finishCompleted(const QString& finalAnswer)
{
    state_ = State::Completed;
    emit runCompleted(runGeneration_, finalAnswer);
}

void AgentRuntime::sendNextRequest()
{
    state_ = State::WaitingForModel;
    client_->requestRound(runGeneration_, messages_, prompt_.toolSchemas);
}

void AgentRuntime::handleRoundSucceeded(std::uint64_t runGeneration,
                                        QJsonObject assistantMessage)
{
    if (isStale(runGeneration)) {
        state_ = State::Idle; // silent stale discard
        return;
    }

    const QJsonValue toolCallsValue = assistantMessage.value(QLatin1String("tool_calls"));
    const QJsonArray toolCalls =
        toolCallsValue.isArray() ? toolCallsValue.toArray() : QJsonArray{};

    if (toolCalls.isEmpty()) {
        // Final round: content is the answer. reasoning_content is NOT the
        // answer (T011 contract) — unusable content is a malformed reply.
        // No tool_calls: the answer must be usable final content. Missing /
        // empty / whitespace-only content is a provider invalid-response —
        // NEVER a runCompleted with an empty answer.
        const QString content = assistantMessage.value(QLatin1String("content")).toString();
        if (content.trimmed().isEmpty()) {
            failProvider(AiDiagnosisErrorCode::InvalidResponse,
                         QStringLiteral("响应中无可用最终回答"));
            return;
        }
        finishCompleted(content);
        return;
    }

    // Tool round: bump counters FIRST, then enforce both hard limits BEFORE
    // any execution — an over-limit batch executes NOTHING.
    state_ = State::ExecutingTools;
    ++toolRounds_;
    totalToolCalls_ += toolCalls.size();
    if (toolRounds_ > kMaxAgentToolRounds) {
        failLocal(AgentRunLocalError::ToolRoundLimitExceeded,
                  localErrorMessage(AgentRunLocalError::ToolRoundLimitExceeded));
        return;
    }
    if (totalToolCalls_ > kMaxAgentTotalToolCalls) {
        failLocal(AgentRunLocalError::ToolCallLimitExceeded,
                  localErrorMessage(AgentRunLocalError::ToolCallLimitExceeded));
        return;
    }

    // Validate the WHOLE batch (structure, uniqueness, whitelist, arguments)
    // before executing ANY of it — transaction-like, no partial execution.
    struct ParsedCall {
        QString id;
        QJsonObject arguments;
        AgentToolName tool{};
    };
    std::vector<ParsedCall> parsed;
    QSet<QString> seenIds;
    for (const QJsonValue& value : toolCalls) {
        if (!value.isObject()) {
            failLocal(AgentRunLocalError::MalformedToolCall,
                      localErrorMessage(AgentRunLocalError::MalformedToolCall));
            return;
        }
        const QJsonObject call = value.toObject();
        const QString id = call.value(QLatin1String("id")).toString();
        const QString type = call.value(QLatin1String("type")).toString();
        const QJsonObject function = call.value(QLatin1String("function")).toObject();
        const QString name = function.value(QLatin1String("name")).toString();
        const QString argumentsText = function.value(QLatin1String("arguments")).toString();

        if (type != QLatin1String("function") || id.isEmpty() || name.isEmpty()) {
            failLocal(AgentRunLocalError::MalformedToolCall,
                      localErrorMessage(AgentRunLocalError::MalformedToolCall));
            return;
        }
        if (seenIds.contains(id)) {
            failLocal(AgentRunLocalError::MalformedToolCall,
                      localErrorMessage(AgentRunLocalError::MalformedToolCall));
            return;
        }
        seenIds.insert(id);

        QJsonParseError parseError{};
        const QJsonDocument argsDoc =
            QJsonDocument::fromJson(argumentsText.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !argsDoc.isObject()) {
            failLocal(AgentRunLocalError::MalformedToolCall,
                      localErrorMessage(AgentRunLocalError::MalformedToolCall));
            return;
        }

        const auto tool = modbuslens::agent::agentToolNameFromString(name.toStdString());
        if (!tool.has_value()) {
            failLocal(AgentRunLocalError::UnknownTool,
                      localErrorMessage(AgentRunLocalError::UnknownTool));
            return;
        }
        const QJsonObject arguments = argsDoc.object();
        if (const auto validationError =
                modbuslens::agent::validateAgentToolCall(context_, *tool, arguments);
            validationError.has_value()) {
            switch (*validationError) {
            case modbuslens::agent::AgentToolErrorCode::InvalidArguments:
                failLocal(AgentRunLocalError::InvalidArguments,
                          localErrorMessage(AgentRunLocalError::InvalidArguments));
                return;
            case modbuslens::agent::AgentToolErrorCode::TransactionNotFound:
                failLocal(AgentRunLocalError::TransactionNotFound,
                          localErrorMessage(AgentRunLocalError::TransactionNotFound));
                return;
            case modbuslens::agent::AgentToolErrorCode::UnknownTool:
                failLocal(AgentRunLocalError::UnknownTool,
                          localErrorMessage(AgentRunLocalError::UnknownTool));
                return;
            }
        }
        parsed.push_back(ParsedCall{id, arguments, *tool});
    }

    // All validated: execute in provider order, one tool message per call
    // each carrying its OWN tool_call_id (never invented).
    messages_.append(assistantMessage);
    for (const ParsedCall& call : parsed) {
        const auto result = modbuslens::agent::dispatchAgentTool(
            context_, modbuslens::agent::agentToolNameString(call.tool), call.arguments);
        const QJsonObject resultJson = std::visit(
            [](const auto& value) -> QJsonObject {
                return modbuslens::agent::toJsonObject(value);
            },
            result);
        messages_.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("tool")},
            {QStringLiteral("tool_call_id"), call.id},
            {QStringLiteral("content"),
             QString::fromUtf8(QJsonDocument(resultJson).toJson(QJsonDocument::Compact))},
        });
    }
    sendNextRequest();
}

void AgentRuntime::handleRoundFailed(std::uint64_t runGeneration,
                                     AiDiagnosisErrorCode code,
                                     QString sanitizedMessage)
{
    if (isStale(runGeneration)) {
        state_ = State::Idle; // silent stale discard
        return;
    }
    failProvider(code, std::move(sanitizedMessage));
}