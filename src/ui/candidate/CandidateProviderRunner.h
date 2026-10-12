#pragma once

#include <memory>
#include <string>

#include "ui/ai/AiProviderProfile.h"
#include "ui/ai/CandidateProviderAdapter.h"
#include "ui/candidate/CandidateExtractionRunner.h"
#include "ui/candidate/ChatCompletionsTransport.h"
#include "ui/candidate/ChatCompletionsHttpClient.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 FOURTH SLICE runner, extended by T034-B2-LC2 into a STRICTLY
// EXPLICIT-CONFIGURATION dispatch: the ONLY authority is the resolved saved
// global AI configuration (provider/model/credential) handed in through
// applyExplicitConfig(). The legacy process-environment authority is removed —
// begin() without an applied configuration fails closed with zero dispatches.
//
// Fail-closed rules (each yields ZERO provider dispatches):
//   · no applied explicit configuration        -> begin() returns false
//   · missing/empty provider-specific credential -> begin() returns false
//   · missing/empty model id                   -> begin() returns false
// The secret is never logged, persisted, echoed into an error, or placed in
// the request body — it exists only as the Authorization header of one
// exchange.
// ---------------------------------------------------------------------------
class CandidateProviderRunner : public ICandidateExtractionRunner
{
public:
    // PRODUCTION: owns the Qt Network HTTP client.
    CandidateProviderRunner();
    // INJECTION for the deterministic offline suite: the real transport code
    // path runs, but the HTTP exchange is captured by the test's fake client,
    // so no socket is ever opened.
    explicit CandidateProviderRunner(IChatCompletionsHttpClient &http);

    [[nodiscard]] bool begin(const core::ExtractionRequest &request,
                             std::uint64_t generation,
                             const CompletionHandler &onDone) override;

    [[nodiscard]] int beginCount() const override { return beginCount_; }

    // ---- harness sentinel (NOT an authority) -------------------------------
    // DETECTS an ambient legacy ModelScope credential so the deterministic
    // consent gate can refuse to run (consumed by main.cpp). It never
    // configures or authorizes a provider.
    [[nodiscard]] static bool hasConfiguredCredential();
    // The ModelScope official endpoint (frozen P03 assertion).
    [[nodiscard]] static std::string officialEndpoint();
    // Bounded production timeout (the accepted M6 value).
    [[nodiscard]] static int productionTimeoutMs();

    // Diagnostic: provider-level dispatches performed by the owned transport
    // (0 on every fail-closed path).
    [[nodiscard]] int dispatchCount() const;

    // ---- B2.1 explicit (saved GUI) configuration --------------------------
    // When the runtime configuration owner resolves a saved provider/model/
    // secret, it applies them HERE before begin(); they take precedence over
    // the env fallback and survive until replaced or cleared. Every change
    // marks the transport/adapter for a rebuild so a hot switch takes effect
    // on the NEXT request without a restart (Human §11).
    void applyExplicitConfig(AiProviderKind kind, std::string model,
                             std::string secret);
    void clearExplicitConfig();

private:
    void ensureWired(AiProviderKind kind, const std::string &model);

    std::unique_ptr<QtChatCompletionsHttpClient> ownedHttp_;
    IChatCompletionsHttpClient *http_{nullptr};
    std::unique_ptr<ChatCompletionsTransport> transport_;
    std::unique_ptr<CandidateProviderAdapter> adapter_;
    int beginCount_{0};
    // B2.1 explicit configuration state (saved GUI path).
    bool explicitConfig_{false};
    bool rewireNeeded_{false};
    AiProviderKind explicitKind_{AiProviderKind::ModelScope};
    std::string explicitModel_;
    std::string explicitSecret_;
};

} // namespace modbuslens::ui
