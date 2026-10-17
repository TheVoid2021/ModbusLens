#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/analysis/RegisterDecode.h"
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
// AMBIGUOUS EVIDENCE MATCH POLICY = DEFERRED / NOT GUESSED.
// The slice neither invents ranking nor merge semantics: it computes a location
// only when the exact excerpt is UNIQUELY locatable, and refuses otherwise
// rather than fabricate a span (the conservative reading of H6). Fixtures MUST
// use unique excerpts; no global ambiguity policy is frozen here.
//
// T035-A.3.2.3 AMENDMENT (Human-adjudicated Option A, LIMITED): the
// PROFILE-FIELD / manufacturer path stays EXACT-ONLY as it always was. The
// REGISTER path additionally accepts a unique match under LINE-ENDING
// EQUIVALENCE ONLY (CRLF / lone CR / LF) — see locateEvidenceExcerpt below for
// the full scope, the safety properties and the explicit non-goals. Ambiguity
// remains a refusal on every path, and the first match is never taken.
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

// Reverse of profileFieldTargetToken. The strict provider parser must turn a
// provider-supplied token back into the frozen target identity; anything
// outside the frozen list yields false and must be REFUSED, never guessed
// (C2 SECOND SLICE, T027 §68). This resolves domain identity only — it does
// NOT apply the first-slice restriction, which stays owned by the SESSION M
// validator (isC2FirstSliceProfileField) so the two rules cannot drift apart.
[[nodiscard]] bool profileFieldTargetFromToken(std::string_view token,
                                               ProfileFieldTarget &out);

// The FIRST SLICE implements exactly ONE target (T027 §66.3 / SESSION M §6):
// realizing every ProfileField plus RegisterEntryCandidate at once is out of
// scope. A proposal for any other target is refused by the deterministic
// validator, so this set is real behaviour — not a vacuous guard.
inline constexpr ProfileFieldTarget kC2FirstSliceProfileField =
    ProfileFieldTarget::Manufacturer;

[[nodiscard]] bool isC2FirstSliceProfileField(ProfileFieldTarget target);

// H7: C2 v1 only ever produces PendingReview. Accept / Edit / Reject are C3.
// C3-H5/H6 (T027 §81): Accepted / Rejected are CONSUMED states — a consumed
// Candidate has left the PendingReview set and can never be applied again.
// They are session-only (H4) and never persisted anywhere.
enum class CandidateLifecycleState {
    PendingReview,
    Accepted,
    Rejected,
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

// ---------------------------------------------------------------------------
// T035-A.1 — REGISTER CANDIDATE DOMAIN (ADR-004 evidence-gated revision,
// HUMAN-APPROVED direction; v1 minimal schema).
//
// A RegisterCandidateProposal is the provider-neutral, UNVALIDATED output of
// the strict register parser; a RegisterEntryCandidate is the session-only
// PendingReview result that survived DETERMINISTIC validation. Exactly like
// ProfileFieldCandidate: never a DeviceProfile, never a RegisterEntry, never
// persisted, and never applied without the controlled Human review path.
// byteOrder / wordOrder / access are deliberately ABSENT in v1 (Human
// T035-A.1 §4 scope ruling); assembly into a RegisterEntry happens in a
// later slice and MUST run through validateDeviceProfile as the only
// validation owner. Defaults are frozen: scale 1.0, offset 0.0, unit empty.
// ---------------------------------------------------------------------------
struct RegisterCandidateProposal {
    std::uint8_t functionCode{0};
    std::uint16_t address{0};
    std::string name;
    std::string description; // optional on the wire; default empty
    RegisterDecodeType dataType{RegisterDecodeType::UInt16};
    int quantity{0}; // REQUIRED: must equal the dataType word count (1 or 2)
    double scale{1.0};  // optional on the wire; default 1.0
    double offset{0.0}; // optional on the wire; default 0.0
    std::string unit;   // optional on the wire; default empty
    std::string evidenceExcerpt; // REQUIRED non-empty exact excerpt
    // T035-A.3.2.2: OPTIONAL-MEMBER PRESENCE flags. The defaults above are
    // frozen, but the review surface must show HONESTLY whether the manual
    // actually stated an optional member (Human T035-A.3.2.2 §4) — a default
    // value must never be presented as an AI extraction.
    bool hasDescription{false};
    bool hasScale{false};
    bool hasOffset{false};
    bool hasUnit{false};

