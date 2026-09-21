#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M10-A: unified ACTIVE request intent (Pure C++20, Zero Qt).
//
// One type-safe envelope for every master-initiated request. The envelope
// carries the facts every function shares (function identity, unicast unit,
// timeout) and a CLOSED variant for the function-specific payload — never a
// QVariant map, never a stringly-typed function name.
//
// Naming discipline (M10 Phase 1 §2 / FC1): 0x10 is "Write Multiple
// Registers" and is decimal 16; the protocol module keeps the historical
// class name Function16 and no "Function10" ever exists.
//
// This header is a REPRESENTATION + VALIDATION layer only. It carries no
// request bytes of its own: the active path builds 0x03 and — since M10-D1/D2 —
// 0x06 requests, while 0x10 has no encoder and stays impossible by construction
// until M10-E (Passive decoding support is NOT an active write capability).
// Encoding a request is still not a product capability: whether a write can
// actually be dispatched is a separate question answered by
// ProductWriteCapability.h.
// ---------------------------------------------------------------------------

enum class ActiveFunction : std::uint8_t {
    ReadHoldingRegisters = 0x03,
    WriteSingleRegister = 0x06,
    WriteMultipleRegisters = 0x10, // decimal 16
};

// Wire function code carried by the enum value itself.
[[nodiscard]] std::uint8_t activeFunctionCode(ActiveFunction function);

// Stable machine token (e.g. "read_holding_registers"). Deliberately NOT
// human UI prose — that mapping belongs to the Qt adapter layer.
[[nodiscard]] std::string_view activeFunctionName(ActiveFunction function);

struct ReadHoldingRegistersIntent {
    std::uint16_t startAddress{}; // 0-based protocol address
    std::uint16_t quantity{};     // 1..125

    bool operator==(const ReadHoldingRegistersIntent&) const = default;
};

struct WriteSingleRegisterIntent {
    std::uint16_t registerAddress{};
    std::uint16_t value{};

    bool operator==(const WriteSingleRegisterIntent&) const = default;
};

struct WriteMultipleRegistersIntent {
    std::uint16_t startAddress{};
    // SINGLE authority for the multi-write payload: quantity and byteCount are
    // DERIVED from values.size() — never stored next to it (M10 Phase 1 §24).
    std::vector<std::uint16_t> values;

    bool operator==(const WriteMultipleRegistersIntent&) const = default;
};

using ActiveRequestPayload = std::variant<ReadHoldingRegistersIntent,
                                          WriteSingleRegisterIntent,
                                          WriteMultipleRegistersIntent>;

struct ActiveRequestIntent {
    ActiveFunction function{ActiveFunction::ReadHoldingRegisters};
    // Unicast unit id 1..247. 0 (broadcast) is representable but NEVER
    // actively sent: validation rejects it before any transport call (M10
    // Phase 1 §4 / FC3 — active broadcast stays out of v1).
    std::uint8_t unitId{};
    std::chrono::milliseconds timeout{0};
    ActiveRequestPayload payload{};

    bool operator==(const ActiveRequestIntent&) const = default;
};

// Local (pre-transport) validation facts. These are NOT Modbus transaction
// statuses: a rejected intent produces zero sends and no fabricated
// ProtocolError transaction (M10 Phase 1 §11 / FC9 NotSent).
enum class ActiveRequestValidationError {
    UnitIdNotUnicast,     // 0 (broadcast) or > 247
    QuantityOutOfRange,   // outside the function's legal domain
    TimeoutNotPositive,
    PayloadFunctionMismatch, // payload alternative does not match `function`
};

[[nodiscard]] std::optional<ActiveRequestValidationError>
validateActiveRequestIntent(const ActiveRequestIntent& intent);

// Function-specific legal domain helpers (single source shared by validation
// and by tests; no duplicated magic numbers).
inline constexpr std::uint16_t kReadHoldingRegistersMinQuantity = 1;
inline constexpr std::uint16_t kReadHoldingRegistersMaxQuantity = 125;
inline constexpr std::uint16_t kWriteMultipleRegistersMinQuantity = 1;
inline constexpr std::uint16_t kWriteMultipleRegistersMaxQuantity = 123;
inline constexpr std::uint8_t kMinUnicastUnitId = 1;
inline constexpr std::uint8_t kMaxUnicastUnitId = 247;

// ---------------------------------------------------------------------------
// Encoded request: the intent plus its TWO derived products — the semantic
// frame (identity/bookkeeping) and the exact wire ADU (CRC included) that the
// transport must write as-is. Encoding happens ONCE; the transport never
// re-encodes and the session never re-derives bytes from a draft.
// ---------------------------------------------------------------------------
struct ActiveRequestDescriptor {
    ActiveRequestIntent intent{};
    ModbusRtuFrame frame{};
    std::vector<std::uint8_t> wire{};

    bool operator==(const ActiveRequestDescriptor&) const = default;
};

enum class ActiveRequestEncodeErrorCode {
    // Reserved for a function whose request encoding does not exist. No
    // current ActiveFunction hits this any more (0x10 gained its encoder in
    // M10-E1), but the branch stays as the deterministic "cannot encode" arm
    // for any future function — the same discipline as
    // TransactionIssueCode::UnknownProtocolError.
    UnsupportedFunction,
    InvalidQuantity,
    IntentInvalid, // validateActiveRequestIntent rejected it (see that result)
};

struct ActiveRequestEncodeError {
    ActiveRequestEncodeErrorCode code{};

    bool operator==(const ActiveRequestEncodeError&) const = default;
};

using ActiveRequestEncodeResult =
    std::variant<ActiveRequestDescriptor, ActiveRequestEncodeError>;

// Build the descriptor for a validated intent. 0x03 (M10-A), 0x06 (M10-D1)
// and 0x10 (M10-E1) are implemented.
//
// An encoder is NOT a product capability and NOT active support: 0x10 still
// cannot be SENT, because the session gate (activeFunctionSupported) does not
// admit it until the active response analyzer and its session lifecycle land
// (M10-E2). The Controller capability check and the production UI stay absent
// for the same reason.
ActiveRequestEncodeResult encodeActiveRequest(const ActiveRequestIntent& intent);

} // namespace modbuslens::core