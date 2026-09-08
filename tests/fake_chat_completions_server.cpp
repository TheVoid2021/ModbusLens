#include "fake_chat_completions_server.h"

#include <QTimer>

void FakeChatCompletionsServer::setNextResponse(int statusCode,
                                                const QByteArray& body,
                                                int delayMs,
                                                std::function<void()> onReceived)
{
    nextStatus_ = statusCode;
    nextBody_ = body;
    nextDelayMs_ = delayMs;
    onReceived_ = std::move(onReceived);
}

void FakeChatCompletionsServer::onNewConnection()
{
    socket_ = server_.nextPendingConnection();
    pending_.clear();
    connect(socket_, &QTcpSocket::readyRead, this, [this] {
        pending_ += socket_->readAll();

        // Header terminator: tolerate CRLF (normal) and LF line endings.
        int termLen = 4;
        int headerEnd = pending_.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            termLen = 2;
            headerEnd = pending_.indexOf("\n\n");
        }
        if (headerEnd < 0) {
            return; // headers not complete yet
        }

        const QByteArray headerBlock = pending_.left(headerEnd);
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
        if (pending_.size() < headerEnd + termLen + contentLength) {
            return; // body still incomplete
        }

        const QByteArray body = pending_.mid(headerEnd + termLen, contentLength);

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
        ++requestCount_;
        if (onReceived_) {
            onReceived_();
        }
        emit requestReceived();

        // Scripted response (optionally delayed). Capture THIS connection's
        // socket (not the shared member, which the next connection
        // overwrites) so a delayed response can only ever be written to the
        // wire it answers.
        const int status = nextStatus_;
        const QByteArray payload = nextBody_;
        QTcpSocket* target = socket_;
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
        if (nextDelayMs_ > 0) {
            QTimer::singleShot(nextDelayMs_, target, responder);
        } else {
            responder();
        }
    });
}