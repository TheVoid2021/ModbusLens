#include "core/protocol/Function06.h"

namespace modbuslens::core {

namespace {

constexpr std::uint8_t kWriteSingleRegisterFunction = 0x06;
constexpr std::size_t kWriteSingleRegisterDataSize = 4;

std::uint16_t readBigEndianUint16(std::uint8_t high, std::uint8_t low)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(high) << 8) | low);
}

} // namespace

WriteSingleRegisterRequestResult
decodeWriteSingleRegisterRequest(const ModbusRtuFrame& frame)
{
    if (frame.functionCode != kWriteSingleRegisterFunction) {
        return Function06DecodeError{Function06DecodeErrorCode::WrongFunctionCode};
    }
    if (frame.data.size() != kWriteSingleRegisterDataSize) {
        return Function06DecodeError{Function06DecodeErrorCode::InvalidRequestLength};
    }
    return WriteSingleRegisterRequest{
        .registerAddress = readBigEndianUint16(frame.data[0], frame.data[1]),
        .registerValue = readBigEndianUint16(frame.data[2], frame.data[3]),
    };
}

WriteSingleRegisterResponseResult
decodeWriteSingleRegisterResponse(const ModbusRtuFrame& frame)
{
    if (frame.functionCode != kWriteSingleRegisterFunction) {
        return Function06DecodeError{Function06DecodeErrorCode::WrongFunctionCode};
    }
    if (frame.data.size() != kWriteSingleRegisterDataSize) {
        return Function06DecodeError{Function06DecodeErrorCode::InvalidResponseLength};
    }
    return WriteSingleRegisterResponse{
        .registerAddress = readBigEndianUint16(frame.data[0], frame.data[1]),
        .registerValue = readBigEndianUint16(frame.data[2], frame.data[3]),
    };
}

ModbusRtuFrame encodeWriteSingleRegisterRequest(std::uint8_t address,
                                                std::uint16_t registerAddress,
                                                std::uint16_t registerValue)
{
    // Symmetric to Function 0x03's encoder: field order and big-endian packing
    // are the ONLY things this layer owns. A 0x06 payload is exactly register
    // address + register value, so every uint16 pair is encodable — there is
    // no fake error branch here, and no unit validation either (that belongs
    // to the intent/session layer, which refuses broadcast before any
    // transport call).
    return ModbusRtuFrame{
        .address = address,
        .functionCode = kWriteSingleRegisterFunction,
        .data =
            {
                static_cast<std::uint8_t>(registerAddress >> 8),
                static_cast<std::uint8_t>(registerAddress & 0xFF),
                static_cast<std::uint8_t>(registerValue >> 8),
                static_cast<std::uint8_t>(registerValue & 0xFF),
            },
    };
}

} // namespace modbuslens::core
