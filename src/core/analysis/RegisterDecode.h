#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M11 first slice: register decode views (Pure C++20, Zero Qt).
//
// This layer answers exactly ONE question: "how should an ALREADY-CANONICAL
// raw uint16 register word be presented, given the user's decode
// configuration?" It never sees RTU bytes, never parses a CRC or a byteCount,
// and never judges a Modbus transaction — the wire parser/session and the
// analyzers own those. Its output is a DERIVED PRESENTATION result; the raw
// word itself stays untouched in the canonical owner
// (TransactionAnalysis.values), so a decode can never rewrite wire truth.
//
// Two ordering axes, per canonical M11 definition (11_V2_UPGRADE_PLAN §M11:
// "…（含 byte/word order）"), each with an explicit default:
//   · byte order WITHIN a 16-bit register: Normal (protocol big-endian
//     passthrough, default) or ByteSwapped (high/low bytes exchanged — for
//     this decode view only, never for the raw evidence);
//   · word order ACROSS registers (2-register types): HighWordFirst
//     (big-endian AB CD, default) or LowWordFirst (little-endian CD AB).
//
// DecodeStatus is the T024 §9 contract enum: Ok plus four decode/configuration
// failure states. It is ORTHOGONAL to TransactionStatus — a decode failure can
// never rewrite a wire result (Success stays Success).
// ---------------------------------------------------------------------------

enum class RegisterDecodeType {
    Hex = 0,    // derived formatting view (distinct from the RAW HEX column)
    Binary = 1, // derived formatting view, fixed 16-bit width
    UInt16 = 2, // unsigned (the default view — closest to the raw truth)
    Int16 = 3,  // 16-bit two's-complement
    UInt32 = 4, // 2-register unsigned 32-bit (word order applies)
    Int32 = 5,  // 2-register 32-bit two's-complement (word order applies)
    Float32 = 6, // 2-register IEEE-754 binary32 reinterpretation
};

enum class RegisterByteOrder {
    Normal = 0,      // protocol big-endian passthrough (default)
    ByteSwapped = 1, // high/low bytes exchanged within the register
};

enum class RegisterWordOrder {
    HighWordFirst = 0, // big-endian words: words[0] is the high 16 bits (AB CD)
    LowWordFirst = 1,  // little-endian words: words[1] is the high 16 bits (CD AB)
};

enum class RegisterDecodeStatus {
    Ok,                   // the decode view was produced
    InsufficientWords,    // (multi-register types) not enough words supplied
    OutOfRangeSelection,  // (multi-register types) selection outside the data
    InvalidConfiguration, // (multi-register types) width/count inconsistent
    UnsupportedType,      // the requested type is outside the v1 matrix
};

// Stable machine tokens for the adapter layer ("ok", "insufficient_words",
// …) — never human UI prose.
[[nodiscard]] std::string_view registerDecodeStatusName(RegisterDecodeStatus status);

// The effective word for a byte-order view: Normal returns the raw word
// unchanged; ByteSwapped exchanges its high/low bytes. The canonical raw word
// itself is NEVER mutated by this layer.
[[nodiscard]] std::uint16_t decodeEffectiveWord(std::uint16_t rawWord,
                                                RegisterByteOrder byteOrder);

struct RegisterDecodedWord {
    RegisterDecodeStatus status{RegisterDecodeStatus::Ok};
    std::string text; // the DERIVED presentation value (empty when not Ok)

    bool operator==(const RegisterDecodedWord&) const = default;
};

// Decode ONE canonical raw uint16 word into the requested derived view.
// Every valid (type, byteOrder) combination in the first-slice matrix decodes
// successfully for every input word: a 16-bit view has no insufficient-words
// or out-of-range failure mode. Unknown enum values (a caller bug or a future
// type reaching this layer) deterministically yield UnsupportedType.
[[nodiscard]] RegisterDecodedWord decodeRegisterWord(
    std::uint16_t rawWord, RegisterDecodeType type, RegisterByteOrder byteOrder);

// How many canonical uint16 words the type consumes: 1 for the 16-bit views,
// 2 for the 32-bit views; 0 for a value outside the v1 matrix (defensive).
[[nodiscard]] int registerDecodeTypeWordCount(RegisterDecodeType type);

