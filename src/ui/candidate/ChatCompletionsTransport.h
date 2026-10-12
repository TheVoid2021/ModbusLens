#pragma once

#include <string>

#include "ui/ai/ExtractionTransport.h"
#include "ui/candidate/ChatCompletionsHttpClient.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 FOURTH SLICE — PRODUCTION ModelScope transport (T027 §76).
//
// It satisfies the accepted SESSION N seam (`IExtractionTransport`) over the
// thin HTTP seam above, so the whole accepted chain stays untouched:
//
//   runner -> adapter (wire body + envelope) -> THIS -> HTTP seam
//
// It owns the two things the accepted seam deliberately refuses to know:
// the official endpoint and the Authorization header. Neither is
// user-configurable: the endpoint is a compiled constant and the token comes
// from the canonical process-environment credential.
//
// Status mapping is deliberately coarse (2xx = success, everything else =
// not success). SESSION N §14 forbids inventing an HTTP error taxonomy in the
// C2 domain, so a 401 / 429 / 5xx all surface as the same bounded,
// non-secret "provider rejected" outcome.
// ---------------------------------------------------------------------------
class ChatCompletionsTransport : public IExtractionTransport
{
public:
    // The ONLY content type the official API-Inference contract accepts for
    // chat completions (verified 2026-09-30 against the official docs).
    static constexpr const char *kJsonContentType = "application/json";

    // T034-B2.1 SECURITY CORRECTION SLICE B2: the transport no longer binds
    // a plaintext credential at construction. The bearer token is supplied
    // PER REQUEST via setOneShotBearerToken() and consumed (cleared) by the
    // next send() — the transport never retains it beyond one dispatch.
    ChatCompletionsTransport(IChatCompletionsHttpClient &http, std::string endpoint,
                                  int timeoutMs);

    // One-shot credential supply for the NEXT send() only. Consumed and
    // cleared by send(); a send() without a pending one-shot token fails
    // closed (transportOk=false, no dispatch, dispatchCount_ unchanged).
    void setOneShotBearerToken(std::string token);

    [[nodiscard]] Exchange send(std::string_view requestBody) override;

    // T034-B2.2-A-R2: additive ASYNCHRONOUS path. Starts ONE asynchronous,
    // non-blocking exchange through the injected shared HTTP seam. The
    // one-shot bearer token is consumed for THIS request exactly like send()
    // (empty token / body / endpoint => NOT started, nothing dispatched).
    //
    // When the returned handle is VALID, `onDone` is invoked EXACTLY ONCE
    // (reply, transport failure or timeout). When the handle is INVALID,
    // nothing was dispatched and `onDone` is never invoked. Network
    // cancellation is deliberately NOT part of this contract.
    [[nodiscard]] ChatCompletionsHttpAsyncHandle sendAsync(
        std::string requestBody, QObject *lifetimeContext,
        ChatCompletionsHttpCompletion onDone);

    // Number of provider-level exchanges actually dispatched. Zero is the
    // number that proves a fail-closed path sent nothing.
    [[nodiscard]] int dispatchCount() const { return dispatchCount_; }

    // T034-B2.2-A-R2: number of ASYNCHRONOUS exchanges actually started
    // (0 proves an async fail-closed path dispatched nothing).
    [[nodiscard]] int asyncDispatchCount() const { return asyncDispatchCount_; }

private:
    IChatCompletionsHttpClient &http_;
    std::string endpoint_;
    // One-shot credential slot: populated per request, cleared by send().
    // Empty outside the request window — the transport retains no plaintext
    // credential between requests (T034-B2.1 security invariant).
    std::string oneShotBearerToken_;
    int timeoutMs_;
    int dispatchCount_{0};
    // T034-B2.2-A-R2: started (not merely attempted) async dispatches.
    int asyncDispatchCount_{0};
};

} // namespace modbuslens::ui