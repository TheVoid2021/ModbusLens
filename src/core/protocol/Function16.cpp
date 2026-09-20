#include "core/protocol/Function16.h"

namespace modbuslens::core {

namespace {

constexpr std::uint8_t kWriteMultipleRegistersFunction = 0x10;
constexpr std::size_t kWriteMultipleRegistersHeaderSize = 5;
constexpr std::size_t kWriteMultipleRegistersResponseDataSize = 4;
constexpr std::uint16_t kWriteMultipleRegistersMinQuantity = 1;
constexpr std::uint16_t kWriteMultipleRegistersMaxQuantity = 123;

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

WriteMultipleRegistersRequestResult
decodeWriteMultipleRegistersRequest(const ModbusRtuFrame& frame)
{
    if (frame.functionCode != kWriteMultipleRegistersFunction) {
        return Function16DecodeError{Function16DecodeErrorCode::WrongFunctionCode};
    }
    if (frame.data.size() < kWriteMultipleRegistersHeaderSize) {
        return Function16DecodeError{Function16DecodeErrorCode::InvalidRequestLength};
    }

    const auto fields = readWriteMultipleRegistersFields(frame);
    if (!fields.has_value()) { // unreachable after the length check above
        return Function16DecodeError{Function16DecodeErrorCode::InvalidRequestLength};
    }
    if (fields->quantity < kWriteMultipleRegistersMinQuantity
        || fields->quantity > kWriteMultipleRegistersMaxQuantity) {
        return Function16DecodeError{Function16DecodeErrorCode::InvalidQuantity};
    }
    // byteCount must agree with the declared quantity AND with the bytes that
    // actually arrived — a device must never act on a half-declared payload.
    const auto expectedByteCount = static_cast<std::uint16_t>(2u * fields->quantity);
    if (fields->byteCount != expectedByteCount
        || fields->actualValueByteCount != expectedByteCount) {
        return Function16DecodeError{Function16DecodeErrorCode::InvalidByteCount};
    }

    WriteMultipleRegistersRequest request{
        .startingAddress = fields->startingAddress,
        .values = {},
    };
    request.values.reserve(fields->quantity);
    for (std::uint16_t index = 0; index < fields->quantity; ++index) {
        const auto offset =
            kWriteMultipleRegistersHeaderSize + static_cast<std::size_t>(2u * index);
        request.values.push_back(
            readBigEndianUint16(frame.data[offset], frame.data[offset + 1]));
    }
    return request;
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