#include "ui/AnalysisController.h"

#include "core/analysis/TransactionStatistics.h"
#include "core/active/WriteDraftParsing.h"
#include "core/active/ProductWriteCapability.h"
#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/replay/ReplayAnalysis.h"
#include "core/replay/ReplayLog.h"
#include "core/diagnosis/DiagnosisContext.h"
#include "core/diagnosis/RuleBasedDiagnosis.h"
#include "core/serial/SerialTransactionSession.h"
#include "core/simulator/SimulatedSlave.h"
#include "core/simulator/SimulationFault.h"
#include "ui/ai/DiagnosisPromptBuilder.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSerialPortInfo>
#include <QUrl>

#include <algorithm>
#include <array>
#include <string_view>
#include <variant>
#include <vector>

namespace {

// Bounded display contract: the UI adapter narrows core size_t counters to
// int for QML. Demo/history batches are far below INT_MAX; core stays size_t.
int toInt(std::size_t value)
{
    return static_cast<int>(value);
}

// Build a Function 0x03 read request (deterministic demo fixture, not a
// public protocol API — see T008 Part B rule B).
modbuslens::core::ModbusRtuFrame makeFc03Read(
    std::uint8_t address, std::uint16_t start, std::uint16_t quantity)
{
    return modbuslens::core::ModbusRtuFrame{
        .address = address,
        .functionCode = 0x03,
        .data = {
            static_cast<std::uint8_t>(start >> 8),
            static_cast<std::uint8_t>(start & 0xFF),
            static_cast<std::uint8_t>(quantity >> 8),
            static_cast<std::uint8_t>(quantity & 0xFF),
        },
    };
}

// ---- Replay error presentation adapters (Core -> user-readable QString).
// Core stays enum + line/index; this file owns the human phrasing. ----

QString parseErrorMessage(const modbuslens::core::ReplayParseError& error)
{
    // Chinese phrases are QStringLiteral (UTF-16 from source) — never a
    // const char* + QLatin1String combo, which would mojibake UTF-8 bytes.
    QString phrase = QStringLiteral("回放日志无效");
    switch (error.code) {
    case modbuslens::core::ReplayParseErrorCode::MissingHeader:
        phrase = QStringLiteral("缺少日志头");
        break;
    case modbuslens::core::ReplayParseErrorCode::UnsupportedVersion:
        phrase = QStringLiteral("日志版本不受支持");
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidHeader:
        phrase = QStringLiteral("日志头无效");
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidRecord:
        phrase = QStringLiteral("记录无效");
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidElapsed:
        phrase = QStringLiteral("耗时值无效");
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidHex:
        phrase = QStringLiteral("十六进制数据无效");
        break;
    case modbuslens::core::ReplayParseErrorCode::MissingRequest:
        phrase = QStringLiteral("请求字段为空");
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidResponseField:
        phrase = QStringLiteral("响应字段无效");
        break;
    }
    // lineNumber == 0 means "no specific offending line" (e.g. a log with no
    // header at all) — never render a meaningless "line 0".
    if (error.lineNumber == 0) {
        return QStringLiteral("回放解析错误：%1").arg(phrase);
    }
    return QStringLiteral("回放解析错误（第 %1 行）：%2")
        .arg(error.lineNumber)
        .arg(phrase);
}

QString executionErrorMessage(
    const modbuslens::core::ReplayExecutionError& error)
{
    QString phrase = QStringLiteral("回放分析失败");
    switch (error.code) {
    case modbuslens::core::ReplayExecutionErrorCode::InvalidRequestWire:
        phrase = QStringLiteral("请求报文无效");
        break;
    }
    // Core indexing is 0-based; humans count transactions from 1.
    return QStringLiteral("回放分析错误（事务 %1）：%2")
        .arg(error.transactionIndex + 1)
        .arg(phrase);
}

QString agentFailureText(const modbuslens::agent::AgentRunFailure& failure)
{
    using modbuslens::agent::AgentRunLocalError;
    if (failure.providerError) {
        switch (failure.providerCode) {
        case AiDiagnosisErrorCode::NotConfigured: return QStringLiteral("ModelScope 尚未配置，请先配置 API Key 和模型。");
        case AiDiagnosisErrorCode::Unauthorized: return QStringLiteral("ModelScope API Key 无效或没有访问权限。");
        case AiDiagnosisErrorCode::RateLimited: return QStringLiteral("ModelScope 请求受限或额度不足，请检查账户状态后重试。");
        case AiDiagnosisErrorCode::Timeout: return QStringLiteral("模型请求超时，请稍后重试。");
        case AiDiagnosisErrorCode::NetworkError: return QStringLiteral("无法连接到 ModelScope，请检查网络连接。");
        case AiDiagnosisErrorCode::ProviderRequestError: return QStringLiteral("ModelScope 请求失败，请稍后重试。");
        case AiDiagnosisErrorCode::ServerError: return QStringLiteral("ModelScope 服务暂时不可用，请稍后重试。");
        case AiDiagnosisErrorCode::InvalidResponse: return QStringLiteral("模型返回了无法处理的响应格式。");
        case AiDiagnosisErrorCode::Busy: return QStringLiteral("已有请求进行中。");
        default: return QStringLiteral("Agent 请求失败。");
        }
    }
    switch (failure.localError) {
    case AgentRunLocalError::InvalidQuestion: return QStringLiteral("问题不能为空，且最多 1000 字符。");
    case AgentRunLocalError::UnknownTool: return QStringLiteral("Agent 尝试调用不存在的工具。");
    case AgentRunLocalError::MalformedToolCall: return QStringLiteral("模型返回的工具调用格式无效。");
    case AgentRunLocalError::InvalidArguments: return QStringLiteral("模型返回的工具参数无效。");
    case AgentRunLocalError::TransactionNotFound: return QStringLiteral("模型请求的事务不存在。");
    case AgentRunLocalError::ToolRoundLimitExceeded: return QStringLiteral("工具调用轮次已达上限。");
    case AgentRunLocalError::ToolCallLimitExceeded: return QStringLiteral("工具调用次数已达上限。");
    }
    return QStringLiteral("Agent 运行失败。");
}

} // namespace

AnalysisController::AnalysisController(QObject* parent)
    : QObject(parent),
      agentClient_(this),
      agentRuntime_(&agentClient_, this)
{
    // Same source of truth as T007: an empty batch produces the zeroed
    // snapshot with undefined successRate/latency (hasX == false).
    applySnapshot(summarizeTransactions(
        std::span<const modbuslens::core::TransactionAnalysis>{}));

    // The adapter is the ONLY QSerialPort owner in the app layer; QML never
    // sees it. Its signals are the only entry doors for serial results/errors.
    // M10-A: the controller talks to the SERIAL TRANSPORT SEAM, whose
    // production implementation is that adapter — tests may repoint it (see
    // setSerialTransport) without touching any production behaviour.
    serialTransport_ = &serialAdapter_;
    connectSerialTransportSignals(*serialTransport_);

    // AI (T011 Part B): production config comes from the process environment
    // ONLY (BYOK). QML never sees the token — just aiConfigured.
    ModelScopeClientConfig productionConfig;
    if (buildModelScopeProductionConfig(productionConfig)) {
        // ONE validated config source feeds BOTH cloud clients (UI-AG19):
        // aiConfigured_==true can never coexist with an unconfigured Agent
        // client.
        aiClient_.configure(productionConfig);
        agentClient_.configure(productionConfig);
        aiConfigured_ = true;
        aiModelName_ = productionConfig.modelId;
    }
    connect(&aiClient_, &ModelScopeDiagnosisClient::diagnosisSucceeded,
            this, &AnalysisController::handleAiSucceeded);
    connect(&aiClient_, &ModelScopeDiagnosisClient::diagnosisFailed,
            this, &AnalysisController::handleAiFailed);
    connect(&agentRuntime_, &AgentRuntime::runCompleted,
            this, &AnalysisController::handleAgentCompleted);
    connect(&agentRuntime_, &AgentRuntime::runFailed,
            this, &AnalysisController::handleAgentFailed);
    connect(&agentRuntime_, &AgentRuntime::runCancelled,
            this, &AnalysisController::handleAgentCancelled);
}

int AnalysisController::observedCount() const
{
    return toInt(statistics_.observedCount);
}

int AnalysisController::pendingCount() const
{
    return toInt(statistics_.pendingCount);
}

int AnalysisController::completedCount() const
{
    return toInt(statistics_.completedCount);
}

int AnalysisController::successCount() const
{
    return toInt(statistics_.successCount);
}

int AnalysisController::exceptionCount() const
{
    return toInt(statistics_.exceptionCount);
}

int AnalysisController::crcErrorCount() const
{
    return toInt(statistics_.crcErrorCount);
}

int AnalysisController::timeoutCount() const
{
    return toInt(statistics_.timeoutCount);
}

int AnalysisController::protocolErrorCount() const
{
    return toInt(statistics_.protocolErrorCount);
}

int AnalysisController::expectedNoResponseCount() const
{
    return toInt(statistics_.expectedNoResponseCount);
}

bool AnalysisController::hasSuccessRate() const
{
    return statistics_.successRate.has_value();
}

double AnalysisController::successRate() const
{
    // 0.0 is only a safe placeholder when hasSuccessRate() is false — QML
    // must branch on hasSuccessRate first (never show "0%" for no data).
    return statistics_.successRate.value_or(0.0);
}

bool AnalysisController::hasAverageSuccessLatency() const
{
    return statistics_.averageSuccessLatencyMs.has_value();
}

double AnalysisController::averageSuccessLatencyMs() const
{
    return statistics_.averageSuccessLatencyMs.value_or(0.0);
}

QAbstractItemModel* AnalysisController::transactionModel()
{
    return &transactionModel_;
}

bool AnalysisController::hasReplayError() const
{
    return hasReplayError_;
}

QString AnalysisController::replayErrorMessage() const
{
    return replayErrorMessage_;
}

bool AnalysisController::hasReplayNotice() const
{
    return hasReplayNotice_;
}

QString AnalysisController::replayNoticeText() const
{
    return replayNoticeText_;
}

QString AnalysisController::modeLabel() const
{
    return modeLabel_;
}

QString AnalysisController::sourceLabel() const
{
    return sourceLabel_;
}

