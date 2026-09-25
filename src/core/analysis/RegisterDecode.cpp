#include "core/analysis/RegisterDecode.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace modbuslens::core {

namespace {

// Fixed 16-bit binary rendering ("0b" + 16 characters, MSB first). The repo
// has no pre-existing binary convention, so the T024 §J4 default applies:
// fixed 16-bit width.
std::string toBinary16(std::uint16_t word)
{
    std::string text = "0b";
    for (int bit = 15; bit >= 0; --bit) {
        text.push_back(((word >> bit) & 0x01) != 0 ? '1' : '0');
    }
    return text;
}

// Same uppercase 4-digit-wide convention the controller's RAW HEX column uses
// (readResultHex16), so the derived Hex view and the raw column stay visually
// comparable without a second style.
std::string toHex16(std::uint16_t word)
{
    char buffer[7];
    std::snprintf(buffer, sizeof(buffer), "0x%04X", static_cast<unsigned>(word));
    return std::string(buffer);
}

// IEEE-754 binary32 presentation (T024 §22 C3, frozen). NaN / ±Inf are legal
// decode results (status Ok), rendered with the frozen Chinese UI text; the
// minus in −Inf is U+2212 exactly as frozen (explicit UTF-8 escape). Finite
// values use std::to_chars — locale-independent by specification, shortest
// round-trip — with ".0" appended to integral-looking results so whole
// numbers still read as floating-point (acceptance A11-A13: "1.0"/"0.0"/
// "-2.0").
std::string formatFloat32(float value)
{
    if (std::isnan(value)) {
        return "非数字（NaN）";
    }
    if (std::isinf(value)) {
        return value > 0.0f ? "正无穷大（+Inf）" : "负无穷大（\xe2\x88\x92Inf）";
    }
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    std::string text(buffer, result.ptr);
    if (text.find('.') == std::string::npos
        && text.find('e') == std::string::npos
        && text.find('E') == std::string::npos) {
        text += ".0";
    }
    return text;
}

// 32-bit two's complement without an implementation-defined unsigned→signed
// conversion (same discipline as the Int16 branch): bits below 0x80000000 map
// to themselves; the upper half maps to (bits − 0x80000000) − 0x40000000 −
// 0x40000000 = bits − 2³².
std::int32_t toInt32(std::uint32_t bits)
{
    if (bits >= 0x80000000u) {
        return static_cast<std::int32_t>(bits - 0x80000000u) - 0x40000000
               - 0x40000000;
    }
    return static_cast<std::int32_t>(bits);
}

} // namespace

std::string_view registerDecodeStatusName(RegisterDecodeStatus status)
{
    switch (status) {
    case RegisterDecodeStatus::Ok:
        return "ok";
    case RegisterDecodeStatus::InsufficientWords:
        return "insufficient_words";
    case RegisterDecodeStatus::OutOfRangeSelection:
        return "out_of_range_selection";
    case RegisterDecodeStatus::InvalidConfiguration:
        return "invalid_configuration";
    case RegisterDecodeStatus::UnsupportedType:
        return "unsupported_type";
    }
    // Defensive: an unknown enum value gets the generic fallback token rather
    // than a fabricated meaning (same discipline as TransactionIssueCode).
    return "unsupported_type";
}

std::uint16_t decodeEffectiveWord(std::uint16_t rawWord,
                                  RegisterByteOrder byteOrder)
{
    if (byteOrder == RegisterByteOrder::ByteSwapped) {
        return static_cast<std::uint16_t>(((rawWord & 0xFF) << 8)
                                          | (rawWord >> 8));
    }
    return rawWord;
}

RegisterDecodedWord decodeRegisterWord(std::uint16_t rawWord,
                                       RegisterDecodeType type,
                                       RegisterByteOrder byteOrder)
{
    const std::uint16_t effective = decodeEffectiveWord(rawWord, byteOrder);
    switch (type) {
    case RegisterDecodeType::Hex:
        return {RegisterDecodeStatus::Ok, toHex16(effective)};
    case RegisterDecodeType::Binary:
        return {RegisterDecodeStatus::Ok, toBinary16(effective)};
    case RegisterDecodeType::UInt16:
        return {RegisterDecodeStatus::Ok,
                std::to_string(static_cast<unsigned>(effective))};
    case RegisterDecodeType::Int16: {
        // 16-bit two's complement, derived without an implementation-defined
        // signed conversion: subtract 0x10000 from values at or above 0x8000.
        int value = static_cast<int>(effective);
        if (value >= 0x8000) {
            value -= 0x10000;
        }
        return {RegisterDecodeStatus::Ok, std::to_string(value)};
    }
    }
    // Outside the first-slice matrix (a caller bug or a future type): the
    // honest derived result is UnsupportedType with no text, never a guess.
    return {RegisterDecodeStatus::UnsupportedType, std::string{}};
}

