#include "core/analysis/TransactionAnalysis.h"

namespace modbuslens::core {

std::string_view transactionIssueName(TransactionIssueCode code)
{
    switch (code) {
    case TransactionIssueCode::ResponseFrameTooShort:
        return "response_frame_too_short";
    case TransactionIssueCode::ResponseAddressMismatch:
        return "response_address_mismatch";
    case TransactionIssueCode::MalformedExceptionResponse:
        return "malformed_exception_response";
    case TransactionIssueCode::MalformedNormalResponse:
        return "malformed_normal_response";
    case TransactionIssueCode::QuantityMismatch:
        return "quantity_mismatch";
    case TransactionIssueCode::UnexpectedResponseFunction:
        return "unexpected_response_function";
    case TransactionIssueCode::UnknownProtocolError:
        return "unknown_protocol_error";
    case TransactionIssueCode::WriteSingleRegisterEchoMismatch:
        return "write_single_register_echo_mismatch";
    case TransactionIssueCode::UnexpectedResponseForBroadcast:
        return "unexpected_response_for_broadcast";
    case TransactionIssueCode::WriteMultipleRegistersEchoMismatch:
        return "write_multiple_registers_echo_mismatch";
    }
    // Defensive: an enum value this build does not know cannot be given a
    // fabricated meaning — fall back to the generic token, never a guess.
    return "unknown_protocol_error";
}

std::string_view transactionRequestIssueName(TransactionRequestIssueCode code)
{
    switch (code) {
    case TransactionRequestIssueCode::InvalidRequestQuantity:
        return "invalid_request_quantity";
    case TransactionRequestIssueCode::InvalidRequestLength:
        return "invalid_request_length";
    case TransactionRequestIssueCode::InvalidRequestByteCount:
        return "invalid_request_byte_count";
    case TransactionRequestIssueCode::InvalidBroadcastFunction:
        return "invalid_broadcast_function";
    }
    return "unknown_request_issue";
}

namespace {

// Single funnel so the invariants hold on every return path: elapsed is the
// caller-provided fact, exceptionCode exists only for Exception, and issue
// exists only for ProtocolError (T014 I1/I2).
TransactionAnalysis makeAnalysis(
    TransactionStatus status,
    std::chrono::milliseconds elapsed,
    std::optional<std::uint8_t> exceptionCode = std::nullopt,
    std::optional<TransactionIssue> issue = std::nullopt)
{
    return TransactionAnalysis{
        .status = status,
        .elapsed = elapsed,
        .exceptionCode = std::move(exceptionCode),
        .issue = std::move(issue),
    };
}

// Every ProtocolError return goes through this helper, so the production
// invariant "ProtocolError => issue present" cannot drift per-branch.
TransactionAnalysis makeProtocolError(std::chrono::milliseconds elapsed,
                                      TransactionIssue issue)
{
    return makeAnalysis(TransactionStatus::ProtocolError, elapsed,
                        std::nullopt, std::move(issue));
}

// Value-initialized issue factory: the sparse payload columns start empty;
// per-code branches fill exactly the columns their code requires (T014 I5).
TransactionIssue makeIssue(TransactionIssueCode code)
{
    TransactionIssue issue;
    issue.code = code;
    return issue;
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
    //    protocol error; the fallthrough return keeps the function total
    //    and records a defensive UnknownProtocolError (T014 branch 11).
    if (auto* decodeError = std::get_if<RtuDecodeError>(&observation)) {
        switch (decodeError->code) {
        case RtuDecodeErrorCode::CrcMismatch:
            return makeAnalysis(TransactionStatus::CrcError, elapsed);
        case RtuDecodeErrorCode::FrameTooShort:
            return makeProtocolError(
                elapsed, makeIssue(TransactionIssueCode::ResponseFrameTooShort));
        }
        return makeProtocolError(
            elapsed, makeIssue(TransactionIssueCode::UnknownProtocolError));
    }

    // 3. A decoded frame: pairing gates before any semantic interpretation.
    const auto& response = std::get<ModbusRtuFrame>(observation);

    if (response.address != request.address) {
        // Another device's reply can never be this transaction's result.
        // T014: keep the two observed address bytes — a directly observed
        // fact, not a claim about whose configuration is wrong.
        auto issue = makeIssue(TransactionIssueCode::ResponseAddressMismatch);
        issue.expectedAddress = request.address;
        issue.actualAddress = response.address;
        return makeProtocolError(elapsed, std::move(issue));
    }

    if (response.functionCode == 0x83) {
        // Matching exception response: reuse the T004B decoder, keep the
        // numeric code, never map it to text here. A shape failure (data
        // length != 1) is MalformedExceptionResponse.
        const auto exceptionDecode = decodeReadHoldingRegistersException(response);
        if (std::get_if<Function03DecodeError>(&exceptionDecode) != nullptr) {
            return makeProtocolError(
                elapsed,
                makeIssue(TransactionIssueCode::MalformedExceptionResponse));
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
        if (std::holds_alternative<Function03DecodeError>(requestDecode)) {
            // The request contract says the request is valid; this branch is
            // a defensive mapping with no finer deterministic fact
            // (T014 branch 7) — never a crash, never a fabricated reason.
            return makeProtocolError(
                elapsed, makeIssue(TransactionIssueCode::UnknownProtocolError));
        }
        const auto responseDecode = decodeReadHoldingRegistersResponse(response);
        if (std::holds_alternative<Function03DecodeError>(responseDecode)) {
            return makeProtocolError(
                elapsed,
                makeIssue(TransactionIssueCode::MalformedNormalResponse));
        }
        const auto& requestModel =
            std::get<ReadHoldingRegistersRequest>(requestDecode);
        const auto& responseModel =
            std::get<ReadHoldingRegistersResponse>(responseDecode);

        if (static_cast<std::size_t>(requestModel.quantity)
            != responseModel.values.size()) {
            // Cross-frame fact: the single-frame response is well-formed but
            // answers the request with a different register count.
            auto issue = makeIssue(TransactionIssueCode::QuantityMismatch);
            issue.expectedQuantity = requestModel.quantity;
            issue.actualQuantity = static_cast<std::uint16_t>(
                responseModel.values.size());
            return makeProtocolError(elapsed, std::move(issue));
        }
        return makeAnalysis(TransactionStatus::Success, elapsed);
    }

    // 4. Any other function code (0x04, 0x84, 0x06, ...) cannot answer a
    //    0x03 request — even exception-shaped ones like 0x84. The expected
    //    codes {0x03, 0x83} are derivable from the request, so only the
    //    actual code is carried.
    auto issue = makeIssue(TransactionIssueCode::UnexpectedResponseFunction);
    issue.actualFunctionCode = response.functionCode;
    return makeProtocolError(elapsed, std::move(issue));
}

} // namespace modbuslens::core