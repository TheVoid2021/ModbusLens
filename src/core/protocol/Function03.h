#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// Function 0x03 (Read Holding Registers) semantic models. Input is always a
// Part-A-validated ModbusRtuFrame; these decoders never touch wire bytes or
// CRC and never interpret anything beyond 0x03's own data layout.
// ---------------------------------------------------------------------------

struct ReadHoldingRegistersRequest {
    std::uint16_t startAddress{};   // 0-based protocol address
    std::uint16_t quantity{};       // 1..125 (V1.1b3 section 6.3)

    bool operator==(const ReadHoldingRegistersRequest&) const = default;
};

struct ReadHoldingRegistersResponse {
    std::vector<std::uint16_t> values;   // high byte first on the wire

    bool operator==(const ReadHoldingRegistersResponse&) const = default;
};

// Exception shape is generic across function codes (0x80 | fn + one code
// byte), hence no "03" in the name; the decoder below only accepts 0x83.
struct ModbusExceptionResponse {
    std::uint8_t exceptionCode{};   // numeric only; text mapping is analysis's job

    bool operator==(const ModbusExceptionResponse&) const = default;
};

enum class Function03DecodeErrorCode {
    WrongFunctionCode,
    InvalidRequestLength,
    InvalidQuantity,
    InvalidByteCount,
    InvalidExceptionLength,
};

struct Function03DecodeError {
    Function03DecodeErrorCode code;
};

// Encode side (added in T010 Part A — T004 deliberately shipped decode-only).
// Builds the SEMANTIC frame only: wire bytes and CRC remain ModbusRtuCodec's
// job. Validates quantity (1..125); unicast slave-address validation
// (1..247) deliberately lives in the Serial session layer, not here — a
// generic codec stays address-agnostic.
enum class Function03EncodeErrorCode {
    InvalidQuantity,
};

struct Function03EncodeError {
    Function03EncodeErrorCode code;
};

// startAddress is the protocol 0-based address (uint16_t, no 40001-style
// 1-based presentation here).
using ReadHoldingRegistersEncodeResult =
    std::variant<ModbusRtuFrame, Function03EncodeError>;

ReadHoldingRegistersEncodeResult encodeReadHoldingRegistersRequest(
    std::uint8_t address, std::uint16_t startAddress, std::uint16_t quantity);

using ReadHoldingRegistersRequestResult =
    std::variant<ReadHoldingRegistersRequest, Function03DecodeError>;
using ReadHoldingRegistersResponseResult =
    std::variant<ReadHoldingRegistersResponse, Function03DecodeError>;
using ModbusExceptionResponseResult =
    std::variant<ModbusExceptionResponse, Function03DecodeError>;

// frame.functionCode must be 0x03; data must be exactly 4 bytes
// (startAddress + quantity, both big-endian); quantity must be 1..125.
ReadHoldingRegistersRequestResult
decodeReadHoldingRegistersRequest(const ModbusRtuFrame& frame);

// frame.functionCode must be 0x03; data[0] is byteCount (2..250, even) and
// must match the remaining byte count; register pairs are big-endian.
ReadHoldingRegistersResponseResult
decodeReadHoldingRegistersResponse(const ModbusRtuFrame& frame);

// frame.functionCode must be 0x83; data must be exactly one exception code
// byte (stored numerically — text mapping belongs to the analysis layer).
ModbusExceptionResponseResult
decodeReadHoldingRegistersException(const ModbusRtuFrame& frame);

} // namespace modbuslens::core