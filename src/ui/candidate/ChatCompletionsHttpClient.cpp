#include "ui/candidate/ChatCompletionsHttpClient.h"

#include <QByteArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <utility>

namespace modbuslens::ui {

namespace {

// Production endpoints are ALWAYS https. The ONE deliberate exception is the
// loopback affordance that lets the deterministic offline suites drive the
// REAL async path against a localhost fake server: http://127.0.0.1 is not a
// reachable production target and carries no exfiltration risk. This applies
// to the ASYNC path ONLY — the accepted synchronous post() semantics used by
// the Candidate chain are untouched.
[[nodiscard]] bool asyncEndpointAcceptable(const QUrl &endpoint)
{
    if (!endpoint.isValid()) {
        return false;
    }
    if (endpoint.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive)
        == 0) {
        return true;
    }
    return endpoint.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive)
               == 0
           && endpoint.host() == QLatin1String("127.0.0.1");
}

} // namespace

QtChatCompletionsHttpClient::QtChatCompletionsHttpClient(QObject *parent) : QObject(parent)
{
}

QtChatCompletionsHttpClient::~QtChatCompletionsHttpClient()
{
    for (auto &entry : inFlight_) {
        entry.second.done = true;
        teardown(entry.second);
    }
    inFlight_.clear();
}

ChatCompletionsHttpResult QtChatCompletionsHttpClient::post(
    const ChatCompletionsHttpRequest &request)
{
    ChatCompletionsHttpResult result;

    const QUrl endpoint(QString::fromStdString(request.url));
    if (!endpoint.isValid()
        || endpoint.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive)
               != 0) {
        // Production must target the official HTTPS endpoint only. Anything
        // else fails closed and sends NOTHING.
        return result;
    }

    // One manager per exchange keeps reply lifetime trivially bounded: nothing
    // can outlive this call.
    QNetworkAccessManager manager;

    QNetworkRequest networkRequest(endpoint);
    networkRequest.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QString::fromStdString(request.contentType));
    // Authorization is attached ONLY to the endpoint above; nothing else ever
    // receives this header.
    networkRequest.setRawHeader(
        "Authorization",
        QByteArray("Bearer ") + QByteArray::fromStdString(request.bearerToken));
    // TLS: default peer verification stays ON. Redirects: same-origin only.
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                               QNetworkRequest::SameOriginRedirectPolicy);
    // NOTE (mirrors M6 / ISSUE-005): NO setTransferTimeout — the QTimer below
    // is the SINGLE timeout owner.

    QNetworkReply *reply = manager.post(
        networkRequest, QByteArray::fromStdString(request.body));

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    const int boundedMs =
        std::max(1, std::min(request.timeoutMs, std::numeric_limits<int>::max()));
    bool timedOut = false;
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timeoutTimer.start(boundedMs);
    loop.exec();
    timeoutTimer.stop();

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status > 0) {
        result.status = status;
        result.transportOk = true;
        result.body = reply->readAll().toStdString();
    } else if (timedOut) {
        // Distinguish "the bounded wait expired" from any other transport
        // failure (DNS/TLS/refused): the timeout owner fired and there is no
        // HTTP status at all.
        result.timedOut = true;
    }
    // status == 0 => transport-level failure (DNS / TLS / timeout / abort):
    // transportOk stays false and no response body is surfaced.
    reply->deleteLater();
    return result;
}

