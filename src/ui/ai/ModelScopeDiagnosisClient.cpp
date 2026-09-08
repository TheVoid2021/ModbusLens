#include "ui/ai/ModelScopeDiagnosisClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <algorithm>
#include <climits>

namespace {

const QUrl kOfficialEndpoint{
    QStringLiteral("https://api-inference.modelscope.cn/v1/chat/completions")};
const QString kCandidateModel = QStringLiteral("Qwen/Qwen3.5-27B");
const QString kOfficialApiKeyEnv = QStringLiteral("MODELSCOPE_API_KEY");
const QString kModelOverrideEnv = QStringLiteral("MODBUSLENS_MODELSCOPE_MODEL");
constexpr qsizetype kMaxOutputTokens = 768;

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
QString sanitizeProviderMessage(const QByteArray& body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject()) {
        const QJsonValue errorValue = doc.object().value(QLatin1String("error"));
        if (errorValue.isObject()) {
            const QJsonValue message = errorValue.toObject().value(QLatin1String("message"));
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
    return QStringLiteral("provider error (no sanitized message)");
}

AiDiagnosisErrorCode mapReplyError(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::AuthenticationRequiredError:
        return AiDiagnosisErrorCode::Unauthorized;
    // OperationCanceledError is handled by the explicit abort-reason branch
    // before this mapper (ISSUE-005) — never fuzz through here.
    case QNetworkReply::ProtocolInvalidOperationError:
        return AiDiagnosisErrorCode::ProviderRequestError; // e.g. rejected redirect
    default:
        return AiDiagnosisErrorCode::NetworkError;
    }
}

} // namespace

ModelScopeDiagnosisClient::ModelScopeDiagnosisClient(QObject* parent)
    : QObject(parent)
{
    network_ = new QNetworkAccessManager(this);
    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, &ModelScopeDiagnosisClient::handleTimeout);
}

void ModelScopeDiagnosisClient::configure(const ModelScopeClientConfig& config)
{
    config_ = config;
}

bool ModelScopeDiagnosisClient::isConfigured() const
{
    return config_.endpoint.isValid() && !config_.apiKey.isEmpty()
        && !config_.modelId.isEmpty();
}

QString ModelScopeDiagnosisClient::modelName() const
{
    return config_.modelId;
}

void ModelScopeDiagnosisClient::requestDiagnosis(const QString& systemPrompt,
                                                 const QString& userPrompt,
                                                 std::uint64_t requestId)
{
    if (busy_) {
        fail(requestId, AiDiagnosisErrorCode::Busy, QStringLiteral("already busy"));
        return;
    }
    if (!isConfigured()) {
        fail(requestId, AiDiagnosisErrorCode::NotConfigured,
             QStringLiteral("ModelScope client is not configured"));
        return;
    }

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
        {QStringLiteral("model"), config_.modelId},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("stream"), false},
        {QStringLiteral("max_tokens"), kMaxOutputTokens},
    };

    QNetworkRequest request(config_.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    // Authorization is attached ONLY here — to the configured endpoint.
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + config_.apiKey.toUtf8());
    // TLS: default peer verification stays ON. Redirects: same-origin only
    // (a cross-origin redirect must fail instead of carrying the Bearer
    // token to another origin).
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    // NOTE (ISSUE-005): NO QNetworkRequest::setTransferTimeout here — the
    // QTimer below is the SINGLE timeout owner. Dual native-transfer-timeout
    // + QTimer ownership made "which timeout aborted" racy and leaked the
    // localized OperationCanceledError text into the UI.

    reply_ = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::finished, this, &ModelScopeDiagnosisClient::handleFinished);
    currentRequestId_ = requestId;
    busy_ = true;
    abortReason_ = AiAbortReason::None;
    timeoutTimer_->start(static_cast<int>(std::min<std::chrono::milliseconds>(
        config_.timeout, std::chrono::milliseconds{INT_MAX}).count()));
}

void ModelScopeDiagnosisClient::cancel(AiAbortReason reason)
{
    if (!busy_) {
        return;
    }
    abortReason_ = reason; // recorded BEFORE the abort (ISSUE-005 ownership)
    timeoutTimer_->stop();
    busy_ = false;
    if (reply_) {
        reply_->abort();
    }
    currentRequestId_ = 0;
}

bool ModelScopeDiagnosisClient::isBusy() const
{
    return busy_;
}

void ModelScopeDiagnosisClient::fail(std::uint64_t requestId,
                                     AiDiagnosisErrorCode code,
                                     const QString& message)
{
    busy_ = false;
    currentRequestId_ = 0;
    emit diagnosisFailed(requestId, code, message);
}

