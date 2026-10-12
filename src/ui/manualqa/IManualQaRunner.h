#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/manualqa/ManualQaContract.h"
#include "ui/ai/AiProviderProfile.h" // AiProviderKind (Qt-free header)

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
//
// T034-B2.2-B MIGRATION: the seam now carries an ALREADY-RESOLVED dispatch
// configuration. The owner resolves provider/model/credential from the single
// global AI configuration owner and hands the result down; the runner NEVER
// reads the process environment and holds no credential authority of its own.
// That is what makes the runner interchangeable (ModelScope / DeepSeek /
// future providers) instead of ModelScope-specific.
// ---------------------------------------------------------------------------

// The resolved dispatch inputs. Provider-neutral by construction: a provider
// identity, an endpoint, a model id and a credential are exactly what an
// OpenAI-compatible Chat Completions exchange needs.
//
// T034-B2.2-B-R1: `kind` carries the PROVIDER IDENTITY from the resolved
// global snapshot. It exists so the payload builder can select
// provider-specific wire extras WITHOUT any string sniffing of the endpoint
// (e.g. `if (endpoint contains "modelscope")`), which is explicitly
// forbidden.
struct ManualQaDispatchConfig {
    AiProviderKind kind{AiProviderKind::ModelScope}; // from the resolved snapshot
    std::string endpoint;      // from AiProviderProfile (or a test override)
    std::string model;         // from the resolved global snapshot
    std::string secret;        // provider-scoped credential for THIS dispatch
    int timeoutMs{90000};      // bounded, non-interactive inference budget
};

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

    // Begins one Q&A attempt with an ALREADY-RESOLVED configuration. Returns
    // false when the attempt cannot even start (e.g. no configured credential
    // — fail-closed, zero network); in that case onDone is never invoked and
    // the controller records the failure itself. When it returns true, onDone
    // runs exactly once — synchronously or later (asynchronously in
    // production).
    [[nodiscard]] virtual bool begin(const ManualQaDispatchConfig& config,
                                     const core::ManualQaRequest& request,
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