#include "core/analysis/TransactionAnalysis.h"

namespace modbuslens::core {

namespace {

// Single funnel so the invariants hold on every return path: elapsed is the
// caller-provided fact, exceptionCode exists only for Exception (rule A/B).
TransactionAnalysis makeAnalysis(
    TransactionStatus status,
    std::chrono::milliseconds elapsed,
    std::optional<std::uint8_t> exceptionCode = std::nullopt)
{
    return TransactionAnalysis{
        .status = status,
        .elapsed = elapsed,
        .exceptionCode = std::move(exceptionCode),
    };
}

} // namespace

TransactionAnalysis analyzeFunction03Transaction(
    const ModbusRtuFrame& request,
    const ResponseObservation& observation,
    std::chrono::milliseconds elapsed,
    std::chrono::milliseconds timeoutThreshold)
{
    // 1. NoResponse: pending below the threshold, timeout at/above it.
    if (std::holds_alternative<NoResponse>(observation)) {
        if (elapsed < timeoutThreshold) {
            return makeAnalysis(TransactionStatus::Pending, elapsed);
        }
        return makeAnalysis(TransactionStatus::Timeout, elapsed);
    }

    // 2. Wire-level failure. Exhaustive switch (no default): a new
    //    RtuDecodeErrorCode trips -Wswitch instead of silently becoming a
    //    protocol error; the fallthrough return keeps the function total.
    if (auto* decodeError = std::get_if<RtuDecodeError>(&observation)) {
        switch (decodeError->code) {
        case RtuDecodeErrorCode::CrcMismatch:
            return makeAnalysis(TransactionStatus::CrcError, elapsed);
        case RtuDecodeErrorCode::FrameTooShort:
            return makeAnalysis(TransactionStatus::ProtocolError, elapsed);
        }
        return makeAnalysis(TransactionStatus::ProtocolError, elapsed);
    }

    // 3. A decoded frame: pairing gates before any semantic interpretation.
    const auto& response = std::get<ModbusRtuFrame>(observation);

    if (response.address != request.address) {
        // Another device's reply can never be this transaction's result.
        return makeAnalysis(TransactionStatus::ProtocolError, elapsed);
    }

    if (response.functionCode == 0x83) {
        // Matching exception response: reuse the T004B decoder, keep the
        // numeric code, never map it to text here.
        const auto exceptionDecode = decodeReadHoldingRegistersException(response);
        if (auto* error = std::get_if<Function03DecodeError>(&exceptionDecode)) {
            (void)error;
            return makeAnalysis(TransactionStatus::ProtocolError, elapsed);
        }
        const auto& exception =
            std::get<ModbusExceptionResponse>(exceptionDecode);
        return makeAnalysis(
            TransactionStatus::Exception, elapsed, exception.exceptionCode);
    }

    if (response.functionCode == 0x03) {
        // Normal response: reuse both T004B decoders, then run the first
        // true cross-frame check — quantity consistency.
        const auto requestDecode = decodeReadHoldingRegistersRequest(request);
        const auto responseDecode = decodeReadHoldingRegistersResponse(response);
        if (std::holds_alternative<Function03DecodeError>(requestDecode)
            || std::holds_alternative<Function03DecodeError>(responseDecode)) {
            // The request contract says it is valid; this branch is a
            // defensive mapping, never a crash or a seventh status.
            return makeAnalysis(TransactionStatus::ProtocolError, elapsed);
        }
        const auto& requestModel =
            std::get<ReadHoldingRegistersRequest>(requestDecode);
        const auto& responseModel =
            std::get<ReadHoldingRegistersResponse>(responseDecode);

        if (static_cast<std::size_t>(requestModel.quantity)
            != responseModel.values.size()) {
            return makeAnalysis(TransactionStatus::ProtocolError, elapsed);
        }
        return makeAnalysis(TransactionStatus::Success, elapsed);
    }

    // 4. Any other function code (0x04, 0x84, 0x06, ...) cannot answer a
    //    0x03 request — even exception-shaped ones like 0x84.
    return makeAnalysis(TransactionStatus::ProtocolError, elapsed);
}

} // namespace modbuslens::core