void AnalysisController::setReplayError(const QString& message)
{
    hasReplayError_ = true;
    replayErrorMessage_ = message;
    emit replayStateChanged();
}

void AnalysisController::clearReplayError()
{
    hasReplayError_ = false;
    replayErrorMessage_.clear();
    emit replayStateChanged();
}

void AnalysisController::setReplayNotice(const QString& message)
{
    hasReplayNotice_ = true;
    replayNoticeText_ = message;
    emit replayStateChanged();
}

void AnalysisController::clearReplayNotice()
{
    hasReplayNotice_ = false;
    replayNoticeText_.clear();
    emit replayStateChanged();
}

// ---- T010 Part B: serial source ----

namespace {

constexpr std::array<int, 5> kSupportedSerialBauds = {9600, 19200, 38400, 57600, 115200};

// T015: one deterministic secondary line combining the response-side issue
// (T014) and the request-side issue (T015). Presentation-only; both facts
// come from Core, never re-derived here.
QString composeIssueText(
    const modbuslens::core::TransactionAnalysis& analysis,
    const std::vector<modbuslens::core::TransactionRequestIssue>& requestIssues)
{
    const QString responseText = issueDetailText(analysis);
    const QString requestText = requestIssueDetailText(requestIssues);
    if (responseText.isEmpty()) {
        return requestText;
    }
    if (requestText.isEmpty()) {
        return responseText;
    }
    return responseText + QStringLiteral("；") + requestText;
}

// ---- T011 Part A: deterministic baseline presentation formatter ----

QString findingPhrase(const modbuslens::core::DiagnosisFinding& finding)
{
    using namespace modbuslens::core;
    switch (finding.code) {
    case DiagnosisFindingCode::Healthy:
        return QStringLiteral("全部 %1 笔通信均成功")
            .arg(finding.affectedCount);
    case DiagnosisFindingCode::PendingObserved:
        return QStringLiteral("进行中的通信：%1").arg(finding.affectedCount);
    case DiagnosisFindingCode::ExceptionObserved:
        return QStringLiteral("设备异常 0x%1：%2")
            .arg(QString::number(finding.exceptionCode.value_or(0), 16)
                     .toUpper()
                     .rightJustified(2, QLatin1Char('0')))
            .arg(finding.affectedCount);
    case DiagnosisFindingCode::CrcErrorObserved:
        return QStringLiteral("CRC 错误：%1").arg(finding.affectedCount);
    case DiagnosisFindingCode::TimeoutObserved:
        return QStringLiteral("无响应超时：%1").arg(finding.affectedCount);
    case DiagnosisFindingCode::ProtocolErrorObserved:
        return QStringLiteral("协议错误：%1").arg(finding.affectedCount);
    case DiagnosisFindingCode::ExpectedNoResponseObserved:
        // T015/ADR-003 wording: an observation only — never "写入成功".
        return QStringLiteral("预期无响应的广播事务：%1").arg(finding.affectedCount);
    case DiagnosisFindingCode::RequestIssueObserved:
        // T015 Part C audit wording: observed request-side protocol facts,
        // never an accusation against the requester.
        return QStringLiteral("请求参数不符合协议约束的事务：%1").arg(finding.affectedCount);
    case DiagnosisFindingCode::NoData:
        return QStringLiteral("暂无可分析数据");
    }
    return {};
}

QString actionPhrase(modbuslens::core::DiagnosisActionCode action)
{
    using namespace modbuslens::core;
    switch (action) {
    case DiagnosisActionCode::WaitForCompletion:
        return QStringLiteral("等待当前通信完成");
    case DiagnosisActionCode::CheckDevicePower:
        return QStringLiteral("检查设备供电");
    case DiagnosisActionCode::CheckSlaveAddress:
        return QStringLiteral("核对从站地址");
    case DiagnosisActionCode::CheckSerialSettings:
        return QStringLiteral("检查串口参数");
    case DiagnosisActionCode::CheckWiring:
        return QStringLiteral("检查 RS485 接线");
    case DiagnosisActionCode::CheckNoiseAndGrounding:
        return QStringLiteral("检查接地与线路干扰");
    case DiagnosisActionCode::CheckFunctionSupport:
        return QStringLiteral("确认设备是否支持该功能码");
    case DiagnosisActionCode::CheckRegisterMap:
        return QStringLiteral("核对寄存器地址表");
    case DiagnosisActionCode::CheckRequestParameters:
        return QStringLiteral("核对请求参数");
    case DiagnosisActionCode::CheckDeviceHealth:
        return QStringLiteral("检查设备运行状态");
    case DiagnosisActionCode::CheckDeviceDocumentation:
        return QStringLiteral("查阅设备通信文档");
    case DiagnosisActionCode::InspectProtocolConsistency:
        return QStringLiteral("检查请求与响应的协议一致性");
    }
    return {};
}

QString formatDiagnosisReport(const modbuslens::core::DiagnosisReport& report)
{
    // A lone NoData report = "diagnosis was RUN on an empty batch" — a
    // different state from "no diagnosis has been run yet".
    if (report.findings.size() == 1
        && report.findings.front().code
            == modbuslens::core::DiagnosisFindingCode::NoData) {
        return QStringLiteral("暂无可分析数据");
    }

    QString text;
    text += QStringLiteral("诊断结果：\n");
    for (const auto& finding : report.findings) {
        text += QStringLiteral("- ") + findingPhrase(finding) + QLatin1Char('\n');
    }

    // Suggested checks: union of all findings' actions, deduplicated in the
    // fixed enum declaration order (deterministic — never unordered).
    constexpr std::array<modbuslens::core::DiagnosisActionCode, 12> kActionOrder = {
        modbuslens::core::DiagnosisActionCode::WaitForCompletion,
        modbuslens::core::DiagnosisActionCode::CheckDevicePower,
        modbuslens::core::DiagnosisActionCode::CheckSlaveAddress,
        modbuslens::core::DiagnosisActionCode::CheckSerialSettings,
        modbuslens::core::DiagnosisActionCode::CheckWiring,
        modbuslens::core::DiagnosisActionCode::CheckNoiseAndGrounding,
        modbuslens::core::DiagnosisActionCode::CheckFunctionSupport,
        modbuslens::core::DiagnosisActionCode::CheckRegisterMap,
        modbuslens::core::DiagnosisActionCode::CheckRequestParameters,
        modbuslens::core::DiagnosisActionCode::CheckDeviceHealth,
        modbuslens::core::DiagnosisActionCode::CheckDeviceDocumentation,
        modbuslens::core::DiagnosisActionCode::InspectProtocolConsistency,
    };
    const auto hasAction = [&report](modbuslens::core::DiagnosisActionCode action) {
        for (const auto& finding : report.findings) {
            for (const auto candidate : finding.recommendedActions) {
                if (candidate == action) {
                    return true;
                }
            }
        }
        return false;
    };
    QStringList checkLines;
    for (const auto action : kActionOrder) {
        if (hasAction(action)) {
            checkLines << actionPhrase(action);
        }
    }
    if (!checkLines.isEmpty()) {
        text += QStringLiteral("\n建议检查：\n");
        for (const auto& line : checkLines) {
            text += QStringLiteral("- ") + line + QLatin1Char('\n');
        }
    }
    return text;
}

} // namespace

bool AnalysisController::serialConnected() const
{
    return serialConnected_;
}

bool AnalysisController::serialBusy() const
{
    return serialBusy_;
}

bool AnalysisController::hasSerialError() const
{
    return hasSerialError_;
}

QString AnalysisController::serialErrorMessage() const
{
    return serialErrorMessage_;
}

QStringList AnalysisController::serialPortNames() const
{
    return serialPortNames_;
}

bool AnalysisController::hasBaselineDiagnosis() const
{
    return hasBaselineDiagnosis_;
}

QString AnalysisController::baselineDiagnosisText() const
{
    return baselineDiagnosisText_;
}

bool AnalysisController::aiConfigured() const
{
    return aiConfigured_;
}

bool AnalysisController::aiDiagnosisBusy() const
{
    return aiDiagnosisBusy_;
}

bool AnalysisController::hasAiDiagnosis() const
{
    return hasAiDiagnosis_;
}

QString AnalysisController::aiDiagnosisText() const
{
    return aiDiagnosisText_;
}

QString AnalysisController::aiDiagnosisErrorMessage() const
{
    return aiDiagnosisErrorMessage_;
}

QString AnalysisController::aiModelName() const
{
    return aiModelName_;
}

bool AnalysisController::agentBusy() const
{
    return agentRuntime_.isBusy();
}

bool AnalysisController::hasAgentAnswer() const
{
    return hasAgentAnswer_;
}

QString AnalysisController::agentAnswerText() const
{
    return agentAnswerText_;
}

QString AnalysisController::agentErrorText() const
{
    return agentErrorText_;
}

bool AnalysisController::agentAvailable() const
{
    return aiConfigured_;
}

bool AnalysisController::cloudAiBusy() const
{
    // Derived single-flight state: the runtime IS the busy truth for Agent,
    // aiDiagnosisBusy_ is the truth for Ask AI. No third mutable bool.
    return aiDiagnosisBusy_ || agentRuntime_.isBusy();
}

void AnalysisController::askAgent(const QString& question)
{
    // Backend single-flight guards (the QML button state is only UX).
    if (aiDiagnosisBusy_) {
        agentErrorText_ = QStringLiteral("AI 解释请求进行中，暂不能发起 Agent 问答。");
        emit agentStateChanged();
        return;
    }
    if (agentRuntime_.isBusy()) {
        agentErrorText_ = QStringLiteral("已有 Agent 请求进行中。");
        emit agentStateChanged();
        return;
    }
    if (activeDiagnosisTransactions_.empty()) {
        agentErrorText_ = QStringLiteral("当前没有可分析的事务数据。");
        emit agentStateChanged();
        return;
    }
    if (!agentClient_.isConfigured()) {
        agentErrorText_ = QStringLiteral("ModelScope 未配置，无法使用 Agent 问答。");
        emit agentStateChanged();
        return;
    }

    // Accepted run. The snapshot comes ONLY from the structured active batch
    // (self-consistent statistics re-derived by makeAgentToolContext) — never
    // from QML rows / statusText / statistics labels.
    ++agentRequestGeneration_;
    const auto context = modbuslens::agent::makeAgentToolContext(
        activeDiagnosisTransactions_, activeBatchRevision_);
    agentErrorText_.clear(); // accepted run clears the previous error; old answer stays
    agentRuntime_.start(modbuslens::agent::AgentRunRequest{
        .userQuestion = question,
        .context = context,
        .runGeneration = agentRequestGeneration_,
    });
    emit agentStateChanged();
    emit cloudAiChanged();
}

