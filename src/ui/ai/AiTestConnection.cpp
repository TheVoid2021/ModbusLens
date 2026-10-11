#include "ui/ai/AiTestConnection.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <utility>

#include "ui/candidate/ChatCompletionsHttpClient.h"

namespace modbuslens::ui {

ChatCompletionsHttpRequest buildTestConnectionHttpRequest(
    const AiTestConnectionRequest &request)
{
    const AiProviderProfile &profile = aiProviderProfile(request.kind);

    // FIXED synthetic probe: no manual/profile/transaction data whatsoever.
    QJsonObject body{{QStringLiteral("model"), request.model},
                     {QStringLiteral("stream"), false},
                     {QStringLiteral("max_tokens"), 8},
                     {QStringLiteral("messages"),
                      QJsonArray{QJsonObject{
                          {QStringLiteral("role"), QStringLiteral("user")},
                          {QStringLiteral("content"),
                           QStringLiteral("Reply with the single word OK.")}}}}};
    if (profile.sendThinkingDisabledExtra) {
        // DeepSeek: the probe must not burn the output budget on a thinking
        // pass (same provider-specific semantics as the extraction chain).
        body.insert(QStringLiteral("thinking"),
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("disabled")}});
    }

    ChatCompletionsHttpRequest httpRequest;
    httpRequest.url = profile.chatCompletionsEndpoint;
    httpRequest.bearerToken = request.secret.toStdString();
    httpRequest.contentType = "application/json";
    httpRequest.body = std::string(QJsonDocument(body).toJson(QJsonDocument::Compact));
    httpRequest.timeoutMs = request.timeoutMs;
    return httpRequest;
}

AiTestConnectionResult classifyTestConnectionReply(
    const ChatCompletionsHttpResult &reply)
{
    AiTestConnectionResult result;
    result.httpStatus = reply.status;

    if (!reply.transportOk) {
        result.token = reply.timedOut ? QStringLiteral("timeout")
                                      : QStringLiteral("network_error");
        return result;
    }
    const bool statusOk = reply.status >= 200 && reply.status < 300;
    if (!statusOk) {
        switch (reply.status) {
        case 401:
        case 403:
            result.token = QStringLiteral("auth_failed");
            break;
        case 402:
            result.token = QStringLiteral("insufficient_balance");
            break;
        case 429:
            result.token = QStringLiteral("rate_limited");
            break;
        case 400:
        case 404:
        case 422:
            result.token = QStringLiteral("model_or_params_rejected");
            break;
        default:
            result.token = QStringLiteral("provider_error");
            break;
        }
        return result;
    }

    // 2xx: Test Connection is a CONNECTIVITY probe, not an answer-quality
    // parser. A valid Chat Completions envelope proves the endpoint is
    // reachable, the credential was accepted, the model was accepted and the
    // request was understood. It must NOT require a usable final assistant
    // STRING: a thinking-first model under a tiny synthetic token budget can
    // legitimately return an empty/null `content` (sometimes alongside a
    // populated `reasoning_content`). `reasoning_content` is NEVER read, and
    // the raw body is never logged or exposed.
    // (T034-B2.1-F1 classifier correction — request shape unchanged.)
    const QJsonDocument document =
        QJsonDocument::fromJson(QByteArray::fromStdString(reply.body));
    if (!document.isObject()) {
        result.token = QStringLiteral("invalid_response");
        return result;
    }
    const QJsonObject root = document.object();
    const QJsonValue choicesValue = root.value(QStringLiteral("choices"));
    if (!choicesValue.isArray()) {
        result.token = QStringLiteral("invalid_response");
        return result;
    }
    const QJsonArray choices = choicesValue.toArray();
    if (choices.isEmpty()) {
        result.token = QStringLiteral("invalid_response");
        return result;
    }
    // Any choice whose `message` is itself a JSON object is a well-formed
    // Chat Completions envelope. No requirement is placed on content type,
    // content emptiness, role, reasoning_content or the final text.
    for (const QJsonValue &choice : choices) {
        if (!choice.isObject()) {
            continue;
        }
        const QJsonValue message =
            choice.toObject().value(QStringLiteral("message"));
        if (message.isObject()) {
            result.ok = true;
            result.token = QStringLiteral("connection_ok");
            return result;
        }
    }
    result.token = QStringLiteral("invalid_response");
    return result;
}

