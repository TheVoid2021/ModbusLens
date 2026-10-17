#pragma once

#include <QObject>

#include <cstdint>
#include <functional>
#include <map>
#include <string>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 FOURTH SLICE — the thin HTTP seam under the production transport
// (T027 §76, SESSION P §9/§14).
//
// This exists for ONE reason: the production transport must be exercisable by
// deterministic offline tests WITHOUT making the production endpoint
// user-configurable. The endpoint stays a compiled constant inside the
// transport; only this thin layer is injected, so a test can capture the exact
// method target / Authorization / content type / body that production would
// have sent while performing zero network I/O.
// ---------------------------------------------------------------------------
struct ChatCompletionsHttpRequest {
    std::string url;
    std::string bearerToken;
    std::string contentType;
    std::string body;
    int timeoutMs{0};
};

struct ChatCompletionsHttpResult {
    int status{0};           // 0 = no HTTP status at all (transport-level)
    std::string body;
    bool transportOk{false}; // a reply was obtained (any status)
    // T034-B2.1 (Test Connection): true only when the SINGLE timeout owner
    // fired (lets the settings UI distinguish a timeout from a network
    // failure). Does not change any existing semantics.
    bool timedOut{false};
};

// T034-B2.2-A-R2: completion delivered to the ASYNC caller. Invoked exactly
// once per successfully started request.
using ChatCompletionsHttpCompletion =
    std::function<void(ChatCompletionsHttpResult)>;

// T034-B2.2-A-R2: handle for ONE asynchronous exchange. `id == 0` means the
// request was NOT started: nothing was dispatched and the completion is never
// invoked. A non-zero id identifies exactly one in-flight exchange.
struct ChatCompletionsHttpAsyncHandle {
    std::uint64_t id{0};
    [[nodiscard]] bool valid() const { return id != 0; }
};

class IChatCompletionsHttpClient
{
public:
    virtual ~IChatCompletionsHttpClient() = default;

    [[nodiscard]] virtual ChatCompletionsHttpResult post(
        const ChatCompletionsHttpRequest &request) = 0;

    // ---- T034-B2.2-A-R2: additive ASYNCHRONOUS path ------------------------
    // DEFAULT CONTRACT (deliberate, load-bearing): async is UNSUPPORTED here.
    // This returns an INVALID handle and dispatches NOTHING; the completion is
    // never invoked. It MUST NOT fall back to the synchronous post() — that
    // would silently block the caller's thread and defeat the whole point of
    // the async path. Because the default is defined (not pure), every
    // existing Candidate fake stays source- AND behaviour-compatible.
    //
    // `lifetimeContext` (optional) is the QObject the exchange is bound to: if
    // it is destroyed before completion, the exchange is abandoned and the
    // completion is never delivered — no use-after-free is possible.
    [[nodiscard]] virtual ChatCompletionsHttpAsyncHandle startAsync(
        const ChatCompletionsHttpRequest &request,
        QObject *lifetimeContext,
        ChatCompletionsHttpCompletion completion)
    {
        (void)request;
        (void)lifetimeContext;
        (void)completion;
        return ChatCompletionsHttpAsyncHandle{};
    }
};

// ---------------------------------------------------------------------------
// PRODUCTION implementation (Qt Network).
//
// Ownership mirrors the accepted M6 client: one QNetworkAccessManager per
// exchange owned locally, one QTimer as the SINGLE timeout owner (no
// setTransferTimeout — dual ownership was ISSUE-005), Authorization attached
// only to the configured endpoint, and a same-origin redirect policy so the
// bearer token can never be forwarded to an unrelated host. TLS peer
// verification stays ON (no bypass, no certificate-ignore code).
//
// The C2 provider seam (SESSION N `IExtractionTransport::send`) is synchronous
// by accepted design, so post() runs a bounded local event loop until the
// reply or the timeout fires. The bound is the accepted 90 s M6 timeout.
// KNOWN LIMITATION: a synchronous seam means the calling thread waits for the
// exchange; making the chain asynchronous would be a contract change to the
// accepted SESSION N seam and is deliberately out of scope for this slice.
//
// T034-B2.2-A-R2 adds startAsync(): the SAME production safety rules, but
// genuinely asynchronous (QNetworkAccessManager is already async — this just
// exposes that capability). Multiple exchanges may be in flight at once: there
// is NO global single-flight state; each exchange owns its own manager,
// reply, timeout timer and completion exactly once. Network CANCELLATION is
// not implemented and must not be claimed.
// ---------------------------------------------------------------------------
class QtChatCompletionsHttpClient : public QObject, public IChatCompletionsHttpClient
{
    Q_OBJECT

public:
    explicit QtChatCompletionsHttpClient(QObject *parent = nullptr);
    ~QtChatCompletionsHttpClient() override;

#ifdef MODBUSLENS_ENABLE_TEST_LOOPBACK
    // T035-A Round H.1 — TEST-ONLY tag/constructor, compiled ONLY into the
    // deterministic test targets (PRIVATE compile definition; never into the
    // `modbuslens` product target). It admits the expressly-allowed loopback
    // host (http://127.0.0.1) for the SYNCHRONOUS post() path so the socket
    // matrix (S01–S05) can drive the REAL client without widening production.
    // Frozen constraints (mirrors the accepted E2 precedent):
    //   · C++ constructor dependency ONLY — never Q_PROPERTY / Q_INVOKABLE /
    //     slot / signal / QSettings / env var / CLI / global mutable static;
    //   · LOOPBACK-ONLY (host must be 127.0.0.1) — any other non-https
    //     endpoint still fails closed and sends NOTHING;
    //   · the production default constructor never enables it, and in a
    //     production build this constructor does not exist at all.
    struct TestLoopbackTag {
    };
    explicit QtChatCompletionsHttpClient(TestLoopbackTag, QObject *parent = nullptr);
#endif

    [[nodiscard]] ChatCompletionsHttpResult post(
        const ChatCompletionsHttpRequest &request) override;

    [[nodiscard]] ChatCompletionsHttpAsyncHandle startAsync(
        const ChatCompletionsHttpRequest &request,
        QObject *lifetimeContext,
        ChatCompletionsHttpCompletion completion) override;

private:
    // One independent in-flight exchange. Nothing here is shared between
    // exchanges, so a slow / held / never-finished request can never block or
    // corrupt another.
    struct InFlight {
        QNetworkAccessManager *manager{nullptr};
        QNetworkReply *reply{nullptr};
        QTimer *timer{nullptr};
        ChatCompletionsHttpCompletion completion;
        bool done{false};
    };

    void finish(std::uint64_t id, bool timedOut);
    void abandon(std::uint64_t id);
    static void teardown(InFlight &flight);

    std::uint64_t nextId_{0};
    std::map<std::uint64_t, InFlight> inFlight_;
#ifdef MODBUSLENS_ENABLE_TEST_LOOPBACK
    // TEST-ONLY: false for every production-default instance; true only when
    // constructed with TestLoopbackTag. Consulted ONLY inside post()'s
    // non-https branch, which itself only exists under this macro.
    bool allowLoopbackTestEndpoint_{false};
#endif
};

} // namespace modbuslens::ui
