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

} // namespace modbuslens::core
