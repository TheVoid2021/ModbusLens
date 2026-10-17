#include "core/candidate/CandidateExtraction.h"

#include <cmath>
#include <vector>

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

// EXACT-ONLY locator — the PROFILE-FIELD / manufacturer rule, deliberately
// UNCHANGED by T035-A.3.2.3. Manufacturers keep the original conservative H6
// behaviour: the excerpt must appear VERBATIM, exactly once. The line-ending
// fallback is a REGISTER-only concession (Human §5/§10 forbid touching the
// manufacturer evidence contract), so this helper does NOT delegate to the
// shared matcher — delegating would have silently widened manufacturer
// behaviour, which is exactly the kind of quiet relaxation the task forbids.
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
    if (canonicalText.find(excerpt, first + 1) != std::string_view::npos) {
        return false; // ambiguous location: DEFERRED, never guessed
    }
    outStart = static_cast<std::int64_t>(first);
    outEnd = static_cast<std::int64_t>(first + excerpt.size());
    return true;
}

} // namespace

bool isNormalizedLineEndingEquivalent(char lhs, char rhs)
{
    // The ONLY permitted equivalence: the three line-ending conventions are
    // interchangeable. Everything else must be byte-identical — this function
    // is deliberately total and stateless so tests and forensics can share it.
    const auto canonical = [](char value) -> char {
        return value == '\r' ? '\n' : value;
    };
    return canonical(lhs) == canonical(rhs);
}

std::string normalizeLineEndings(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const char current = raw[index];
        if (current == '\r') {
            // CRLF (two raw chars) and a lone CR both collapse to ONE '\n'.
            // The look-ahead consumes the LF of a CRLF pair so the pair can
            // never be counted twice.
            if (index + 1 < raw.size() && raw[index + 1] == '\n') {
                ++index;
            }
            out.push_back('\n');
            continue;
        }
        out.push_back(current);
    }
    return out;
}

std::string_view evidenceLocateCodeName(EvidenceLocateCode code)
{
    switch (code) {
    case EvidenceLocateCode::Located:
        return "located";
    case EvidenceLocateCode::NotFound:
        return "not_found";
    case EvidenceLocateCode::Ambiguous:
        return "ambiguous";
    }
    return "unknown";
}

namespace {

// Build the deterministic normalized-index -> raw-offset map for `raw`.
// mapping[k] is the RAW byte offset that produced normalized character k;
// mapping[size()] is the raw end of the view (== raw.size()). A CRLF pair
// (two raw bytes) yields exactly one entry, anchored on the FIRST byte of the
// run, so a normalized span can always be mapped back to a real raw span.
[[nodiscard]] std::vector<std::size_t> buildRawOffsetMap(std::string_view raw)
{
    std::vector<std::size_t> mapping;
    mapping.reserve(raw.size() + 1);
    for (std::size_t index = 0; index < raw.size(); ++index) {
        const char current = raw[index];
        mapping.push_back(index);
        if (current == '\r' && index + 1 < raw.size() && raw[index + 1] == '\n') {
            ++index; // the LF belongs to the same normalized character
        }
    }
    mapping.push_back(raw.size()); // sentinel: one-past-the-end raw offset
    return mapping;
}

} // namespace

EvidenceLocateCode locateEvidenceExcerpt(std::string_view canonicalText,
                                         std::string_view excerpt,
                                         std::int64_t &outStart,
                                         std::int64_t &outEnd)
{
    outStart = -1;
    outEnd = -1;
    if (excerpt.empty()) {
        return EvidenceLocateCode::NotFound; // empty evidence is its own refusal
    }

    // --- STEP 1: EXACT FIRST (unchanged behaviour) -------------------------
    const std::size_t exactFirst = canonicalText.find(excerpt);
    if (exactFirst != std::string_view::npos) {
        if (canonicalText.find(excerpt, exactFirst + 1) != std::string_view::npos) {
            return EvidenceLocateCode::Ambiguous; // never guess which one
        }
        outStart = static_cast<std::int64_t>(exactFirst);
        outEnd = static_cast<std::int64_t>(exactFirst + excerpt.size());
        return EvidenceLocateCode::Located;
    }

    // --- STEP 2: LINE-ENDING-NORMALIZED FALLBACK --------------------------
    // Reached ONLY because the exact match failed. Both sides are normalized
    // with the SAME rule, so this can only ever bridge pure line-ending
    // differences; a TAB, a collapsed space, a changed digit or any other
    // non-line-ending difference still fails here.
    const std::string normalizedEvidence = normalizeLineEndings(excerpt);
    if (normalizedEvidence.empty()) {
        return EvidenceLocateCode::NotFound; // e.g. an all-CR excerpt
    }
    const std::string normalizedText = normalizeLineEndings(canonicalText);

    const std::size_t normalizedFirst = normalizedText.find(normalizedEvidence);
    if (normalizedFirst == std::string::npos) {
        return EvidenceLocateCode::NotFound;
    }
    // UNIQUENESS IS MANDATORY AND NOT RELAXED. CRLF -> LF can merge two
    // originally distinct raw spans into one normalized span, so this check is
    // at least as strict as the exact one — the first hit is never taken.
    if (normalizedText.find(normalizedEvidence, normalizedFirst + 1)
        != std::string::npos) {
        return EvidenceLocateCode::Ambiguous;
    }

    // --- STEP 3: REAL RAW INDEX MAPPING -----------------------------------
    // Map the normalized span back onto the ORIGINAL text. The end offset is
    // the raw offset of the character AFTER the last normalized character, so
    // a span ending inside a CRLF pair still round-trips through normalization.
    const std::vector<std::size_t> mapping = buildRawOffsetMap(canonicalText);
    const std::size_t normalizedEnd = normalizedFirst + normalizedEvidence.size();
    if (normalizedEnd >= mapping.size()) {
        return EvidenceLocateCode::NotFound; // defensive: impossible by build
    }
    const std::size_t rawStart = mapping[normalizedFirst];
    const std::size_t rawEnd = mapping[normalizedEnd];
    if (rawEnd <= rawStart || rawEnd > canonicalText.size()) {
        return EvidenceLocateCode::NotFound; // defensive: never a fake span
    }

    // --- STEP 4: ROUND-TRIP BEFORE PUBLISHING THE SPAN --------------------
    // The mapped raw slice must normalize to EXACTLY the normalized evidence.
    // A legal range is not enough — the content must be equivalent under the
    // authorized rule, otherwise the span is rejected rather than stored.
    const std::string_view rawSlice =
        canonicalText.substr(rawStart, rawEnd - rawStart);
    if (normalizeLineEndings(rawSlice) != normalizedEvidence) {
        return EvidenceLocateCode::NotFound;
    }

    outStart = static_cast<std::int64_t>(rawStart);
    outEnd = static_cast<std::int64_t>(rawEnd);
    return EvidenceLocateCode::Located;
}

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

