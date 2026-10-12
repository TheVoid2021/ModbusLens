#include "fake_chat_completions_server.h"

#include <QTimer>

#include <algorithm>

void FakeChatCompletionsServer::setNextResponse(int statusCode,
                                                const QByteArray& body,
                                                int delayMs,
                                                std::function<void()> onReceived)
{
    clearScript();
    lastScripted_ = ScriptedResponse{statusCode, body, delayMs};
    onReceived_ = std::move(onReceived);
}

void FakeChatCompletionsServer::enqueueResponse(int statusCode,
                                                const QByteArray& body,
                                                int delayMs)
{
    script_.push(ScriptedResponse{statusCode, body, delayMs});
    lastScripted_ = script_.back();
}

void FakeChatCompletionsServer::clearScript()
{
    while (!script_.empty()) {
        script_.pop();
    }
}

void FakeChatCompletionsServer::onNewConnection()
{
    // T034-B2.2-A-R2: accept EVERY pending connection. Two requests may arrive
    // in the same event-loop pass (e.g. a held request A plus request B), and
    // each must get its own connection record + parse buffer.
    while (server_.hasPendingConnections()) {
        QTcpSocket* socket = server_.nextPendingConnection();
        auto* connection = new Connection{socket, QByteArray{}, false};
        connections_.push_back(connection);
        // Reclaim the record when its socket dies.
        connect(socket, &QObject::destroyed, this, [this, connection] {
            const auto it =
                std::find(connections_.begin(), connections_.end(), connection);
            if (it != connections_.end()) {
                connections_.erase(it);
            }
            delete connection;
        });
        connect(socket, &QTcpSocket::readyRead, this, [this, connection] {
            handleReadyRead(*connection);
        });
    }
}

void FakeChatCompletionsServer::handleReadyRead(Connection& connection)
{
    if (connection.parsed) {
        return; // one request per connection (the server always answers close)
    }
    QTcpSocket* const socket = connection.socket;
    connection.pending += socket->readAll();

    // Header terminator: tolerate CRLF (normal) and LF line endings.
    int termLen = 4;
    int headerEnd = connection.pending.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        termLen = 2;
        headerEnd = connection.pending.indexOf("\n\n");
    }
    if (headerEnd < 0) {
        return; // headers not complete yet
    }

    const QByteArray headerBlock = connection.pending.left(headerEnd);
    const QList<QByteArray> rawLines = headerBlock.split('\n');
    QList<QByteArray> lines;
    for (const QByteArray& raw : rawLines) {
        QByteArray line = raw;
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        if (!line.isEmpty()) {
            lines.append(line);
        }
    }

    int contentLength = 0;
    for (const QByteArray& line : lines) {
        const QByteArray lowered = line.toLower();
        if (lowered.startsWith("content-length:")) {
            contentLength = line.mid(line.indexOf(':') + 1).trimmed().toInt();
        }
    }
    if (connection.pending.size() < headerEnd + termLen + contentLength) {
        return; // body still incomplete
    }
    connection.parsed = true;

    const QByteArray body =
        connection.pending.mid(headerEnd + termLen, contentLength);

    // Capture the transport facts the tests assert on.
    if (!lines.isEmpty()) {
        const QList<QByteArray> requestLine = lines.first().split(' ');
        lastRequest_.method = requestLine.value(0).trimmed();
        lastRequest_.path = QString::fromUtf8(requestLine.value(1).trimmed());
    } else {
        lastRequest_.method.clear();
        lastRequest_.path.clear();
    }
    lastRequest_.authorization.clear();
    lastRequest_.contentType.clear();
    for (const QByteArray& line : lines) {
        const QByteArray lowered = line.toLower();
        if (lowered.startsWith("authorization:")) {
            lastRequest_.authorization = line.mid(line.indexOf(':') + 1).trimmed();
        } else if (lowered.startsWith("content-type:")) {
            lastRequest_.contentType = line.mid(line.indexOf(':') + 1).trimmed();
        }
    }
    lastRequest_.body = body;
    requests_.push_back(lastRequest_);
    ++requestCount_;
    if (onReceived_) {
        onReceived_();
    }
    emit requestReceived();

    // Scripted response (optionally delayed): pop the FIFO queue, replay
    // the last scripted one once drained (http 200 with an empty object
    // body is the default when nothing was ever scripted).
    ScriptedResponse scripted{};
    if (!script_.empty()) {
        scripted = script_.front();
        script_.pop();
    } else if (requestCount_ <= 1 && lastScripted_.body.isEmpty()
               && lastScripted_.statusCode == 0) {
        scripted = ScriptedResponse{200, "{}", 0};
    } else {
        scripted = lastScripted_;
    }
    // Capture THIS connection's socket so a delayed response can only ever be
    // written to the wire it answers.
    const int status = scripted.statusCode;
    const QByteArray payload = scripted.body;
    QTcpSocket* const target = socket;
    const auto responder = [target, status, payload] {
        if (target->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        const QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n"
            + "Content-Type: application/json\r\n"
            + "Content-Length: " + QByteArray::number(payload.size()) + "\r\n"
            + "Connection: close\r\n\r\n";
        target->write(head);
        target->write(payload);
        // Flush before closing: an immediate disconnect can reset the
        // connection before the reply bytes are delivered.
        target->flush();
        QTimer::singleShot(50, target, [target] {
            target->disconnectFromHost();
            target->deleteLater();
        });
    };
    if (scripted.delayMs > 0) {
        QTimer::singleShot(scripted.delayMs, target, responder);
    } else {
        responder();
    }
}
