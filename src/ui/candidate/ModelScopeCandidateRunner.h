#pragma once

#include "ui/candidate/CandidateExtractionRunner.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 THIRD SLICE — the PRODUCTION runner (T027 §70, §10/§11).
//
// It is the only object allowed to know about the ModelScope adapter. It is
// deliberately inert until both halves of the safety gate are satisfied:
//
//   1. a credential must be present. The canonical source is the already
//      accepted process-environment contract (MODELSCOPE_API_KEY); no new
//      credential source is invented here.
//   2. a provider transport must be configured. SESSION N deliberately ships
//      no concrete network transport, so this is unconfigured in SESSION O.
//
// Either gap yields a deterministic `NotConfigured` failure with ZERO provider
// calls — a missing credential can never degrade into an authenticated request,
// and SESSION O can never execute a live inference.
// ---------------------------------------------------------------------------
class ModelScopeCandidateRunner : public ICandidateExtractionRunner
{
public:
    ModelScopeCandidateRunner() = default;

    [[nodiscard]] bool begin(const core::ExtractionRequest &request,
                             std::uint64_t generation,
                             const CompletionHandler &onDone) override;

    [[nodiscard]] int beginCount() const override { return beginCount_; }

    // Credential presence, read from the CANONICAL environment name only.
    [[nodiscard]] static bool hasConfiguredCredential();

private:
    int beginCount_{0};
};

} // namespace modbuslens::ui