#include "core/active/WritePrepareValidation.h"

namespace modbuslens::core {

namespace {

constexpr std::int64_t kMinAddress = 0;
constexpr std::int64_t kMaxAddress = 65535;

// A validated value is only ever narrowed AFTER these checks pass.
std::optional<WriteValidationError> validateUnit(std::int64_t unitId)
{
    if (unitId < static_cast<std::int64_t>(kMinUnicastUnitId)
        || unitId > static_cast<std::int64_t>(kMaxUnicastUnitId)) {
        return WriteValidationError{WriteValidationErrorCode::UnitIdOutOfRange,
                                    std::nullopt};
    }
    return std::nullopt;
}

std::optional<WriteValidationError> validateAddress(std::int64_t address)
{
    if (address < kMinAddress || address > kMaxAddress) {
        return WriteValidationError{WriteValidationErrorCode::AddressOutOfRange,
                                    std::nullopt};
    }
    return std::nullopt;
}

std::optional<WriteValidationError> validateTimeout(std::int64_t timeoutMs)
{
    if (timeoutMs < kWriteUiMinTimeoutMs || timeoutMs > kWriteUiMaxTimeoutMs) {
        return WriteValidationError{WriteValidationErrorCode::TimeoutOutOfRange,
                                    std::nullopt};
    }
    return std::nullopt;
}

} // namespace

bool registerSpanFitsAddressSpace(std::uint16_t startAddress, std::size_t quantity)
{
    if (quantity == 0) {
        return false;
    }
    // Widened arithmetic: uint16 addition would wrap (65535 + 2 -> 1) and the
    // comparison would silently pass.
    const std::uint32_t start = static_cast<std::uint32_t>(startAddress);
    const std::uint32_t count = static_cast<std::uint32_t>(quantity);
    return start + count <= 65536u;
}

WriteIntentResult prepareWriteSingleRegisterIntent(std::int64_t unitId,
                                                   std::int64_t registerAddress,
                                                   std::int64_t value,
                                                   std::int64_t timeoutMs)
{
    if (const auto error = validateUnit(unitId); error.has_value()) {
        return *error;
    }
    if (const auto error = validateAddress(registerAddress); error.has_value()) {
        return *error;
    }
    if (value < 0 || value > kMaxAddress) {
        return WriteValidationError{WriteValidationErrorCode::ValueOutOfRange,
                                    std::nullopt};
    }
    if (const auto error = validateTimeout(timeoutMs); error.has_value()) {
        return *error;
    }

    // Every local check passed: NOW the narrowing casts are safe.
    return ActiveRequestIntent{
        .function = ActiveFunction::WriteSingleRegister,
        .unitId = static_cast<std::uint8_t>(unitId),
        .timeout = std::chrono::milliseconds{timeoutMs},
        .payload = WriteSingleRegisterIntent{
            .registerAddress = static_cast<std::uint16_t>(registerAddress),
            .value = static_cast<std::uint16_t>(value),
        },
    };
}

WriteIntentResult prepareWriteMultipleRegistersIntent(std::int64_t unitId,
                                                      std::int64_t startAddress,
                                                      std::string_view valuesText,
                                                      std::int64_t timeoutMs)
{
    if (const auto error = validateUnit(unitId); error.has_value()) {
        return *error;
    }
    if (const auto error = validateAddress(startAddress); error.has_value()) {
        return *error;
    }
    if (const auto error = validateTimeout(timeoutMs); error.has_value()) {
        return *error;
    }

    const auto parsed = parseRegisterValues(valuesText);
    if (const auto* parseError = std::get_if<ValuesParseError>(&parsed)) {
        // The parser already enforces the 1..123 quantity domain, so a parse
        // error is reported as one (with line context) rather than being
        // re-labeled afterwards.
        return WriteValidationError{WriteValidationErrorCode::ValuesParseError,
                                    *parseError};
    }
    const auto& values = std::get<ParsedRegisterValues>(parsed).values;
    if (values.size() < kWriteMultipleRegistersMinQuantity
        || values.size() > kWriteMultipleRegistersMaxQuantity) {
        return WriteValidationError{WriteValidationErrorCode::QuantityOutOfRange,
                                    std::nullopt};
    }
    if (!registerSpanFitsAddressSpace(static_cast<std::uint16_t>(startAddress),
                                      values.size())) {
        return WriteValidationError{
            WriteValidationErrorCode::AddressSpanOutOfRange, std::nullopt};
    }

    return ActiveRequestIntent{
        .function = ActiveFunction::WriteMultipleRegisters,
        .unitId = static_cast<std::uint8_t>(unitId),
        .timeout = std::chrono::milliseconds{timeoutMs},
        .payload = WriteMultipleRegistersIntent{
            .startAddress = static_cast<std::uint16_t>(startAddress),
            .values = values,
        },
    };
}

} // namespace modbuslens::core