ChatCompletionsHttpAsyncHandle QtChatCompletionsHttpClient::startAsync(
    const ChatCompletionsHttpRequest &request,
    QObject *lifetimeContext,
    ChatCompletionsHttpCompletion completion)
{
    // Fail closed BEFORE anything is dispatched: an unacceptable endpoint or a
    // missing completion yields an invalid handle, dispatches nothing and
    // never invokes the callback (exactly the shared default contract).
    const QUrl endpoint(QString::fromStdString(request.url));
    if (!asyncEndpointAcceptable(endpoint) || !completion) {
        return ChatCompletionsHttpAsyncHandle{};
    }

    const std::uint64_t id = ++nextId_;

    InFlight flight;
    flight.completion = std::move(completion);
    // Per-request manager/reply/timer: exchanges are FULLY independent, so a
    // slow, held or never-finished request can never block another and there
    // is no global single-flight state anywhere.
    flight.manager = new QNetworkAccessManager(this);

    QNetworkRequest networkRequest(endpoint);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QString::fromStdString(request.contentType));
    // Authorization is attached ONLY to this endpoint.
    networkRequest.setRawHeader(
        "Authorization",
        QByteArray("Bearer ") + QByteArray::fromStdString(request.bearerToken));
    // TLS: default peer verification stays ON. Redirects: same-origin only.
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                               QNetworkRequest::SameOriginRedirectPolicy);
    // NO setTransferTimeout — the per-request QTimer below is the SINGLE
    // timeout owner (ISSUE-005).
    flight.reply = flight.manager->post(
        networkRequest, QByteArray::fromStdString(request.body));
    flight.timer = new QTimer(this);
    flight.timer->setSingleShot(true);

    const auto inserted = inFlight_.emplace(id, std::move(flight));
    InFlight &slot = inserted.first->second;

    // Every finish path funnels through finish(), guarded by `done`, so the
    // completion is delivered EXACTLY ONCE (reply OR timeout).
    QObject::connect(slot.reply, &QNetworkReply::finished, this, [this, id]() {
        finish(id, /*timedOut=*/false);
    });
    QObject::connect(slot.timer, &QTimer::timeout, this, [this, id]() {
        finish(id, /*timedOut=*/true);
    });

    // Lifetime binding: if the caller's context dies first the exchange is
    // abandoned and its completion is never invoked — no use-after-free.
    if (lifetimeContext != nullptr) {
        QObject::connect(lifetimeContext, &QObject::destroyed, this, [this, id]() {
            abandon(id);
        });
    }

    const int boundedMs =
        std::max(1, std::min(request.timeoutMs, std::numeric_limits<int>::max()));
    slot.timer->start(boundedMs);

    return ChatCompletionsHttpAsyncHandle{id};
}

void QtChatCompletionsHttpClient::finish(std::uint64_t id, bool timedOut)
{
    const auto it = inFlight_.find(id);
    if (it == inFlight_.end() || it->second.done) {
        return; // exactly-once: any signal after delivery is ignored
    }
    InFlight &slot = it->second;
    slot.done = true;
    slot.timer->stop();

    ChatCompletionsHttpResult result;
    if (timedOut) {
        result.timedOut = true;
    } else {
        const int status =
            slot.reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status > 0) {
            result.status = status;
            result.transportOk = true;
            result.body = slot.reply->readAll().toStdString();
        }
    }

    ChatCompletionsHttpCompletion completion = std::move(slot.completion);
    teardown(slot);      // tear down BEFORE re-entering any callback
    inFlight_.erase(it); // `slot` is dead from here on

    if (completion) {
        completion(std::move(result));
    }
}

void QtChatCompletionsHttpClient::abandon(std::uint64_t id)
{
    const auto it = inFlight_.find(id);
    if (it == inFlight_.end()) {
        return;
    }
    InFlight &slot = it->second;
    slot.done = true;
    teardown(slot);
    inFlight_.erase(it);
}

void QtChatCompletionsHttpClient::teardown(InFlight &flight)
{
    if (flight.timer != nullptr) {
        flight.timer->stop();
        flight.timer->deleteLater();
        flight.timer = nullptr;
    }
    if (flight.reply != nullptr) {
        // Only OUR connections to this reply are dropped; QNAM internals stay.
        QObject::disconnect(flight.reply, nullptr, nullptr, nullptr);
        flight.reply->abort();
        flight.reply->deleteLater();
        flight.reply = nullptr;
    }
    if (flight.manager != nullptr) {
        flight.manager->deleteLater();
        flight.manager = nullptr;
    }
}

} // namespace modbuslens::ui
