#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/WriteDraftParsing.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M10-C1: write-preparation validation (Pure C++20, Zero Qt).
//
// This is a LOCAL write-preparation domain, deliberately NOT the Modbus
// outcome taxonomy and NOT TransactionIssue: a rejected draft is not a
// protocol fact, it is "the user's input cannot become a write intent".
//
// Hard rule (M10-C1 §4): every value arriving from a draft / UI boundary is
// validated in a WIDE type (int64_t) BEFORE any narrowing cast. "65536 must
// not become 0 and pass" and "-1 must not become 65535 and pass".
// ---------------------------------------------------------------------------

enum class WriteValidationErrorCode {
    UnitIdOutOfRange,       // unicast 1..247 (0 = broadcast: not supported)
    AddressOutOfRange,      // 0..65535
    ValueOutOfRange,        // 0x06 value: 0..65535
    TimeoutOutOfRange,      // write UI presentation range (see kWriteUiTimeout*)
    ValuesParseError,       // 0x10 multi-line text failed to parse (see parseError)
    QuantityOutOfRange,     // 0x10 value count outside 1..123
    AddressSpanOutOfRange,  // start + quantity - 1 > 0xFFFF (widened arithmetic)
};

struct WriteValidationError {
    WriteValidationErrorCode code{};
    // Present only for ValuesParseError (line/value context for the UI).
    std::optional<ValuesParseError> parseError;

    bool operator==(const WriteValidationError&) const = default;
};

// Write UI presentation range (M10-C Phase 1 §J4): the write draft reuses the
// existing Communication read UX bounds. The core authority stays
// `timeout > 0` (ActiveRequestIntent) — the UI range is deliberately
// narrower, and is never applied by silently clamping user input.
inline constexpr std::int64_t kWriteUiMinTimeoutMs = 100;
inline constexpr std::int64_t kWriteUiMaxTimeoutMs = 10000;

using WriteIntentResult = std::variant<ActiveRequestIntent, WriteValidationError>;

// 0x06: validate (wide) -> narrow -> typed intent. No encoder, no wire bytes.
WriteIntentResult prepareWriteSingleRegisterIntent(std::int64_t unitId,
                                                   std::int64_t registerAddress,
                                                   std::int64_t value,
                                                   std::int64_t timeoutMs);

// 0x10: parse the multi-line text, validate (wide) -> narrow -> typed intent.
// `values` is the ONLY authority: quantity is derived from its size and the
// address span is checked with widened arithmetic.
WriteIntentResult prepareWriteMultipleRegistersIntent(std::int64_t unitId,
                                                      std::int64_t startAddress,
                                                      std::string_view valuesText,
                                                      std::int64_t timeoutMs);

// True when `startAddress + quantity - 1 <= 0xFFFF` for a 16-bit register
// address space. Widened on purpose: uint16 arithmetic would wrap and let an
// overflowing span through.
[[nodiscard]] bool registerSpanFitsAddressSpace(std::uint16_t startAddress,
                                                std::size_t quantity);

} // namespace modbuslens::core