    bool operator==(const RegisterCandidateProposal&) const = default;
};

struct RegisterEntryCandidate {
    std::uint8_t functionCode{0};
    std::uint16_t address{0};
    std::string name;
    std::string description;
    RegisterDecodeType dataType{RegisterDecodeType::UInt16};
    int quantity{0};
    double scale{1.0};
    double offset{0.0};
    std::string unit;
    bool hasDescription{false};
    bool hasScale{false};
    bool hasOffset{false};
    bool hasUnit{false};
    CandidateEvidence evidence;
    CandidateLifecycleState lifecycle{CandidateLifecycleState::PendingReview};

    bool operator==(const RegisterEntryCandidate&) const = default;
};

// Deterministic refusal taxonomy for one register proposal. A refused
// proposal NEVER becomes a candidate and is never "fixed" (no auto-correct:
// a quantity that disagrees with the dataType word count is a refusal, not
// a repair — Human T035-A.1 §5).
enum class RegisterCandidateValidationCode {
    Ok,
    NameMissing,
    InvalidFunctionCode,  // outside the M10 register space 0x01..0x7F
    UnsupportedDataType,  // enum value outside the M11 matrix (defensive)
    QuantityMismatch,     // quantity != registerDecodeTypeWordCount(dataType)
    NonFiniteScale,       // NaN / +-Inf
    NonFiniteOffset,      // NaN / +-Inf
    EmptyExcerpt,         // evidence missing
    EvidenceNotLocatable, // excerpt absent OR not uniquely locatable (H6)
};

[[nodiscard]] std::string_view registerCandidateValidationCodeName(
    RegisterCandidateValidationCode code);

struct RegisterCandidateExtractionResult {
    // Only fully validated candidates, always PendingReview.
    std::vector<RegisterEntryCandidate> candidates;
    // Deterministic accounting of refused proposals. No provider prose.
    std::size_t refusedProposalCount{0};
};

// The deterministic register vertical slice: validate each provider-neutral
// register proposal against the canonical extracted text and return the valid
// PendingReview candidates. Pure function of its arguments; the provider seam
// is deliberately a plain vector here — the adapter-side strict parser (ui/ai)
// owns the wire, this layer owns truth.
//
// T035-A.3.2.3: the evidence rule is the SHARED deterministic locator declared
// above — EXACT FIRST, then LINE-ENDING-normalized fallback with real raw
// offsets and a mandatory uniqueness check. The stored excerpt stays the
// PROVIDER's exact submission; only its LOCATION is derived locally.
[[nodiscard]] RegisterCandidateExtractionResult extractRegisterCandidates(
    const ManualDocument& document,
    std::string_view canonicalExtractedText,
    const std::vector<RegisterCandidateProposal>& proposals);

// ---------------------------------------------------------------------------
// T035-A.3.2.3 — LINE-ENDING NORMALIZATION (Human-adjudicated Option A, limited
// scope). Deterministic LINE-ENDING EQUIVALENCE ONLY: CRLF -> LF, a lone CR ->
// LF, an LF stays LF. NOTHING else is normalized — no TAB folding, no
// whitespace-run collapsing, no Unicode normalization, no case folding, no
// digit/punctuation rewriting, no fuzzy or similarity matching. Every
// NON-line-ending character must still match CHARACTER FOR CHARACTER.
//
// WHY (measured fact, not an assumption — see the task file §3B-D5-B): the
// canonical extracted text of a real PDF is MIXED — CRLF inside a page (the
// extractor's own page text) and a bare LF at page boundaries (the
// ManualStore::loadText aggregation join). A provider that returns LF where
// the canonical text holds CRLF therefore fails an EXACT raw match even for a
// single page. The real provider evidence format remains UNKNOWN and the
// historical failure root cause remains UNRESOLVED: this helper does NOT claim
// to have fixed any past incident, it only removes ONE deterministic
// typographic mismatch class.
//
// SAFETY PROPERTIES (all mandatory):
//   · EXACT FIRST — the raw exact match is always tried first and its result
//     is unchanged; normalization is a FALLBACK reached only when exact fails.
//   · UNIQUENESS IS NEVER RELAXED — the fallback still requires EXACTLY ONE
//     normalized occurrence. Zero hits => NotFound, two or more => Ambiguous.
//     CRLF -> LF can MERGE two originally distinct raw spans into one
//     normalized span, so the fallback may legitimately be STRICTER, never
//     looser. The first hit is never silently taken.
//   · REAL RAW OFFSETS — the returned span is an index into the ORIGINAL text,
//     derived through a deterministic normalized-index -> raw-offset map.
//     CRLF (two raw characters) maps to one normalized character; the mapping
//     always anchors on the FIRST raw character of a merged run.
//   · NO DOCUMENT IDENTITY CHANGE — this never rewrites the canonical text and
//     never touches documentId / contentHash; it is a pure function over a
//     caller-supplied view.
// ---------------------------------------------------------------------------

// The normalization V1 rule as a pure function of a character (no state): the
// ONLY permitted transformation is line-ending equivalence. Exposed so tests
// and forensics share ONE definition instead of re-implementing the rule.
[[nodiscard]] bool isNormalizedLineEndingEquivalent(char lhs, char rhs);

// Deterministic line-ending normalization of a byte sequence: "\r\n" -> "\n",
// a lone '\r' -> '\n', '\n' -> '\n'; every other byte is copied verbatim. The
// result is the SAME LENGTH or SHORTER (never longer), and the per-character
// rule above is exactly what the mapping below relies on.
[[nodiscard]] std::string normalizeLineEndings(std::string_view raw);

// Why an evidence excerpt could not be located. Distinct from — and never
// merged with — the schema / field-validation refusals.
enum class EvidenceLocateCode {
    Located,   // exactly one match; outStart/outEnd are REAL raw offsets
    NotFound,  // zero matches (raw AND normalized)
    Ambiguous, // two or more matches (raw, or normalized once raw missed)
};

[[nodiscard]] std::string_view evidenceLocateCodeName(EvidenceLocateCode code);

// The ONE deterministic locate rule shared by generation-time register
// validation, Accept-time revalidation and the read-only forensics counter.
// Exact-first; on an exact miss, a line-ending-normalized fallback whose
// uniqueness is checked over the WHOLE normalized view and whose span is
// mapped back to REAL raw offsets. Returns Located with a raw span only when
// the match is unique under this rule.
[[nodiscard]] EvidenceLocateCode locateEvidenceExcerpt(
    std::string_view canonicalText, std::string_view excerpt,
    std::int64_t &outStart, std::int64_t &outEnd);

// ---------------------------------------------------------------------------
// C3 evidence freshness gate (T027 §81, C3-H3). Before a Human Accept (or a
// future edited-confirm) may reach the verified Profile, the STORED evidence
// must be re-validated against the canonical extracted text it references —
// the world may have changed since the Candidate was generated. This is the
// SAME deterministic vocabulary as generation time: the excerpt must still be
// uniquely locatable and the recorded [textStart, textEnd) span must still
// round-trip to the exact excerpt. No provider input is consulted and nothing
// is guessed: any failure is a refusal, never a repair.
//
// T035-A.3.2.3: "round-trip" is now defined over the SAME line-ending rule as
// location — normalize(raw.slice(textStart, textEnd)) must equal
// normalize(stored excerpt). An EXACT byte round-trip remains the primary
// case; the normalized form is accepted only for PURE line-ending differences,
// which is exactly the equivalence this slice is authorized to establish.
// ---------------------------------------------------------------------------
enum class CandidateEvidenceRevalidationCode {
    Ok,
    EmptyExcerpt,     // the stored excerpt is empty
    InvalidRange,     // textStart/textEnd do not describe a usable span
    RoundTripFailed,  // canonical[start, end) != excerpt any more
    NotUnique,        // the excerpt has become ambiguous in the canonical text
};

[[nodiscard]] std::string_view candidateEvidenceRevalidationCodeName(
    CandidateEvidenceRevalidationCode code);

[[nodiscard]] CandidateEvidenceRevalidationCode revalidateCandidateEvidence(
    const CandidateEvidence& evidence,
    std::string_view canonicalExtractedText);

// T035-A.3.2.3 — the REGISTER Accept-time freshness gate. Identical contract to
// revalidateCandidateEvidence EXCEPT that the round-trip and the uniqueness
// check use the SAME line-ending rule as register GENERATION time
// (locateEvidenceExcerpt), so an excerpt that passed extraction can never fail
// revalidation purely because of a CRLF/LF difference. The manufacturer path
// deliberately keeps the exact-only revalidateCandidateEvidence above — the two
// functions exist separately precisely so that widening the register rule
// cannot silently widen the frozen manufacturer contract (Human §5/§10).
[[nodiscard]] CandidateEvidenceRevalidationCode
revalidateRegisterCandidateEvidence(const CandidateEvidence& evidence,
                                    std::string_view canonicalExtractedText);

} // namespace modbuslens::core