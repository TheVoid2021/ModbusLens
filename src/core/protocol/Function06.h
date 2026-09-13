#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// Function 0x06 (Write Single Register) PASSIVE semantic models — T015
// Phase B. Pure C++20, decode-only on purpose:
//
//   The passive replay analyzer needs to understand what a captured request
//   MEANS and whether a captured response is the exact echo the protocol
//   requires. Active writing remains impossible by construction: there is no
//   encoder, no wire builder, no send API anywhere in this file (Active
//   Serial stays FC03 read-only — Passive understanding != Active capability).
//
// Official protocol facts (MODBUS Application Protocol V1.1b3, backfilled in
// docs/03_MODBUS_LEARNING.md §4.5):
//   request  = register address (uint16) + register value (uint16)
//   response = exact echo of the request fields
//   exception function = 0x86 (generic exception path handles it)
// ---------------------------------------------------------------------------

struct WriteSingleRegisterRequest {
    std::uint16_t registerAddress{};
    std::uint16_t registerValue{};

    bool operator==(const WriteSingleRegisterRequest&) const = default;
};

struct WriteSingleRegisterResponse {
    std::uint16_t registerAddress{};
    std::uint16_t registerValue{};

    bool operator==(const WriteSingleRegisterResponse&) const = default;
};

enum class Function06DecodeErrorCode {
    WrongFunctionCode,
    InvalidRequestLength,
    InvalidResponseLength,
};

struct Function06DecodeError {
    Function06DecodeErrorCode code;

    bool operator==(const Function06DecodeError&) const = default;
};

using WriteSingleRegisterRequestResult =
    std::variant<WriteSingleRegisterRequest, Function06DecodeError>;
using WriteSingleRegisterResponseResult =
    std::variant<WriteSingleRegisterResponse, Function06DecodeError>;

// frame.functionCode must be 0x06; data must be exactly 4 bytes
// (registerAddress + registerValue, both big-endian).
WriteSingleRegisterRequestResult
decodeWriteSingleRegisterRequest(const ModbusRtuFrame& frame);

// frame.functionCode must be 0x06; data must be exactly 4 bytes.
WriteSingleRegisterResponseResult
decodeWriteSingleRegisterResponse(const ModbusRtuFrame& frame);

} // namespace modbuslens::core
