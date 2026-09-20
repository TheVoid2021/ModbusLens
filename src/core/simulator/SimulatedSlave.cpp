#include "core/simulator/SimulatedSlave.h"

#include "core/protocol/Function03.h"
#include "core/protocol/Function06.h"
#include "core/protocol/Function16.h"

namespace modbuslens::core {

namespace {

constexpr std::uint8_t kReadHoldingRegistersFunction = 0x03;
constexpr std::uint8_t kWriteSingleRegisterFunction = 0x06;
constexpr std::uint8_t kWriteMultipleRegistersFunction = 0x10;
constexpr std::uint8_t kExceptionFlag = 0x80;
constexpr std::uint8_t kIllegalFunction = 0x01;
constexpr std::uint8_t kIllegalDataAddress = 0x02;
constexpr std::uint8_t kIllegalDataValue = 0x03;

// Exception responses are ordinary frames: fn|0x80 + one code byte. The code
// is a protocol number on purpose — text mapping belongs to the analysis
// layer, never here.
ModbusRtuFrame makeExceptionFrame(
    std::uint8_t slaveAddress, std::uint8_t requestFunction, std::uint8_t exceptionCode)
{
    return ModbusRtuFrame{
        .address = slaveAddress,
        .functionCode = static_cast<std::uint8_t>(requestFunction | kExceptionFlag),
        .data = {exceptionCode},
    };
}

} // namespace

SimulatedSlave::SimulatedSlave(std::uint8_t address, WriteMode mode)
    : address_(address)
    , mode_(mode)
{
}

SimulatedSlave::WriteMode SimulatedSlave::writeMode() const
{
    return mode_;
}

void SimulatedSlave::setHoldingRegister(std::uint16_t address, std::uint16_t value)
{
    // Contiguous register file: growing fills the gap with zeroes, so the
    // legal address range is exactly [0, size) and behaviour is deterministic.
    const auto index = static_cast<std::size_t>(address);
    if (index >= holdingRegisters_.size()) {
        holdingRegisters_.resize(index + 1, 0);
    }
    holdingRegisters_[index] = value;
}

std::optional<std::uint16_t> SimulatedSlave::holdingRegister(
    std::uint16_t address) const
{
    const auto index = static_cast<std::size_t>(address);
    if (index >= holdingRegisters_.size()) {
        return std::nullopt;
    }
    return holdingRegisters_[index];
}

std::size_t SimulatedSlave::registerCount() const
{
    return holdingRegisters_.size();
}

SimulatorWriteOutcome SimulatedSlave::applyWriteRequest(const ModbusRtuFrame& request)
{
    // 1. Ownership: a slave never acts on another device's request, and v1 has
    //    no broadcast semantics (address 0 is nobody's unicast address).
    if (request.address != address_) {
        return SimulatorWriteOutcome::NotMyAddress;
    }
    // 2. Exception-shaped frames are not requests at all.
    if ((request.functionCode & kExceptionFlag) != 0) {
        return SimulatorWriteOutcome::UnsupportedFunction;
    }
    // 3. Opt-in gate: the read-only endpoint mutates nothing, ever.
    if (mode_ != WriteMode::Writable) {
        return SimulatorWriteOutcome::ReadOnlyMode;
    }

    switch (request.functionCode) {
    case kWriteSingleRegisterFunction: {
        const auto decoded = decodeWriteSingleRegisterRequest(request);
        if (const auto* error = std::get_if<Function06DecodeError>(&decoded)) {
            static_cast<void>(error);
            return SimulatorWriteOutcome::MalformedRequest;
        }
        const auto& write = std::get<WriteSingleRegisterRequest>(decoded);
        setHoldingRegister(write.registerAddress, write.registerValue);
        return SimulatorWriteOutcome::Applied;
    }
    case kWriteMultipleRegistersFunction: {
        const auto decoded = decodeWriteMultipleRegistersRequest(request);
        if (std::get_if<Function16DecodeError>(&decoded) != nullptr) {
            return SimulatorWriteOutcome::MalformedRequest;
        }
        const auto& write = std::get<WriteMultipleRegistersRequest>(decoded);
        // Contiguous block write: the values are the single authority, so the
        // destination range is exactly values.size() registers from the start.
        for (std::size_t offset = 0; offset < write.values.size(); ++offset) {
            setHoldingRegister(
                static_cast<std::uint16_t>(write.startingAddress + offset),
                write.values[offset]);
        }
        return SimulatorWriteOutcome::Applied;
    }
    default:
        return SimulatorWriteOutcome::UnsupportedFunction;
    }
}

SimulatorResult SimulatedSlave::handleRequest(const ModbusRtuFrame& request) const
{
    // 1. Ownership first: a slave never answers for another device (and v1
    //    has no broadcast semantics, so address 0 is ignored the same way).
    if (request.address != address_) {
        return IgnoredRequest{};
    }

    // 2. Function dispatch. v1 supports only 0x03; anything else gets the
    //    spec-mandated Illegal Function exception. Exception-shaped requests
    //    (fn with 0x80 set) fall into the same branch — masters do not send
    //    them, and 0x80 | 0x80 keeps the reply exception-shaped.
    if (request.functionCode != kReadHoldingRegistersFunction) {
        return makeExceptionFrame(address_, request.functionCode, kIllegalFunction);
    }

    // 3. Reuse the T004 decoder — the request layout is never re-parsed here.
    const auto decoded = decodeReadHoldingRegistersRequest(request);
    if (std::holds_alternative<Function03DecodeError>(decoded)) {
        // Function is supported, so a decode failure means the request data
        // itself is invalid: Illegal Data Value, not a simulator error.
        return makeExceptionFrame(address_, request.functionCode, kIllegalDataValue);
    }
    const auto& requestModel = std::get<ReadHoldingRegistersRequest>(decoded);

    // 4. Range check in 32-bit space: [start, start+count) must lie inside
    //    [0, size). No partial reads on a partially legal range.
    const auto start = static_cast<std::uint32_t>(requestModel.startAddress);
    const auto count = static_cast<std::uint32_t>(requestModel.quantity);
    const auto available = static_cast<std::uint32_t>(holdingRegisters_.size());
    if (start >= available || start + count > available) {
        return makeExceptionFrame(address_, request.functionCode, kIllegalDataAddress);
    }

    // 5. Normal response: byteCount first, then registers high byte first.
    //    Explicit shifts — never memcpy, host endianness must not leak.
    ModbusRtuFrame response{
        .address = address_,
        .functionCode = kReadHoldingRegistersFunction,
        .data = {},
    };
    response.data.reserve(1u + 2u * count);
    response.data.push_back(static_cast<std::uint8_t>(2u * count));
    for (std::uint32_t offset = 0; offset < count; ++offset) {
        const std::uint16_t value =
            holdingRegisters_[static_cast<std::size_t>(start + offset)];
        response.data.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        response.data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }
    return response;
}

} // namespace modbuslens::core