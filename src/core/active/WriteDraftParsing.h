#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M10-C1: write draft parsing (Pure C++20, Zero Qt).
//
// The 0x10 write draft is a multi-line text field: ONE decimal unsigned
// register value per line. This parser is the ONLY place where that text
// becomes typed values — the UI never pre-parses, and the snapshot never
// re-reads the text (see PreparedWriteSnapshot).
//
// Frozen rules (M10-C Phase 1 §J3 / §K19):
//   · decimal only (no 0x..., no +12, no -1, no 1.5, no comma lists);
//   · per value 0..65535;
//   · input order is preserved (order == register order);
//   · leading/trailing blank lines are ignored (trimmed);
//   · a blank line BETWEEN values is an error — silently skipping it would
//     shift every later value by one register;
//   · 1..123 values;
//   · a parse failure never mutates the draft.
// ---------------------------------------------------------------------------

enum class ValuesParseErrorCode {
    NoValues,         // input carried no value at all (all blank)
    BlankLineInside,  // a blank line between values (structural error)
    InvalidCharacter, // non-digit content (see rules above)
    ValueOutOfRange,  // decimal value above 65535 (or an overflow-sized number)
    TooManyValues,    // more than 123 values
    // M10-D1: the SINGLE-value fields (0x06 register address / value) accept
    // exactly one value on one line. Two values ("1\n2", "1\r\n2") are not a
    // value list for such a field — silently taking the first, the last or a
    // concatenation would invent a number the user never wrote.
    MultipleValuesInSingleField,
};

struct ValuesParseError {
    ValuesParseErrorCode code{};
    // 0-based line index of the offending line (the last one for TooManyValues).
    std::size_t lineIndex{};
    // 0-based index of the offending value within the parsed sequence
    // (valueIndex counts only VALUES, so it is the register offset).
    std::size_t valueIndex{};

    bool operator==(const ValuesParseError&) const = default;
};

struct ParsedRegisterValues {
    std::vector<std::uint16_t> values;

    bool operator==(const ParsedRegisterValues&) const = default;
};

using ValuesParseResult = std::variant<ParsedRegisterValues, ValuesParseError>;

// Accepts LF and CRLF (a trailing '\r' is trimmed per line — never treated as
// an illegal digit character, and never dependent on a GUI widget to
// normalize it).
ValuesParseResult parseRegisterValues(std::string_view text);

// ---------------------------------------------------------------------------
// M10-D1: the SINGLE decimal field (0x06 register address / register value).
//
// This is the ONE decimal business authority for a one-value field: the
// QML-side field is presentation only and hands over the RAW text the user
// really typed ("00010", " 1234 ", "-1", "12x", "65536", ""), never a
// pre-normalized string. The rules are deliberately the SAME rules as
// parseRegisterValues — one acceptance table, not two:
//   · leading/trailing whitespace (spaces, tabs, CR/LF) is trimmed;
//   · empty after trimming           -> NoValues;
//   · digits only, decimal accumulate, 0..65535;
//   · any other character            -> InvalidCharacter;
//   · above 65535 / overflow-sized   -> ValueOutOfRange (never a wrap);
//   · more than one value (a newline separating two values, or a value list)
//                                    -> MultipleValuesInSingleField.
// ---------------------------------------------------------------------------

struct SingleRegisterValue {
    std::uint16_t value{};

    bool operator==(const SingleRegisterValue&) const = default;
};

using SingleValueParseResult = std::variant<SingleRegisterValue, ValuesParseError>;

SingleValueParseResult parseDecimalRegisterValue(std::string_view rawText);

} // namespace modbuslens::core