void AnalysisController::cancelAgent()
{
    if (agentRuntime_.isBusy()) {
        agentRuntime_.cancel(); // runCancelled slot refreshes UI state
    }
}

void AnalysisController::handleAgentCompleted(std::uint64_t /*runGeneration*/,
                                              const QString& answer)
{
    hasAgentAnswer_ = true;
    agentAnswerText_ = answer;
    agentErrorText_.clear();
    emit agentStateChanged();
    emit cloudAiChanged();
}

void AnalysisController::handleAgentFailed(
    std::uint64_t /*runGeneration*/,
    const modbuslens::agent::AgentRunFailure& failure)
{
    // A failed attempt keeps a previous same-batch answer (T011 keep-old-text
    // style); only the error text is replaced. Facts never change here.
    agentErrorText_ = agentFailureText(failure);
    emit agentStateChanged();
    emit cloudAiChanged();
}

void AnalysisController::handleAgentCancelled(std::uint64_t /*runGeneration*/)
{
    // User cancel: no red error. Old answer/error stay; only busy clears
    // (the runtime already invalidated the generation).
    emit agentStateChanged();
    emit cloudAiChanged();
}

void AnalysisController::configureAiClient(const QUrl& endpoint,
                                           const QString& apiKey,
                                           const QString& modelId,
                                           std::chrono::milliseconds timeout)
{
    // C++-side test seam (never QML): explicit adoption of a client config.
    // An empty endpoint clears the configured state ("not configured" path).
    if (endpoint.isEmpty()) {
        aiConfigured_ = false;
        aiModelName_.clear();
        aiClient_.configure(ModelScopeClientConfig{});
        agentClient_.configure(ModelScopeClientConfig{});
        emit aiStateChanged();
        return;
    }
    const ModelScopeClientConfig config{
        .endpoint = endpoint,
        .apiKey = apiKey,
        .modelId = modelId,
        .timeout = timeout,
    };
    aiClient_.configure(config);
    agentClient_.configure(config);
    aiConfigured_ = true;
    aiModelName_ = modelId;
    emit aiStateChanged();
}

void AnalysisController::setAiError(const QString& message)
{
    aiDiagnosisErrorMessage_ =
        QStringLiteral("最近一次 AI 请求失败：%1").arg(message);
    emit aiStateChanged();
}

void AnalysisController::handleAiSucceeded(std::uint64_t requestId,
                                           const QString& text)
{
    // Two-dimensional stale guard: the delivery is applied only when BOTH
    // dimensions still match — same analysis batch AND latest AI request.
    if (!activeAiRequestId_.has_value() || requestId != *activeAiRequestId_) {
        return;
    }
    if (!requestBatchRevision_.has_value()
        || *requestBatchRevision_ != activeBatchRevision_) {
        return;
    }
    activeAiRequestId_.reset();
    requestBatchRevision_.reset();
    aiDiagnosisBusy_ = false;
    hasAiDiagnosis_ = true;
    aiDiagnosisText_ = text;
    aiDiagnosisErrorMessage_.clear();
    emit aiStateChanged();
    emit cloudAiChanged();
}

void AnalysisController::handleAiFailed(std::uint64_t requestId,
                                        AiDiagnosisErrorCode code,
                                        const QString& sanitizedMessage)
{
    if (!activeAiRequestId_.has_value() || requestId != *activeAiRequestId_) {
        return;
    }
    if (!requestBatchRevision_.has_value()
        || *requestBatchRevision_ != activeBatchRevision_) {
        return;
    }
    activeAiRequestId_.reset();
    requestBatchRevision_.reset();
    aiDiagnosisBusy_ = false;
    // A failed attempt keeps a previous same-batch explanation (if any):
    // error and result may legitimately coexist.
    Q_UNUSED(code);
    setAiError(sanitizedMessage);
    emit cloudAiChanged();
}

void AnalysisController::cancelAiDiagnosis()
{
    if (!aiDiagnosisBusy_ && !activeAiRequestId_.has_value()) {
        return;
    }
    // Invalidate identity FIRST, then abort — a late finished() delivery can
    // never write UI state again (UI-AI11).
    ++aiRequestGeneration_;
    activeAiRequestId_.reset();
    requestBatchRevision_.reset();
    aiClient_.cancel(AiAbortReason::UserCancel);
    aiDiagnosisBusy_ = false;
    emit aiStateChanged();
    emit cloudAiChanged();
}

void AnalysisController::askAiDiagnosis()
{
    // C++ re-validates every precondition (the QML button is only UX).
    if (agentRuntime_.isBusy()) {
        setAiError(QStringLiteral("Agent 问答进行中，暂不能生成 AI 解释。"));
        emit cloudAiChanged();
        return;
    }
    if (!aiConfigured_) {
        setAiError(QStringLiteral(
            "未配置 ModelScope API 令牌（MODELSCOPE_API_KEY），"
            " 请设置后重启应用。"));
        return;
    }
    if (activeDiagnosisTransactions_.empty()) {
        setAiError(QStringLiteral("暂无可分析数据。"));
        return;
    }
    if (!hasBaselineDiagnosis_) {
        setAiError(QStringLiteral("请先运行基线诊断。"));
        return;
    }
    if (aiDiagnosisBusy_) {
        return;
    }

    // Prompt is derived ONLY from structured deterministic facts.
    const auto context = modbuslens::core::buildDiagnosisContext(
        activeDiagnosisTransactions_);
    const auto report = modbuslens::core::diagnoseTransactions(context);
    const auto prompt = buildDiagnosisPrompt(context, report);

    ++aiRequestGeneration_;
    activeAiRequestId_ = aiRequestGeneration_;
    requestBatchRevision_ = activeBatchRevision_;
    aiDiagnosisBusy_ = true;
    aiDiagnosisErrorMessage_.clear(); // clear the LATEST error, keep old text
    emit aiStateChanged();
    emit cloudAiChanged();
    aiClient_.requestDiagnosis(prompt.systemInstructions, prompt.userPrompt,
                               aiRequestGeneration_);
}

void AnalysisController::invalidateAiForBatchChange()
{
    // Backing state goes consistent BEFORE any notification (§36): bump the
    // batch revision, abort any in-flight AI request and invalidate its
    // identity, then clear every derived view (AI result/error + baseline).
    ++activeBatchRevision_;
    // Agent ordering (Phase 2 contract): live revision becomes the NEW value
    // BEFORE any invalidate/callback can race, then the in-flight run dies
    // silently and all Agent presentation is cleared.
    agentRuntime_.setCurrentBatchRevision(activeBatchRevision_);
    agentRuntime_.invalidateForBatchChange();
    hasAgentAnswer_ = false;
    agentAnswerText_.clear();
    agentErrorText_.clear();
    if (aiDiagnosisBusy_ || activeAiRequestId_.has_value()) {
        ++aiRequestGeneration_;
        activeAiRequestId_.reset();
        requestBatchRevision_.reset();
        aiClient_.cancel(AiAbortReason::BatchInvalidated);
    }
    aiDiagnosisBusy_ = false;
    hasAiDiagnosis_ = false;
    aiDiagnosisText_.clear();
    aiDiagnosisErrorMessage_.clear();
    clearDiagnosisState();
    emit aiStateChanged();
    emit agentStateChanged();
    emit cloudAiChanged();
}

void AnalysisController::clearDiagnosisState()
{
    hasBaselineDiagnosis_ = false;
    baselineDiagnosisText_.clear();
    emit diagnosisChanged();
}

void AnalysisController::clearDiagnosis()
{
    // Diagnosis-only clear: the batch, rows, statistics and source all stay.
    // Part B: this also drops the AI explanation and aborts an in-flight AI
    // request (identity invalidation first). The batch revision does NOT
    // change — the deterministic facts did not change.
    if (aiDiagnosisBusy_ || activeAiRequestId_.has_value()) {
        ++aiRequestGeneration_;
        activeAiRequestId_.reset();
        requestBatchRevision_.reset();
        aiClient_.cancel(AiAbortReason::DiagnosisCleared);
    }
    aiDiagnosisBusy_ = false;
    hasAiDiagnosis_ = false;
    aiDiagnosisText_.clear();
    aiDiagnosisErrorMessage_.clear();
    clearDiagnosisState();
    emit aiStateChanged();
    emit cloudAiChanged();
}

void AnalysisController::runBaselineDiagnosis()
{
    // Presentation is generated ONLY from the rule core's report — the
    // controller never re-judges statuses or hand-counts here.
    const auto context = modbuslens::core::buildDiagnosisContext(
        activeDiagnosisTransactions_);
    const auto report = modbuslens::core::diagnoseTransactions(context);
    baselineDiagnosisText_ = formatDiagnosisReport(report);
    hasBaselineDiagnosis_ = true;
    emit diagnosisChanged();
}

void AnalysisController::setSerialError(const QString& message)
{
    hasSerialError_ = true;
    serialErrorMessage_ = message;
    emit serialErrorChanged();
}

void AnalysisController::clearSerialError()
{
    hasSerialError_ = false;
    serialErrorMessage_.clear();
    emit serialErrorChanged();
}

void AnalysisController::teardownSerialTransport()
{
    // Silent local close (the adapter guarantees no transportError on intent):
    // cancels pending transaction, closes port, resets every serial flag.
    // M10-C1: losing the connection invalidates a prepared write snapshot (a
    // confirmation must never outlive the session it was captured for).
    if (preparedWriteStore_.invalidate(
            modbuslens::core::PreparedWriteInvalidReason::Disconnected)) {
        announcePreparedWriteChanged();
    }
    serialTransport_->closePort();
    serialConnected_ = false;
    serialBusy_ = false;
    pendingRequest_.reset();
    emit serialConnChanged();
    emit serialStatusChanged();
}

