#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace modbuslens::core {

// One recorded transaction from a historical .mlog log: the elapsed time, the
// raw request wire bytes, and the raw response wire bytes if any bytes were
// received. nullopt responseWire means explicitly "no response was observed"
// (NO_RESPONSE in the text format) — NOT "an empty response arrived": those
// are different facts, same distinction as T006 DeliveredWire/DroppedResponse.
struct ReplayTransactionRecord {
    std::chrono::milliseconds elapsed{0};
    std::vector<std::uint8_t> requestWire;
    std::optional<std::vector<std::uint8_t>> responseWire;

    bool operator==(const ReplayTransactionRecord&) const = default;
};

// A whole parsed .mlog v1 file: one timeout threshold shared by every
// transaction (a file-level, not per-record, property in v1) and the records.
struct ReplayLog {
    std::chrono::milliseconds timeoutThreshold{1000};
    std::vector<ReplayTransactionRecord> transactions;

    bool operator==(const ReplayLog&) const = default;
};

// Text-syntax-only failure classes. No CRC / frame-length / function-code
// knowledge here — those belong to the wire codec and the analyzer.
enum class ReplayParseErrorCode {
    MissingHeader,       // no header before the first non-comment content
    UnsupportedVersion,  // header present, version != 1
    InvalidHeader,       // header present but fields do not parse (timeout etc.)
    InvalidRecord,       // record line does not have exactly 4 '|' fields / not TXN
    InvalidElapsed,      // elapsed field not a non-negative integer
    InvalidHex,          // a wire field contains bad hex tokens
    MissingRequest,      // request field empty after trimming
    InvalidResponseField // response field empty after trimming
};

struct ReplayParseError {
    ReplayParseErrorCode code{};
    // 1-based physical line number in the source text, blanks and comments
    // included. 0 means "no specific offending line" (e.g. an entirely
    // blank/comment-only input has no header at all).
    std::size_t lineNumber{};

    bool operator==(const ReplayParseError&) const = default;
};

using ReplayParseResult = std::variant<ReplayLog, ReplayParseError>;

// Text -> structured ReplayLog. Pure function over the caller's text: nothing
// is stored by reference, so the returned value stays fully valid after
// `text` is destroyed or mutated. FRAME/CRC/0x03 validation is NOT performed
// here (see docs/tasks/T009: Text Syntax -> Wire Codec -> Transaction
// Analysis layering).
ReplayParseResult parseReplayLog(std::string_view text);

} // namespace modbuslens::core