AiTestConnectionResult runAiTestConnection(IChatCompletionsHttpClient &http,
                                           AiProviderKind kind, const QString &model,
                                           const QString &secret, int timeoutMs)
{
    const AiTestConnectionRequest request{kind, model, secret, timeoutMs};
    const ChatCompletionsHttpResult reply =
        http.post(buildTestConnectionHttpRequest(request));
    return classifyTestConnectionReply(reply);
}

QString aiConnectionStatusText(const QString &token)
{
    if (token == QStringLiteral("connection_ok")) {
        return QStringLiteral("连接成功");
    }
    if (token == QStringLiteral("auth_failed")) {
        // E1b-C: source-neutral wording — the tested key may be a TRANSIENT
        // unsaved value or the stored credential; the UI must not claim
        // which one it was.
        return QStringLiteral("API Key 验证失败，请核对当前 API Key");
    }
    if (token == QStringLiteral("insufficient_balance")) {
        return QStringLiteral("Provider 账户余额/额度不足");
    }
    if (token == QStringLiteral("rate_limited")) {
        return QStringLiteral("请求过于频繁（Provider 限流），请稍后再试");
    }
    if (token == QStringLiteral("model_or_params_rejected")) {
        return QStringLiteral("模型 ID 或请求参数不可用，请检查 Model");
    }
    if (token == QStringLiteral("provider_error")) {
        return QStringLiteral("Provider 服务暂时异常，请稍后再试");
    }
    if (token == QStringLiteral("network_error")) {
        return QStringLiteral("网络连接失败");
    }
    if (token == QStringLiteral("timeout")) {
        return QStringLiteral("请求超时");
    }
    if (token == QStringLiteral("invalid_response")) {
        return QStringLiteral("Provider 返回格式异常");
    }
    if (token == QStringLiteral("not_tested")) {
        return QStringLiteral("未测试");
    }
    if (token == QStringLiteral("testing")) {
        return QStringLiteral("测试中…");
    }
    return token;
}

// ---------------------------------------------------------------------------
// E1a — the production ASYNC implementation. Same security posture as the
// accepted synchronous client (official HTTPS endpoint only, Authorization
// only to the configured endpoint, same-origin redirects, TLS verification
// ON, single QTimer timeout owner, NO setTransferTimeout) but WITHOUT any
// QEventLoop: dispatch returns immediately, completion arrives via the
// reply's finished signal on the caller's thread, and exactly ONE terminal
// result is published per accepted request (a late finish after the timeout
// is ignored by the per-request finished flag).
// ---------------------------------------------------------------------------
struct QtAsyncTestConnectionClient::InFlight
{
    QNetworkReply *reply{nullptr};
    bool finished{false};
};

QtAsyncTestConnectionClient::QtAsyncTestConnectionClient(QObject *parent)
    : QObject(parent)
{
}

QtAsyncTestConnectionClient::QtAsyncTestConnectionClient(
    std::function<QUrl(AiProviderKind)> testEndpointResolver, QObject *parent)
    : QObject(parent),
      testEndpointResolver_(std::move(testEndpointResolver))
{
}

QtAsyncTestConnectionClient::~QtAsyncTestConnectionClient() = default;