void AnalysisController::handleSerialTransportError(const QString& message)
{
    // Transport errors are NOT Modbus diagnoses: sync from the transport,
    // drop the pending snapshot, surface the message — and touch NOTHING in
    // statistics/rows/mode/source.
    serialBusy_ = false;
    pendingRequest_.reset();
    serialConnected_ = serialTransport_->isPortOpen();
    if (!serialConnected_) {
        // The connection is gone: a prepared snapshot cannot outlive it.
        if (preparedWriteStore_.invalidate(
                modbuslens::core::PreparedWriteInvalidReason::Disconnected)) {
            announcePreparedWriteChanged();
        }
    }
    setSerialError(QStringLiteral("串口传输错误：%1").arg(message));
    emit serialConnChanged();
    emit serialStatusChanged();
}

void AnalysisController::refreshSerialPorts()
{
    // Discovery only: enumerate. NEVER open/write/probe any detected port.
    QStringList names;
    const auto ports = QSerialPortInfo::availablePorts();
    names.reserve(ports.size());
    for (const auto& info : ports) {
        if (!info.portName().isEmpty()) {
            names.append(info.portName());
        }
    }
    serialPortNames_ = std::move(names);
    emit serialPortsChanged();
}

void AnalysisController::connectSerial(const QString& portName, int baudRate)
{
    if (portName.isEmpty()) {
        setSerialError(QStringLiteral("串口错误：串口名为空"));
        return;
    }
    if (std::find(kSupportedSerialBauds.begin(), kSupportedSerialBauds.end(), baudRate)
        == kSupportedSerialBauds.end()) {
        setSerialError(QStringLiteral("串口错误：波特率不受支持"));
        return;
    }

    // Open FIRST; only a fully successful transport open may transition the
    // source. On failure the transport's bounded transportError reaches
    // handleSerialTransportError — the old batch/mode/source stay untouched
    // (UI-S02). The seam is used here too, so an injected transport is opened
    // by exactly the same rule as the production adapter.
    if (!serialTransport_->openPort(portName, baudRate)) {
        return;
    }

    // Connect success = explicit source transition: clear the old active
    // batch so a Serial header can never sit over Replay/Demo rows. A new
    // Active Serial session also starts here: a fresh session id keeps the
    // append-only history of two connections apart.
    applySnapshot(summarizeTransactions(
        std::span<const modbuslens::core::TransactionAnalysis>{}));
    transactionModel_.setEntries({});
    activeDiagnosisTransactions_.clear();
    activeSerialRecords_.clear();
    activeSerialTerminations_.clear();
    // M10-C1: a new Active Serial session invalidates any prepared snapshot
    // from the previous one (even with the same port and baud).
    if (preparedWriteStore_.invalidate(
            modbuslens::core::PreparedWriteInvalidReason::SessionChanged)) {
        announcePreparedWriteChanged();
    }
    ++activeSerialSessionId_;
    sourceKind_ = modbuslens::core::TransactionSourceKind::ActiveSerial;
    invalidateAiForBatchChange();

    serialSourceLabel_ = QStringLiteral("%1 @ %2").arg(portName).arg(baudRate);
    modeLabel_ = QStringLiteral("串口模式");
    sourceLabel_ = serialSourceLabel_;
    serialConnected_ = true;
    serialBusy_ = false;
    clearSerialError();
    emit serialConnChanged();
    emit serialStatusChanged();
    emit sourceChanged();
}

void AnalysisController::disconnectSerial()
{
    // Intentional disconnect: transport-only. The last completed result and
    // the Serial source identity stay on the dashboard — Clear is the only
    // clearer, and no transport error is shown for a user close.
    teardownSerialTransport();
}

void AnalysisController::readHoldingRegistersOnce(
    int slaveAddress, int startAddress, int quantity, int timeoutMs)
{
    // Range validation BEFORE any narrowing cast: QML numbers arrive as int,
    // and a silent uint8_t/uint16_t wrap here would be undefined-behavior
    // territory the Core must never be handed. The messages are part of the
    // frozen FC03 contract and stay verbatim.
    if (slaveAddress < 1 || slaveAddress > 247) {
        setSerialError(QStringLiteral("串口错误：从站地址须在 1..247 之间"));
        return;
    }
    if (startAddress < 0 || startAddress > 65535) {
        setSerialError(QStringLiteral("串口错误：起始地址须在 0..65535 之间"));
        return;
    }
    if (quantity < 1 || quantity > 125) {
        setSerialError(QStringLiteral("串口错误：寄存器数量须在 1..125 之间"));
        return;
    }
    if (timeoutMs <= 0) {
        setSerialError(QStringLiteral("串口错误：超时时间须大于 0 ms"));
        return;
    }
    if (!serialConnected_) {
        setSerialError(QStringLiteral("串口错误：串口未连接"));
        return;
    }
    if (serialBusy_) {
        setSerialError(QStringLiteral("串口错误：已有事务进行中"));
        return;
    }

    // M10-A: the unified intent is the single request model. Encode ONCE —
    // the descriptor (intent + semantic frame + exact wire) is what travels,
    // and the transport writes `wire` as-is.
    const modbuslens::core::ActiveRequestIntent intent{
        .function = modbuslens::core::ActiveFunction::ReadHoldingRegisters,
        .unitId = static_cast<std::uint8_t>(slaveAddress),
        .timeout = std::chrono::milliseconds{timeoutMs},
        .payload = modbuslens::core::ReadHoldingRegistersIntent{
            .startAddress = static_cast<std::uint16_t>(startAddress),
            .quantity = static_cast<std::uint16_t>(quantity),
        },
    };
    const auto encoded = modbuslens::core::encodeActiveRequest(intent);
    if (std::get_if<modbuslens::core::ActiveRequestEncodeError>(&encoded)
        != nullptr) {
        // Defensive only: every encode-relevant range was validated above, so
        // this branch is unreachable with the current function set. A local
        // rejection sends NOTHING and fabricates no Modbus transaction.
        setSerialError(QStringLiteral("串口错误：请求无效"));
        return;
    }
    const auto& descriptor =
        std::get<modbuslens::core::ActiveRequestDescriptor>(encoded);

    // Shared active-dispatch core (M10-D3): the read path and the write path
    // use the SAME helper, so busy/start-result/terminal bookkeeping cannot
    // fork into a write-only lifecycle. A failure drops us back with no
    // pending state at all.
    (void)startActiveDescriptor(descriptor);
    // The previous completed result stays visible until the new analysis
    // replaces it (Reading... state).
}

modbuslens::core::ActiveStartResult AnalysisController::startActiveDescriptor(
    const modbuslens::core::ActiveRequestDescriptor& descriptor)
{
    using modbuslens::core::ActiveStartResult;

    // The transport is the only authority on whether the bytes entered the
    // transmission lifecycle.
    const auto start = serialTransport_->startActiveRequest(descriptor);
    if (!start.accepted) {
        // A submission that terminated during the write itself (short count)
        // already handed part of the ADU over: its evidence is archived
        // synchronously here, because no pending request exists that a later
        // signal could be matched against. The evidence belongs to this
        // Active Serial session and therefore follows the same Clear Results /
        // source-replacement contracts as every other terminal.
        if (start.terminatedDuringSubmission.has_value()) {
            activeSerialTerminations_.push_back(*start.terminatedDuringSubmission);
        }
        // NOTHING entered flight: no pending request, no busy transition and
        // (deliberately) no snapshot invalidation — an attempt that never left
        // the process must not destroy a confirmation context. Pre-send
        // rejections therefore stay exactly as bounded as they were before
        // M10-D3.
        return start;
    }

    pendingRequest_ = descriptor;
    serialBusy_ = true;
    // M10-C1: another request entered flight => a still-PREPARED write
    // snapshot is permanently invalidated (busy returning to false does not
    // revive it). A snapshot that was already Consumed is a terminal
    // generation, so invalidate() is a no-op for it: entering flight can never
    // rewrite a write's own confirmation outcome (M10-D3 §21).
    if (preparedWriteStore_.invalidate(
            modbuslens::core::PreparedWriteInvalidReason::BusyBecameTrue)) {
        announcePreparedWriteChanged();
    }
    clearSerialError();
    emit serialStatusChanged();
    return start;
}

void AnalysisController::setSerialTransport(SerialTransport* transport)
{
    // nullptr restores the production adapter. Repointing is only allowed
    // while nothing is in flight: a pending transaction belongs to the
    // transport that accepted it.
    if (serialBusy_) {
        return;
    }
    if (serialTransport_ != nullptr) {
        disconnect(serialTransport_, nullptr, this, nullptr);
    }
    serialTransport_ = transport != nullptr ? transport : &serialAdapter_;
    connectSerialTransportSignals(*serialTransport_);
}

void AnalysisController::connectSerialTransportSignals(SerialTransport& transport)
{
    connect(&transport, &SerialTransport::transactionCompleted,
            this, &AnalysisController::handleSerialTransactionCompleted);
    connect(&transport, &SerialTransport::transactionTerminated,
            this, &AnalysisController::handleSerialTransactionTerminated);
    connect(&transport, &SerialTransport::transportError,
            this, &AnalysisController::handleSerialTransportError);
}

modbuslens::core::TransactionSourceKind AnalysisController::sourceKind() const
{
    return sourceKind_;
}

std::uint64_t AnalysisController::activeSerialSessionId() const
{
    return activeSerialSessionId_;
}

int AnalysisController::activeSerialRecordCount() const
{
    return static_cast<int>(activeSerialRecords_.size());
}

const std::vector<modbuslens::core::ActiveTransactionRecord>&
AnalysisController::activeSerialRecords() const
{
    return activeSerialRecords_;
}

int AnalysisController::activeSerialTerminalCount() const
{
    return static_cast<int>(activeSerialTerminations_.size());
}

const std::vector<modbuslens::core::ActiveTransportTerminal>&
AnalysisController::activeSerialTerminations() const
{
    return activeSerialTerminations_;
}

