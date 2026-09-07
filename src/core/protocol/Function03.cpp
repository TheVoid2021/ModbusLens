#include "core/protocol/Function03.h"

namespace modbuslens::core {

namespace {

constexpr std::uint8_t kReadHoldingRegistersFunction = 0x03;
constexpr std::uint8_t kExceptionFlag = 0x80;
constexpr std::uint16_t kMinQuantity = 1;
constexpr std::uint16_t kMaxQuantity = 125;   // 250 data bytes max per V1.1b3
constexpr std::uint8_t kMinByteCount = 2;     // quantity >= 1 -> >= 2 bytes
constexpr std::uint8_t kMaxByteCount = 250;   // quantity <= 125 -> <= 250 bytes

// Registers are 16-bit big-endian fields; CRC's low-byte-first rule does not
// apply here. Explicit shifts, never memcpy — host endianness must not leak.
std::uint16_t readBigEndianUint16(std::uint8_t high, std::uint8_t low)
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(high) << 8) | low);
}

} // namespace

ReadHoldingRegistersRequestResult
decodeReadHoldingRegistersRequest(const ModbusRtuFrame& frame)
{
    if (frame.functionCode != kReadHoldingRegistersFunction) {
        return Function03DecodeError{Function03DecodeErrorCode::WrongFunctionCode};
    }
    if (frame.data.size() != 4) {
        return Function03DecodeError{Function03DecodeErrorCode::InvalidRequestLength};
    }

    const std::uint16_t startAddress =
        readBigEndianUint16(frame.data[0], frame.data[1]);
    const std::uint16_t quantity =
        readBigEndianUint16(frame.data[2], frame.data[3]);
    if (quantity < kMinQuantity || quantity > kMaxQuantity) {
        return Function03DecodeError{Function03DecodeErrorCode::InvalidQuantity};
    }

    return ReadHoldingRegistersRequest{
        .startAddress = startAddress,
        .quantity = quantity,
    };
}

ReadHoldingRegistersResponseResult
decodeReadHoldingRegistersResponse(const ModbusRtuFrame& frame)
{
    if (frame.functionCode != kReadHoldingRegistersFunction) {
        return Function03DecodeError{Function03DecodeErrorCode::WrongFunctionCode};
    }
    if (frame.data.empty()) {
        return Function03DecodeError{Function03DecodeErrorCode::InvalidByteCount};
    }

    const std::uint8_t byteCount = frame.data[0];
    // quantity 1..125 => byteCount = 2*quantity => 2..250, always even. A
    // zero byte count already violates the 0x03 response format by itself.
    if (byteCount < kMinByteCount || byteCount > kMaxByteCount
        || (byteCount % 2) != 0) {
        return Function03DecodeError{Function03DecodeErrorCode::InvalidByteCount};
    }
    if (frame.data.size() != 1u + byteCount) {
        return Function03DecodeError{Function03DecodeErrorCode::InvalidByteCount};
    }

    ReadHoldingRegistersResponse response;
    response.values.reserve(byteCount / 2);
    for (std::size_t offset = 1; offset + 1 < frame.data.size(); offset += 2) {
        response.values.push_back(
            readBigEndianUint16(frame.data[offset], frame.data[offset + 1]));
    }
    return response;
}

ModbusExceptionResponseResult
decodeReadHoldingRegistersException(const ModbusRtuFrame& frame)
{
    if (frame.functionCode != (kReadHoldingRegistersFunction | kExceptionFlag)) {
        return Function03DecodeError{Function03DecodeErrorCode::WrongFunctionCode};
    }
    if (frame.data.size() != 1) {
        return Function03DecodeError{Function03DecodeErrorCode::InvalidExceptionLength};
    }

    // Stored numerically on purpose: unknown codes still carry diagnostic
    // value; the text mapping lives in the analysis layer.
    return ModbusExceptionResponse{.exceptionCode = frame.data[0]};
}

ReadHoldingRegistersEncodeResult encodeReadHoldingRegistersRequest(
    std::uint8_t address, std::uint16_t startAddress, std::uint16_t quantity)
{
    // T010 addition (T004 shipped decode-only): the symmetric encoder.
    // Quantity is validated HERE; unicast slave-address validation belongs
    // to the Serial session layer because a generic codec stays
    // address-agnostic. Wire bytes and CRC stay ModbusRtuCodec's job.
    if (quantity < kMinQuantity || quantity > kMaxQuantity) {
        return Function03EncodeError{Function03EncodeErrorCode::InvalidQuantity};
    }
    return ModbusRtuFrame{
        .address = address,
        .functionCode = kReadHoldingRegistersFunction,
        .data = {
            static_cast<std::uint8_t>(startAddress >> 8),
            static_cast<std::uint8_t>(startAddress & 0xFF),
            static_cast<std::uint8_t>(quantity >> 8),
            static_cast<std::uint8_t>(quantity & 0xFF),
        },
    };
}

} // namespace modbuslens::core