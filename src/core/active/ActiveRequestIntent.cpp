#include "core/active/ActiveRequestIntent.h"

#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"

namespace modbuslens::core {

std::uint8_t activeFunctionCode(ActiveFunction function)
{
    return static_cast<std::uint8_t>(function);
}

std::string_view activeFunctionName(ActiveFunction function)
{
    switch (function) {
    case ActiveFunction::ReadHoldingRegisters:
        return "read_holding_registers";
    case ActiveFunction::WriteSingleRegister:
        return "write_single_register";
    case ActiveFunction::WriteMultipleRegisters:
        return "write_multiple_registers";
    }
    return "unknown";
}

namespace {

// Quantity domain of the function the payload belongs to.
std::optional<ActiveRequestValidationError>
validatePayload(const ActiveRequestIntent& intent)
{
    switch (intent.function) {
    case ActiveFunction::ReadHoldingRegisters: {
        const auto* payload =
            std::get_if<ReadHoldingRegistersIntent>(&intent.payload);
        if (payload == nullptr) {
            return ActiveRequestValidationError::PayloadFunctionMismatch;
        }
        if (payload->quantity < kReadHoldingRegistersMinQuantity
            || payload->quantity > kReadHoldingRegistersMaxQuantity) {
            return ActiveRequestValidationError::QuantityOutOfRange;
        }
        return std::nullopt;
    }
    case ActiveFunction::WriteSingleRegister: {
        const auto* payload =
            std::get_if<WriteSingleRegisterIntent>(&intent.payload);
        if (payload == nullptr) {
            return ActiveRequestValidationError::PayloadFunctionMismatch;
        }
        // Every uint16 address/value pair is a structurally legal single
        // write; range policy beyond that belongs to the device, not here.
        return std::nullopt;
    }
    case ActiveFunction::WriteMultipleRegisters: {
        const auto* payload =
            std::get_if<WriteMultipleRegistersIntent>(&intent.payload);
        if (payload == nullptr) {
            return ActiveRequestValidationError::PayloadFunctionMismatch;
        }
        const auto count = payload->values.size();
        if (count < kWriteMultipleRegistersMinQuantity
            || count > kWriteMultipleRegistersMaxQuantity) {
            return ActiveRequestValidationError::QuantityOutOfRange;
        }
        return std::nullopt;
    }
    }
    return ActiveRequestValidationError::PayloadFunctionMismatch;
}

} // namespace

std::optional<ActiveRequestValidationError>
validateActiveRequestIntent(const ActiveRequestIntent& intent)
{
    // Unicast only: a broadcast unit id can be REPRESENTED (the domain model
    // must be able to describe one) but never actively transmitted in v1.
    if (intent.unitId < kMinUnicastUnitId || intent.unitId > kMaxUnicastUnitId) {
        return ActiveRequestValidationError::UnitIdNotUnicast;
    }
    if (intent.timeout.count() <= 0) {
        return ActiveRequestValidationError::TimeoutNotPositive;
    }
    return validatePayload(intent);
}

ActiveRequestEncodeResult encodeActiveRequest(const ActiveRequestIntent& intent)
{
    if (validateActiveRequestIntent(intent).has_value()) {
        return ActiveRequestEncodeError{ActiveRequestEncodeErrorCode::IntentInvalid};
    }

    switch (intent.function) {
    case ActiveFunction::ReadHoldingRegisters: {
        const auto& payload = std::get<ReadHoldingRegistersIntent>(intent.payload);
        const auto encoded = encodeReadHoldingRegistersRequest(
            intent.unitId, payload.startAddress, payload.quantity);
        if (std::get_if<Function03EncodeError>(&encoded) != nullptr) {
            return ActiveRequestEncodeError{
                ActiveRequestEncodeErrorCode::InvalidQuantity};
        }
        const auto frame = std::get<ModbusRtuFrame>(encoded);
        return ActiveRequestDescriptor{
            .intent = intent,
            .frame = frame,
            .wire = encodeRtuFrame(frame),
        };
    }
    case ActiveFunction::WriteSingleRegister:
    case ActiveFunction::WriteMultipleRegisters:
        // Active write encoding does not exist yet BY DESIGN (M10-A scope:
        // no write-request encoder). Passive decoding of captured write
        // traffic is a different capability and stays untouched.
        return ActiveRequestEncodeError{
            ActiveRequestEncodeErrorCode::UnsupportedFunction};
    }
    return ActiveRequestEncodeError{ActiveRequestEncodeErrorCode::UnsupportedFunction};
}

} // namespace modbuslens::core