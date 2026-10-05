#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M12-D FIRST SLICE (T027 §100, HUMAN-FROZEN D1–D5): the Manual Q&A domain
// contract. Pure C++20, zero Qt — the same layering as the M12-C Candidate
// domain (core/candidate), so tests and every future surface share one
// vocabulary.
//
// Frozen semantics this layer encodes (§100):
//   · D2: a citation reuses the manual evidence IDENTITY SHAPE (documentId /
//     contentHash / pageNumber / textStart / textEnd / excerpt) without
//     importing the M12-C Candidate-provenance semantics. Local deterministic
//     validation = document ownership + contentHash freshness + range
//     validity + exact range→excerpt round-trip. NO global uniqueness check
//     (the corrected Human-approved plan: repeated text elsewhere in the
//     Manual does NOT invalidate an otherwise exact citation).
//   · D3: user-visible semantic states are FOUND / NOT_FOUND /
//     INSUFFICIENT_EVIDENCE; ERROR is a separate LOCAL/system state and must
//     never be reported as one of the three semantic states. FOUND requires a
//     non-empty answer and at least one locally valid citation; a provider
//     declaration alone never authorizes FOUND.
//   · D5: everything here is session-only; nothing in this layer writes to
//     any store.
//
// The exact provider JSON syntax is an engineering detail (§100.6): the
// parser below accepts the strict internal shape and is fail-closed — any
// deviation yields a parse error, never a coerced semantic state.
// ---------------------------------------------------------------------------

// User-visible semantic states (D3). Never overloaded with technical failure.
enum class ManualQaStatus {
    Found,
    NotFound,
    InsufficientEvidence,
};

[[nodiscard]] std::string_view manualQaStatusToken(ManualQaStatus status);

// One citation: the manual evidence identity shape (D2).
struct ManualQaCitation {
    std::string documentId;
    std::string contentHash;
    std::int64_t pageNumber{-1};
    std::int64_t textStart{-1};
    std::int64_t textEnd{-1};
    std::string excerpt;

    bool operator==(const ManualQaCitation&) const = default;
};

// Strict internal provider result representation (§100.6): whatever the wire
// syntax looks like, the parsed outcome must carry exactly this. The wire
// JSON parser lives in the runner/adapter layer (same layering as the M12-C
// adapter, which keeps core Qt-free and network-free) and must be strict and
// fail-closed: any malformed output is surfaced as the local ERROR state and
// is never coerced into one of the three semantic states.
struct ManualQaParsedResult {
    ManualQaStatus status{ManualQaStatus::NotFound};
    std::string answer;
    std::vector<ManualQaCitation> citations;
};

// Local deterministic citation validation codes (D2 A–D).
enum class ManualQaCitationCode {
    Ok,
    DocumentMismatch,    // citation does not belong to the selected Manual
    ContentHashMismatch, // citation bound to different/current content
    InvalidRange,        // start/end out of bounds or inverted
    RoundTripFailed,     // range no longer yields the exact excerpt
};

[[nodiscard]] std::string_view manualQaCitationCodeName(
    ManualQaCitationCode code);

// One bounded context block handed to the provider (first-slice deterministic
// retrieval within the selected Manual — §100.6: retrieval strategy is an
// engineering detail; scope is frozen to the selected Manual only).
struct ManualQaContextBlock {
    std::int64_t start{0}; // character offset into the canonical text
    std::int64_t end{0};
    std::string text;
};

// A question targeted at ONE selected Manual (D1). The canonical text is the
// selected Manual's managed cached text; identity fields are the selected
// document's identity at ASK time and are re-checked at completion time.
struct ManualQaRequest {
    std::string question;
    std::string documentId;
    std::string contentHash;
    std::vector<ManualQaContextBlock> blocks;
};

// Validate ONE citation against the selected Manual identity and its CURRENT
// canonical text (D2 A–D, no global uniqueness). `selectedContentHash` is the
// identity recorded at ask time; freshness is enforced because the canonical
// text must be loaded again at completion time under that hash.
[[nodiscard]] ManualQaCitationCode validateManualQaCitation(
    const ManualQaCitation& citation, std::string_view selectedDocumentId,
    std::string_view selectedContentHash, std::string_view canonicalText);

// Deterministic bounded context construction (first-slice retrieval): split
// the canonical text into chunk-sized blocks on line boundaries and rank them
// by case-insensitive token overlap with the question. Fully deterministic,
// no network, no embedding, selected-Manual-only. The number of blocks is
// bounded so the provider payload stays bounded.
[[nodiscard]] std::vector<ManualQaContextBlock> buildManualQaContextBlocks(
    std::string_view canonicalText, std::string_view question);

// Validate a FOUND result end-to-end (D2/D3): non-empty answer + at least one
// citation + every displayed citation locally valid. Returns the FIRST
// failing citation code when any displayed citation is invalid (which makes
// the result ERROR — never INSUFFICIENT_EVIDENCE). NotFound /
// InsufficientEvidence results carry no citation obligations.
struct ManualQaFoundValidation {
    bool ok{false};
    // First failure detail (Ok when the whole result is valid).
    ManualQaCitationCode citationCode{ManualQaCitationCode::Ok};
    std::size_t failingCitationIndex{0};
};

[[nodiscard]] ManualQaFoundValidation validateManualQaFoundResult(
    const ManualQaParsedResult& result, std::string_view selectedDocumentId,
    std::string_view selectedContentHash, std::string_view canonicalText);

} // namespace modbuslens::core
