#pragma once

#include <cstdint>
#include <vector>

namespace modbuslens::core {

// In-memory semantic model of one Modbus RTU message (address + function
// code + data). Deliberately holds NO CRC: CRC is derived from the three
// fields, so storing it next to mutable data would invite stale-CRC bugs.
// Wire-level encoding, decoding and CRC validation belong to the codec
// (T004); a raw-bytes view for diagnostics may be added separately there.
struct ModbusRtuFrame {
    std::uint8_t address{};
    std::uint8_t functionCode{};
    std::vector<std::uint8_t> data;

    bool operator==(const ModbusRtuFrame&) const = default;
};

// True when the function code carries the exception flag (high bit set,
// e.g. 0x83 = exception reply to 0x03). Generic frame-level property;
// interpreting the exception code itself belongs to codec/analysis.
inline bool isExceptionResponse(const ModbusRtuFrame& frame)
{
    return (frame.functionCode & 0x80) != 0;
}

} // namespace modbuslens::core