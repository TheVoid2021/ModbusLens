#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

#include "core/active/WriteDraftParsing.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M10 correction (Human: the read function code must be editable): the HEX
// text parser of the register-read function field.
//
// Authority discipline — IDENTICAL to the write draft parsers: this is the
// ONLY place where the field's text becomes a typed value; the QML field is
// presentation only and hands over the RAW text the user really typed; the
// business-range rule (0x01..0x7F) is enforced by the intent validator and
// the controller's typed guards, never here and never in QML.
//
// Frozen rules:
//   · leading/trailing whitespace is trimmed;
//   · 1..2 HEX digits, optionally prefixed with 0x / 0X (case-insensitive):
//       "03" "3" "41" "0x41" "0X41" "c1" are accepted (value = the HEX number);
//   · empty after trimming                       -> NoValues;
//   · any non-HEX-digit character outside the
//     prefix ("GG", "0x", "-1", "+3", " 0x G")   -> InvalidCharacter;
//   · more than two digits ("123", "0x123")      -> ValueOutOfRange
//     (a function code is one byte; a longer run can never be one, and
//     truncating it would silently change what the user asked for);
//   · the 0x01..0x7F range rule is NOT this parser's job (see the intent
//     validator) — "00" and "80" parse fine here and are rejected as ranges.
// ---------------------------------------------------------------------------

struct ReadFunctionCode {
    std::uint8_t functionCode{};

    bool operator==(const ReadFunctionCode&) const = default;
};

using ReadFunctionCodeParseResult =
    std::variant<ReadFunctionCode, ValuesParseError>;

ReadFunctionCodeParseResult parseReadFunctionCode(std::string_view rawText);

} // namespace modbuslens::core
