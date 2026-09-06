#include "core/simulator/SimulationFault.h"

namespace modbuslens::core {

SimulatedDelivery applySimulationFault(
    std::span<const std::uint8_t> validWire,
    const SimulationFaultConfig& config)
{
    switch (config.mode) {
    case SimulationFaultMode::DropResponse:
        // "No response delivered" is a different fact than "a 0-byte packet
        // was delivered" — hence a dedicated variant branch, never an empty
        // DeliveredWire.
        return DroppedResponse{};

    case SimulationFaultMode::CorruptCrc: {
        // Fixed deterministic policy: flip the lowest bit of the final CRC
        // byte; payload bytes stay untouched. Input contract: validWire comes
        // from encodeRtuFrame (>= 4 bytes). Minimal defensive guard: an empty
        // input has nothing to corrupt, so it passes through unchanged
        // (contract-violation fallback, documented in the task doc).
        std::vector<std::uint8_t> corrupted{validWire.begin(), validWire.end()};
        if (!corrupted.empty()) {
            corrupted.back() = static_cast<std::uint8_t>(corrupted.back() ^ 0x01);
        }
        // Mode isolation: delay metadata belongs to ArtificialDelay only.
        return DeliveredWire{
            .bytes = std::move(corrupted),
            .artificialDelay = std::chrono::milliseconds{0}};
    }

    case SimulationFaultMode::ArtificialDelay:
        // Metadata only — no sleep/timer/thread anywhere in this layer; the
        // future session runtime decides how to realize the delay.
        return DeliveredWire{
            .bytes = {validWire.begin(), validWire.end()},
            .artificialDelay = config.artificialDelay};

    case SimulationFaultMode::None:
    default:
        // Plain passthrough: the injector is not a protocol participant and
        // never recalculates CRC.
        return DeliveredWire{
            .bytes = {validWire.begin(), validWire.end()},
            .artificialDelay = std::chrono::milliseconds{0}};
    }
}

} // namespace modbuslens::core