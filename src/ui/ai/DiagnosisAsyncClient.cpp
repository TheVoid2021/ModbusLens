#include "ui/ai/DiagnosisAsyncClient.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <utility>

namespace modbuslens::ui {

namespace {

// Diagnosis payload contract (frozen M6 semantics): a plain OpenAI-compatible
// chat-completions body. Diagnosis deliberately does NOT apply the Candidate
// chain's provider extras (response_format / thinking) — its payload and
// parser are Diagnosis-specific.
constexpr int kMaxOutputTokens = 768;

AiDiagnosisErrorCode mapHttpStatus(int status)
{
    if (status == 401 || status == 403) {
        return AiDiagnosisErrorCode::Unauthorized;
    }
    if (status == 429) {
        return AiDiagnosisErrorCode::RateLimited;
    }
    if (status == 400 || status == 404 || status == 422) {
        return AiDiagnosisErrorCode::ProviderRequestError;
    }
    if (status >= 500) {
        return AiDiagnosisErrorCode::ServerError;
    }
    return AiDiagnosisErrorCode::ProviderRequestError;
}

// Never surfaced: token / Authorization / full body. Only a bounded, sanitized
// provider message.
QString sanitizeProviderMessage(const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonValue errorValue = doc.object().value(QLatin1String("error"));
        if (errorValue.isObject()) {
            const QJsonValue message =
                errorValue.toObject().value(QLatin1String("message"));
            if (message.isString()) {
                QString text = message.toString().simplified();
                if (text.size() > 200) {
                    text.truncate(200);
                    text += QStringLiteral("...");
                }
                return text;
            }
        }
    }
    return QStringLiteral("模型服务错误（无可用错误信息）");
}

std::string buildDiagnosisBody(const std::string& model,
                               const QString& systemPrompt,
                               const QString& userPrompt,
                               AiProviderKind kind)
{
    QJsonArray messages;
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("system")},
        {QStringLiteral("content"), systemPrompt},
    });
    messages.append(QJsonObject{
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("content"), userPrompt},
    });
    QJsonObject body{
        {QStringLiteral("model"), QString::fromStdString(model)},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("stream"), false},
        {QStringLiteral("max_tokens"), kMaxOutputTokens},
    };
    // D3-F2 Part A: the SAME provider extra the Candidate chain applies
    // (CandidateProviderAdapter.cpp:857) — DeepSeek's thinking pass defaults to
    // ON, and this contract also consumes ONLY the final assistant content, so
    // the bounded output budget must not be spent on a reasoning pass whose
    // text is deliberately never surfaced. ModelScope's profile flag is false,
    // so its request stays byte-for-byte what it was.
    if (aiProviderProfile(kind).sendThinkingDisabledExtra) {
        body.insert(QStringLiteral("thinking"),
                    QJsonObject{{QStringLiteral("type"),
                                 QStringLiteral("disabled")}});
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();
}

} // namespace

DiagnosisAsyncClient::DiagnosisAsyncClient(QObject* parent)
    : QObject(parent),
      ownedHttp_(std::make_unique<QtChatCompletionsHttpClient>()),
      http_(ownedHttp_.get())
{
}

DiagnosisAsyncClient::DiagnosisAsyncClient(IChatCompletionsHttpClient& http,
                                           QObject* parent)
    : QObject(parent),
      http_(&http)
{
}

