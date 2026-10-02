#include "core/candidate/CandidateExtraction.h"

namespace modbuslens::core {

std::string_view profileFieldTargetToken(ProfileFieldTarget target)
{
    switch (target) {
    case ProfileFieldTarget::ProfileId:
        return "profile_id";
    case ProfileFieldTarget::DisplayName:
        return "display_name";
    case ProfileFieldTarget::Manufacturer:
        return "manufacturer";
    case ProfileFieldTarget::Model:
        return "model";
    case ProfileFieldTarget::Revision:
        return "revision";
    case ProfileFieldTarget::Description:
        return "description";
    }
    return "unknown";
}

bool profileFieldTargetFromToken(std::string_view token, ProfileFieldTarget &out)
{
    // Single source of truth: each candidate is accepted only when its OWN
    // forward token round-trips, so the two directions cannot drift apart.
    // An unknown token is refused (the provider cannot widen the contract).
    for (const ProfileFieldTarget target : {
             ProfileFieldTarget::ProfileId,
             ProfileFieldTarget::DisplayName,
             ProfileFieldTarget::Manufacturer,
             ProfileFieldTarget::Model,
             ProfileFieldTarget::Revision,
             ProfileFieldTarget::Description,
         }) {
        if (profileFieldTargetToken(target) == token) {
            out = target;
            return true;
        }
    }
    return false;
}

bool isC2FirstSliceProfileField(ProfileFieldTarget target)
{
    // First slice = exactly one target (T027 §66.3). Everything else is a
    // deterministic refusal, not an accidental pass-through.
    return target == kC2FirstSliceProfileField;
}

std::string_view candidateLifecycleStateToken(CandidateLifecycleState state)
{
    switch (state) {
    case CandidateLifecycleState::PendingReview:
        return "pending_review";
    case CandidateLifecycleState::Accepted:
        return "accepted";
    case CandidateLifecycleState::Rejected:
        return "rejected";
    }
    return "unknown";
}

namespace {

// Locate `excerpt` in the canonical extracted text. Returns false when the
// excerpt is absent — or NOT UNIQUELY locatable. Ambiguity has no frozen rule
// in this slice (AMBIGUOUS EVIDENCE MATCH POLICY = DEFERRED / NOT GUESSED), so
// the conservative reading of H6 is applied: refuse rather than fabricate a
// span. Providers cannot influence this: they supply nothing but a string, and
// any location hint they send is ignored by construction.
[[nodiscard]] bool locateUniqueExcerpt(std::string_view canonicalText,
                                       std::string_view excerpt,
                                       std::int64_t &outStart,
                                       std::int64_t &outEnd)
{
    if (excerpt.empty()) {
        return false;
    }
    const std::size_t first = canonicalText.find(excerpt);
    if (first == std::string_view::npos) {
        return false; // cannot be verified => MUST NOT become a Candidate (H6)
    }
    const std::size_t second = canonicalText.find(excerpt, first + 1);
    if (second != std::string_view::npos) {
        return false; // ambiguous location: DEFERRED, never guessed
    }
    outStart = static_cast<std::int64_t>(first);
    outEnd = static_cast<std::int64_t>(first + excerpt.size());
    return true;
}

} // namespace

CandidateExtractionResult extractProfileFieldCandidates(
    const ManualDocument& document,
    std::string_view canonicalExtractedText,
    ICandidateProposalProvider& provider)
{
    CandidateExtractionResult result;

    // The provider is the only proposal source; its transport, credentials and
    // lifecycle stay entirely on its own side of the seam (H1/H3).
    const std::vector<CandidateProposal> proposals =
        provider.propose(document, canonicalExtractedText);

    for (const CandidateProposal &proposal : proposals) {
        // --- deterministic schema validation (H8) --------------------------
        // Only the frozen first-slice target is accepted; a provider cannot
        // widen the contract by proposing something else.
        if (!isC2FirstSliceProfileField(proposal.target)) {
            ++result.refusedProposalCount;
            continue;
        }
        // The AI may only report what the source supports: an empty proposed
        // value is a guess and never a candidate.
        if (proposal.proposedValue.empty()) {
            ++result.refusedProposalCount;
            continue;
        }

        // --- deterministic evidence validation (H6) ------------------------
        // NOTE: proposal.locationHint is deliberately NOT consulted. A
        // provider-reported offset is UNTRUSTED input; the trusted location is
        // recomputed here from the canonical extracted text.
        std::int64_t textStart = -1;
        std::int64_t textEnd = -1;
        if (!locateUniqueExcerpt(canonicalExtractedText, proposal.evidenceExcerpt,
                                 textStart, textEnd)) {
            ++result.refusedProposalCount;
            continue;
        }

        ProfileFieldCandidate candidate;
        candidate.target = proposal.target;
        candidate.proposedValue = proposal.proposedValue;
        candidate.evidence.documentId = document.documentId;   // identity
        candidate.evidence.contentHash = document.contentHash; // content identity
        candidate.evidence.textStart = textStart;              // locally computed
        candidate.evidence.textEnd = textEnd;
        candidate.evidence.excerpt = proposal.evidenceExcerpt;
        candidate.lifecycle = CandidateLifecycleState::PendingReview;
        result.candidates.push_back(std::move(candidate));
    }

    return result;
}

std::string_view candidateEvidenceRevalidationCodeName(
    CandidateEvidenceRevalidationCode code)
{
    switch (code) {
    case CandidateEvidenceRevalidationCode::Ok:
        return "evidence_ok";
    case CandidateEvidenceRevalidationCode::EmptyExcerpt:
        return "evidence_excerpt_missing";
    case CandidateEvidenceRevalidationCode::InvalidRange:
        return "evidence_range_invalid";
    case CandidateEvidenceRevalidationCode::RoundTripFailed:
        return "evidence_round_trip_failed";
    case CandidateEvidenceRevalidationCode::NotUnique:
        return "evidence_not_unique";
    }
    return "evidence_invalid";
}

CandidateEvidenceRevalidationCode revalidateCandidateEvidence(
    const CandidateEvidence& evidence,
    std::string_view canonicalExtractedText)
{
    if (evidence.excerpt.empty()) {
        return CandidateEvidenceRevalidationCode::EmptyExcerpt;
    }
    if (evidence.textStart < 0 || evidence.textEnd <= evidence.textStart
        || evidence.textEnd > static_cast<std::int64_t>(canonicalExtractedText.size())) {
        return CandidateEvidenceRevalidationCode::InvalidRange;
    }
    // The recorded span must still round-trip to the exact excerpt. A position
    // drift with identical content is caught by the uniqueness rule below; a
    // content change is caught here.
    if (canonicalExtractedText.substr(static_cast<std::size_t>(evidence.textStart),
                                      static_cast<std::size_t>(
                                          evidence.textEnd - evidence.textStart))
        != evidence.excerpt) {
        return CandidateEvidenceRevalidationCode::RoundTripFailed;
    }
    // The excerpt must STILL be uniquely locatable (same conservative H6 rule
    // as generation time — the world may have made it ambiguous since).
    std::int64_t locatedStart = -1;
    std::int64_t locatedEnd = -1;
    if (!locateUniqueExcerpt(canonicalExtractedText, evidence.excerpt, locatedStart,
                             locatedEnd)) {
        return CandidateEvidenceRevalidationCode::NotUnique;
    }
    if (locatedStart != evidence.textStart || locatedEnd != evidence.textEnd) {
        return CandidateEvidenceRevalidationCode::RoundTripFailed;
    }
    return CandidateEvidenceRevalidationCode::Ok;
}

} // namespace modbuslens::core