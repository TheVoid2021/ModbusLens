#include "ui/agent/ModelScopeAgentClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include <algorithm>
#include <climits>

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
    return QStringLiteral("模型服务错误（无可用错误信息）");
}

AiDiagnosisErrorCode mapReplyError(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::AuthenticationRequiredError:
        return AiDiagnosisErrorCode::Unauthorized;
    case QNetworkReply::ProtocolInvalidOperationError:
        return AiDiagnosisErrorCode::ProviderRequestError;
    default:
        return AiDiagnosisErrorCode::NetworkError;
    }
}

} // namespace

ModelScopeAgentClient::ModelScopeAgentClient(QObject* parent)
    : QObject(parent)
{
    network_ = new QNetworkAccessManager(this);
    timeoutTimer_ = new QTimer(this);
    timeoutTimer_->setSingleShot(true);
    connect(timeoutTimer_, &QTimer::timeout, this, &ModelScopeAgentClient::handleTimeout);
}

void ModelScopeAgentClient::configure(const ModelScopeClientConfig& config)
{
    config_ = config;
}

bool ModelScopeAgentClient::isConfigured() const
{
    return config_.endpoint.isValid() && !config_.apiKey.isEmpty()
        && !config_.modelId.isEmpty();
}

QString ModelScopeAgentClient::modelName() const
{
    return config_.modelId;
}

void ModelScopeAgentClient::requestRound(std::uint64_t runGeneration,
                                         const QJsonArray& messages,
                                         const QJsonArray& tools)
{
    // Single-flight: exactly one in-flight round (one QNetworkAccessManager,
    // one reply, one QTimer — the single timeout owner, ISSUE-005).
    if (busy_) {
        fail(runGeneration, AiDiagnosisErrorCode::Busy,
             QStringLiteral("已有请求进行中"));
        return;
    }
    if (!isConfigured()) {
        fail(runGeneration, AiDiagnosisErrorCode::NotConfigured,
             QStringLiteral("ModelScope Agent 客户端未配置"));
        return;
    }

    QJsonObject body{
        {QStringLiteral("model"), config_.modelId},
        {QStringLiteral("messages"), messages},
        {QStringLiteral("tools"), tools},
        {QStringLiteral("stream"), false},
        {QStringLiteral("max_tokens"), 768},
    };

    QNetworkRequest request(config_.endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/json"));
    request.setRawHeader("Authorization",
                         QByteArray("Bearer ") + config_.apiKey.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
    // SINGLE timeout owner: the QTimer below (no setTransferTimeout — see
    // ISSUE-005: dual ownership made aborts racy).

    reply_ = network_->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply_, &QNetworkReply::finished, this, &ModelScopeAgentClient::handleFinished);
    currentRunGeneration_ = runGeneration;
    busy_ = true;
    abortReason_ = AiAbortReason::None;
    timeoutTimer_->start(static_cast<int>(std::min<std::chrono::milliseconds>(
        config_.timeout, std::chrono::milliseconds{INT_MAX}).count()));
}

void ModelScopeAgentClient::cancel(AiAbortReason reason)
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
    currentRunGeneration_ = 0;
}

bool ModelScopeAgentClient::isBusy() const
{
    return busy_;
}

void ModelScopeAgentClient::fail(std::uint64_t runGeneration,
                                 AiDiagnosisErrorCode code,
                                 const QString& message)
{
    busy_ = false;
    currentRunGeneration_ = 0;
    emit roundFailed(runGeneration, code, message);
}

void ModelScopeAgentClient::handleTimeout()
{
    if (!busy_) {
        return;
    }
    const std::uint64_t runGeneration = currentRunGeneration_;
    timeoutTimer_->stop();
    abortReason_ = AiAbortReason::Timeout; // BEFORE the abort
    if (reply_) {
        reply_->abort();
    }
    fail(runGeneration, AiDiagnosisErrorCode::Timeout,
         QStringLiteral("AI 请求超时"));
}

void ModelScopeAgentClient::handleFinished()
{
    if (!busy_) {
        return; // stray finish after timeout or cancel: nothing to deliver
    }
    timeoutTimer_->stop();

    const std::uint64_t runGeneration = currentRunGeneration_;
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    busy_ = false;
    currentRunGeneration_ = 0;

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (httpStatus > 0 && (httpStatus < 200 || httpStatus >= 300)) {
        const auto code = mapHttpStatus(httpStatus);
        const QString message = sanitizeProviderMessage(reply->readAll());
        reply->deleteLater();
        emit roundFailed(runGeneration, code, message);
        return;
    }
    if (httpStatus == 0
        && reply->error() == QNetworkReply::OperationCanceledError) {
        if (abortReason_ == AiAbortReason::Timeout) {
            reply->deleteLater();
            emit roundFailed(runGeneration, AiDiagnosisErrorCode::Timeout,
                             QStringLiteral("AI 请求超时"));
            return;
        }
        reply->deleteLater();
        if (abortReason_ != AiAbortReason::None) {
            return; // local intentional abort: silent, no signal
        }
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::NetworkError,
                         QStringLiteral("AI 请求意外中止"));
        return;
    }
    if (httpStatus == 0 && reply->error() != QNetworkReply::NoError) {
        const auto code = mapReplyError(reply->error());
        reply->deleteLater();
        emit roundFailed(runGeneration, code, QStringLiteral("网络错误"));
        return;
    }

    // HTTP 2xx: the success contract is a usable assistant MESSAGE OBJECT
    // (tool_calls live inside it). reasoning_content is carried by the
    // provider but is never treated as the answer (T011 contract).
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()
        || !document.object().value(QLatin1String("choices")).isArray()) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::InvalidResponse,
                         QStringLiteral("响应格式无效"));
        return;
    }
    const QJsonArray choices = document.object().value(QLatin1String("choices")).toArray();
    if (choices.isEmpty() || !choices.first().isObject()) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::InvalidResponse,
                         QStringLiteral("响应格式无效"));
        return;
    }
    const QJsonValue messageValue = choices.first().toObject().value(QLatin1String("message"));
    if (!messageValue.isObject()) {
        emit roundFailed(runGeneration, AiDiagnosisErrorCode::InvalidResponse,
                         QStringLiteral("响应格式无效"));
        return;
    }
    emit roundSucceeded(runGeneration, messageValue.toObject());
}