#pragma once

#include <memory>
#include <string>

#include "ui/ai/ModelScopeCandidateAdapter.h"
#include "ui/candidate/CandidateExtractionRunner.h"
#include "ui/candidate/ModelScopeExtractionTransport.h"
#include "ui/candidate/ModelScopeHttpClient.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 FOURTH SLICE — the PRODUCTION runner (T027 §76, SESSION P §7/§8/§24).
//
// It is the only object that resolves configuration and reaches the real
// transport layer. Everything it reads comes from the PROCESS ENVIRONMENT:
//
//   MODELSCOPE_API_KEY            credential (canonical M6 name, reused)
//   MODBUSLENS_MODELSCOPE_MODEL   model id override (canonical M6 name, reused)
//
// The model id is CONFIGURATION, never business truth. The official ModelScope
// docs verified in this session state that model names in examples are
// illustrative and may be deprecated, so no new model is chosen here: the
// environment override wins, otherwise the accepted canonical default is used.
//
// Fail-closed rules (each yields ZERO provider dispatches):
//   · missing/empty credential  -> begin() returns false, no transport call
//   · missing/empty model id    -> begin() returns false, no transport call
// The token is never logged, persisted, echoed into an error, or placed in the
// request body — it exists only as the Authorization header of one exchange.
// ---------------------------------------------------------------------------
class ModelScopeCandidateRunner : public ICandidateExtractionRunner
{
public:
    // PRODUCTION: owns the Qt Network HTTP client.
    ModelScopeCandidateRunner();
    // INJECTION for the deterministic offline suite: the real transport code
    // path runs, but the HTTP exchange is captured by the test's fake client,
    // so no socket is ever opened.
    explicit ModelScopeCandidateRunner(IModelScopeHttpClient &http);

    [[nodiscard]] bool begin(const core::ExtractionRequest &request,
                             std::uint64_t generation,
                             const CompletionHandler &onDone) override;

    [[nodiscard]] int beginCount() const override { return beginCount_; }

    // Credential presence, read from the CANONICAL environment name only.
    [[nodiscard]] static bool hasConfiguredCredential();
    // Model id: environment override, else the accepted canonical default.
    [[nodiscard]] static std::string configuredModelId();
    // The official endpoint is a compiled constant, never user-configurable.
    [[nodiscard]] static std::string officialEndpoint();
    // Bounded production timeout (the accepted M6 value).
    [[nodiscard]] static int productionTimeoutMs();

    // Diagnostic: provider-level dispatches performed by the owned transport
    // (0 on every fail-closed path).
    [[nodiscard]] int dispatchCount() const;

private:
    void ensureWired();

    std::unique_ptr<QtModelScopeHttpClient> ownedHttp_;
    IModelScopeHttpClient *http_{nullptr};
    std::unique_ptr<ModelScopeExtractionTransport> transport_;
    std::unique_ptr<ModelScopeCandidateAdapter> adapter_;
    int beginCount_{0};
};

} // namespace modbuslens::ui