std::string_view registerCandidateValidationCodeName(
    RegisterCandidateValidationCode code)
{
    switch (code) {
    case RegisterCandidateValidationCode::Ok:
        return "ok";
    case RegisterCandidateValidationCode::NameMissing:
        return "name_missing";
    case RegisterCandidateValidationCode::InvalidFunctionCode:
        return "invalid_function_code";
    case RegisterCandidateValidationCode::UnsupportedDataType:
        return "unsupported_data_type";
    case RegisterCandidateValidationCode::QuantityMismatch:
        return "quantity_mismatch";
    case RegisterCandidateValidationCode::NonFiniteScale:
        return "non_finite_scale";
    case RegisterCandidateValidationCode::NonFiniteOffset:
        return "non_finite_offset";
    case RegisterCandidateValidationCode::EmptyExcerpt:
        return "evidence_excerpt_missing";
    case RegisterCandidateValidationCode::EvidenceNotLocatable:
        return "evidence_not_found";
    }
    return "evidence_invalid";
}

RegisterCandidateExtractionResult extractRegisterCandidates(
    const ManualDocument& document,
    std::string_view canonicalExtractedText,
    const std::vector<RegisterCandidateProposal>& proposals)
{
    RegisterCandidateExtractionResult result;

    for (const RegisterCandidateProposal &proposal : proposals) {
        // --- deterministic schema validation (ADR-004 / T035-A.1 §5) -------
        // The AI may only report what the source supports; a missing name is
        // a guess and never a candidate.
        if (proposal.name.empty()) {
            ++result.refusedProposalCount;
            continue;
        }
        // Register-space identity: the same frozen M10 domain as
        // validateDeviceProfile (0x01..0x7F; 0x00 and the exception space
        // are never accepted and never defaulted).
        if (proposal.functionCode < 0x01 || proposal.functionCode > 0x7F) {
            ++result.refusedProposalCount;
            continue;
        }
        // quantity MUST equal the type's word count — never auto-corrected
        // (Human T035-A.1 §5: quantity=3 with Uint16 is a refusal, not a
        // repair). A type outside the M11 matrix has no word count at all.
        const int requiredWords =
            registerDecodeTypeWordCount(proposal.dataType);
        if (requiredWords == 0 || proposal.quantity != requiredWords) {
            ++result.refusedProposalCount;
            continue;
        }
        if (!std::isfinite(proposal.scale)) {
            ++result.refusedProposalCount;
            continue;
        }
        if (!std::isfinite(proposal.offset)) {
            ++result.refusedProposalCount;
            continue;
        }

        // --- deterministic evidence validation (H6) ------------------------
        // T035-A.3.2.3: the ONE shared rule — EXACT FIRST, then a
        // LINE-ENDING-normalized fallback (CRLF / lone CR / LF equivalence
        // only). Uniqueness is still mandatory and is checked over the WHOLE
        // view, so CRLF -> LF can only ever make this STRICTER, never looser.
        // The returned span is always a REAL raw offset, never a normalized
        // index. The STORED excerpt remains the PROVIDER's submitted string
        // (unmodified): the fallback proves it is line-ending-equivalent to a
        // unique raw slice, it does NOT replace the provider's evidence with a
        // re-read of the canonical text (no laundering — Human §5).
        std::int64_t textStart = -1;
        std::int64_t textEnd = -1;
        const EvidenceLocateCode located = locateEvidenceExcerpt(
            canonicalExtractedText, proposal.evidenceExcerpt, textStart,
            textEnd);
        if (located != EvidenceLocateCode::Located) {
            ++result.refusedProposalCount;
            continue;
        }

        RegisterEntryCandidate candidate;
        candidate.functionCode = proposal.functionCode;
        candidate.address = proposal.address;
        candidate.name = proposal.name;
        candidate.description = proposal.description;
        candidate.dataType = proposal.dataType;
        candidate.quantity = proposal.quantity;
        candidate.scale = proposal.scale;
        candidate.offset = proposal.offset;
        candidate.unit = proposal.unit;
        candidate.hasDescription = proposal.hasDescription;
        candidate.hasScale = proposal.hasScale;
        candidate.hasOffset = proposal.hasOffset;
        candidate.hasUnit = proposal.hasUnit;
        candidate.evidence.documentId = document.documentId;   // identity
        candidate.evidence.contentHash = document.contentHash; // content
        candidate.evidence.textStart = textStart;              // local
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
    // PROFILE-FIELD / manufacturer path — EXACT-ONLY, byte-for-byte the rule
    // this function has always applied. T035-A.3.2.3 deliberately does NOT
    // widen it (Human §5/§10: the manufacturer evidence contract is frozen);
    // the line-ending concession lives in revalidateRegisterCandidateEvidence.
    if (evidence.excerpt.empty()) {
        return CandidateEvidenceRevalidationCode::EmptyExcerpt;
    }
    if (evidence.textStart < 0 || evidence.textEnd <= evidence.textStart
        || evidence.textEnd > static_cast<std::int64_t>(canonicalExtractedText.size())) {
        return CandidateEvidenceRevalidationCode::InvalidRange;
    }
    const std::string_view storedSlice =
        canonicalExtractedText.substr(
            static_cast<std::size_t>(evidence.textStart),
            static_cast<std::size_t>(evidence.textEnd - evidence.textStart));
    if (storedSlice != evidence.excerpt) {
        return CandidateEvidenceRevalidationCode::RoundTripFailed;
    }
    std::int64_t locatedStart = -1;
    std::int64_t locatedEnd = -1;
    if (!locateUniqueExcerpt(canonicalExtractedText, evidence.excerpt,
                             locatedStart, locatedEnd)) {
        return CandidateEvidenceRevalidationCode::NotUnique;
    }
    if (locatedStart != evidence.textStart || locatedEnd != evidence.textEnd) {
        return CandidateEvidenceRevalidationCode::RoundTripFailed;
    }
    return CandidateEvidenceRevalidationCode::Ok;
}

CandidateEvidenceRevalidationCode revalidateRegisterCandidateEvidence(
    const CandidateEvidence& evidence,
    std::string_view canonicalExtractedText)
{
    // REGISTER path — the SAME deterministic rule as register GENERATION time
    // (locateEvidenceExcerpt: exact first, then the line-ending-normalized
    // fallback with real raw offsets and a mandatory uniqueness check). This is
    // the Human §5 "same deterministic evidence rule at Accept" requirement:
    // what passed extraction must pass revalidation, and what failed must fail.
    //
    // The stored excerpt is STILL the provider's exact submission — the
    // normalized comparison below only ESTABLISHES line-ending equivalence, it
    // never rewrites the stored evidence, so authenticity is not laundered.
    if (evidence.excerpt.empty()) {
        return CandidateEvidenceRevalidationCode::EmptyExcerpt;
    }
    if (evidence.textStart < 0 || evidence.textEnd <= evidence.textStart
        || evidence.textEnd > static_cast<std::int64_t>(canonicalExtractedText.size())) {
        return CandidateEvidenceRevalidationCode::InvalidRange;
    }
    const std::string_view storedSlice =
        canonicalExtractedText.substr(
            static_cast<std::size_t>(evidence.textStart),
            static_cast<std::size_t>(evidence.textEnd - evidence.textStart));
    if (storedSlice != evidence.excerpt
        && normalizeLineEndings(storedSlice)
               != normalizeLineEndings(evidence.excerpt)) {
        return CandidateEvidenceRevalidationCode::RoundTripFailed;
    }
    std::int64_t locatedStart = -1;
    std::int64_t locatedEnd = -1;
    const EvidenceLocateCode located = locateEvidenceExcerpt(
        canonicalExtractedText, evidence.excerpt, locatedStart, locatedEnd);
    if (located == EvidenceLocateCode::Ambiguous) {
        return CandidateEvidenceRevalidationCode::NotUnique;
    }
    if (located != EvidenceLocateCode::Located) {
        return CandidateEvidenceRevalidationCode::RoundTripFailed;
    }
    if (locatedStart != evidence.textStart || locatedEnd != evidence.textEnd) {
        return CandidateEvidenceRevalidationCode::RoundTripFailed;
    }
    return CandidateEvidenceRevalidationCode::Ok;
}

} // namespace modbuslens::core