void AnalysisController::handleSerialTransactionTerminated(
    const modbuslens::core::ActiveTransportTerminal& terminal)
{
    // Same identity rule as a completion: only the request that is actually
    // pending may be terminated. A terminal for anything else (or a second
    // terminal for the same request, which the transports must never emit) is
    // ignored whole — this is the no-double-terminal guard.
    if (!pendingRequest_.has_value()) {
        return;
    }
    if (terminal.request != *pendingRequest_) {
        return;
    }

    // Retain the attempt: send-time snapshot + exact request ADU + every byte
    // observed before the abort + PossiblySent + the typed termination reason.
    // No TransactionAnalysis is invented here — a transport abort is not a
    // Modbus response.
    activeSerialTerminations_.push_back(terminal);

    // The request is over (locally): the in-flight state ends, but nothing in
    // statistics / rows / mode / source is touched (a transport abort never
    // rewrites the last completed analysis).
    pendingRequest_.reset();
    serialBusy_ = false;
    emit serialStatusChanged();
}

void AnalysisController::announcePreparedWriteChanged()
{
    emit preparedWriteChanged();
}

void AnalysisController::setWriteDraftError(const QString& message,
                                            const QString& fieldToken)
{
    hasWriteDraftError_ = true;
    writeDraftError_ = message;
    writeDraftErrorField_ = fieldToken;
    emit writeDraftErrorChanged();
}

void AnalysisController::setWriteDraftParseError(
    const QString& fieldToken, const QString& fieldLabel,
    const modbuslens::core::ValuesParseError& error)
{
    using modbuslens::core::ValuesParseErrorCode;
    // One shared reason phrase per typed code (the parser taxonomy stays the
    // single source); the FIELD identity is added by this boundary, so an
    // address error and a value error never collapse into "输入错误".
    const QString reason =
        error.code == ValuesParseErrorCode::NoValues
            ? QStringLiteral("请输入一个十进制数值")
        : error.code == ValuesParseErrorCode::MultipleValuesInSingleField
            ? QStringLiteral("只能输入一个数值（不能包含换行）")
        : error.code == ValuesParseErrorCode::InvalidCharacter
            ? QStringLiteral("只能输入十进制数字（0-9）")
            : QStringLiteral("数值须在 0..65535 之间");
    setWriteDraftError(QStringLiteral("%1：%2").arg(fieldLabel, reason), fieldToken);
}

void AnalysisController::clearWriteDraftError()
{
    if (!hasWriteDraftError_ && writeDraftError_.isEmpty()
        && writeDraftErrorField_.isEmpty()) {
        return;
    }
    hasWriteDraftError_ = false;
    writeDraftError_.clear();
    writeDraftErrorField_.clear();
    emit writeDraftErrorChanged();
}

void AnalysisController::setWriteDraftErrorFrom(
    const modbuslens::core::PrepareRejected& rejected)
{
    using namespace modbuslens::core;

    // Presentation mapping lives here (Qt adapter layer); the typed codes stay
    // in core. Line numbers are converted to the one-based convention users
    // see in the editor.
    if (rejected.reason != PrepareRejectReason::ValidationFailed
        || !rejected.validationError.has_value()) {
        setWriteDraftError(rejected.reason == PrepareRejectReason::Busy
                               ? QStringLiteral("已有请求进行中，请稍后再写入")
                           : rejected.reason == PrepareRejectReason::NotConnected
                               ? QStringLiteral("串口未连接")
                               : QStringLiteral("当前数据源不是串口模式"));
        return;
    }

    const auto& error = *rejected.validationError;
    switch (error.code) {
    case WriteValidationErrorCode::UnitIdOutOfRange:
        setWriteDraftError(QStringLiteral("从站地址须在 1..247 之间（当前不支持广播）"),
                           QStringLiteral("unit"));
        return;
    case WriteValidationErrorCode::AddressOutOfRange:
        setWriteDraftError(QStringLiteral("寄存器地址须在 0..65535 之间"),
                           QStringLiteral("address"));
        return;
    case WriteValidationErrorCode::ValueOutOfRange:
        setWriteDraftError(QStringLiteral("寄存器值须在 0..65535 之间"),
                           QStringLiteral("value"));
        return;
    case WriteValidationErrorCode::TimeoutOutOfRange:
        setWriteDraftError(QStringLiteral("超时须在 %1..%2 ms 之间")
                               .arg(kWriteUiMinTimeoutMs)
                               .arg(kWriteUiMaxTimeoutMs),
                           QStringLiteral("timeout"));
        return;
    case WriteValidationErrorCode::QuantityOutOfRange:
        setWriteDraftError(QStringLiteral("寄存器数量须在 1..123 之间"),
                           QStringLiteral("quantity"));
        return;
    case WriteValidationErrorCode::AddressSpanOutOfRange:
        setWriteDraftError(
            QStringLiteral("起始地址与数量超出 16 位寄存器地址空间"),
            QStringLiteral("span"));
        return;
    case WriteValidationErrorCode::ValuesParseError:
        break;
    }

    if (!error.parseError.has_value()) {
        setWriteDraftError(QStringLiteral("寄存器值列表无法解析"));
        return;
    }
    const auto& parse = *error.parseError;
    const auto line = static_cast<int>(parse.lineIndex) + 1; // one-based for users
    switch (parse.code) {
    case ValuesParseErrorCode::NoValues:
        setWriteDraftError(QStringLiteral("请输入至少一个寄存器值（每行一个）"),
                           QStringLiteral("values"));
        return;
    case ValuesParseErrorCode::BlankLineInside:
        setWriteDraftError(QStringLiteral("第 %1 行为空行：中间不能有空行")
                               .arg(line),
                           QStringLiteral("values"));
        return;
    case ValuesParseErrorCode::InvalidCharacter:
        setWriteDraftError(QStringLiteral("第 %1 行不是合法的十进制数值")
                               .arg(line),
                           QStringLiteral("values"));
        return;
    case ValuesParseErrorCode::ValueOutOfRange:
        setWriteDraftError(QStringLiteral("第 %1 行的数值超出 0..65535")
                               .arg(line),
                           QStringLiteral("values"));
        return;
    case ValuesParseErrorCode::TooManyValues:
        setWriteDraftError(QStringLiteral("寄存器数量须在 1..123 之间"),
                           QStringLiteral("values"));
        return;
    case ValuesParseErrorCode::MultipleValuesInSingleField:
        // Not reachable from the multi-line 0x10 list (it EXPECTS many lines);
        // kept deterministic so a future single-value reuse stays exhaustive.
        setWriteDraftError(QStringLiteral("只能输入一个数值（不能包含换行）"),
                           QStringLiteral("values"));
        return;
    }
    setWriteDraftError(QStringLiteral("寄存器值列表无法解析"),
                       QStringLiteral("values"));
}

bool AnalysisController::prepareWrite06(int unitId, int registerAddress, int value,
                                        int timeoutMs)
{
    const auto outcome = prepareWriteSingleRegister(unitId, registerAddress, value,
                                                    timeoutMs);
    if (const auto* rejected = std::get_if<modbuslens::core::PrepareRejected>(&outcome)) {
        setWriteDraftErrorFrom(*rejected);
        announcePreparedWriteChanged();
        return false;
    }
    // AlreadyPrepared counts as "the dialog may open" (the snapshot is there).
    clearWriteDraftError();
    announcePreparedWriteChanged();
    return true;
}

bool AnalysisController::prepareWrite06Draft(int unitId, const QString& addressRaw,
                                             const QString& valueRaw, int timeoutMs)
{
    using modbuslens::core::parseDecimalRegisterValue;
    using modbuslens::core::SingleRegisterValue;
    using modbuslens::core::ValuesParseError;

    // The QML side hands over the RAW text (it never normalizes). Parsing is
    // the core decimal authority; a failure keeps the raw draft untouched and
    // reports WHICH field failed. On success the typed helper below stays the
    // single range-validation / snapshot path (no duplicated validation).
    const auto addressParse =
        parseDecimalRegisterValue(addressRaw.toStdString());
    if (const auto* error = std::get_if<ValuesParseError>(&addressParse)) {
        setWriteDraftParseError(QStringLiteral("address"),
                                QStringLiteral("寄存器地址"), *error);
        announcePreparedWriteChanged();
        return false;
    }
    const auto valueParse = parseDecimalRegisterValue(valueRaw.toStdString());
    if (const auto* error = std::get_if<ValuesParseError>(&valueParse)) {
        setWriteDraftParseError(QStringLiteral("value"), QStringLiteral("写入值"),
                                *error);
        announcePreparedWriteChanged();
        return false;
    }
    return prepareWrite06(unitId,
                          std::get<SingleRegisterValue>(addressParse).value,
                          std::get<SingleRegisterValue>(valueParse).value,
                          timeoutMs);
}

bool AnalysisController::prepareWrite10(int unitId, int startAddress,
                                        const QString& valuesText, int timeoutMs)
{
    const auto utf8 = valuesText.toUtf8();
    const auto outcome = prepareWriteMultipleRegisters(
        unitId, startAddress,
        std::string_view{utf8.constData(), static_cast<std::size_t>(utf8.size())},
        timeoutMs);
    if (const auto* rejected = std::get_if<modbuslens::core::PrepareRejected>(&outcome)) {
        setWriteDraftErrorFrom(*rejected);
        announcePreparedWriteChanged();
        return false;
    }
    clearWriteDraftError();
    announcePreparedWriteChanged();
    return true;
}

bool AnalysisController::confirmPreparedWriteToken(qulonglong token)
{
    const auto outcome = confirmPreparedWrite(static_cast<std::uint64_t>(token));
    const bool accepted =
        std::get_if<modbuslens::core::ConfirmAccepted>(&outcome) != nullptr;
    announcePreparedWriteChanged();
    return accepted;
}

bool AnalysisController::cancelPreparedWriteToken(qulonglong token)
{
    const bool cancelled = cancelPreparedWrite(static_cast<std::uint64_t>(token));
    announcePreparedWriteChanged();
    return cancelled;
}

bool AnalysisController::hasPreparedWrite() const
{
    return preparedWriteStore_.state() == modbuslens::core::PreparedWriteState::Prepared;
}

QString AnalysisController::preparedWriteStateToken() const
{
    switch (preparedWriteStore_.state()) {
    case modbuslens::core::PreparedWriteState::None:
        return QStringLiteral("none");
    case modbuslens::core::PreparedWriteState::Prepared:
        return QStringLiteral("prepared");
    case modbuslens::core::PreparedWriteState::Consumed:
        return QStringLiteral("consumed");
    case modbuslens::core::PreparedWriteState::Invalidated:
        return QStringLiteral("invalidated");
    }
    return QStringLiteral("none");
}

