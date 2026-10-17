#include "ui/candidate/ChatCompletionsHttpClient.h"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <utility>

namespace modbuslens::ui {

namespace {

// ---------------------------------------------------------------------------
// T035-A Round H — SYNCHRONOUS response-lifecycle forensics (sanitized).
//
// Same MODBUSLENS_PROVIDER_FORENSICS=1 gate and the SAME append file as the
// transport/provider blocks, so a single replay yields one coherent record.
// This block exists because `ChatCompletionsTransport::send()` (which writes
// the [transport] block) does NOT hold the QNetworkReply — only THIS call sees
// `reply->error()`, the raw header surface and the read-time byte counts. It
// records ONLY bounded metadata: never body bytes, credentials, URLs, prompts
// or any server-returned string. It reads NO bytes from the reply itself; the
// counts are taken around the existing single readAll() so the reader's
// behavior is unchanged.
// ---------------------------------------------------------------------------
[[nodiscard]] bool syncForensicsEnabled()
{
    return qEnvironmentVariable("MODBUSLENS_PROVIDER_FORENSICS")
           == QLatin1String("1");
}

// Bounded enum, NEVER the raw header value: an unrecognized scheme is reported
// as "other" so no arbitrary server string can reach the log.
[[nodiscard]] QString transferEncodingCategory(const QByteArray &raw)
{
    const QByteArray trimmed = raw.trimmed().toLower();
    if (trimmed.isEmpty()) {
        return QStringLiteral("absent");
    }
    if (trimmed == "identity") {
        return QStringLiteral("identity");
    }
    if (trimmed == "chunked") {
        return QStringLiteral("chunked");
    }
    if (trimmed == "gzip" || trimmed == "x-gzip") {
        return QStringLiteral("gzip");
    }
    if (trimmed == "deflate") {
        return QStringLiteral("deflate");
    }
    if (trimmed == "br") {
        return QStringLiteral("br");
    }
    return QStringLiteral("other");
}

[[nodiscard]] QString replyErrorCategory(QNetworkReply::NetworkError error)
{
    switch (error) {
    case QNetworkReply::NoError: return QStringLiteral("NoError");
    case QNetworkReply::OperationCanceledError:
        return QStringLiteral("OperationCanceledError");
    case QNetworkReply::RemoteHostClosedError:
        return QStringLiteral("RemoteHostClosedError");
    case QNetworkReply::TimeoutError: return QStringLiteral("TimeoutError");
    case QNetworkReply::ConnectionRefusedError:
        return QStringLiteral("ConnectionRefusedError");
    case QNetworkReply::HostNotFoundError:
        return QStringLiteral("HostNotFoundError");
    case QNetworkReply::SslHandshakeFailedError:
        return QStringLiteral("SslHandshakeFailedError");
    case QNetworkReply::ContentNotFoundError:
        return QStringLiteral("ContentNotFoundError");
    case QNetworkReply::ContentAccessDenied:
        return QStringLiteral("ContentAccessDenied");
    case QNetworkReply::ProtocolFailure:
        return QStringLiteral("ProtocolFailure");
    case QNetworkReply::TemporaryNetworkFailureError:
        return QStringLiteral("TemporaryNetworkFailureError");
    default: return QStringLiteral("other");
    }
}

void writeSyncPostForensics(qint64 elapsedMs, bool timedOut, bool replyFinished,
                            QNetworkReply::NetworkError replyError,
                            int httpStatus, bool contentLengthPresent,
                            qint64 contentLengthValue,
                            const QString &transferCategory,
                            qint64 bytesAvailableBeforeRead, qint64 bytesRead)
{
    if (!syncForensicsEnabled()) {
        return;
    }
    QString block = QStringLiteral(
                        "[sync post forensics] %1 (sanitized: bounded metadata "
                        "only — no body, no credentials, no URL, no prompt)")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    block += QStringLiteral(
                 "\n  elapsed_ms=%1 timed_out=%2 reply_finished=%3 reply_error=%4")
                 .arg(elapsedMs)
                 .arg(timedOut ? 1 : 0)
                 .arg(replyFinished ? 1 : 0)
                 .arg(replyErrorCategory(replyError));
    block += QStringLiteral(
                 "\n  http_status=%1 content_length_present=%2 content_length_value=%3")
                 .arg(httpStatus)
                 .arg(contentLengthPresent ? 1 : 0)
                 .arg(contentLengthValue);
    block += QStringLiteral(
                 "\n  transfer_encoding_category=%1 bytes_available_before_read=%2 "
                 "bytes_read=%3")
                 .arg(transferCategory)
                 .arg(bytesAvailableBeforeRead)
                 .arg(bytesRead);
    qWarning().noquote() << block;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + QStringLiteral("/modbuslens_provider_forensics.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(block.toUtf8());
        file.write("\n");
    }
}

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

// T035-A Round H.1 — TEST-ONLY synchronous loopback affordance.
//
// Round H (S01–S05) needed to drive the REAL synchronous post() at a local
// socket, but the production https-only guard hard-rejects http://127.0.0.1.
// Round H first widened post()'s guard UNCONDITIONALLY; Human review rejected
// that (the loopback allowance must not exist in production code at all).
//
// This helper is compiled ONLY when MODBUSLENS_ENABLE_TEST_LOOPBACK is defined
// — a PRIVATE compile definition applied to the deterministic TEST targets
// only (never to the `modbuslens` product target). In a production build this
// code does not exist; the flag it consults cannot be set at runtime, so the
// extra allowance is unreachable there. It mirrors the repository's accepted
// E2 precedent (devlog 2026-10-09: "TEST-ONLY resolver injection + loopback-only
// hard guard, the production default path unchanged"): the injected affordance
// decides ONLY whether the expressly-allowed loopback host is admitted, and is
// LOOPBACK-ONLY — any other scheme/host still fails closed and sends NOTHING.
#ifdef MODBUSLENS_ENABLE_TEST_LOOPBACK
[[nodiscard]] bool loopbackTestEndpointAcceptable(const QUrl &endpoint)
{
    return endpoint.isValid()
           && endpoint.scheme().compare(QStringLiteral("http"),
                                        Qt::CaseInsensitive)
                  == 0
           && endpoint.host() == QLatin1String("127.0.0.1");
}
#endif

} // namespace

QtChatCompletionsHttpClient::QtChatCompletionsHttpClient(QObject *parent) : QObject(parent)
{
}

#ifdef MODBUSLENS_ENABLE_TEST_LOOPBACK
QtChatCompletionsHttpClient::QtChatCompletionsHttpClient(
    TestLoopbackTag, QObject *parent)
    : QObject(parent), allowLoopbackTestEndpoint_(true)
{
}
#endif

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
#ifdef MODBUSLENS_ENABLE_TEST_LOOPBACK
        // TEST-ONLY (compiled into deterministic test targets only): the
        // expressly-allowed loopback host is admitted so the synchronous
        // socket matrix can drive post(). This branch does not exist in a
        // production build — the flag cannot be set at runtime.
        if (!allowLoopbackTestEndpoint_
            || !loopbackTestEndpointAcceptable(endpoint)) {
            return result;
        }
#else
        // Production must target the official HTTPS endpoint only. Anything
        // else fails closed and sends NOTHING.
        return result;
#endif
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
    bool replyFinished = false;
    QElapsedTimer elapsed;
    elapsed.start();
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop,
                     [&]() {
                         replyFinished = true;
                         loop.quit();
                     });
    timeoutTimer.start(boundedMs);
    loop.exec();
    timeoutTimer.stop();

    // T035-A Round H: bounded lifecycle metadata captured AFTER the loop. The
    // reply is NOT consumed here; counts are taken around the single readAll().
    const QNetworkReply::NetworkError replyError = reply->error();
    const bool contentLengthPresent =
        reply->hasRawHeader(QByteArrayLiteral("Content-Length"));
    const qint64 contentLengthValue =
        contentLengthPresent
            ? reply->rawHeader(QByteArrayLiteral("Content-Length")).toLongLong()
            : -1;
    const QString transferCategory =
        transferEncodingCategory(reply->rawHeader(QByteArrayLiteral("Transfer-Encoding")));

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qint64 bytesAvailableBeforeRead = 0;
    qint64 bytesRead = 0;
    QByteArray body;
    if (status > 0) {
        // The ONLY read of the reply — unchanged from before. The counts are
        // taken immediately around it and consume nothing extra.
        bytesAvailableBeforeRead = reply->bytesAvailable();
        body = reply->readAll();
        bytesRead = body.size();
    }

    // T035-A Round I — classify by the NETWORK OUTCOME FIRST; the HTTP status
    // is diagnostic metadata and can NEVER upgrade a failed exchange into a
    // transport success. (Real replay 2026-10-15 12:38:42: 90 s timeout fired,
    // abort => OperationCanceledError, yet HTTP 200 headers had arrived and
    // the old code classified the aborted exchange as transportOk with an
    // empty body — i.e. a local timeout reported as a provider response.)
    //  (1) The local timeout owner fired: transport failure + timedOut=true.
    //      (send() then surfaces an empty exchange; the adapter maps that to
    //      TransportError/"transport_error" — never to provider HTTP failure.)
    //  (2) The reply ended in a NETWORK ERROR (premature close, TLS, ...):
    //      transport failure even when 200 headers arrived. The truncated
    //      body is no longer surfaced.
    //  (3) Otherwise, a reply carrying an HTTP status is transport success —
    //      S01/S02/S03 behavior byte-for-byte unchanged; the envelope and the
    //      strict parser still own the semantic verdict downstream.
    if (timedOut) {
        result.timedOut = true;
        result.status = status; // diagnostic only; transportOk stays false
    } else if (replyError != QNetworkReply::NoError) {
        result.status = status; // diagnostic only; transportOk stays false
    } else if (status > 0) {
        result.status = status;
        result.transportOk = true;
        result.body = body.toStdString();
    }
    writeSyncPostForensics(elapsed.elapsed(), timedOut, replyFinished, replyError,
                           status, contentLengthPresent, contentLengthValue,
                           transferCategory, bytesAvailableBeforeRead, bytesRead);
    // transportOk == false => transport-level failure (timeout / network error
    // / no status): no response body is surfaced; result.status (if any) is
    // diagnostics only and is NOT consumed by send() on this path.
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
