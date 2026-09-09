#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>
#include <queue>
#include <vector>

// Test-only minimal fake Chat Completions endpoint (T011 Part B; extended in
// T012 Part B Phase 1 with a multi-turn script queue for agent loops).
//
// Listens on 127.0.0.1 with an ephemeral port, captures the real
// QNetworkAccessManager POST (method/path/headers/body) and scripts the next
// response(s) (status + body + optional delay). NEVER touches the public
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

    struct ScriptedResponse {
        int statusCode = 200;
        QByteArray body;
        int delayMs = 0;
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

    // Scripts ONE response for the next incoming request (T011 style;
    // replaces any previously queued script).
    void setNextResponse(int statusCode, const QByteArray& body,
                         int delayMs = 0,
                         std::function<void()> onReceived = {});

    // T012 Part B Phase 1: multi-turn FIFO script queue — every incoming
    // request pops the next response. When the queue is empty, the last
    // scripted response is replayed (keeps single-shot behavior stable).
    void enqueueResponse(int statusCode, const QByteArray& body,
                         int delayMs = 0);
    void clearScript();

    const CapturedRequest& lastRequest() const { return lastRequest_; }
    const std::vector<CapturedRequest>& requests() const { return requests_; }
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
    std::vector<CapturedRequest> requests_;

    std::queue<ScriptedResponse> script_;
    ScriptedResponse lastScripted_; // replayed while the queue is drained
    std::function<void()> onReceived_;

    QByteArray pending_;
    QTcpSocket* socket_ = nullptr;
};