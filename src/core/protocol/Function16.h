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

struct WriteMultipleRegistersResponse {
    std::uint16_t startingAddress{};
    std::uint16_t quantityWritten{};

    bool operator==(const WriteMultipleRegistersResponse&) const = default;
};

enum class Function16DecodeErrorCode {
    WrongFunctionCode,
    InvalidResponseLength,
};

struct Function16DecodeError {
    Function16DecodeErrorCode code;

    bool operator==(const Function16DecodeError&) const = default;
};

using WriteMultipleRegistersResponseResult =
    std::variant<WriteMultipleRegistersResponse, Function16DecodeError>;

// frame.functionCode must be 0x10; data must be exactly 4 bytes
// (startingAddress + quantityWritten, both big-endian).
WriteMultipleRegistersResponseResult
decodeWriteMultipleRegistersResponse(const ModbusRtuFrame& frame);

} // namespace modbuslens::core