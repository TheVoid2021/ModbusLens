#include "ui/agent/AgentAsyncClient.h"

#include <QByteArray>
#include <QJsonDocument>

#include <utility>

namespace {

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

// Never log token / Authorization / full body. Only status + sanitized text.
QString sanitizeProviderMessage(const std::string& body)
{
    const QJsonDocument doc =
        QJsonDocument::fromJson(QByteArray::fromStdString(body));
    if (doc.isObject()) {
        const QJsonValue errorValue =
            doc.object().value(QLatin1String("error"));
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

} // namespace

AgentAsyncClient::AgentAsyncClient(QObject* parent)
    : AgentProviderClient(parent),
      ownedHttp_(std::make_unique<modbuslens::ui::QtChatCompletionsHttpClient>()),
      http_(ownedHttp_.get())
{
}

AgentAsyncClient::AgentAsyncClient(
    modbuslens::ui::IChatCompletionsHttpClient& http,
                                   QObject* parent)
    : AgentProviderClient(parent),
      http_(&http)
{
}

void AgentAsyncClient::setHttpClientForAutomation(
    modbuslens::ui::IChatCompletionsHttpClient& http)
{
    http_ = &http;
}

void AgentAsyncClient::requestRound(
    const modbuslens::agent::AgentRunConfig& config,
    std::uint64_t runGeneration, const QJsonArray& messages,
    const QJsonArray& tools)
{
    // Fail closed: a run whose snapshot is not usable never dispatches.
    if (!config.usable() || http_ == nullptr) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::NotConfigured,
                         QStringLiteral("Agent 未配置，无法发起请求"));
        return;
    }

    // The Agent payload is provider-NEUTRAL by construction: it carries the
    // standard OpenAI-compatible fields only, and there is NO provider-specific
    // wire extra in this request for ANY provider (audited T034-B2.2-C §10 —
    // ModelScope-only extras such as chat_template_kwargs do not exist here).
    // If one is ever added it MUST be gated on config.kind, never on the
    // endpoint string.
    QJsonObject body{
        {QStringLiteral("model"), QString::fromStdString(config.model)},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("stream"), false},
        {QStringLiteral("max_tokens"), 768},
    };

    const std::string bodyBytes =
        QJsonDocument(body).toJson(QJsonDocument::Compact).toStdString();

    // Request-scoped transport: the credential is handed over as a ONE-SHOT
    // bearer token consumed by sendAsync, and the transport itself is erased
    // when this exchange completes. Nothing is retained between exchanges.
    auto transport = std::make_unique<modbuslens::ui::ChatCompletionsTransport>(
        *http_, config.endpoint, config.timeoutMs);
    transport->setOneShotBearerToken(config.secret);

    const std::uint64_t exchangeId = ++nextExchangeId_;
    const modbuslens::ui::ChatCompletionsHttpAsyncHandle handle =
        transport->sendAsync(
        bodyBytes, this,
        [this, exchangeId, runGeneration](
            modbuslens::ui::ChatCompletionsHttpResult result) {
            handleCompletion(exchangeId, runGeneration, std::move(result));
        });
    if (!handle.valid()) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::NetworkError,
                         QStringLiteral("网络错误"));
        return;
    }
    transports_.emplace(exchangeId, std::move(transport));
    ++dispatchCount_;
}

void AgentAsyncClient::cancel(AiAbortReason /*reason*/)
{
    // Best-effort only and intentionally NON-destructive: the shared transport
    // offers no network cancellation, and this client must never drop another
    // run's in-flight exchange. Staleness is resolved by the owner's
    // generation guard, not here.
}

void AgentAsyncClient::handleCompletion(std::uint64_t exchangeId,
                                        std::uint64_t runGeneration,
                                        modbuslens::ui::ChatCompletionsHttpResult result)
{
    // Release this exchange's request-scoped transport (and any credential it
    // carried) before delivering the outcome.
    transports_.erase(exchangeId);

    if (result.timedOut) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::Timeout,
                         QStringLiteral("AI 请求超时"));
        return;
    }
    if (!result.transportOk) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::NetworkError,
                         QStringLiteral("网络错误"));
        return;
    }
    if (result.status < 200 || result.status >= 300) {
        emit roundFailed(runGeneration, mapHttpStatus(result.status),
                         sanitizeProviderMessage(result.body));
        return;
    }

    // HTTP 2xx: the success contract is a usable assistant MESSAGE OBJECT
    // (tool_calls live inside it). reasoning_content is carried by the
    // provider but is never treated as the answer (T011 contract).
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(result.body));
    if (!document.isObject()
        || !document.object().value(QLatin1String("choices")).isArray()) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::InvalidResponse,
                         QStringLiteral("响应格式无效"));
        return;
    }
    const QJsonArray choices =
        document.object().value(QLatin1String("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject()) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::InvalidResponse,
                         QStringLiteral("响应格式无效"));
        return;
    }
    const QJsonValue messageValue =
        choices.first().toObject().value(QLatin1String("message"));
    if (!messageValue.isObject()) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::InvalidResponse,
                         QStringLiteral("响应格式无效"));
        return;
    }
    emit roundSucceeded(runGeneration, messageValue.toObject());
}
