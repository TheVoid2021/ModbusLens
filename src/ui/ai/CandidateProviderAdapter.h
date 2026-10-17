#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/candidate/ProviderExtractionContract.h"
#include "ui/ai/AiProviderProfile.h"
#include "ui/ai/ExtractionTransport.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C2 SECOND SLICE — ModelScope candidate-extraction ADAPTER boundary
// (T027 §68, HUMAN-AUTHORIZED).
//
// This file is the ONE place where provider-specific shape is allowed to
// exist: the wire envelope, the response content extraction and the strict
// provider schema. Everything here is deliberately Qt-free and
// NETWORK-FREE: this translation unit links no network type at all, so a
// product path physically cannot issue a live request through it.
//
// Boundaries this header enforces:
//   · The provider-neutral request/result types come from core and carry no
//     provider specifics (see ProviderExtractionContract.h).
//   · The ModelScope wire envelope is modelled EXACTLY as the accepted M6
//     client already parses it (T011 live evidence + docs/08): an OpenAI-style
//     Chat Completions object with a `choices` array whose first usable
//     `message.content` is a string. No new envelope is invented here.
//   · Raw provider text is transient: it lives only in the parser call scope
//     and never reaches CandidateProposal / CandidateEvidence (H4).
// ---------------------------------------------------------------------------

// The outcome of pulling usable content out of a provider response envelope.
struct ProviderContentExtraction {
    bool ok{false};
    // "choices" present + array + a non-empty string message.content. The M6
    // contract deliberately consumes ONLY message.content (never
    // reasoning_content), and so does this adapter.
    std::string content;
};

// Extract the assistant content from a ModelScope-shaped Chat Completions
// response. Purely structural: it does NOT validate the extraction schema —
// that is the strict parser's job below. Returns ok=false (zero content) for
// anything that is not a usable envelope.
[[nodiscard]] ProviderContentExtraction extractChatCompletionsResponseContent(
    std::string_view responseBody);

// ---------------------------------------------------------------------------
// STRICT provider payload parser (H8, §12).
//
// The expected content is a JSON object with EXACTLY these members and no
// others:
//
//   { "proposals": [ { "target": "<frozen token>",
//                      "value": "<non-empty string>",
//                      "evidence": "<non-empty exact excerpt>" } ] }
//
// Fail-closed rules (each yields ok=false with ZERO proposals — never a
// partial mixture, §13 provider-response atomicity):
//   · malformed JSON / wrong top-level type / missing required member
//   · required member of the wrong JSON type  (no coercion: a number is never
//     silently read as a string)
//   · empty required string where empty is invalid
//   · unknown / unexpected member ANYWHERE in the tree (strict, not permissive)
//   · unknown target token (the provider cannot widen the frozen contract)
//   · any provider-reported numeric confidence / score / probability member
//     (H5: such a value must never reach a Candidate, so its PRESENCE is a
//     schema violation rather than something to quietly drop)
//
// An empty `proposals` array is legal and means "the source supports nothing".
// ---------------------------------------------------------------------------
[[nodiscard]] core::ProviderExtractionResult parseStrictCandidateProposals(
    std::string_view providerContent);

// ---------------------------------------------------------------------------
// T035-A.1 — STRICT register-candidate payload parser (ADR-004 / Human
// T035-A.1 §5-§7). Same fail-closed discipline as the profile-field parser:
//
//   { "registerProposals": [ { "functionCode": <int 1..127>,
//                              "address":      <int 0..65535>,
//                              "name":         "<non-empty string>",
//                              "dataType":     "<frozen M11 token>",
//                              "quantity":     <int == type word count>,
//                              "evidence":     "<non-empty exact excerpt>",
//                              // optional members (frozen defaults):
//                              "description": "<string>",
//                              "scale": <number>,   // default 1.0
//                              "offset": <number>,  // default 0.0
//                              "unit": "<string>" } ] }  // default ""
//
// Fail-closed (whole response, ZERO proposals — never a partial mixture):
//   · malformed JSON / wrong top-level type
//   · ANY unexpected member at any level — in particular a response carrying
//     BOTH "proposals" and "registerProposals" is a violation: the two
//     contracts never mix and are never routed implicitly (§6)
//   · missing required member / wrong JSON type (numbers are never strings)
//   · non-integral or out-of-range functionCode / address / quantity
//   · unknown dataType token (outside the frozen M11 profile token set)
//   · quantity != the dataType word count (NO auto-correction, §5)
//   · non-finite scale / offset (an overflowing literal such as 1e999 is
//     out of range for from_chars and is refused, never clamped)
//   · empty "name" / empty "evidence"
// An empty "registerProposals" array is legal and means "no registers found".
// This slice is the PARSER FOUNDATION only: wiring it into extract() /
// prompt building / orchestration belongs to the next slice, so the
// manufacturer path is untouched by construction.
// ---------------------------------------------------------------------------
struct RegisterProviderExtractionResult {
    bool ok{false};
    core::ProviderExtractionFailure failure{
        core::ProviderExtractionFailure::None};
    std::vector<core::RegisterCandidateProposal> proposals;
};