int registerDecodeTypeWordCount(RegisterDecodeType type)
{
    switch (type) {
    case RegisterDecodeType::Hex:
    case RegisterDecodeType::Binary:
    case RegisterDecodeType::UInt16:
    case RegisterDecodeType::Int16:
        return 1;
    case RegisterDecodeType::UInt32:
    case RegisterDecodeType::Int32:
    case RegisterDecodeType::Float32:
        return 2;
    }
    // A value outside the v1 matrix consumes nothing (defensive).
    return 0;
}

std::string_view registerWordOrderName(RegisterWordOrder wordOrder)
{
    switch (wordOrder) {
    case RegisterWordOrder::HighWordFirst:
        return "high_word_first";
    case RegisterWordOrder::LowWordFirst:
        return "low_word_first";
    }
    // Defensive: unknown enum values get a non-fabricating fallback token.
    return "low_word_first";
}

RegisterDecodedPair decodeRegisterPair(std::uint16_t word0,
                                       std::uint16_t word1,
                                       RegisterDecodeType type,
                                       RegisterByteOrder byteOrder,
                                       RegisterWordOrder wordOrder)
{
    switch (type) {
    case RegisterDecodeType::UInt32:
    case RegisterDecodeType::Int32:
    case RegisterDecodeType::Float32:
        break;
    default:
        // The pair entry is ONLY the 2-register entry: the 16-bit views (and
        // anything outside the matrix) must not silently decode through it.
        return {RegisterDecodeStatus::UnsupportedType, std::string{}};
    }

    // Frozen pipeline (T024 §7): byte swap within each register → word-order
    // combination → target-type reinterpretation. The raw words themselves
    // are never mutated.
    const std::uint16_t effective0 = decodeEffectiveWord(word0, byteOrder);
    const std::uint16_t effective1 = decodeEffectiveWord(word1, byteOrder);
    const std::uint32_t high =
        wordOrder == RegisterWordOrder::HighWordFirst ? effective0 : effective1;
    const std::uint32_t low =
        wordOrder == RegisterWordOrder::HighWordFirst ? effective1 : effective0;
    const std::uint32_t bits = (high << 16) | low;

    switch (type) {
    case RegisterDecodeType::UInt32:
        return {RegisterDecodeStatus::Ok, std::to_string(bits)};
    case RegisterDecodeType::Int32:
        return {RegisterDecodeStatus::Ok, std::to_string(toInt32(bits))};
    case RegisterDecodeType::Float32: {
        // Bit-level reinterpretation via memcpy: no type-punning through a
        // pointer, no assumption beyond size (checked statically).
        static_assert(sizeof(float) == sizeof(std::uint32_t));
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return {RegisterDecodeStatus::Ok, formatFloat32(value)};
    }
    default:
        break;
    }
    // Unreachable (the type filter above guarantees a 32-bit type here); the
    // honest defensive result is UnsupportedType with no text.
    return {RegisterDecodeStatus::UnsupportedType, std::string{}};
}

RegisterDecodedView decodeRegisterView(const std::vector<std::uint16_t>& words,
                                       int start, RegisterDecodeType type,
                                       RegisterByteOrder byteOrder,
                                       RegisterWordOrder wordOrder)
{
    const int needed = registerDecodeTypeWordCount(type);
    if (needed <= 0 || start < 0) {
        // T024 §9 InvalidConfiguration: the requested word count is not a
        // legal view width, or the selection itself is malformed.
        return {RegisterDecodeStatus::InvalidConfiguration, std::string{},
                start, 0};
    }
    if (start >= static_cast<int>(words.size())) {
        // T024 §9 OutOfRangeSelection: the selection is outside the data.
        return {RegisterDecodeStatus::OutOfRangeSelection, std::string{}, start,
                needed};
    }
    if (start + needed > static_cast<int>(words.size())) {
        // T024 §9 InsufficientWords: e.g. the last register takes a UInt32.
        // The wire result is untouched — raw stays Success.
        return {RegisterDecodeStatus::InsufficientWords, std::string{}, start,
                needed};
    }
    if (needed == 1) {
        const auto word =
            decodeRegisterWord(words.at(static_cast<std::size_t>(start)), type,
                               byteOrder);
        return {word.status, word.text, start, 1};
    }
    const auto pair = decodeRegisterPair(
        words.at(static_cast<std::size_t>(start)),
        words.at(static_cast<std::size_t>(start) + 1), type, byteOrder,
        wordOrder);
    return {pair.status, pair.text, start, 2};
}

} // namespace modbuslens::core