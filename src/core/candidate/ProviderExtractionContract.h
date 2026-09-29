#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/candidate/CandidateExtraction.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M12-C C2 SECOND SLICE — provider-neutral EXTRACTION REQUEST / RESULT contract
// (T027 §68, HUMAN-AUTHORIZED). Pure C++20, Zero Qt.
//
// This header carries ONLY provider-NEUTRAL data. There is deliberately no
// endpoint, no model id, no wire envelope, no status code and no transport
// object here: those are provider-specific and must stay inside the adapter
// boundary (src/ui/ai), which is where the ModelScope adapter lives. Anything
// provider-shaped that leaked into this file would be a contract violation.
//
// Canonical basis (T027 §66 H1–H10 + §68):
//   · H1 PROVIDER — provider-neutral. The model id is configuration and never
//     domain truth, so it cannot appear in this contract.
//   · H2 CLOUD — the ONLY document payload allowed outbound is the selected
//     document's canonical extracted text. ExtractionRequest therefore carries
//     nothing else about the document: local-only identifiers (documentId /
//     contentHash / original path) are DELIBERATELY ABSENT so they cannot be
//     serialized to a provider even by accident. Evidence identity is
//     re-attached locally, on SESSION M's side of the seam (H6).
//   · H4 PERSISTENCE — a result is SESSION-ONLY. It holds validated proposals
//     and a failure reason; it never holds the raw provider response.
//   · H8 OUTPUT CONTRACT — failure is a first-class, deterministic outcome.
//     There is no best-effort / partial-success mode.
// ---------------------------------------------------------------------------

// Deterministic failure taxonomy for one extraction attempt. These are
// provider-NEUTRAL: they say what went wrong, never which provider said so.
enum class ProviderExtractionFailure {
    None,
    TransportError,          // the request never produced a provider response
    ProviderRejectedStatus,  // provider returned a non-success result status
    MalformedResponse,       // envelope / payload not parseable at all
    SchemaViolation,         // strict schema violated => fail closed
};

[[nodiscard]] std::string_view providerExtractionFailureToken(
    ProviderExtractionFailure failure);

// ---------------------------------------------------------------------------
// The provider-neutral extraction REQUEST (H2 / §10 payload minimization).
//
// targetFieldToken  — the frozen target identity the provider must address
//                     (e.g. "manufacturer"), never a provider-specific name.
// documentTypeToken — coarse format context ("txt" | "markdown" | "pdf" |
//                     "docx"). Context, NOT identity: it cannot be used to
//                     reconstruct or locate the local document.
// canonicalExtractedText — the one and only document payload permitted to
//                     leave the process.
//
// Forbidden by construction (the fields simply do not exist): original
// PDF/DOCX bytes, the original source path, other ManualDocuments, verified
// DeviceProfile contents, transaction history, raw TX/RX, API secrets,
// candidate history, unrelated application state.
// ---------------------------------------------------------------------------
struct ExtractionRequest {
    std::string targetFieldToken;
    std::string documentTypeToken;
    std::string canonicalExtractedText;
};

// Build the provider-neutral request for the C2 first slice from the selected
// document. It is a pure function of (document, canonical text) and copies
// ONLY the extraction payload (H2): the document's local-only identifiers
// (documentId / contentHash / originalFileName / originalPath), its byte size
// and its extraction bookkeeping are all deliberately NOT carried, so they
// cannot be serialized to a provider even by accident. The target token comes
// from the frozen first-slice field; the type token is coarse format context.
[[nodiscard]] ExtractionRequest buildC2FirstSliceExtractionRequest(
    const ManualDocument &document, std::string_view canonicalExtractedText);

// ---------------------------------------------------------------------------
// The outcome of one strict extraction attempt.
//
// `ok == true` means: a provider response was received, its envelope was
// usable, and its payload satisfied the strict provider schema — so
// `proposals` are schema-valid, provider-neutral proposals. It does NOT mean
// the proposals are TRUE: verifying them is SESSION M's local, deterministic
// Evidence validation, which happens after this contract (H6/H8).
//
// `ok == false` means a deterministic failure with zero proposals. There is no
// raw provider response member: the raw body lives only inside the adapter /
// parser call scope and MUST NOT be retained (H4 / §14).
// ---------------------------------------------------------------------------
struct ProviderExtractionResult {
    bool ok{false};
    ProviderExtractionFailure failure{ProviderExtractionFailure::None};
    std::vector<CandidateProposal> proposals;
};

} // namespace modbuslens::core