qulonglong AnalysisController::preparedWriteTokenValue() const
{
    const auto token = preparedWriteStore_.token();
    return token.has_value() ? static_cast<qulonglong>(*token) : 0;
}

int AnalysisController::preparedWriteFunction() const
{
    const auto snapshot = preparedWriteStore_.snapshot();
    if (!snapshot.has_value()) {
        return 0;
    }
    return static_cast<int>(modbuslens::core::activeFunctionCode(snapshot->intent.function));
}

int AnalysisController::preparedWriteUnitId() const
{
    const auto snapshot = preparedWriteStore_.snapshot();
    return snapshot.has_value() ? static_cast<int>(snapshot->intent.unitId) : 0;
}

int AnalysisController::preparedWriteAddress() const
{
    using namespace modbuslens::core;
    const auto snapshot = preparedWriteStore_.snapshot();
    if (!snapshot.has_value()) {
        return 0;
    }
    if (const auto* single =
            std::get_if<WriteSingleRegisterIntent>(&snapshot->intent.payload)) {
        return static_cast<int>(single->registerAddress);
    }
    if (const auto* multiple =
            std::get_if<WriteMultipleRegistersIntent>(&snapshot->intent.payload)) {
        return static_cast<int>(multiple->startAddress);
    }
    return 0;
}

int AnalysisController::preparedWriteValue() const
{
    using namespace modbuslens::core;
    const auto snapshot = preparedWriteStore_.snapshot();
    if (!snapshot.has_value()) {
        return 0;
    }
    if (const auto* single =
            std::get_if<WriteSingleRegisterIntent>(&snapshot->intent.payload)) {
        return static_cast<int>(single->value);
    }
    return 0;
}

QVariantList AnalysisController::preparedWriteValues() const
{
    using namespace modbuslens::core;
    QVariantList values;
    const auto snapshot = preparedWriteStore_.snapshot();
    if (!snapshot.has_value()) {
        return values;
    }
    if (const auto* multiple =
            std::get_if<WriteMultipleRegistersIntent>(&snapshot->intent.payload)) {
        values.reserve(static_cast<int>(multiple->values.size()));
        for (const auto value : multiple->values) {
            values.append(static_cast<int>(value));
        }
    }
    return values;
}

int AnalysisController::preparedWriteQuantity() const
{
    const auto snapshot = preparedWriteStore_.snapshot();
    return snapshot.has_value()
        ? static_cast<int>(modbuslens::core::preparedQuantity(snapshot->intent))
        : 0;
}

int AnalysisController::preparedWriteTimeoutMs() const
{
    const auto snapshot = preparedWriteStore_.snapshot();
    return snapshot.has_value()
        ? static_cast<int>(snapshot->intent.timeout.count())
        : 0;
}

QString AnalysisController::preparedWriteConnectionLabel() const
{
    const auto snapshot = preparedWriteStore_.snapshot();
    return snapshot.has_value()
        ? QString::fromStdString(snapshot->connectionLabel)
        : QString();
}

QString AnalysisController::preparedWriteInvalidReasonToken() const
{
    using modbuslens::core::PreparedWriteInvalidReason;
    const auto reason = preparedWriteStore_.invalidReason();
    if (!reason.has_value()) {
        return QString();
    }
    switch (*reason) {
    case PreparedWriteInvalidReason::UserCancelled:
        return QStringLiteral("user_cancelled");
    case PreparedWriteInvalidReason::Disconnected:
        return QStringLiteral("disconnected");
    case PreparedWriteInvalidReason::SessionChanged:
        return QStringLiteral("session_changed");
    case PreparedWriteInvalidReason::SourceChanged:
        return QStringLiteral("source_changed");
    case PreparedWriteInvalidReason::BusyBecameTrue:
        return QStringLiteral("busy_became_true");
    case PreparedWriteInvalidReason::CapabilityUnavailable:
        return QStringLiteral("capability_unavailable");
    }
    return QString();
}

bool AnalysisController::write06Supported() const
{
    // STRUCTURAL product capability, not runtime availability: it answers
    // "does this build end-to-end own 0x06?" (encoder + protocol/session
    // response support + this Controller's atomic confirm+dispatch + the
    // evidence/outcome integration). It deliberately does NOT consult
    // sourceKind_ / serialConnected_ / serialBusy_ / the draft: those decide
    // whether the ACTION is currently enabled, and binding visibility to them
    // would unload the write UI (and the user's draft) on every disconnect.
    return modbuslens::core::kProductWrite06Supported;
}

bool AnalysisController::hasWriteDraftError() const
{
    return hasWriteDraftError_;
}

QString AnalysisController::writeDraftError() const
{
    return writeDraftError_;
}

QString AnalysisController::writeDraftErrorField() const
{
    return writeDraftErrorField_;
}

modbuslens::core::WritePrepareOutcome AnalysisController::prepareWriteIntent(
    modbuslens::core::WriteIntentResult intentResult)
{
    using namespace modbuslens::core;

    // Repeated Write: an active prepared snapshot is KEPT (the caller focuses
    // the existing dialog) and no second generation is minted.
    if (preparedWriteStore_.state() == PreparedWriteState::Prepared) {
        return PrepareAlreadyPrepared{};
    }
    // Context guards are runtime facts, never QML ones.
    if (sourceKind_ != TransactionSourceKind::ActiveSerial) {
        return PrepareRejected{PrepareRejectReason::SourceNotActiveSerial,
                               std::nullopt};
    }
    if (!serialConnected_) {
        return PrepareRejected{PrepareRejectReason::NotConnected, std::nullopt};
    }
    if (serialBusy_) {
        return PrepareRejected{PrepareRejectReason::Busy, std::nullopt};
    }
    if (const auto* error = std::get_if<WriteValidationError>(&intentResult)) {
        return PrepareRejected{PrepareRejectReason::ValidationFailed, *error};
    }

    ++preparedWriteGeneration_;
    const auto stored = preparedWriteStore_.prepare(PreparedWriteSnapshot{
        .token = preparedWriteGeneration_,
        .intent = std::get<ActiveRequestIntent>(intentResult),
        .sourceKind = sourceKind_,
        .sessionId = activeSerialSessionId_,
        .connectionLabel = serialSourceLabel_.toStdString(),
    });
    if (auto* already = std::get_if<PrepareAlreadyPrepared>(&stored)) {
        return *already;
    }
    return PreparedWrite{};
}

modbuslens::core::WritePrepareOutcome
AnalysisController::prepareWriteSingleRegister(std::int64_t unitId,
                                               std::int64_t registerAddress,
                                               std::int64_t value,
                                               std::int64_t timeoutMs)
{
    return prepareWriteIntent(modbuslens::core::prepareWriteSingleRegisterIntent(
        unitId, registerAddress, value, timeoutMs));
}

modbuslens::core::WritePrepareOutcome
AnalysisController::prepareWriteMultipleRegisters(std::int64_t unitId,
                                                  std::int64_t startAddress,
                                                  std::string_view valuesText,
                                                  std::int64_t timeoutMs)
{
    return prepareWriteIntent(
        modbuslens::core::prepareWriteMultipleRegistersIntent(
            unitId, startAddress, valuesText, timeoutMs));
}

modbuslens::core::ConfirmWriteOutcome AnalysisController::confirmPreparedWrite(
    std::uint64_t token)
{
    using namespace modbuslens::core;

    // The runtime re-checks its authoritative state at confirmation time
    // (the same guard family confirmAndDispatchPreparedWrite re-uses); a
    // failing guard invalidates the now-meaningless snapshot.
    if (sourceKind_ != TransactionSourceKind::ActiveSerial) {
        preparedWriteStore_.invalidate(PreparedWriteInvalidReason::SourceChanged);
        return ConfirmRejected{ConfirmRejectReason::SourceNotActiveSerial};
    }
    if (!serialConnected_) {
        preparedWriteStore_.invalidate(PreparedWriteInvalidReason::Disconnected);
        return ConfirmRejected{ConfirmRejectReason::NotConnected};
    }
    if (const auto snapshot = preparedWriteStore_.snapshot();
        snapshot.has_value() && snapshot->sessionId != activeSerialSessionId_) {
        preparedWriteStore_.invalidate(PreparedWriteInvalidReason::SessionChanged);
        return ConfirmRejected{ConfirmRejectReason::SessionChanged};
    }
    if (serialBusy_) {
        preparedWriteStore_.invalidate(PreparedWriteInvalidReason::BusyBecameTrue);
        return ConfirmRejected{ConfirmRejectReason::Busy};
    }

    // Token + generation lifecycle only — no encode, no transport, no send.
    return preparedWriteStore_.confirm(token);
}

bool AnalysisController::cancelPreparedWrite(std::uint64_t token)
{
    return preparedWriteStore_.cancel(token);
}

