#pragma once

#include <QString>

#include <functional>
#include <map>
#include <memory>

#include "ui/candidate/ChatCompletionsHttpClient.h"
#include "ui/ai/AiProviderProfile.h"

class QNetworkReply; // E1a: forward declaration (global scope, NOT ui::)

namespace modbuslens::ui {

class IChatCompletionsHttpClient;

// ---------------------------------------------------------------------------
// T034-B2.1 — the "Test Connection" probe (Human §13/§14/§15).
//
// FIXED SYNTHETIC PAYLOAD ONLY: the probe request carries the model id and a
// one-line synthetic instruction ("Reply with the single word OK."). It NEVER
// contains manual text, canonical text, DeviceProfile data, transaction
// history, raw TX/RX, diagnosis facts or Agent tools — its ONLY purpose is to
// verify endpoint reachability, credential validity, model acceptance and
// basic request support.
//
// The result token is a USER-FRIENDLY classification (never the raw
// `provider_rejected_status` token); the settings UI maps it to Chinese text.
// ---------------------------------------------------------------------------
struct AiTestConnectionResult {
    bool ok{false};
    QString token; // connection_ok / auth_failed / insufficient_balance /
                   // rate_limited / model_or_params_rejected / provider_error /
                   // network_error / timeout / invalid_response
    int httpStatus{0};
};

[[nodiscard]] AiTestConnectionResult runAiTestConnection(
    IChatCompletionsHttpClient &http, AiProviderKind kind, const QString &model,
    const QString &secret, int timeoutMs);

// The Chinese wording for a connection token (single mapping point, §15).
[[nodiscard]] QString aiConnectionStatusText(const QString &token);

// ---------------------------------------------------------------------------
// E1a — the ASYNC Test Connection seam (Test-Connection-specific; the shared
// Candidate IChatCompletionsHttpClient contract stays synchronous by
// accepted design and is NOT touched).
//
// Semantics:
//   · start() builds the SAME fixed synthetic probe as the synchronous path
//     (shared buildTestConnectionHttpRequest) and dispatches it WITHOUT
//     blocking the caller; returns a request id >= 0, or negative when the
//     dispatch failed (invalid endpoint — zero network I/O).
//   · the completion callback is invoked EXACTLY ONCE per accepted request
//     on the caller's thread, asynchronously; it may still arrive AFTER
//     cancel() (late completion) — stale-safety is the CALLER's generation
//     guard, never the cancel itself.
//   · the timeout is timer/signal driven (single timeout owner, mirrors the
//     accepted ISSUE-005 rule) and publishes the terminal "timeout" result;
//     a late network finish after the timeout is ignored by the client.
//   · cancel() is BEST-EFFORT network cancellation (QNetworkReply::abort()).
// ---------------------------------------------------------------------------

struct AiTestConnectionRequest {
    AiProviderKind kind;
    QString model;
    QString secret;
    int timeoutMs{45000};
};

class IAsyncTestConnectionClient
{
public:
    virtual ~IAsyncTestConnectionClient() = default;

    [[nodiscard]] virtual int start(
        const AiTestConnectionRequest &request,
        const std::function<void(const AiTestConnectionResult &)> &onDone) = 0;
    virtual void cancel(int requestId) = 0;
};

// Shared probe construction (synchronous and asynchronous paths MUST NOT
// drift): same endpoint, same profile rules, same fixed synthetic payload.
[[nodiscard]] ChatCompletionsHttpRequest buildTestConnectionHttpRequest(
    const AiTestConnectionRequest &request);

// Shared reply classification (status taxonomy identical to the sync path).
[[nodiscard]] AiTestConnectionResult classifyTestConnectionReply(
    const ChatCompletionsHttpResult &reply);

class QtAsyncTestConnectionClient
    : public QObject,
      public IAsyncTestConnectionClient
{
    Q_OBJECT

public:
    // PRODUCTION default: endpoint authority = provider profile compiled
    // constant, HTTPS-only guard. 100% unchanged by E2.
    explicit QtAsyncTestConnectionClient(QObject *parent = nullptr);
    ~QtAsyncTestConnectionClient() override;

    // E2 TEST-ONLY constructor: an explicit endpoint resolver replaces the
    // URL authority for requests dispatched through THIS client instance.
    // Frozen constraints:
    //   · C++ constructor dependency ONLY — never Q_PROPERTY / Q_INVOKABLE /
    //     slot / signal / QSettings / env / CLI / global mutable static;
    //   · resolved URLs are LOOPBACK-ONLY (host must be 127.0.0.1 or ::1;
    //     anything else is rejected before any network object is created);
    //   · resolver decides ONLY the URL — payload, headers, provider extras
    //     and classification are built by the shared probe builder;
    //   · the production default constructor never consults the resolver.
    explicit QtAsyncTestConnectionClient(
        std::function<QUrl(AiProviderKind)> testEndpointResolver,
        QObject *parent = nullptr);

    [[nodiscard]] int start(
        const AiTestConnectionRequest &request,
        const std::function<void(const AiTestConnectionResult &)> &onDone)
        override;
    void cancel(int requestId) override;

private:
    struct InFlight; // defined in the .cpp (QNetworkReply lifetime detail)
    std::map<int, std::unique_ptr<InFlight>> inFlight_;
    int nextId_{0};
    // E2: TEST-ONLY endpoint resolver (empty = production profile authority).
    std::function<QUrl(AiProviderKind)> testEndpointResolver_;
};

} // namespace modbuslens::ui
