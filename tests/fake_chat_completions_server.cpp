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

// ---------------------------------------------------------------------------
// T034-B2.2-C-G3-FH1: EXPLICIT, DETERMINISTIC TEARDOWN.
//
// WHY the old teardown was undefined behaviour:
// members are destroyed in REVERSE declaration order, and `server_` is declared
// FIRST while `connections_` is declared LAST. So `connections_` died BEFORE
// `server_`. Destroying `server_` destroys its child sockets, and each socket's
// `destroyed` signal still had this lambda connected:
//
//     connect(socket, &QObject::destroyed, this, [this, connection] {
//         std::find(connections_.begin(), connections_.end(), connection); ...
//     });
//
// The receiver is `this` (the server), which is only destroyed in the
// base-class destructor — i.e. EVEN LATER. So the lambda really ran, while
// `connections_` was already destroyed: a use-after-destruction of a std::vector
// member (UB; intermittently a SEGV depending on heap reuse).
//
// THIS destructor runs while EVERY member is still alive and tears down
// deterministically:
//   1. stop accepting / drop un-adopted connections;
//   2. neutralize every fixture callback that could reach `this`, a
//      `Connection*` or `connections_` (disconnect BY RECEIVER == this removes
//      both the `destroyed` and the `readyRead` lambdas);
//   3. detach the bookkeeping FIRST (so no callback can observe it), then
//      delete each socket by value (never deleteLater, no reliance on a later
//      event loop) and each record exactly once.
// Afterwards no socket, no Connection* and no fixture callback can reach this
// object — independent of event-loop timing, vector memory reuse or "the
// socket usually disconnects first".
// ---------------------------------------------------------------------------
FakeChatCompletionsServer::~FakeChatCompletionsServer()
{
    // (A) Stop accepting new connections while all members are alive.
    QObject::disconnect(&server_, nullptr, this, nullptr);
    server_.close();

    // A connection accepted but not yet adopted by onNewConnection() must not
    // outlive the bookkeeping either: drop it deterministically.
    while (server_.hasPendingConnections()) {
        delete server_.nextPendingConnection();
    }

    // (B/C) Detach the bookkeeping BEFORE any socket dies. The member is left
    // alive but EMPTY, so even a theoretically surviving callback would see a
    // valid, empty container instead of freed storage.
    std::vector<Connection*> records;
    records.swap(connections_);

    // (D/E/F) Neutralize + free. The delayed responder timer is bound to the
    // socket as its Qt context and captures only the socket/status/payload
    // (never a Connection*), so deleting the socket here cancels it safely.
    for (Connection* record : records) {
        if (record == nullptr) {
            continue;
        }
        QTcpSocket* const socket = record->socket;
        if (socket != nullptr) {
            // Receiver == this: removes the `destroyed` lambda, the
            // `readyRead` lambda and any other fixture callback bound to us.
            QObject::disconnect(socket, nullptr, this, nullptr);
            // Deterministic destruction. The socket is a child of `server_`,
            // so this also removes it from the child list — `server_`'s later
            // destruction cannot double-delete it.
            delete socket;
        }
        // Exactly one delete per record: the lambda that used to delete it
        // can no longer run (disconnected above).
        delete record;
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