int QtAsyncTestConnectionClient::start(
    const AiTestConnectionRequest &request,
    const std::function<void(const AiTestConnectionResult &)> &onDone)
{
    const ChatCompletionsHttpRequest httpRequest =
        buildTestConnectionHttpRequest(request);

    // E2 ENDPOINT AUTHORITY — two strict modes:
    //   · PRODUCTION default: provider profile compiled constant, HTTPS-only
    //     guard (unchanged by E2);
    //   · TEST-ONLY injected resolver: the resolver decides ONLY the URL and
    //     its output is LOOPBACK-ONLY (host 127.0.0.1 / ::1, scheme http or
    //     https). Anything else is rejected BEFORE any network object exists.
    // Payload, headers and classification are IDENTICAL in both modes.
    QUrl endpoint;
    if (testEndpointResolver_) {
        endpoint = testEndpointResolver_(request.kind);
        if (!endpoint.isValid()
            || (endpoint.scheme() != QLatin1String("http")
                && endpoint.scheme() != QLatin1String("https"))
            || (endpoint.host() != QLatin1String("127.0.0.1")
                && endpoint.host() != QLatin1String("::1"))) {
            // PUBLIC NETWORK HARD FAIL GUARD: non-loopback test endpoints are
            // rejected before any network object is created.
            return -1;
        }
    } else {
        endpoint = QUrl(QString::fromStdString(httpRequest.url));
        if (!endpoint.isValid()
            || endpoint.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive)
                   != 0) {
            // Production must target the official HTTPS endpoint only. Anything
            // else fails closed and dispatches NOTHING.
            return -1;
        }
    }

    auto *manager = new QNetworkAccessManager(this);

    QNetworkRequest networkRequest(endpoint);
    networkRequest.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QString::fromStdString(httpRequest.contentType));
    networkRequest.setRawHeader(
        "Authorization",
        QByteArray("Bearer ") + QByteArray::fromStdString(httpRequest.bearerToken));
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::SameOriginRedirectPolicy);
    // NOTE (mirrors M6 / ISSUE-005): NO setTransferTimeout — the QTimer below
    // is the SINGLE timeout owner.

    QNetworkReply *reply =
        manager->post(networkRequest, QByteArray::fromStdString(httpRequest.body));

    const int requestId = ++nextId_;
    auto state = std::make_unique<InFlight>();
    state->reply = reply;
    state->finished = false;
    inFlight_[requestId] = std::move(state);

    auto *timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    const int boundedMs = std::max(1, std::min(request.timeoutMs,
                                               std::numeric_limits<int>::max()));

    // Terminal publisher: exactly one per request (the finished flag makes a
    // late network finish after the timeout a no-op).
    auto publishFromReply =
        [this, requestId, reply, manager, onDone]() {
            auto it = inFlight_.find(requestId);
            if (it == inFlight_.end() || it->second->finished) {
                return; // already published / invalidated: never republish
            }
            it->second->finished = true;
            inFlight_.erase(it);

            ChatCompletionsHttpResult replyResult;
            replyResult.status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (replyResult.status > 0) {
                replyResult.transportOk = true;
                replyResult.body = reply->readAll().toStdString();
            }
            // status == 0 without the timeout flag = transport-level failure.
            if (onDone) {
                onDone(classifyTestConnectionReply(replyResult));
            }
            reply->deleteLater();
            manager->deleteLater();
        };

    // Timeout owner (SINGLE authority): abort (best-effort network
    // cancellation) and publish the terminal "timeout" result exactly once.
    // A late network finished after this is ignored by the finished flag.
    QObject::connect(timeoutTimer, &QTimer::timeout, reply,
                     [this, requestId, reply, manager, onDone]() {
                         auto it = inFlight_.find(requestId);
                         if (it == inFlight_.end() || it->second->finished) {
                             return;
                         }
                         it->second->finished = true;
                         inFlight_.erase(it);
                         reply->abort(); // best-effort network cancellation
                         AiTestConnectionResult result;
                         result.token = QStringLiteral("timeout");
                         if (onDone) {
                             onDone(result);
                         }
                         reply->deleteLater();
                         manager->deleteLater();
                     });

    QObject::connect(reply, &QNetworkReply::finished, reply,
                     [publishFromReply]() { publishFromReply(); });

    timeoutTimer->start(boundedMs);
    return requestId;
}

void QtAsyncTestConnectionClient::cancel(int requestId)
{
    // BEST-EFFORT network cancellation only. Stale-safety is the caller's
    // generation guard: a late finished after this abort is ignored by the
    // per-request finished flag / controller generation.
    const auto it = inFlight_.find(requestId);
    if (it != inFlight_.end() && it->second->reply != nullptr) {
        it->second->reply->abort();
    }
}

} // namespace modbuslens::ui
