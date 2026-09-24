#include "core/active/ReadRequestParsing.h"

#include <optional>

namespace modbuslens::core {

namespace {

int hexDigitValue(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool isOuterSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'
        || c == '\v';
}

std::string_view trimOuter(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isOuterSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && isOuterSpace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

} // namespace

ReadFunctionCodeParseResult parseReadFunctionCode(std::string_view rawText)
{
    const std::string_view text = trimOuter(rawText);
    if (text.empty()) {
        return ValuesParseError{ValuesParseErrorCode::NoValues, 0, 0};
    }

    // Optional 0x / 0X prefix (case-insensitive), then 1..2 HEX digits.
    std::string_view digits = text;
    if (digits.size() >= 2 && digits[0] == '0'
        && (digits[1] == 'x' || digits[1] == 'X')) {
        digits = digits.substr(2);
    }
    if (digits.empty()) {
        // "0x" alone: a prefix with no value is not a function code.
        return ValuesParseError{ValuesParseErrorCode::InvalidCharacter, 0, 0};
    }
    if (digits.size() > 2) {
        // More than one byte of HEX can never be a function code; truncating
        // it would silently change what the user asked for.
        return ValuesParseError{ValuesParseErrorCode::ValueOutOfRange, 0, 0};
    }

    int value = 0;
    for (const char c : digits) {
        const int digit = hexDigitValue(c);
        if (digit < 0) {
            return ValuesParseError{ValuesParseErrorCode::InvalidCharacter, 0,
                                    0};
        }
        value = (value << 4) | digit;
    }
    return ReadFunctionCode{static_cast<std::uint8_t>(value)};
}

} // namespace modbuslens::core
