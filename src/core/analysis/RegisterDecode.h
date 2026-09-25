#pragma once

#include <cstdint>
#include <string>
#include <string_view>

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
//   · word order ACROSS registers (2-register types, later slice).
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
};

enum class RegisterByteOrder {
    Normal = 0,      // protocol big-endian passthrough (default)
    ByteSwapped = 1, // high/low bytes exchanged within the register
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

} // namespace modbuslens::core