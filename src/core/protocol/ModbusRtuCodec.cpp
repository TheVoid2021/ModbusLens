#include "core/protocol/ModbusRtuCodec.h"

#include "core/protocol/ModbusCrc.h"

namespace modbuslens::core {

std::vector<std::uint8_t> encodeRtuFrame(const ModbusRtuFrame& frame)
{
    std::vector<std::uint8_t> wire;
    wire.reserve(2 + frame.data.size() + 2);

    wire.push_back(frame.address);
    wire.push_back(frame.functionCode);
    wire.insert(wire.end(), frame.data.begin(), frame.data.end());

    const std::uint16_t crc = calculateModbusCrc(wire);
    const auto lowByte = static_cast<std::uint8_t>(crc & 0xFF);
    const auto highByte = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
    wire.push_back(lowByte);
    wire.push_back(highByte);

    return wire;
}

RtuDecodeResult decodeRtuFrame(std::span<const std::uint8_t> bytes)
{
    // Minimal RTU wire container: address(1) + function(1) + CRC(2).
    if (bytes.size() < 4) {
        return RtuDecodeError{RtuDecodeErrorCode::FrameTooShort};
    }

    const std::size_t payloadSize = bytes.size() - 2;
    const auto payload = bytes.first(payloadSize);

    // Reassemble the wire CRC explicitly, low byte first (84 0A -> 0x0A84).
    const auto crcLow = static_cast<std::uint16_t>(bytes[payloadSize]);
    const auto crcHigh = static_cast<std::uint16_t>(bytes[payloadSize + 1]);
    const std::uint16_t receivedCrc = static_cast<std::uint16_t>(
        (static_cast<unsigned>(crcHigh) << 8) | crcLow);

    if (calculateModbusCrc(payload) != receivedCrc) {
        return RtuDecodeError{RtuDecodeErrorCode::CrcMismatch};
    }

    return ModbusRtuFrame{
        .address = bytes[0],
        .functionCode = bytes[1],
        .data = {payload.begin() + 2, payload.end()},
    };
}

} // namespace modbuslens::core