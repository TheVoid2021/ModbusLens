#include "core/manualqa/ManualQaContract.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_set>

namespace modbuslens::core {
namespace {

// Deterministic bounds for the first-slice bounded-context strategy
// (§100.6: engineering details, defensive only — they never redefine an
// approved product semantic; every ordinary imported Manual stays usable).
constexpr std::size_t kContextChunkTargetChars = 700;
constexpr std::size_t kContextMaxBlocks = 6;
constexpr std::size_t kContextMinQuestionTokens = 1;

std::string toLowerAscii(std::string_view text)
{
    std::string lowered;
    lowered.reserve(text.size());
    for (const char c : text) {
        lowered.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return lowered;
}

bool isWordChar(unsigned char c)
{
    return std::isalnum(c) != 0 || c == '_' || static_cast<unsigned char>(c)
        >= 0x80; // keep multi-byte UTF-8 sequences inside one "word"
}

std::unordered_set<std::string> tokenize(std::string_view text)
{
    std::unordered_set<std::string> tokens;
    std::string current;
    const std::string lowered = toLowerAscii(text);
    for (const char c : lowered) {
        if (isWordChar(static_cast<unsigned char>(c))) {
            current.push_back(c);
        } else if (!current.empty()) {
            tokens.insert(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        tokens.insert(std::move(current));
    }
    return tokens;
}

} // namespace

std::string_view manualQaStatusToken(ManualQaStatus status)
{
    switch (status) {
    case ManualQaStatus::Found:
        return "found";
    case ManualQaStatus::NotFound:
        return "not_found";
    case ManualQaStatus::InsufficientEvidence:
        return "insufficient_evidence";
    }
    return "not_found";
}

std::string_view manualQaCitationCodeName(ManualQaCitationCode code)
{
    switch (code) {
    case ManualQaCitationCode::Ok:
        return "ok";
    case ManualQaCitationCode::DocumentMismatch:
        return "qa_citation_document_mismatch";
    case ManualQaCitationCode::ContentHashMismatch:
        return "qa_citation_content_hash_mismatch";
    case ManualQaCitationCode::InvalidRange:
        return "qa_citation_invalid_range";
    case ManualQaCitationCode::RoundTripFailed:
        return "qa_citation_round_trip_failed";
    }
    return "qa_citation_invalid_range";
}

ManualQaCitationCode validateManualQaCitation(
    const ManualQaCitation& citation, std::string_view selectedDocumentId,
    std::string_view selectedContentHash, std::string_view canonicalText)
{
    // D2 (A): the citation must belong to the selected Manual context.
    if (citation.documentId != selectedDocumentId) {
        return ManualQaCitationCode::DocumentMismatch;
    }
    // D2 (B): contentHash freshness — the identity recorded at ask time must
    // still be the identity of the CURRENT managed Manual content.
    if (citation.contentHash != selectedContentHash) {
        return ManualQaCitationCode::ContentHashMismatch;
    }
    // D2 (C): the range must be valid and in bounds.
    const std::int64_t size = static_cast<std::int64_t>(canonicalText.size());
    if (citation.textStart < 0 || citation.textEnd <= citation.textStart
        || citation.textEnd > size) {
        return ManualQaCitationCode::InvalidRange;
    }
    // D2 (D): exact range → excerpt round-trip. Deliberately NO global
    // uniqueness check (§100.2, corrected Human-approved plan): repeated text
    // elsewhere in the Manual does not invalidate an otherwise exact
    // citation.
    const std::string_view span = canonicalText.substr(
        static_cast<std::size_t>(citation.textStart),
        static_cast<std::size_t>(citation.textEnd - citation.textStart));
    if (span != citation.excerpt) {
        return ManualQaCitationCode::RoundTripFailed;
    }
    return ManualQaCitationCode::Ok;
}

ManualQaFoundValidation validateManualQaFoundResult(
    const ManualQaParsedResult& result, std::string_view selectedDocumentId,
    std::string_view selectedContentHash, std::string_view canonicalText)
{
    ManualQaFoundValidation validation;
    // D3: FOUND requires a non-empty answer and at least one citation.
    if (result.answer.find_first_not_of(" \t\r\n") == std::string::npos) {
        validation.citationCode = ManualQaCitationCode::InvalidRange;
        validation.failingCitationIndex = 0;
        return validation;
    }
    if (result.citations.empty()) {
        validation.citationCode = ManualQaCitationCode::InvalidRange;
        validation.failingCitationIndex = 0;
        return validation;
    }
    for (std::size_t i = 0; i < result.citations.size(); ++i) {
        // D2/D3: EVERY displayed citation must be locally valid; the first
        // failing one turns the whole result into ERROR (never
        // INSUFFICIENT_EVIDENCE).
        const ManualQaCitationCode code = validateManualQaCitation(
            result.citations[i], selectedDocumentId, selectedContentHash,
            canonicalText);
        if (code != ManualQaCitationCode::Ok) {
            validation.citationCode = code;
            validation.failingCitationIndex = i;
            return validation;
        }
    }
    validation.ok = true;
    validation.citationCode = ManualQaCitationCode::Ok;
    return validation;
}

std::vector<ManualQaContextBlock> buildManualQaContextBlocks(
    std::string_view canonicalText, std::string_view question)
{
    // Deterministic bounded retrieval: cut the canonical text into
    // line-anchored chunks of ~kContextChunkTargetChars, rank them by
    // question-token overlap, keep the top kContextMaxBlocks in DOCUMENT
    // ORDER. No network, no embedding, selected-Manual-only (D1).
    std::vector<ManualQaContextBlock> chunks;
    const std::int64_t total = static_cast<std::int64_t>(canonicalText.size());
    std::int64_t cursor = 0;
    while (cursor < total) {
        std::int64_t end = cursor;
        std::int64_t lastBreak = -1;
        while (end < total
               && end - cursor
                   < static_cast<std::int64_t>(kContextChunkTargetChars)) {
            if (canonicalText[static_cast<std::size_t>(end)] == '\n') {
                lastBreak = end + 1;
            }
            ++end;
        }
        if (end < total && lastBreak > cursor) {
            end = lastBreak;
        } else if (end < total) {
            // Hard chunk boundary inside a long line; still a valid span.
            end = cursor + static_cast<std::int64_t>(kContextChunkTargetChars);
        }
        ManualQaContextBlock block;
        block.start = cursor;
        block.end = end;
        block.text = std::string(
            canonicalText.substr(static_cast<std::size_t>(cursor),
                                 static_cast<std::size_t>(end - cursor)));
        chunks.push_back(std::move(block));
        cursor = end;
    }

    const auto questionTokens = tokenize(question);
    if (questionTokens.size() < kContextMinQuestionTokens) {
        // Degenerate question: return the first bounded blocks unchanged so
        // the provider still sees bounded selected-Manual context.
        if (chunks.size() > kContextMaxBlocks) {
            chunks.resize(kContextMaxBlocks);
        }
        return chunks;
    }

    std::vector<std::pair<std::size_t, std::size_t>> ranked;
    ranked.reserve(chunks.size());
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto chunkTokens = tokenize(chunks[i].text);
        std::size_t overlap = 0;
        for (const auto& token : questionTokens) {
            if (chunkTokens.count(token) != 0) {
                ++overlap;
            }
        }
        ranked.emplace_back(overlap, i);
    }
    std::sort(ranked.begin(), ranked.end(),
              [](const auto& a, const auto& b) {
                  if (a.first != b.first) {
                      return a.first > b.first; // higher overlap first
                  }
                  return a.second < b.second; // then document order
              });
    if (ranked.size() > kContextMaxBlocks) {
        ranked.resize(kContextMaxBlocks);
    }
    std::vector<std::size_t> indices;
    indices.reserve(ranked.size());
    for (const auto& entry : ranked) {
        indices.push_back(entry.second);
    }
    std::sort(indices.begin(), indices.end());

    std::vector<ManualQaContextBlock> selected;
    selected.reserve(indices.size());
    for (const std::size_t i : indices) {
        selected.push_back(std::move(chunks[i]));
    }
    return selected;
}

} // namespace modbuslens::core
