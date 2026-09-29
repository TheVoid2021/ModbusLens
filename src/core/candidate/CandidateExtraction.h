#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/manual/ManualDocument.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M12-C C2 — Provider-neutral AI Candidate Extraction (first deterministic
// slice). Pure C++20, Zero Qt: this layer owns the domain shape, the frozen
// tokens and the DETERMINISTIC validation. It performs no I/O, no network, no
// credential access and no persistence whatsoever — it cannot, because it is
// deliberately built on nothing but the standard library and the manual
// domain. Transport / provider-specific objects stay OUTSIDE this boundary.
//
// Canonical basis (T027 §66, HUMAN-APPROVED C2 CONTRACT):
//   · H1 PROVIDER — architecture stays provider-neutral. No ModelScope /
//     OpenAI -specific business type may appear here; the model id is
//     configuration and never domain truth.
//   · H4 PERSISTENCE — a Candidate is SESSION-ONLY / in-memory. Nothing in
//     this slice may write a Device Profile JSON, manual metadata, a cache or
//     any long-term store.
//   · H5 CONFIDENCE — C2 v1 does NOT use, display or persist a provider
//     reported numeric confidence. There is deliberately no confidence field
//     and no probability-like value anywhere in this file.
//   · H6 EVIDENCE — every valid Candidate must carry evidence that LOCAL
//     DETERMINISTIC CODE can verify: the Manual document identity, the content
//     identity, the exact excerpt and a location the program recomputed itself.
//     A provider-reported quote / location is UNTRUSTED INPUT; an excerpt that
//     cannot be verified MUST NOT become a valid Candidate.
//   · H7 LIFECYCLE — C2 only produces PendingReview. Accept / Edit / Reject
//     belong to C3; there is no AI -> verified Profile direct write.
//   · H8 OUTPUT CONTRACT — the AI may only extract from the supplied canonical
//     extracted text; it must never guess scale / offset / unit / register
//     meaning / address / field value. Provider output must pass deterministic
//     schema + evidence validation before it becomes a Candidate.
//
// AMBIGUOUS EVIDENCE MATCH POLICY = DEFERRED / NOT IMPLEMENTED / NOT GUESSED.
// The slice neither invents ranking nor merge semantics: it computes a location
// only when the exact excerpt is UNIQUELY locatable, and refuses otherwise
// rather than fabricate a span (the conservative reading of H6). Fixtures MUST
// use unique excerpts; no global ambiguity policy is frozen here.
// ---------------------------------------------------------------------------

// The §47.9 ProfileField candidate targets. This enumeration is a direct
// transcription of the frozen list (profileId / displayName / manufacturer /
// model / revision / description) — it is NOT a new invention.
enum class ProfileFieldTarget {
    ProfileId,
    DisplayName,
    Manufacturer,
    Model,
    Revision,
    Description,
};

[[nodiscard]] std::string_view profileFieldTargetToken(ProfileFieldTarget target);

// The FIRST SLICE implements exactly ONE target (T027 §66.3 / SESSION M §6):
// realizing every ProfileField plus RegisterEntryCandidate at once is out of
// scope. A proposal for any other target is refused by the deterministic
// validator, so this set is real behaviour — not a vacuous guard.
inline constexpr ProfileFieldTarget kC2FirstSliceProfileField =
    ProfileFieldTarget::Manufacturer;

[[nodiscard]] bool isC2FirstSliceProfileField(ProfileFieldTarget target);

// H7: C2 v1 only ever produces PendingReview. Accept / Edit / Reject are C3.
enum class CandidateLifecycleState {
    PendingReview,
};

[[nodiscard]] std::string_view candidateLifecycleStateToken(
    CandidateLifecycleState state);

// ---------------------------------------------------------------------------
// Candidate evidence (H6).
//
// documentId = the ManualDocument's program-generated identity. It is NEVER
// the contentHash: content identity and document identity are distinct
// (T027 §60.3). textStart / textEnd are CHARACTER offsets into the canonical
// extracted text, half open [start, end) — the SAME convention as the existing
// ManualEvidenceReference, so no second vocabulary is introduced. The location
// is computed by local deterministic code; a provider hint never appears here.
// ---------------------------------------------------------------------------
struct CandidateEvidence {
    std::string documentId;
    std::string contentHash;
    std::int64_t textStart{-1};
    std::int64_t textEnd{-1};
    std::string excerpt;

    [[nodiscard]] bool operator==(const CandidateEvidence&) const = default;
};

// A PendingReview ProfileField candidate. It is deliberately NOT a DeviceProfile
// and NOT a RegisterEntry: it can never be mistaken for verified truth.
//
// Deliberately absent: any confidence / probability / score field (H5), any
// raw provider response (H4 — the provider owns its own transient transport),
// and any persistence handle (H4).
struct ProfileFieldCandidate {
    ProfileFieldTarget target{ProfileFieldTarget::Manufacturer};
    std::string proposedValue;
    CandidateEvidence evidence;
    CandidateLifecycleState lifecycle{CandidateLifecycleState::PendingReview};

    [[nodiscard]] bool operator==(const ProfileFieldCandidate&) const = default;
};

// ---------------------------------------------------------------------------
// Provider-neutral seam (H1 / SESSION M §7).
//
// This is the ONLY thing the domain knows about a proposal source. A concrete
// provider (or its adapter) implements it; the production UI must never expose
// a fake implementation, and nothing here may carry a provider-specific type.
//
// locationHint exists because real providers DO report offsets — it is
// modelled as UNTRUSTED input on purpose so that ignoring it is testable real
// behaviour (C2-A06) instead of an untestable assumption. It must never reach
// CandidateEvidence.
// ---------------------------------------------------------------------------
struct CandidateProposal {
    ProfileFieldTarget target{ProfileFieldTarget::Manufacturer};
    std::string proposedValue;
    std::string evidenceExcerpt;
    std::int64_t locationHint{-1}; // UNTRUSTED provider hint — never truth
};

class ICandidateProposalProvider
{
public:
    virtual ~ICandidateProposalProvider() = default;

    // Returns the provider's raw, UNVALIDATED proposals. The implementation
    // owns its own transport concerns entirely; this layer never performs the
    // request itself and never sees credentials.
    [[nodiscard]] virtual std::vector<CandidateProposal> propose(
        const ManualDocument& document,
        std::string_view canonicalExtractedText) = 0;
};

struct CandidateExtractionResult {
    // Only fully validated candidates, always PendingReview.
    std::vector<ProfileFieldCandidate> candidates;
    // Deterministic accounting of refused proposals (unsupported target, empty
    // value, unverifiable / ambiguous excerpt). No provider prose is retained.
    std::size_t refusedProposalCount{0};
};

// The deterministic vertical slice: consume provider-neutral proposals, verify
// each against the canonical extracted text, and return the valid PendingReview
// candidates. Pure function of its arguments:
//   · it never mutates the ManualDocument, a DeviceProfile or any store;
//   · it performs no network access and requires no credential;
//   · the same inputs always yield an equivalent semantic result.
[[nodiscard]] CandidateExtractionResult extractProfileFieldCandidates(
    const ManualDocument& document,
    std::string_view canonicalExtractedText,
    ICandidateProposalProvider& provider);

} // namespace modbuslens::core