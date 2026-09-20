#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// Function 0x10 (Write Multiple Registers, decimal 16) — PASSIVE semantics
// only (T015 Part C).
//
//   Passive replay needs to know what a captured write request MEANT and
//   whether a captured reply matches its declared echo contract. Active
//   writing remains impossible by construction: there is no encoder, no
//   wire sender, no Serial API and no Qt type anywhere in this file
//   (Passive support != Active capability).
//
// Official facts (MODBUS Application Protocol V1.1b3, see
// docs/03_MODBUS_LEARNING.md §4.5):
//   request  = starting address (2B) + quantity (2B) + byte count (1B)
//              + register values (2N B);  quantity 1..123; byteCount = 2N;
//              actual value bytes must equal byteCount.
//   response = starting address (2B) + quantity written (2B); values are
//              NOT echoed. Exception function = 0x90 (generic matcher).
// ---------------------------------------------------------------------------

// Structural request-field reading (Gate C1) — deliberately NOT a
// strict-only decoder: a semantically invalid request is still a captured
// fact, so every readable field must survive. Readable when data.size()>=5.
struct WriteMultipleRegistersFields {
    std::uint16_t startingAddress{};
    std::uint16_t quantity{};
    std::uint8_t byteCount{};
    // Exact byte count of the value payload (frame.data.size() - 5) — the
    // ODD-byte fact is preserved, never floored to a register count.
    std::uint16_t actualValueByteCount{};

    bool operator==(const WriteMultipleRegistersFields&) const = default;
};

std::optional<WriteMultipleRegistersFields>
readWriteMultipleRegistersFields(const ModbusRtuFrame& frame);

// M10-A: strict request TEXT decoding (the register values themselves), added
// for the deterministic writable-simulator foundation. Decode-only, like the
// rest of this file — no wire builder, no encoder, no send API exists here.
// `quantity` and `byteCount` are validated against the value payload, so the
// decoded vector is the single value authority (no second count field).
struct WriteMultipleRegistersRequest {
    std::uint16_t startingAddress{};
    std::vector<std::uint16_t> values;

    bool operator==(const WriteMultipleRegistersRequest&) const = default;
};

struct WriteMultipleRegistersResponse {
    std::uint16_t startingAddress{};
    std::uint16_t quantityWritten{};

    bool operator==(const WriteMultipleRegistersResponse&) const = default;
};

enum class Function16DecodeErrorCode {
    WrongFunctionCode,
    InvalidResponseLength,
    // ---- M10-A request-side additions ----
    InvalidRequestLength,  // data below the 5-byte header
    InvalidQuantity,       // quantity outside 1..123
    InvalidByteCount,      // byteCount != 2*quantity or != payload size
};

struct Function16DecodeError {
    Function16DecodeErrorCode code;

    bool operator==(const Function16DecodeError&) const = default;
};

using WriteMultipleRegistersResponseResult =
    std::variant<WriteMultipleRegistersResponse, Function16DecodeError>;
using WriteMultipleRegistersRequestResult =
    std::variant<WriteMultipleRegistersRequest, Function16DecodeError>;

// frame.functionCode must be 0x10; data must be 5 + 2*quantity bytes with
// byteCount == 2*quantity (values big-endian). Strict on purpose: this is the
// request a DEVICE would have to act on, so a structurally invalid request
// must never decode into a mutating value list.
WriteMultipleRegistersRequestResult
decodeWriteMultipleRegistersRequest(const ModbusRtuFrame& frame);

// frame.functionCode must be 0x10; data must be exactly 4 bytes
// (startingAddress + quantityWritten, both big-endian).
WriteMultipleRegistersResponseResult
decodeWriteMultipleRegistersResponse(const ModbusRtuFrame& frame);

} // namespace modbuslens::core