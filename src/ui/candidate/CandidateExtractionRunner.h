#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "ui/ai/AiProviderProfile.h"
#include "core/candidate/ProviderExtractionContract.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 THIRD SLICE — the extraction RUNNER seam (T027 §70, §7/§9/§10).
//
// The orchestration layer (CandidateExtractionController) never talks to a
// transport, a model or a credential: it only ever talks to this seam. That is
// what lets the automated suite run the whole consent -> orchestration ->
// Candidate-set pipeline against a DETERMINISTIC fake while the production
// wiring points at the real ModelScope adapter.
//
// Completion is delivered through a callback rather than a Qt signal so the
// seam stays Qt-free and a test can deliver a completion at an exact moment —
// which is what makes the stale-result guard (O12) observable instead of
// merely asserted.
// ---------------------------------------------------------------------------
class ICandidateExtractionRunner
{
public:
    virtual ~ICandidateExtractionRunner() = default;

    // The deterministic outcome of one attempt. It carries the generation that
    // began it, so a late completion for a superseded attempt can be dropped
    // instead of contaminating the newly selected document (§9).
    struct Completion {
        std::uint64_t generation{0};
        bool ok{false};
        core::ProviderExtractionFailure failure{
            core::ProviderExtractionFailure::None};
        std::vector<core::CandidateProposal> proposals;
        // T035-A.3.2.2: the REGISTER completion carries its own proposal
        // vector (a register_map attempt never mixes contracts with the
        // profile-field envelope). Exactly one of the two vectors is
        // meaningful, keyed by the request's target token.
        std::vector<core::RegisterCandidateProposal> registerProposals;
    };

    using CompletionHandler = std::function<void(const Completion &)>;

    // Begins one attempt. Returns false when the attempt cannot even start; in
    // that case no provider call happened and `onDone` is never invoked (the
    // controller records the failure itself). When it returns true, `onDone`
    // runs exactly once — synchronously or later.
    [[nodiscard]] virtual bool begin(const core::ExtractionRequest &request,
                                     std::uint64_t generation,
                                     const CompletionHandler &onDone) = 0;

    // T034-B2.1: saved-GUI configuration handoff (Human §10/§11). The default
    // is a no-op so existing deterministic runners stay valid; the production
    // runner uses them to rebuild its transport/adapter on hot switch. The
    // secret arrives already resolved (read from the credential store by the
    // runtime configuration owner) and is never stored by the controller.
    virtual void applyExplicitConfig(AiProviderKind kind, std::string model,
                                     std::string secret)
    {
        (void)kind;
        (void)model;
        (void)secret;
    }
    virtual void clearExplicitConfig() {}

    // Diagnostic counter for the automated tests: how many provider-level
    // attempts were actually started. Zero is the number that proves the
    // consent gate works (O01 / O02).
    [[nodiscard]] virtual int beginCount() const = 0;
};

} // namespace modbuslens::ui