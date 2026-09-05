#pragma once

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

enum class RtuDecodeErrorCode {
    FrameTooShort,
    CrcMismatch,
};

// Struct (not a bare enum) so future diagnostic fields (e.g. failing byte
// offset) can be added without changing the result type.
struct RtuDecodeError {
    RtuDecodeErrorCode code;
};

using RtuDecodeResult = std::variant<ModbusRtuFrame, RtuDecodeError>;

// Frame -> complete RTU wire bytes:
// address + functionCode + data + CRC (low byte first, per V1.02).
// CRC is recalculated from the current fields; the frame model intentionally
// stores none.
std::vector<std::uint8_t> encodeRtuFrame(const ModbusRtuFrame& frame);

// Wire bytes -> semantic frame. Pure conversion/validation: never owns the
// input, never produces a frame when the CRC does not match, and never
// interprets function-specific data (that is Part B's job).
RtuDecodeResult decodeRtuFrame(std::span<const std::uint8_t> bytes);

} // namespace modbuslens::core