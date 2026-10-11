#include "ui/candidate/ChatCompletionsTransport.h"

#include <utility>

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStandardPaths>
namespace modbuslens::ui {

ChatCompletionsTransport::ChatCompletionsTransport(
    IChatCompletionsHttpClient &http, std::string endpoint, int timeoutMs)
    : http_(http),
      endpoint_(std::move(endpoint)),
      timeoutMs_(timeoutMs)
{
}

void ChatCompletionsTransport::setOneShotBearerToken(std::string token)
{
    // T034-B2.1 SECURITY CORRECTION SLICE B2: per-request credential supply.
    // The token is consumed (cleared) by the next send() — the transport
    // retains no plaintext credential between requests.
    oneShotBearerToken_ = std::move(token);
}


namespace {

// ---------------------------------------------------------------------------
// T034-B1 live RCA — TEMPORARY, EXPLICITLY GATED provider forensics for the
// numeric HTTP status and the provider error-envelope metadata. This is the
// only place the numeric status is available (the SESSION N Exchange seam
// deliberately carries only the coarse booleans, and that public API must not
// spread for forensics). Sanitized by construction: status integer, booleans,
// body length, and the error object's code/type scalars (bounded to 64 chars;
// longer values are reported as length only). NEVER logged: API key,
// Authorization, prompt, manual text, assistant content, reasoning content,
// error.message or any raw response body.
// Human decision pending: delete this instrumentation before the B1 commit, or
// converge it into a default-off controlled diagnostic.
// ---------------------------------------------------------------------------
[[nodiscard]] bool providerForensicsEnabled()
{
    return qEnvironmentVariable("MODBUSLENS_PROVIDER_FORENSICS")
           == QLatin1String("1");
}

[[nodiscard]] QString boundedScalar(const QJsonValue &value)
{
    if (value.isString()) {
        const QString text = value.toString();
        if (text.size() <= 64) {
            return text;
        }
        return QStringLiteral("<string len=%1>").arg(text.size());
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    }
    if (value.isNull()) {
        return QStringLiteral("null");
    }
    return QStringLiteral("<%1>").arg(value.isObject() ? QStringLiteral("object")
                                                       : QStringLiteral("array"));
}

[[nodiscard]] QString scalarType(const QJsonValue &value)
{
    if (value.isString()) return QStringLiteral("string");
    if (value.isDouble()) return QStringLiteral("number");
    if (value.isBool()) return QStringLiteral("bool");
    if (value.isNull()) return QStringLiteral("null");
    return value.isObject() ? QStringLiteral("object") : QStringLiteral("array");
}

void writeTransportForensics(const ChatCompletionsHttpResult &result)
{
    if (!providerForensicsEnabled()) {
        return;
    }
    QString block = QStringLiteral(
                        "[transport forensics] %1 (sanitized: numeric status + error "
                        "code/type metadata only — no key, no headers, no raw body)")
                        .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs));
    block += QStringLiteral("\n  http_status=%1 transport_ok=%2 response_body_len=%3")
                 .arg(result.status)
                 .arg(result.transportOk ? 1 : 0)
                 .arg(static_cast<int>(result.body.size()));
    const QJsonDocument document = QJsonDocument::fromJson(
        QByteArray::fromStdString(result.body));
    if (!document.isObject()) {
        block += QStringLiteral("\n  body_json_parse=FAIL (non-object body)");
    } else {
        const QJsonObject root = document.object();
        const QJsonObject error = root.value(QStringLiteral("error")).toObject();
        if (error.isEmpty()) {
            block += QStringLiteral("\n  error_envelope=absent");
        } else {
            const QJsonValue code = error.value(QStringLiteral("code"));
            const QJsonValue type = error.value(QStringLiteral("type"));
            block += QStringLiteral("\n  error_code_type=%1").arg(scalarType(code));
            if (code.isString() || code.isDouble() || code.isBool()) {
                block += QStringLiteral(" error_code=%1").arg(boundedScalar(code));
            }
            block += QStringLiteral(" error_type_type=%1").arg(scalarType(type));
            if (type.isString() || type.isDouble() || type.isBool()) {
                block += QStringLiteral(" error_type=%1").arg(boundedScalar(type));
            }
        }
    }
    qWarning().noquote() << block;
    QFile file(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + QStringLiteral("/modbuslens_provider_forensics.log"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        file.write(block.toUtf8());
        file.write("\n");
    }
}

} // namespace

IExtractionTransport::Exchange ChatCompletionsTransport::send(
    std::string_view requestBody)
{
    Exchange exchange;
    // One-shot consumption (T034-B2.1): the pending token is taken for THIS
    // request and the slot cleared immediately — the transport retains no
    // plaintext credential between requests. A send() without a pending
    // one-shot token fails closed: nothing dispatched, dispatchCount_
    // unchanged.
    std::string bearerToken = std::move(oneShotBearerToken_);
    oneShotBearerToken_.clear();
    // Fail closed WITHOUT dispatching when there is nothing usable to send
    // with: an empty credential, endpoint or body must never become a request.
    if (bearerToken.empty() || requestBody.empty() || endpoint_.empty()) {
        return exchange;
    }

    ChatCompletionsHttpRequest request;
    request.url = endpoint_;
    request.bearerToken = bearerToken;
    request.contentType = kJsonContentType;
    request.body = std::string(requestBody);
    request.timeoutMs = timeoutMs_;

    ++dispatchCount_;
    const ChatCompletionsHttpResult result = http_.post(request);

    if (!result.transportOk) {
        return exchange; // transport-level failure: transportOk stays false
    }
    exchange.transportOk = true;
    exchange.responseBody = result.body;
    exchange.statusOk = result.status >= 200 && result.status < 300;
    writeTransportForensics(result);
    return exchange;
}

} // namespace modbuslens::ui