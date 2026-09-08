#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>

// Test-only minimal fake Chat Completions endpoint (T011 Part B).
//
// Listens on 127.0.0.1 with an ephemeral port, captures the real
// QNetworkAccessManager POST (method/path/headers/body) and scripts the next
// response (status + body + optional delay). NEVER touches the public
// internet, NEVER reads the developer's real MODELSCOPE_API_KEY — tests
// always inject fake-test-token/fake-model explicitly.
class FakeChatCompletionsServer : public QObject
{
    Q_OBJECT

public:
    struct CapturedRequest {
        QByteArray method;
        QString path;
        QByteArray authorization;
        QByteArray contentType;
        QByteArray body;
    };

    explicit FakeChatCompletionsServer(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(&server_, &QTcpServer::newConnection,
                this, &FakeChatCompletionsServer::onNewConnection);
    }

    bool start()
    {
        return server_.listen(QHostAddress::LocalHost, 0);
    }

    QString baseUrl() const
    {
        return QStringLiteral("http://127.0.0.1:%1").arg(server_.serverPort());
    }

    QString chatCompletionsUrl() const
    {
        return QStringLiteral("%1/chat/completions").arg(baseUrl());
    }

    // Scripts ONE response for the next incoming request.
    void setNextResponse(int statusCode, const QByteArray& body,
                         int delayMs = 0,
                         std::function<void()> onReceived = {});

    const CapturedRequest& lastRequest() const { return lastRequest_; }
    int requestCount() const { return requestCount_; }

signals:
    // Emitted when a request has been fully received (before the scripted
    // response is sent — lets tests inspect capture state safely).
    void requestReceived();

private slots:
    void onNewConnection();

private:
    QTcpServer server_;
    int requestCount_ = 0;
    CapturedRequest lastRequest_;

    int nextStatus_ = 200;
    QByteArray nextBody_;
    int nextDelayMs_ = 0;
    std::function<void()> onReceived_;

    QByteArray pending_;
    QTcpSocket* socket_ = nullptr;
};