modbuslens::core::PreparedDispatchResult
AnalysisController::confirmAndDispatchPreparedWrite(std::uint64_t token)
{
    using namespace modbuslens::core;

    // Fixed ordering, never split into two authority steps:
    //     final guards -> consume -> encode -> start
    //
    // The token is the ONLY input: the request that travels is the Controller's
    // own immutable snapshot, so QML can neither substitute fields nor point a
    // stale token at a different request.
    PreparedDispatchResult result{};

    // ---- final guards (all BEFORE the consume) ----
    const auto snapshot = preparedWriteStore_.snapshot();
    const auto liveToken = preparedWriteStore_.token();
    if (preparedWriteStore_.state() != PreparedWriteState::Prepared
        || !snapshot.has_value() || !liveToken.has_value()) {
        result.rejectedReason = ConfirmRejectReason::NotPrepared;
        return result;
    }
    if (*liveToken != token) {
        result.rejectedReason = ConfirmRejectReason::TokenMismatch;
        return result;
    }

    // Authoritative context guards. A failing guard means the confirmation
    // never happened: confirmationAccepted stays false, the transport is never
    // called and there is NO ActiveStartResult — hence no TransportDisposition
    // either. This must never be reported as "NotSent".
    if (sourceKind_ != TransactionSourceKind::ActiveSerial) {
        preparedWriteStore_.invalidate(PreparedWriteInvalidReason::SourceChanged);
        announcePreparedWriteChanged();
        result.rejectedReason = ConfirmRejectReason::SourceNotActiveSerial;
        return result;
    }
    if (!serialConnected_) {
        preparedWriteStore_.invalidate(PreparedWriteInvalidReason::Disconnected);
        announcePreparedWriteChanged();
        result.rejectedReason = ConfirmRejectReason::NotConnected;
        return result;
    }
    // Identity is (sourceKind, sessionId): the same COM port and baud reconnected
    // is still a NEW session, so an old confirmation can never write through it.
    if (snapshot->sessionId != activeSerialSessionId_) {
        preparedWriteStore_.invalidate(PreparedWriteInvalidReason::SessionChanged);
        announcePreparedWriteChanged();
        result.rejectedReason = ConfirmRejectReason::SessionChanged;
        return result;
    }
    if (serialBusy_) {
        preparedWriteStore_.invalidate(PreparedWriteInvalidReason::BusyBecameTrue);
        announcePreparedWriteChanged();
        result.rejectedReason = ConfirmRejectReason::Busy;
        return result;
    }

    // Capability guard: the prepared FUNCTION must have a real end-to-end
    // dispatch path in this build. A prepared 0x10 snapshot never does (no
    // encoder, no session support), so it lands here and is invalidated with
    // CapabilityUnavailable — zero consume, zero encode, zero transport call.
    //
    // kProductWrite06Supported participates on purpose: it is the same single
    // source of truth the product property exposes, so the claim and the
    // behaviour cannot drift apart (were it ever false, dispatch would be
    // blocked rather than silently contradicting the property).
    if (snapshot->intent.function != ActiveFunction::WriteSingleRegister
        || !activeFunctionSupported(ActiveFunction::WriteSingleRegister)
        || !kProductWrite06Supported) {
        preparedWriteStore_.invalidate(
            PreparedWriteInvalidReason::CapabilityUnavailable);
        announcePreparedWriteChanged();
        result.rejectedReason = ConfirmRejectReason::CapabilityUnavailable;
        return result;
    }

    // ---- CONSUME (one-shot) ----
    // Past this point the confirmation IS accepted and the generation is
    // terminal: no later failure may revive this token or re-open the dialog.
    // (The outcome is bound to a named object first: taking the address of the
    // returned temporary would be an rvalue-address error.)
    const auto confirmOutcome = preparedWriteStore_.confirm(token);
    if (std::get_if<ConfirmAccepted>(&confirmOutcome) == nullptr) {
        // Unreachable in practice (the state was just verified); handled as a
        // plain rejection rather than as an attempt.
        result.rejectedReason = ConfirmRejectReason::NotPrepared;
        announcePreparedWriteChanged();
        return result;
    }
    result.confirmationAccepted = true;
    announcePreparedWriteChanged();

    // ---- ENCODE ----
    // Encoding uses the CAPTURED snapshot: consume clears the store, and
    // re-reading a draft is forbidden — the wire must be exactly the bytes the
    // user confirmed.
    const auto encoded = encodeActiveRequest(snapshot->intent);
    if (std::get_if<ActiveRequestEncodeError>(&encoded) != nullptr) {
        // Internal invariant break: the snapshot was validated and the
        // capability exists, so an encoder failure is OUR bug. It must not be
        // dressed up as a ProtocolError / Timeout / Exception, and it produces
        // no transaction, no terminal and no transport attempt.
        result.localError = PreparedDispatchLocalError::EncodeFailed;
        return result;
    }

    // ---- START (exactly one attempt for this confirmation) ----
    result.dispatchAttempted = true;
    result.startResult = startActiveDescriptor(std::get<ActiveRequestDescriptor>(encoded));
    return result;
}

modbuslens::core::PreparedWriteState AnalysisController::preparedWriteState() const
{
    return preparedWriteStore_.state();
}

std::optional<std::uint64_t> AnalysisController::preparedWriteToken() const
{
    return preparedWriteStore_.token();
}

std::optional<modbuslens::core::PreparedWriteSnapshot>
AnalysisController::preparedWriteSnapshot() const
{
    return preparedWriteStore_.snapshot();
}

std::optional<modbuslens::core::PreparedWriteInvalidReason>
AnalysisController::preparedWriteInvalidReason() const
{
    return preparedWriteStore_.invalidReason();
}

void AnalysisController::appendActiveSerialTransaction(
    const modbuslens::core::ActiveTransactionRecord& record)
{
    // Authority first, presentation second: the record joins the session
    // history, then exactly ONE row is appended for it. The refresh below
    // re-derives the statistics snapshot and the diagnosis batch from the
    // WHOLE session (M10-B contract: the Active Serial source reports its
    // whole session, not the latest transaction).
    activeSerialRecords_.push_back(record);
    transactionModel_.appendEntries({makeSessionRow(record)});
    refreshActiveSessionDerivedViews();
}

TransactionListEntry AnalysisController::makeSessionRow(
    const modbuslens::core::ActiveTransactionRecord& record) const
{
    // Projection only: every presented field is read from the record (the
    // issue text is adapter-formatted from the record's analysis). Nothing is
    // re-derived from a QML draft and no evidence is reconstructed here.
    return TransactionListEntry{
        .deviceAddress = record.unitId(),
        .functionCode = record.functionCode(),
        .status = record.analysis.status,
        .elapsedMs = record.analysis.elapsed.count(),
        .exceptionCode = record.analysis.exceptionCode,
        .issueText = issueDetailText(record.analysis),
        .activeSerialProvenance = modbuslens::core::ActiveSerialProvenance{
            .sessionId = record.sessionId,
            .request = record.request,
            .evidence = record.evidence,
        },
    };
}

void AnalysisController::refreshActiveSessionDerivedViews()
{
    std::vector<modbuslens::core::TransactionAnalysis> analyses;
    analyses.reserve(activeSerialRecords_.size());
    activeDiagnosisTransactions_.clear();
    activeDiagnosisTransactions_.reserve(activeSerialRecords_.size());

    for (const auto& record : activeSerialRecords_) {
        analyses.push_back(record.analysis);
        activeDiagnosisTransactions_.push_back(
            modbuslens::core::DiagnosisTransaction{
                .deviceAddress = record.unitId(),
                .functionCode = record.functionCode(),
                .analysis = record.analysis,
                .requestIssues = {},
            });
    }

    applySnapshot(modbuslens::core::summarizeTransactions(
        std::span<const modbuslens::core::TransactionAnalysis>{analyses}));
    // The batch changed => every derived diagnosis dies together (M9
    // invariant: a new transaction in the session invalidates the previous
    // baseline/AI result instead of silently outdating it).
    invalidateAiForBatchChange();
    modeLabel_ = QStringLiteral("串口模式");
    sourceLabel_ = serialSourceLabel_;
    emit statisticsChanged();
    emit sourceChanged();
}

void AnalysisController::handleSerialTransactionCompleted(
    const modbuslens::core::ActiveTransactionResult& result)
{
    // Stale-completion guard (UI-S10): a result with no pending snapshot
    // (e.g. after a source switch aged the completion out) must NEVER
    // override the current Simulator/Replay batch.
    if (!pendingRequest_.has_value()) {
        return;
    }
    // The result must answer the request that is actually pending: the
    // send-time snapshot is the only accepted identity. Anything else is a
    // stale/duplicated completion and is ignored whole.
    if (result.request != *pendingRequest_) {
        return;
    }

    // One writer only: the append path below is the single place where a
    // completed transaction joins the session history AND the visible
    // history (the record carries the send-time snapshot + wire evidence).
    appendActiveSerialTransaction(modbuslens::core::ActiveTransactionRecord{
        .sessionId = activeSerialSessionId_,
        .request = *pendingRequest_,
        .evidence = result.evidence(),
        .analysis = result.analysis,
    });
    // The request is over: end the in-flight state and clear the stale serial
    // error (a completed transaction proves the transport answered). Only the
    // in-flight/presentation flags change here — rows, statistics, source
    // identity and the session history were handled above.
    pendingRequest_.reset();
    serialBusy_ = false;
    clearSerialError();
    emit serialStatusChanged();
}

void AnalysisController::applySnapshot(
    const modbuslens::core::TransactionStatisticsSnapshot& snapshot)
{
    statistics_ = snapshot;
    emit statisticsChanged();
}

void AnalysisController::setTransactionEntries(
    std::vector<TransactionListEntry> entries)
{
    transactionModel_.setEntries(std::move(entries));
}

// ---- Part B: deterministic demo orchestration ----

