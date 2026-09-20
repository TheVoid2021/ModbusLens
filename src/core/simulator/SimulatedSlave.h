#pragma once

#include <cstdint>
#include <optional>
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

// M10-A: outcome of applying one received WRITE request to the register bank.
// Only Applied mutates anything — every refusal leaves the bank untouched.
enum class SimulatorWriteOutcome {
    Applied,             // the decoded request's values were written
    ReadOnlyMode,        // writable mode not enabled (the default)
    NotMyAddress,        // addressed to another device (or a broadcast)
    UnsupportedFunction, // not 0x06 / 0x10 (exception-shaped requests included)
    MalformedRequest,    // decode rejected it: never a partial mutation
};

// A pure, synchronous Modbus slave endpoint: frame in, frame out. No clock,
// no threads, no randomness — fault injection (T006) wraps around it later.
class SimulatedSlave {
public:
    // M10-A: writable mode is strictly OPT-IN. The default stays the v1
    // read-only endpoint, so no existing behaviour changes by accident.
    enum class WriteMode {
        ReadOnly,
        Writable,
    };

    explicit SimulatedSlave(std::uint8_t address,
                            WriteMode mode = WriteMode::ReadOnly);

    [[nodiscard]] WriteMode writeMode() const;

    // Contiguous holding-register file: grows on demand, gaps default to 0.
    void setHoldingRegister(std::uint16_t address, std::uint16_t value);

    // Read/query seam: nullopt when the address is outside the bank. Lets a
    // caller (or test) assert the register file WITHOUT reaching into it.
    [[nodiscard]] std::optional<std::uint16_t> holdingRegister(
        std::uint16_t address) const;
    [[nodiscard]] std::size_t registerCount() const;

    // Pure responder: reads state, never mutates it. Function 0x03 decoding
    // is delegated to the T004 decoder — never re-parsed here. Write requests
    // are NOT answered here yet (answer framing needs the active write
    // encoders, which do not exist until M10-D/E): they keep receiving the
    // spec-mandated Illegal Function exception exactly as in v1.
    SimulatorResult handleRequest(const ModbusRtuFrame& request) const;

    // M10-A foundation: apply an already-received WRITE request to the bank.
    // Deterministic and mutation-safe: the request is decoded first, and a
    // request that does not decode cleanly mutates NOTHING (no partial write).
    // The bank follows the same "grows on demand" rule as setHoldingRegister:
    // v1 has no device register map yet (that is M12 work), so address-range
    // policy is deliberately NOT invented here.
    SimulatorWriteOutcome applyWriteRequest(const ModbusRtuFrame& request);

private:
    std::uint8_t address_;
    WriteMode mode_ = WriteMode::ReadOnly;
    std::vector<std::uint16_t> holdingRegisters_;
};

} // namespace modbuslens::core