[[nodiscard]] RegisterProviderExtractionResult
parseStrictRegisterCandidateProposals(std::string_view providerContent);

// Provider-side configuration. The model id is CONFIGURATION, never domain
// truth (H1), which is exactly why it lives here and not in a Candidate. The
// provider kind routes the PROFILE-DECIDED request extras (T034-B1): the
// adapter never invents provider behavior beyond what the frozen profile
// table authorizes.
struct CandidateAdapterConfig {
    AiProviderKind kind{AiProviderKind::ModelScope};
    std::string modelId;
    std::size_t maxOutputTokens{768};
};

// ---------------------------------------------------------------------------
// The provider-neutral candidate-extraction ADAPTER (OpenAI-compatible Chat
// Completions; T034 B1 — was the ModelScope-only adapter).
//
// It consumes a provider-NEUTRAL request, serializes the wire body internally
// (QJsonDocument — authoritative JSON escaping for runtime model/prompt
// strings; the provider extras are decided by the COMPILED profile table),
// drives an injected transport, extracts the response content and runs the
// strict parser — then hands back provider-neutral proposals. Everything
// provider-shaped lives inside this class; callers only ever see core types.
//
// It also implements SESSION M's ICandidateProposalProvider, so the adapter
// plugs straight into the already-accepted local Evidence validation path:
// `extractProfileFieldCandidates(document, text, adapter)` consumes its
// proposals exactly like the frozen test double does.
//
// The adapter NEVER performs a network request itself: all transport work is
// delegated to the injected IExtractionTransport. It holds no credential, and
// it retains no raw provider response (H4). Qt Core (JSON) is used for body
// serialization; this translation unit still links NO network type at all.
// ---------------------------------------------------------------------------
class CandidateProviderAdapter : public core::ICandidateProposalProvider
{
public:
    CandidateProviderAdapter(IExtractionTransport &transport,
                               CandidateAdapterConfig config);

    // The provider-neutral entry point: request in, proposals-or-failure out.
    // On any failure the proposal list is EMPTY (fail-closed, §13) and
    // lastFailure() carries the deterministic reason.
    [[nodiscard]] core::ProviderExtractionResult extract(
        const core::ExtractionRequest &request);

    // T035-A.2 — the REGISTER-MAP entry point (ADR-004 / Human T035-A.2 §3-§5).
    // Same transport/envelope/atomicity discipline as extract(), but the
    // prompt branch and the STRICT parser are the register contract
    // ({"registerProposals":[...]} — parseStrictRegisterCandidateProposals).
    // The request MUST carry the frozen kRegisterMapTargetToken; a wrong-mode
    // request is refused closed (SchemaViolation) instead of being silently
    // parsed against the wrong contract. extract() itself is UNTOUCHED: the
    // manufacturer path keeps its exact prompt, parser and failure behavior.
    [[nodiscard]] RegisterProviderExtractionResult extractRegister(
        const core::ExtractionRequest &request);

    // The only provider-specific artifact this class exposes, and only so a
    // test can prove outbound payload MINIMIZATION (H2, §10/N15): it builds
    // the wire body WITHOUT sending it.
    [[nodiscard]] std::string buildWireBodyForTest(
        const core::ExtractionRequest &request) const;

    // SESSION M seam (H1/H7): builds the neutral request for the frozen first
    // slice, runs the extraction and returns the schema-valid neutral
    // proposals (empty on failure). NO live request is possible here because
    // the transport is injected.
    [[nodiscard]] std::vector<core::CandidateProposal> propose(
        const core::ManualDocument &document,
        std::string_view canonicalExtractedText) override;

    [[nodiscard]] core::ProviderExtractionFailure lastFailure() const
    {
        return lastFailure_;
    }

private:
    IExtractionTransport &transport_;
    CandidateAdapterConfig config_;
    core::ProviderExtractionFailure lastFailure_{
        core::ProviderExtractionFailure::None};
};

} // namespace modbuslens::ui