bool DiagnosisAsyncClient::requestDiagnosis(const DiagnosisDispatchConfig& config,
                                            const QString& systemPrompt,
                                            const QString& userPrompt,
                                            std::uint64_t requestId)
{
    // Fail closed BEFORE building anything: a missing endpoint, model,
    // credential — or a MISSING PROVIDER KIND (D3-F2 Part A) — must never
    // become an authenticated request. The kind is required because the
    // provider-specific request semantics depend on it; it is never inferred
    // from the endpoint or model string.
    if (config.endpoint.empty() || config.model.empty() || config.secret.empty()
        || !config.kind.has_value() || http_ == nullptr) {
        return false;
    }

    const std::string body = buildDiagnosisBody(config.model, systemPrompt,
                                                userPrompt, *config.kind);

    // Request-scoped transport. The credential is handed over as a ONE-SHOT
    // bearer token consumed by sendAsync, so it is never retained beyond this
    // dispatch; the transport itself is erased when the dispatch completes.
    auto transport = std::make_unique<ChatCompletionsTransport>(
        *http_, config.endpoint, config.timeoutMs);
    transport->setOneShotBearerToken(config.secret);

    const ChatCompletionsHttpAsyncHandle handle = transport->sendAsync(
        body, this,
        [this, requestId](ChatCompletionsHttpResult result) {
            handleCompletion(requestId, std::move(result));
        });
    if (!handle.valid()) {
        return false; // nothing dispatched, no completion will arrive
    }
    transports_.emplace(requestId, std::move(transport));
    ++dispatchCount_;
    return true;
}

void DiagnosisAsyncClient::handleCompletion(std::uint64_t requestId,
                                            ChatCompletionsHttpResult result)
{
    // Release the request-scoped transport (and any credential it carried)
    // before delivering the outcome.
    transports_.erase(requestId);

    if (result.timedOut) {
        emit diagnosisFailed(requestId, AiDiagnosisErrorCode::Timeout,
                             QStringLiteral("AI 请求超时"));
        return;
    }
    if (!result.transportOk) {
        emit diagnosisFailed(requestId, AiDiagnosisErrorCode::NetworkError,
                             QStringLiteral("网络错误"));
        return;
    }
    const int status = result.status;
    if (status <= 0) {
        emit diagnosisFailed(requestId, AiDiagnosisErrorCode::NetworkError,
                             QStringLiteral("网络错误"));
        return;
    }
    if (status < 200 || status >= 300) {
        emit diagnosisFailed(
            requestId, mapHttpStatus(status),
            sanitizeProviderMessage(QByteArray::fromStdString(result.body)));
        return;
    }

    // 2xx: the success contract needs a usable final assistant content.
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(result.body));
    if (!document.isObject()
        || !document.object().value(QLatin1String("choices")).isArray()) {
        emit diagnosisFailed(requestId, AiDiagnosisErrorCode::InvalidResponse,
                             QStringLiteral("响应格式无效"));
        return;
    }
    const QJsonArray choices =
        document.object().value(QLatin1String("choices")).toArray();
    // D3-F1 (T034-B2-HV2-D-D3-F1): a length stop is recorded REGARDLESS of
    // content usability — it explains WHY no usable text arrived. The D3 RCA
    // could not tell a length-truncated reply apart from a genuinely
    // content-less one, so the offline contract now distinguishes them.
    bool lengthStopped = false;
    for (const QJsonValue& value : choices) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject choice = value.toObject();
        const QJsonValue finish =
            choice.value(QLatin1String("finish_reason"));
        if (finish.isString() && finish.toString() == QLatin1String("length")) {
            lengthStopped = true;
        }
        const QJsonValue message = choice.value(QLatin1String("message"));
        if (!message.isObject()) {
            continue;
        }
        // reasoning_content (whatever the model emits alongside) is
        // deliberately NOT consumed: only the final assistant content is
        // product state.
        const QJsonValue content =
            message.toObject().value(QLatin1String("content"));
        if (content.isString() && !content.toString().isEmpty()) {
            emit diagnosisSucceeded(requestId, content.toString());
            return;
        }
    }
    // D3-F1: distinguish a bounded-length STOP from a genuinely content-less
    // reply. The provider classification (InvalidResponse) is unchanged, no
    // automatic retry happens, and reasoning text is never surfaced as the
    // answer — only the user-facing explanation becomes accurate.
    emit diagnosisFailed(
        requestId, AiDiagnosisErrorCode::InvalidResponse,
        lengthStopped
            ? QStringLiteral("模型输出在长度上限处被截断，未返回可用的解释文本"
                             "（finish_reason=length）")
            : QStringLiteral("响应中无可用内容"));
}

} // namespace modbuslens::ui
