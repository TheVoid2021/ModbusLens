#pragma once

#include <cstdint>
#include <span>

namespace modbuslens::core {

// CRC-16/MODBUS: polynomial 0x8005 (reflected form 0xA001), initial value
// 0xFFFF, LSB-first bitwise algorithm per MODBUS over Serial Line V1.02.
//
// Pure calculation: returns the numeric CRC value. Serializing it into the
// two on-wire frame bytes (low byte first) is the frame model's job (T003).
std::uint16_t calculateModbusCrc(std::span<const std::uint8_t> data);

} // namespace modbuslens::core