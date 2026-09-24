#include "core/active/ActiveRequestIntent.h"

#include "core/protocol/Function03.h"
#include "core/protocol/Function06.h"
#include "core/protocol/Function16.h"
#include "core/protocol/ModbusRtuCodec.h"

namespace modbuslens::core {

std::uint8_t activeFunctionCode(ActiveFunction function)
{
    return static_cast<std::uint8_t>(function);
}

std::uint8_t activeRequestFunctionCode(const ActiveRequestIntent& intent)
{
    // The register-read schema carries its actual wire function code in the
    // payload (user-selectable, default 0x03); the write schemas encode the
    // schema's own code.
    if (intent.function == ActiveFunction::ReadHoldingRegisters) {
        if (const auto* payload =
                std::get_if<ReadHoldingRegistersIntent>(&intent.payload)) {
            return payload->functionCode;
        }
    }
    return activeFunctionCode(intent.function);
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
        // M10 correction: the user-selectable read function code must stay in
        // the ordinary request domain — the exception function-bit space
        // (0x80..0xFF) is a RESPONSE shape, never a silently accepted request.
        if (payload->functionCode < kMinReadFunctionCode
            || payload->functionCode > kMaxReadFunctionCode) {
            return ActiveRequestValidationError::ReadFunctionCodeOutOfRange;
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
        // M10 correction: the wire function byte is the user-selected code the
        // payload carries (0x03 by default) — ONE register-read encoder, never
        // a per-function-code copy.
        const auto encoded = encodeReadHoldingRegistersRequest(
            intent.unitId, payload.startAddress, payload.quantity,
            payload.functionCode);
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
    case ActiveFunction::WriteSingleRegister: {
        // M10-D1: the 0x06 ENCODER exists (semantic frame from Function06,
        // CRC/wire from the codec). An encoder is still not a product
        // capability: the session gate opened in M10-D2, and whether a write
        // may actually be dispatched is decided by the Controller's atomic
        // confirmAndDispatchPreparedWrite (M10-D3) — encoding here decides
        // nothing on its own.
        const auto& payload = std::get<WriteSingleRegisterIntent>(intent.payload);
        const auto frame = encodeWriteSingleRegisterRequest(
            intent.unitId, payload.registerAddress, payload.value);
        return ActiveRequestDescriptor{
            .intent = intent,
            .frame = frame,
            .wire = encodeRtuFrame(frame),
        };
    }
    case ActiveFunction::WriteMultipleRegisters: {
        // M10-E1: the 0x10 REQUEST ENCODER now exists, symmetric to 0x03/0x06.
        // An encoder is still NOT a product capability — nothing here can be
        // sent, because the session gate (activeFunctionSupported) does NOT
        // admit 0x10 yet: the active response analyzer and its session
        // lifecycle tests are M10-E2 work. Keeping that gate closed is what
        // makes "an encoder exists" safe; the Controller's capability check
        // and the production UI stay absent for the same reason.
        const auto& payload =
            std::get<WriteMultipleRegistersIntent>(intent.payload);
        const auto frame = encodeWriteMultipleRegistersRequest(
            intent.unitId, payload.startAddress, payload.values);
        return ActiveRequestDescriptor{
            .intent = intent,
            .frame = frame,
            .wire = encodeRtuFrame(frame),
        };
    }
    }
    return ActiveRequestEncodeError{ActiveRequestEncodeErrorCode::UnsupportedFunction};
}

} // namespace modbuslens::core