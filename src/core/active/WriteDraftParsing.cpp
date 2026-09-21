#include "core/active/WriteDraftParsing.h"

#include "core/active/ActiveRequestIntent.h"

namespace modbuslens::core {

namespace {

constexpr std::uint64_t kMaxRegisterValue = 65535;
constexpr std::size_t kMaxValues = kWriteMultipleRegistersMaxQuantity;

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\v' || c == '\f';
}

// Trailing '\r' is trimmed together with the surrounding spaces, so a CRLF
// document never looks like it contains illegal digit characters.
std::string_view trim(std::string_view text)
{
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && isSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && (isSpace(text[end - 1]) || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// Single-value fields also trim newlines away at the OUTER edges: a pasted
// trailing "\n" is presentation noise, while a newline BETWEEN values is a
// second value (see parseDecimalRegisterValue).
bool isOuterSpace(char c)
{
    return isSpace(c) || c == '\r' || c == '\n';
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

ValuesParseResult parseRegisterValues(std::string_view text)
{
    std::vector<std::uint16_t> values;
    // The first blank line seen AFTER a value. If another value follows it,
    // that blank line was structural (a skipped register position) and the
    // whole input is rejected.
    std::optional<std::size_t> blankLineIndex;
    std::size_t lineIndex = 0;
    std::size_t valueIndex = 0;

    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t newline = text.find('\n', position);
        const std::size_t end =
            newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = trim(text.substr(position, end - position));

        if (line.empty()) {
            if (!values.empty() && !blankLineIndex.has_value()) {
                blankLineIndex = lineIndex;
            }
        } else {
            if (blankLineIndex.has_value()) {
                return ValuesParseError{ValuesParseErrorCode::BlankLineInside,
                                        *blankLineIndex, valueIndex};
            }
            if (values.size() >= kMaxValues) {
                return ValuesParseError{ValuesParseErrorCode::TooManyValues,
                                        lineIndex, valueIndex};
            }

            std::uint64_t value = 0;
            for (const char c : line) {
                if (c < '0' || c > '9') {
                    return ValuesParseError{ValuesParseErrorCode::InvalidCharacter,
                                            lineIndex, valueIndex};
                }
                value = value * 10u + static_cast<std::uint64_t>(c - '0');
                if (value > kMaxRegisterValue) {
                    // An overflow-sized number collapses into the same
                    // deterministic out-of-range error: never a wrap, never an
                    // exception, never locale-dependent parsing.
                    return ValuesParseError{ValuesParseErrorCode::ValueOutOfRange,
                                            lineIndex, valueIndex};
                }
            }
            values.push_back(static_cast<std::uint16_t>(value));
            ++valueIndex;
        }

        if (newline == std::string_view::npos) {
            break;
        }
        position = newline + 1;
        ++lineIndex;
    }

    if (values.empty()) {
        return ValuesParseError{ValuesParseErrorCode::NoValues, 0, 0};
    }
    return ParsedRegisterValues{std::move(values)};
}

SingleValueParseResult parseDecimalRegisterValue(std::string_view rawText)
{
    const std::string_view text = trimOuter(rawText);
    // A newline left inside the trimmed text separates two values. This is a
    // single-value field, so the input is rejected as such — never silently
    // reduced to the first line, the last line, or a concatenation.
    if (text.find_first_of("\r\n") != std::string_view::npos) {
        return ValuesParseError{
            ValuesParseErrorCode::MultipleValuesInSingleField, 0, 1};
    }
    // The remaining acceptance table (empty / digits / range) is the SAME one
    // the multi-line parser uses: parseRegisterValues is the single decimal
    // authority, and a single line can yield at most one value.
    const auto parsed = parseRegisterValues(text);
    if (const auto* error = std::get_if<ValuesParseError>(&parsed)) {
        return *error;
    }
    const auto& values = std::get<ParsedRegisterValues>(parsed).values;
    return SingleRegisterValue{values.front()};
}

} // namespace modbuslens::core