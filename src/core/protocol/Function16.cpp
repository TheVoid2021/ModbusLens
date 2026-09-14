#include "core/protocol/Function16.h"

namespace modbuslens::core {

namespace {

constexpr std::uint8_t kWriteMultipleRegistersFunction = 0x10;
constexpr std::size_t kWriteMultipleRegistersHeaderSize = 5;
constexpr std::size_t kWriteMultipleRegistersResponseDataSize = 4;

std::uint16_t readBigEndianUint16(std::uint8_t high, std::uint8_t low)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(high) << 8) | low);
}

} // namespace

std::optional<WriteMultipleRegistersFields>
readWriteMultipleRegistersFields(const ModbusRtuFrame& frame)
{
    if (frame.functionCode != kWriteMultipleRegistersFunction
        || frame.data.size() < kWriteMultipleRegistersHeaderSize) {
        return std::nullopt;
    }
    return WriteMultipleRegistersFields{
        .startingAddress = readBigEndianUint16(frame.data[0], frame.data[1]),
        .quantity = readBigEndianUint16(frame.data[2], frame.data[3]),
        .byteCount = frame.data[4],
        .actualValueByteCount = static_cast<std::uint16_t>(
            frame.data.size() - kWriteMultipleRegistersHeaderSize),
    };
}

WriteMultipleRegistersResponseResult
decodeWriteMultipleRegistersResponse(const ModbusRtuFrame& frame)
{
    if (frame.functionCode != kWriteMultipleRegistersFunction) {
        return Function16DecodeError{Function16DecodeErrorCode::WrongFunctionCode};
    }
    if (frame.data.size() != kWriteMultipleRegistersResponseDataSize) {
        return Function16DecodeError{Function16DecodeErrorCode::InvalidResponseLength};
    }
    return WriteMultipleRegistersResponse{
        .startingAddress = readBigEndianUint16(frame.data[0], frame.data[1]),
        .quantityWritten = readBigEndianUint16(frame.data[2], frame.data[3]),
    };
}

} // namespace modbuslens::core