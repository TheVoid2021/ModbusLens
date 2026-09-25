#include "core/analysis/RegisterDecode.h"

#include <cstdio>
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

} // namespace modbuslens::core