// Stable machine tokens for the adapter layer ("high_word_first" /
// "low_word_first") — never human UI prose.
[[nodiscard]] std::string_view registerWordOrderName(RegisterWordOrder wordOrder);

struct RegisterDecodedPair {
    RegisterDecodeStatus status{RegisterDecodeStatus::Ok};
    std::string text; // the DERIVED presentation value (empty when not Ok)

    bool operator==(const RegisterDecodedPair&) const = default;
};

// Decode TWO canonical raw uint16 words into a 32-bit derived view. `word0`
// is the word at the lower PDU address, `word1` the next one. Frozen pipeline
// (T024 §7): byte swap within each register → word-order combination →
// target-type reinterpretation. HighWordFirst takes word0 as the high 16
// bits (AB CD); LowWordFirst takes word1 (CD AB). NaN / ±Inf are legal
// IEEE-754 results (status Ok), never decode failures. A type outside the
// 2-register set (including the 16-bit views) yields UnsupportedType — the
// pair entry is not the 16-bit entry.
[[nodiscard]] RegisterDecodedPair decodeRegisterPair(
    std::uint16_t word0, std::uint16_t word1, RegisterDecodeType type,
    RegisterByteOrder byteOrder, RegisterWordOrder wordOrder);

struct RegisterDecodedView {
    RegisterDecodeStatus status{RegisterDecodeStatus::Ok};
    std::string text; // the DERIVED presentation value (empty when not Ok)
    int start{};      // echo of the requested start index
    int wordCount{};  // words the view consumes (1 or 2; 0 = invalid config)

    bool operator==(const RegisterDecodedView&) const = default;
};

// The single decode entry over a canonical word sequence (T024 §12 shape:
// decodeRegisterView(words, type, wordOrder, start) -> result). `start` is
// the 0-based index of the first word to decode. 1-register types decode
// words[start]; 2-register types decode words[start] and words[start+1] —
// a sliding window where EVERY word is a legal start, so the decode column
// stays per-register aligned with the raw rows (T024 §10) and the last
// register alone reports InsufficientWords (T024 §9: 末寄存器取 UInt32).
// Failure mapping (T024 §9): start < 0 or a type outside the matrix →
// InvalidConfiguration; start >= size → OutOfRangeSelection; a start inside
// the data that leaves fewer than the needed words → InsufficientWords.
[[nodiscard]] RegisterDecodedView decodeRegisterView(
    const std::vector<std::uint16_t>& words, int start, RegisterDecodeType type,
    RegisterByteOrder byteOrder, RegisterWordOrder wordOrder);

// ---------------------------------------------------------------------------
// M12-B slice 4 (T027 §43): NON-BREAKING numeric projection of the SAME
// frozen pipeline decodeRegisterView runs. The M12 semantic formula needs
// the decoded NUMERIC scalar (decodedValue * scale + offset), while the view
// API above exposes only presentation text; parsing that text back would be
// string inference (forbidden for special values). This accessor runs the
// IDENTICAL frozen steps (effective word / word-order combination /
// target-type reinterpretation — the exact code paths decodeRegisterWord and
// decodeRegisterPair execute) and returns the numeric value instead of the
// formatted string. No existing function, status mapping, or presentation
// text is touched: ZERO CHANGE for M11 consumers.
//   Hex / Binary / UInt16 -> the effective word itself (the very number the
//                           hex/binary/decimal text formats)
//   Int16                 -> 16-bit two's complement of the effective word
//   UInt32 / Int32        -> the combined 32-bit bits / its signed reading
//   Float32               -> the IEEE-754 bit reinterpretation
// Status mapping matches decodeRegisterView exactly (including
// InsufficientWords for the last register of a 2-register type).
// ---------------------------------------------------------------------------
struct RegisterDecodedNumeric {
    RegisterDecodeStatus status{RegisterDecodeStatus::Ok};
    double value{0.0};
    int wordCount{0};
    bool operator==(const RegisterDecodedNumeric&) const = default;
};

[[nodiscard]] RegisterDecodedNumeric decodeRegisterNumeric(
    const std::vector<std::uint16_t>& words, int start, RegisterDecodeType type,
    RegisterByteOrder byteOrder, RegisterWordOrder wordOrder);

} // namespace modbuslens::core