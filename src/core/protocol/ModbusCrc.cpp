#include "core/protocol/ModbusCrc.h"

namespace modbuslens::core {

std::uint16_t calculateModbusCrc(std::span<const std::uint8_t> data)
{
    std::uint16_t crc = 0xFFFF;

    for (std::uint8_t byte : data) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint16_t previousLsb = crc & 0x0001;
            crc >>= 1;
            if (previousLsb == 1) {
                crc ^= 0xA001;
            }
        }
    }

    return crc;
}

} // namespace modbuslens::core