void AnalysisController::runDemoBatch()
{
    using ms = std::chrono::milliseconds;
    constexpr ms kThreshold{1000};

    // M10-C1: replacing the source invalidates a prepared write snapshot.
    // Recorded BEFORE the teardown reason so the reason stays the truthful
    // "source changed" (a terminal reason is never overwritten).
    if (preparedWriteStore_.invalidate(
            modbuslens::core::PreparedWriteInvalidReason::SourceChanged)) {
        announcePreparedWriteChanged();
    }
    // Source transition: leave the serial transport entirely BEFORE
    // producing Simulator data — no background COM while in Simulator Mode.
    teardownSerialTransport();

    // Deterministic slave (rebuilt each invocation for reproducibility).
    modbuslens::core::SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);
    slave.setHoldingRegister(2, 1500);

    std::vector<modbuslens::core::TransactionAnalysis> analyses;
    std::vector<TransactionListEntry> entries;
    std::vector<modbuslens::core::DiagnosisTransaction> diagnosisTransactions;
    analyses.reserve(4);
    entries.reserve(4);
    diagnosisTransactions.reserve(4);

    // One batch, three same-source views: rows, statistics and the
    // structured diagnosis input all come from the VERY same analyses.
    auto makeEntry = [&](const modbuslens::core::ModbusRtuFrame& request,
                         const modbuslens::core::TransactionAnalysis& analysis) {
        entries.push_back(TransactionListEntry{
            .deviceAddress = request.address,
            .functionCode = static_cast<int>(request.functionCode),
            .status = analysis.status,
            .elapsedMs = analysis.elapsed.count(),
            .exceptionCode = analysis.exceptionCode,
            .issueText = issueDetailText(analysis),
            .activeSerialProvenance = std::nullopt,
        });
        diagnosisTransactions.push_back(modbuslens::core::DiagnosisTransaction{
            .deviceAddress = request.address,
            .functionCode = request.functionCode,
            .analysis = analysis,
            .requestIssues = {},
        });
    };

    // DEMO-1: Success (start=0, qty=2 → {100, 200})
    {
        const auto request = makeFc03Read(0x01, 0, 2);
        const auto slaveResult = slave.handleRequest(request);
        const auto* response = std::get_if<modbuslens::core::ModbusRtuFrame>(&slaveResult);
        if (!response) {
            qWarning("runDemoBatch: DEMO-1 slave returned non-frame result");
            return;
        }
        analyses.push_back(modbuslens::core::analyzeFunction03Transaction(
            request, modbuslens::core::ResponseObservation{*response}, ms{25}, kThreshold));
        makeEntry(request, analyses.back());
    }

    // DEMO-2: Exception (start=100, qty=1 → out of range → 0x83/{0x02})
    {
        const auto request = makeFc03Read(0x01, 100, 1);
        const auto slaveResult = slave.handleRequest(request);
        const auto* response = std::get_if<modbuslens::core::ModbusRtuFrame>(&slaveResult);
        if (!response) {
            qWarning("runDemoBatch: DEMO-2 slave returned non-frame result");
            return;
        }
        analyses.push_back(modbuslens::core::analyzeFunction03Transaction(
            request, modbuslens::core::ResponseObservation{*response}, ms{18}, kThreshold));
        makeEntry(request, analyses.back());
    }

    // DEMO-3: CRC Error (correct response → CorruptCrc → CrcMismatch)
    {
        const auto request = makeFc03Read(0x01, 0, 2);
        const auto slaveResult = slave.handleRequest(request);
        const auto* response = std::get_if<modbuslens::core::ModbusRtuFrame>(&slaveResult);
        if (!response) {
            qWarning("runDemoBatch: DEMO-3 slave returned non-frame result");
            return;
        }

        const auto wire = modbuslens::core::encodeRtuFrame(*response);
        const auto delivery = modbuslens::core::applySimulationFault(
            wire,
            modbuslens::core::SimulationFaultConfig{
                .mode = modbuslens::core::SimulationFaultMode::CorruptCrc});
        const auto* corrupted =
            std::get_if<modbuslens::core::DeliveredWire>(&delivery);
        if (!corrupted) {
            qWarning("runDemoBatch: DEMO-3 expected DeliveredWire");
            return;
        }

        const auto decodeResult =
            modbuslens::core::decodeRtuFrame(corrupted->bytes);
        const auto* decodeError =
            std::get_if<modbuslens::core::RtuDecodeError>(&decodeResult);
        if (!decodeError) {
            qWarning("runDemoBatch: DEMO-3 expected decode error");
            return;
        }

        analyses.push_back(modbuslens::core::analyzeFunction03Transaction(
            request, modbuslens::core::ResponseObservation{*decodeError}, ms{17}, kThreshold));
        makeEntry(request, analyses.back());
    }

    // DEMO-4: Timeout (DropResponse → NoResponse → elapsed >= threshold)
    {
        const auto request = makeFc03Read(0x01, 0, 2);
        const auto slaveResult = slave.handleRequest(request);
        const auto* response = std::get_if<modbuslens::core::ModbusRtuFrame>(&slaveResult);
        if (!response) {
            qWarning("runDemoBatch: DEMO-4 slave returned non-frame result");
            return;
        }

        const auto wire = modbuslens::core::encodeRtuFrame(*response);
        const auto delivery = modbuslens::core::applySimulationFault(
            wire,
            modbuslens::core::SimulationFaultConfig{
                .mode = modbuslens::core::SimulationFaultMode::DropResponse});
        if (!std::holds_alternative<modbuslens::core::DroppedResponse>(delivery)) {
            qWarning("runDemoBatch: DEMO-4 expected DroppedResponse");
            return;
        }

        // T006 delivers DropResponse (delivery fact); T007 judges Timeout
        // from NoResponse + elapsed >= threshold.
        analyses.push_back(modbuslens::core::analyzeFunction03Transaction(
            request, modbuslens::core::ResponseObservation{modbuslens::core::NoResponse{}}, ms{1000}, kThreshold));
        makeEntry(request, analyses.back());
    }

    // Atomic batch publish: model + snapshot from same source. Backing state
    // (diagnosis batch + invalidated diagnosis) is made consistent BEFORE
    // any notification so QML can never observe new-stats + old-diagnosis.
    auto snapshot = modbuslens::core::summarizeTransactions(analyses);
    transactionModel_.setEntries(std::move(entries));
    statistics_ = std::move(snapshot);
    activeDiagnosisTransactions_ = std::move(diagnosisTransactions);
    invalidateAiForBatchChange();
    modeLabel_ = QStringLiteral("模拟器模式");
    sourceLabel_ = QStringLiteral("确定性演示");
    sourceKind_ = modbuslens::core::TransactionSourceKind::Simulator;
    activeSerialRecords_.clear();
    activeSerialTerminations_.clear();
    clearReplayError();
    clearReplayNotice();
    clearSerialError();
    emit statisticsChanged();
    emit sourceChanged();
}

void AnalysisController::clearResults()
{
    // Clears the analysis results and any pending replay/serial error, but
    // NEVER switches the source or closes the transport (Clear !=
    // Disconnect): the user still sees which mode/source they are in. The
    // completed Active Serial session history is result presentation/domain
    // state and is cleared with the rest; a PENDING request is untouched —
    // its future completion enters the now-empty view as a new transaction
    // (M10 Phase 1 §18 / FC18).
    applySnapshot(summarizeTransactions(
        std::span<const modbuslens::core::TransactionAnalysis>{}));
    transactionModel_.setEntries({});
    activeDiagnosisTransactions_.clear();
    activeSerialRecords_.clear();
    activeSerialTerminations_.clear();
    invalidateAiForBatchChange();
    clearReplayError();
    clearReplayNotice();
    clearSerialError();
}

void AnalysisController::loadReplayFile(const QUrl& fileUrl)
{
    using namespace modbuslens::core;

    if (!fileUrl.isLocalFile()) {
        setReplayError(QStringLiteral("回放加载失败：不是本地文件"));
        return;
    }

    const QString filePath = fileUrl.toLocalFile();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setReplayError(QStringLiteral("回放加载失败：无法打开文件"));
        return;
    }

    // QByteArray outlives the parse call; the resulting ReplayLog owns its
    // own bytes, so nothing view-like escapes this scope.
    const QByteArray contents = file.readAll();
    const std::string_view text{
        contents.constData(), static_cast<std::size_t>(contents.size())};

    const auto parseResult = parseReplayLog(text);
    if (const auto* parseError = std::get_if<ReplayParseError>(&parseResult)) {
        setReplayError(parseErrorMessage(*parseError));
        return;
    }
    const auto& replayLog = std::get<ReplayLog>(parseResult);

    const auto analysisResult = analyzeReplayLog(replayLog);
    if (const auto* executionError =
            std::get_if<ReplayExecutionError>(&analysisResult)) {
        setReplayError(executionErrorMessage(*executionError));
        return;
    }
    const auto& batch = std::get<ReplayBatchAnalysis>(analysisResult);

    // Adapter: ReplayTransactionOutcome -> existing presentation entries.
    // No second list model; the shared dashboard renders whatever batch is
    // current, no matter which source produced it.
    std::vector<TransactionListEntry> entries;
    std::vector<modbuslens::core::DiagnosisTransaction> diagnosisTransactions;
    entries.reserve(batch.transactions.size());
    diagnosisTransactions.reserve(batch.transactions.size());
    for (const auto& outcome : batch.transactions) {
        entries.push_back(TransactionListEntry{
            .deviceAddress = static_cast<int>(outcome.deviceAddress),
            .functionCode = static_cast<int>(outcome.functionCode),
            .status = outcome.analysis.status,
            .elapsedMs = outcome.analysis.elapsed.count(),
            .exceptionCode = outcome.analysis.exceptionCode,
            .issueText = composeIssueText(outcome.analysis, outcome.requestIssues),
            .activeSerialProvenance = std::nullopt,
        });
        diagnosisTransactions.push_back(modbuslens::core::DiagnosisTransaction{
            .deviceAddress = outcome.deviceAddress,
            .functionCode = outcome.functionCode,
            .analysis = outcome.analysis,
            .requestIssues = outcome.requestIssues,
        });
    }

    // T015 Gate F disclosure: unsupported records are reported (never
    // silently dropped) without masquerading as a TransactionStatus.
    if (batch.unsupportedRecords.empty()) {
        clearReplayNotice();
    } else {
        setReplayNotice(
            QStringLiteral("提示：%1 条记录当前未支持分析（功能码 %2 等），未计入统计。")
                .arg(batch.unsupportedRecords.size())
                .arg(QStringLiteral("0x%1").arg(
                    QString::number(batch.unsupportedRecords.front().functionCode, 16)
                        .toUpper()
                        .rightJustified(2, QLatin1Char('0')))));
    }

    // Atomic publish (rule B): everything below runs only after the whole
    // read/parse/analyze/adapt pipeline succeeded. Any earlier failure
    // returned without touching a single piece of the old state (rule A) —
    // including an open serial transport, which stays intact on a failed
    // replay load.
    //
    // Serial teardown is part of the successful switch ONLY (SB-13): the
    // port is closed after the replay data is fully validated, never before.
    // M10-C1: a SUCCESSFUL replay load replaces the source. A FAILED load
    // returned long before this point and must invalidate nothing.
    if (preparedWriteStore_.invalidate(
            modbuslens::core::PreparedWriteInvalidReason::SourceChanged)) {
        announcePreparedWriteChanged();
    }
    teardownSerialTransport();
    transactionModel_.setEntries(std::move(entries));
    statistics_ = batch.statistics;
    activeDiagnosisTransactions_ = std::move(diagnosisTransactions);
    invalidateAiForBatchChange();
    modeLabel_ = QStringLiteral("回放模式");
    // Presentation keeps the basename only; the full path never enters the UI.
    sourceLabel_ = QFileInfo(filePath).fileName();
    sourceKind_ = modbuslens::core::TransactionSourceKind::Replay;
    activeSerialRecords_.clear();
    activeSerialTerminations_.clear();
    clearReplayError();
    clearSerialError();
    emit statisticsChanged();
    emit sourceChanged();
}