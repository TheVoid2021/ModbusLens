#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

// Returned when a frame is addressed to another device: the slave must not
// answer on someone else's behalf. v1 has no broadcast semantics.
struct IgnoredRequest {
    bool operator==(const IgnoredRequest&) const = default;
};

// Protocol-level outcomes (normal response, Modbus exception response) are
// expressed as frames; IgnoredRequest is the only non-frame result. There is
// deliberately no SimulatorError in v1 — protocol errors are protocol frames.
using SimulatorResult = std::variant<ModbusRtuFrame, IgnoredRequest>;

// A pure, synchronous Modbus slave endpoint: frame in, frame out. No clock,
// no threads, no randomness — fault injection (T006) wraps around it later.
class SimulatedSlave {
public:
    explicit SimulatedSlave(std::uint8_t address);

    // Contiguous holding-register file: grows on demand, gaps default to 0.
    void setHoldingRegister(std::uint16_t address, std::uint16_t value);

    // Pure responder: reads state, never mutates it. Function 0x03 decoding
    // is delegated to the T004 decoder — never re-parsed here.
    SimulatorResult handleRequest(const ModbusRtuFrame& request) const;

private:
    std::uint8_t address_;
    std::vector<std::uint16_t> holdingRegisters_;
};

} // namespace modbuslens::core