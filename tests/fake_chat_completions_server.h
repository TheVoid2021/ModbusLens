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
//
// T034-B2.2-A-R2: PER-CONNECTION state. The server accepts EVERY pending
// connection and parses each on its own buffer, so MULTIPLE requests can be
// in flight at once (needed to observe a held request A while request B is
// dispatched and completed). The previous single shared socket/buffer could
// not model that and silently corrupted concurrent requests.
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
        // T035-A Round H: deterministic response-lifecycle controls.
        // -1 => declare the real body size in Content-Length (default, unchanged
        // behavior). >=0 => declare THIS many bytes regardless of how many are
        // actually written, so a premature close can be scripted (case D).
        int declaredContentLength = -1;
        // true => write the response HEADERS immediately but withhold the BODY
        // entirely (never send it) so the client sees a validated status while
        // the body never arrives (cases C/E). The connection is held open; the
        // caller's own timeout must fire.
        bool stallBodyAfterHeaders = false;
    };

    explicit FakeChatCompletionsServer(QObject* parent = nullptr)
        : QObject(parent)
    {
        connect(&server_, &QTcpServer::newConnection,
                this, &FakeChatCompletionsServer::onNewConnection);
    }

    // T034-B2.2-C-G3-FH1: EXPLICIT, DETERMINISTIC TEARDOWN (see the .cpp).
    // Declared so that every member is still alive when the teardown body
    // runs; it neutralizes the fixture callbacks before any socket dies and
    // frees the socket/Connection bookkeeping exactly once each.
    ~FakeChatCompletionsServer() override;

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

    // T035-A Round H: script a response whose HEADERS are written immediately
    // but whose BODY is withheld entirely (the client must time out). The
    // declared Content-Length defaults to the real body size (0 keeps header
    // and body consistent). Used to produce the Round F signature
    // (HTTP 200 headers + no body).
    void enqueueStalledBody(int statusCode, int declaredContentLength = 0);

    // T035-A Round H: script a response that DECLARES more bytes than it
    // writes, then closes the connection early (case D). The headers carry
    // `declaredContentLength`; only `body` bytes are actually delivered.
    void enqueueShortBody(int statusCode, const QByteArray& body,
                          int declaredContentLength);

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
    // One accepted connection with its OWN parse buffer, so concurrent
    // connections can never corrupt each other.
    struct Connection {
        QTcpSocket* socket{nullptr};
        QByteArray pending;
        bool parsed{false}; // exactly one request per connection
    };

    void handleReadyRead(Connection& connection);

    QTcpServer server_;
    int requestCount_ = 0;
    CapturedRequest lastRequest_;
    std::vector<CapturedRequest> requests_;

    std::queue<ScriptedResponse> script_;
    ScriptedResponse lastScripted_; // replayed while the queue is drained
    std::function<void()> onReceived_;

    std::vector<Connection*> connections_;
};
