#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/manualqa/ManualQaContract.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-D FIRST SLICE — the Q&A RUNNER seam (T027 §100.6: engineering detail).
// Same pattern as the M12-C ICandidateExtractionRunner: the orchestration
// owner (ManualQaController) never talks to a transport, a model or a
// credential — only to this seam. The semantic interface deliberately does
// NOT expose CandidateExtractionRequest, ProfileFieldCandidate or any C2
// extraction parser type.
//
// The M12-D Q&A consent (D4) is owned by ManualQaController, never by a
// runner: a runner only ever runs an ALREADY-CONSENTED dispatch.
// ---------------------------------------------------------------------------

class IManualQaRunner
{
public:
    virtual ~IManualQaRunner() = default;

    // The raw provider outcome. `ok` mirrors transport-level success; the
    // rawJson is the provider payload (kept in memory only — D5 session-only,
    // never persisted or logged) for the controller's strict fail-closed
    // parse. A completion is always bound to the generation that began it.
    struct Completion {
        std::uint64_t generation{0};
        bool ok{false};
        std::string failureToken; // transport/provider failure when !ok
        std::string rawJson;      // provider payload when ok
    };

    using CompletionHandler = std::function<void(const Completion&)>;

    // Begins one Q&A attempt. Returns false when the attempt cannot even
    // start (e.g. no configured credential — fail-closed, zero network); in
    // that case onDone is never invoked and the controller records the
    // failure itself. When it returns true, onDone runs exactly once —
    // synchronously or later (asynchronously in production).
    [[nodiscard]] virtual bool begin(const core::ManualQaRequest& request,
                                     std::uint64_t generation,
                                     const CompletionHandler& onDone) = 0;

    // Best-effort cancellation of the in-flight attempt. A completion for an
    // already-invalidated generation is dropped by the controller regardless.
    virtual void cancel() = 0;

    // Diagnostic counter for automated tests: how many provider-level
    // dispatches actually started.
    [[nodiscard]] virtual int beginCount() const = 0;
};

} // namespace modbuslens::ui