void ModelScopeDiagnosisClient::handleTimeout()
{
    if (!busy_) {
        return;
    }
    const std::uint64_t requestId = currentRequestId_;
    timeoutTimer_->stop();
    abortReason_ = AiAbortReason::Timeout; // BEFORE the abort (ISSUE-005)
    if (reply_) {
        reply_->abort();
    }
    // Deliver the business-level Timeout immediately; the subsequent
    // finished() callback sees busy_ == false and stays silent.
    fail(requestId, AiDiagnosisErrorCode::Timeout, QStringLiteral("AI request timed out"));
}

void ModelScopeDiagnosisClient::handleFinished()
{
    if (!busy_) {
        // A stray finish after timeout or cancel: nothing to deliver.
        return;
    }
    timeoutTimer_->stop();

    const std::uint64_t requestId = currentRequestId_;
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    busy_ = false;
    currentRequestId_ = 0;

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus > 0 && (httpStatus < 200 || httpStatus >= 300)) {
        // HTTP-level outcome first: Qt also sets reply->error() for non-2xx,
        // but the http status attribute is the authoritative mapping input.
        const auto code = mapHttpStatus(httpStatus);
        const QString message = sanitizeProviderMessage(reply->readAll());
        reply->deleteLater();
        emit diagnosisFailed(requestId, code, message);
        return;
    }
    // Transport cancellation (ISSUE-005): OperationCanceledError alone
    // cannot say WHO aborted — classify by the recorded abort reason. Only
    // an unexpected cancellation becomes a user-visible network error; the
    // Qt localized errorString is never user-facing wording.
    if (httpStatus == 0
        && reply->error() == QNetworkReply::OperationCanceledError) {
        if (abortReason_ == AiAbortReason::Timeout) {
            reply->deleteLater();
            emit diagnosisFailed(requestId, AiDiagnosisErrorCode::Timeout,
                                 QStringLiteral("AI request timed out"));
            return;
        }
        if (abortReason_ != AiAbortReason::None) {
            reply->deleteLater();
            return; // local intentional abort: silent, no signal
        }
        reply->deleteLater();
        emit diagnosisFailed(requestId, AiDiagnosisErrorCode::NetworkError,
                             QStringLiteral("AI request was cancelled unexpectedly"));
        return;
    }
    if (httpStatus == 0 && reply->error() != QNetworkReply::NoError) {
        // No HTTP status at all: a non-cancellation transport failure.
        const auto code = mapReplyError(reply->error());
        reply->deleteLater();
        emit diagnosisFailed(requestId, code, QStringLiteral("network error"));
        return;
    }

    // HTTP 2xx: the success contract needs a usable final content.
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject() || !document.object().value(QLatin1String("choices")).isArray()) {
        emit diagnosisFailed(requestId, AiDiagnosisErrorCode::InvalidResponse,
                             QStringLiteral("malformed response"));
        return;
    }
    const QJsonArray choices = document.object().value(QLatin1String("choices")).toArray();
    for (const QJsonValue& value : choices) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonValue message = value.toObject().value(QLatin1String("message"));
        if (!message.isObject()) {
            continue;
        }
        const QJsonValue content = message.toObject().value(QLatin1String("content"));
        // reasoning_content (whatever the model emits alongside) is
        // deliberately NOT consumed: only the final assistant content is
        // product state.
        if (content.isString() && !content.toString().isEmpty()) {
            emit diagnosisSucceeded(requestId, content.toString());
            return;
        }
    }
    emit diagnosisFailed(requestId, AiDiagnosisErrorCode::InvalidResponse,
                         QStringLiteral("no usable final content in response"));
}

bool buildModelScopeProductionConfig(ModelScopeClientConfig& out)
{
    const QString apiKey = qEnvironmentVariable("MODELSCOPE_API_KEY");
    if (apiKey.trimmed().isEmpty()) {
        return false;
    }
    QString model = qEnvironmentVariable("MODBUSLENS_MODELSCOPE_MODEL");
    if (model.trimmed().isEmpty()) {
        model = kCandidateModel;
    }
    // 90 s production timeout (ISSUE-005): a non-streaming cloud LLM
    // inference is not an interactive millisecond request — live evidence
    // showed the 27B model can exceed 30 s under load. Tests still inject
    // 50~100 ms timeouts for fast coverage.
    out.endpoint = kOfficialEndpoint;
    out.apiKey = apiKey;
    out.modelId = model;
    out.timeout = std::chrono::milliseconds{90000};
    return true;
}