#include "ui/candidate/ModelScopeHttpClient.h"

#include <QByteArray>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <limits>

namespace modbuslens::ui {

QtModelScopeHttpClient::QtModelScopeHttpClient(QObject *parent) : QObject(parent)
{
}

ModelScopeHttpResult QtModelScopeHttpClient::post(
    const ModelScopeHttpRequest &request)
{
    ModelScopeHttpResult result;

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
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
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
    }
    // status == 0 => transport-level failure (DNS / TLS / timeout / abort):
    // transportOk stays false and no response body is surfaced.
    reply->deleteLater();
    return result;
}

} // namespace modbuslens::ui