#pragma once

#include <chrono>
#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace modbuslens::core {

enum class SimulationFaultMode {
    None,            // passthrough, delay = 0
    DropResponse,    // nothing is delivered (a future session may call it Timeout)
    CorruptCrc,      // deterministic corruption: last CRC byte ^= 0x01
    ArtificialDelay, // passthrough + delay metadata (never a real wait)
};

struct SimulationFaultConfig {
    SimulationFaultMode mode{};
    std::chrono::milliseconds artificialDelay{0};
};

struct DeliveredWire {
    std::vector<std::uint8_t> bytes;
    std::chrono::milliseconds artificialDelay{0};

    bool operator==(const DeliveredWire&) const = default;
};

struct DroppedResponse {
    bool operator==(const DroppedResponse&) const = default;
};

using SimulatedDelivery = std::variant<DeliveredWire, DroppedResponse>;

// Pure transport-fault layer over an ALREADY-ENCODED RTU wire (contract: the
// input comes from encodeRtuFrame). One mode per config — no combined faults.
// Delay metadata is only attached by the ArtificialDelay mode; the other
// delivered modes always report 0ms. No CRC recalculation, no sleeping, no
// randomness: the same input+config always yields the same delivery.
SimulatedDelivery applySimulationFault(
    std::span<const std::uint8_t> validWire,
    const SimulationFaultConfig& config);

} // namespace modbuslens::core