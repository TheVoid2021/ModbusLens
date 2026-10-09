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
// M12-C C2 FOURTH SLICE runner, extended by T034 B1 into the PROVIDER-SELECTION
// entry point of the Candidate chain (T034-A §D.1 with the Human design
// corrections of the B1 session).
//
// It is the only object that resolves provider configuration and reaches the
// real transport layer. B1 resolution rules (ALL from the process environment;
// no settings UI yet, no vault yet):
//
//   MODBUSLENS_AI_PROVIDER   "modelscope" | "deepseek"
//                            empty/unset = modelscope (backward compatibility)
//                            any other token = FAIL CLOSED (zero dispatch)
//   MODBUSLENS_AI_MODEL      model id override for EITHER provider
//   MODBUSLENS_MODELSCOPE_MODEL  LEGACY ModelScope-only override; read ONLY
//                            when provider == ModelScope, and only when
//                            MODBUSLENS_AI_MODEL is unset
//   MODELSCOPE_API_KEY       secret, read ONLY when provider == ModelScope
//   DEEPSEEK_API_KEY         secret, read ONLY when provider == DeepSeek
//
// SECRET ISOLATION is structural: the secret variable name comes from the
// provider profile (aiProviderSecretEnv), so no code path can read the other
// provider's credential, and no cross-provider fallback exists.
//
// Model resolution precedence:
//   provider == ModelScope: MODBUSLENS_AI_MODEL → legacy env → accepted
//                           canonical default "Qwen/Qwen3.5-27B" (M6 evidence)
//   provider == DeepSeek:   MODBUSLENS_AI_MODEL → else FAIL CLOSED (no model
//                           is invented; the model id is Human-configured)
//
// Fail-closed rules (each yields ZERO provider dispatches):
//   · unknown MODBUSLENS_AI_PROVIDER token -> begin() returns false
//   · missing/empty provider-specific credential -> begin() returns false
//   · missing/empty model id (DeepSeek)          -> begin() returns false
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

    // ---- legacy statics (ModelScope semantics; kept for the frozen P-matrix)
    // Credential presence for the DEFAULT (ModelScope) path.
    [[nodiscard]] static bool hasConfiguredCredential();
    // Model id for the ModelScope profile: AI_MODEL → legacy env → default.
    [[nodiscard]] static std::string configuredModelId();
    // The ModelScope official endpoint (frozen P03 assertion).
    [[nodiscard]] static std::string officialEndpoint();
    // Bounded production timeout (the accepted M6 value).
    [[nodiscard]] static int productionTimeoutMs();

    // ---- B1 provider-aware resolution --------------------------------------
    [[nodiscard]] static std::optional<AiProviderKind> resolveProviderKind();
    // Per-provider secret lookup (the ONLY credential read path).
    [[nodiscard]] static std::string configuredSecretFor(AiProviderKind kind);
    // Per-provider model resolution (precedence documented above).
    [[nodiscard]] static std::string configuredModelIdFor(AiProviderKind kind);

    // Diagnostic: provider-level dispatches performed by the owned transport
    // (0 on every fail-closed path).
    [[nodiscard]] int dispatchCount() const;

private:
    void ensureWired(AiProviderKind kind, const std::string &secret,
                     const std::string &model);

    std::unique_ptr<QtChatCompletionsHttpClient> ownedHttp_;
    IChatCompletionsHttpClient *http_{nullptr};
    std::unique_ptr<ChatCompletionsTransport> transport_;
    std::unique_ptr<CandidateProviderAdapter> adapter_;
    int beginCount_{0};
};

} // namespace modbuslens::ui
