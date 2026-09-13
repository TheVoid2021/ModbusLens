#include "core/analysis/PassiveTransactionAnalysis.h"

#include "core/protocol/Function03.h"
#include "core/protocol/Function06.h"

#include <optional>

namespace modbuslens::core {

namespace {

using ms = std::chrono::milliseconds;

constexpr std::uint8_t kReadHoldingRegistersFunction = 0x03;
constexpr std::uint8_t kWriteSingleRegisterFunction = 0x06;
constexpr std::uint8_t kExceptionBit = 0x80;
constexpr std::uint8_t kBroadcastAddress = 0x00;
// V1.1b3 §6.3 (mirrors Function03's own kMaxQuantity; kept local because the
// protocol module does not export its constants).
constexpr std::uint16_t kFc03MaxQuantity = 125;

AnalyzedObservedTransaction analyzed(
    TransactionStatus status,
    ms elapsed,
    std::optional<std::uint8_t> exceptionCode = std::nullopt,
    std::optional<TransactionIssue> issue = std::nullopt,
    std::optional<TransactionRequestIssue> requestIssue = std::nullopt)
{
    return AnalyzedObservedTransaction{
        .analysis = TransactionAnalysis{
            .status = status,
            .elapsed = elapsed,
            .exceptionCode = std::move(exceptionCode),
            .issue = std::move(issue),
        },
        .requestIssue = std::move(requestIssue),
    };
}

// Value-initialized issue factory: sparse payload columns start empty and
// per-branch code fills exactly the columns its code requires (T015 I-set,
// same discipline as T014's makeIssue).
TransactionIssue makeIssue(TransactionIssueCode code)
{
    TransactionIssue issue;
    issue.code = code;
    return issue;
}

TransactionRequestIssue quantityIssue(std::optional<std::uint16_t> observed)
{
    TransactionRequestIssue issue;
    issue.code = TransactionRequestIssueCode::InvalidRequestQuantity;
    issue.observedQuantity = observed;
    issue.maxAllowedQuantity = kFc03MaxQuantity;
    return issue;
}

// Gate B: the ONLY place request-side issues are derived. Precedence when
// both facts hold (address 0 + invalid quantity): the function-specific
// invalidity wins — it is the more specific observed violation.
std::optional<TransactionRequestIssue> classifyRequest(const ModbusRtuFrame& request)
{
    std::optional<TransactionRequestIssue> requestIssue;

    switch (request.functionCode) {
    case kReadHoldingRegistersFunction: {
        const auto decoded = decodeReadHoldingRegistersRequest(request);
        if (std::holds_alternative<Function03DecodeError>(decoded)) {
            requestIssue = quantityIssue(readHoldingRegistersRequestQuantity(request));
        }
        break;
    }
    case kWriteSingleRegisterFunction: {
        const auto decoded = decodeWriteSingleRegisterRequest(request);
        if (std::holds_alternative<Function06DecodeError>(decoded)) {
            TransactionRequestIssue issue;
            issue.code = TransactionRequestIssueCode::InvalidRequestLength;
            requestIssue = issue;
        }
        break;
    }
    default:
        break; // unknown functions have no request model to violate
    }

    if (!requestIssue.has_value() && request.address == kBroadcastAddress
        && request.functionCode != kWriteSingleRegisterFunction) {
        // Phase B: FC06 is the only supported broadcast-capable function.
        // A read (or anything else) at address 0 is NOT a broadcast.
        TransactionRequestIssue issue;
        issue.code = TransactionRequestIssueCode::InvalidBroadcastFunction;
        requestIssue = issue;
    }
    return requestIssue;
}

} // namespace

std::string_view unsupportedSemanticsReasonName(UnsupportedSemanticsReason reason)
{
    switch (reason) {
    case UnsupportedSemanticsReason::UnsupportedNormalFunctionSemantics:
        return "unsupported_normal_function_semantics";
    }
    return "unsupported_normal_function_semantics";
}

PassiveObservedTransactionResult analyzeObservedTransaction(
    const ModbusRtuFrame& request,
    const ResponseObservation& observation,
    ms elapsed,
    ms timeoutThreshold)
{
    const auto requestIssue = classifyRequest(request);
    const bool isBroadcast = request.address == kBroadcastAddress
        && request.functionCode == kWriteSingleRegisterFunction;

    // 1. NoResponse: broadcast-capable request -> the protocol itself says
    //    "no response expected" (never Pending/Timeout). Everything else
    //    keeps the shared threshold semantics.
    if (std::holds_alternative<NoResponse>(observation)) {
        if (isBroadcast) {
            return analyzed(TransactionStatus::ExpectedNoResponse, elapsed,
                            std::nullopt, std::nullopt, requestIssue);
        }
        return analyzed(elapsed < timeoutThreshold ? TransactionStatus::Pending
                                                   : TransactionStatus::Timeout,
                        elapsed, std::nullopt, std::nullopt, requestIssue);
    }

    // 2. Broadcast: ANY recorded bytes are an unexpected response, even a
    //    perfect-looking echo and even a corrupt wire (T015 §19).
    if (isBroadcast) {
        return analyzed(
            TransactionStatus::ProtocolError, elapsed, std::nullopt,
            makeIssue(TransactionIssueCode::UnexpectedResponseForBroadcast),
            requestIssue);
    }

    // 3. Wire-level failure (unicast). Exhaustive switch on purpose.
    if (auto* decodeError = std::get_if<RtuDecodeError>(&observation)) {
        switch (decodeError->code) {
        case RtuDecodeErrorCode::CrcMismatch:
            return analyzed(TransactionStatus::CrcError, elapsed, std::nullopt,
                            std::nullopt, requestIssue);
        case RtuDecodeErrorCode::FrameTooShort:
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::ResponseFrameTooShort),
                requestIssue);
        }
        return analyzed(
            TransactionStatus::ProtocolError, elapsed, std::nullopt,
            makeIssue(TransactionIssueCode::UnknownProtocolError),
            requestIssue);
    }

    // 4. A decoded frame.
    const auto& response = std::get<ModbusRtuFrame>(observation);

    if (response.address != request.address) {
        auto issue = makeIssue(TransactionIssueCode::ResponseAddressMismatch);
        issue.expectedAddress = request.address;
        issue.actualAddress = response.address;
        return analyzed(TransactionStatus::ProtocolError, elapsed, std::nullopt,
                        std::move(issue), requestIssue);
    }

    // 5. Generic exception path — written ONCE for every function code.
    //    Boundary guard (T015 semantic audit): only a request function
    //    WITHOUT the exception bit can be answered by (fn | 0x80). A request
    //    whose function already carries 0x80 (e.g. 0x88) is not a normal
    //    Modbus request function, so (fn | 0x80) == fn must never match
    //    itself into a fake Exception; such a pair falls through to the
    //    unsupported / function-mismatch paths below.
    const bool requestHasExceptionBit =
        (request.functionCode & kExceptionBit) != 0;
    if (!requestHasExceptionBit
        && response.functionCode
            == static_cast<std::uint8_t>(request.functionCode | kExceptionBit)) {
        if (response.data.size() != 1) {
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::MalformedExceptionResponse),
                requestIssue);
        }
        return analyzed(TransactionStatus::Exception, elapsed, response.data[0],
                        std::nullopt, requestIssue);
    }

    if (request.functionCode == kReadHoldingRegistersFunction
        && response.functionCode == kReadHoldingRegistersFunction) {
        if (!requestIssue.has_value()) {
            // Valid FC03 request: reuse T007 verbatim — single source of
            // truth for the whole FC03 pair (T014 issue semantics included).
            return AnalyzedObservedTransaction{
                .analysis = analyzeFunction03Transaction(
                    request, observation, elapsed, timeoutThreshold),
                .requestIssue = std::nullopt,
            };
        }
        // Semantic-invalid FC03 request: the response side is still
        // classified, but the invalid request must not be trusted for the
        // cross-frame quantity expectation either — the recorded quantity
        // value stays the honest expected value.
        const auto responseDecode = decodeReadHoldingRegistersResponse(response);
        if (std::holds_alternative<Function03DecodeError>(responseDecode)) {
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::MalformedNormalResponse),
                requestIssue);
        }
        const auto& responseModel = std::get<ReadHoldingRegistersResponse>(responseDecode);
        const auto requestedQuantity =
            readHoldingRegistersRequestQuantity(request).value_or(0);
        if (static_cast<std::size_t>(requestedQuantity)
            != responseModel.values.size()) {
            auto issue = makeIssue(TransactionIssueCode::QuantityMismatch);
            issue.expectedQuantity = requestedQuantity;
            issue.actualQuantity = static_cast<std::uint16_t>(
                responseModel.values.size());
            return analyzed(TransactionStatus::ProtocolError, elapsed, std::nullopt,
                            std::move(issue), requestIssue);
        }
        return analyzed(TransactionStatus::Success, elapsed, std::nullopt,
                        std::nullopt, requestIssue);
    }

    if (request.functionCode == kWriteSingleRegisterFunction
        && response.functionCode == kWriteSingleRegisterFunction) {
        const auto responseDecode = decodeWriteSingleRegisterResponse(response);
        if (std::holds_alternative<Function06DecodeError>(responseDecode)) {
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::MalformedNormalResponse),
                requestIssue);
        }
        const auto requestDecode = decodeWriteSingleRegisterRequest(request);
        if (std::holds_alternative<Function06DecodeError>(requestDecode)) {
            // Request length was already recorded as a request issue; a
            // normal-shaped reply cannot be echo-verified against an
            // undecodable request — no finer deterministic fact exists.
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::UnknownProtocolError),
                requestIssue);
        }
        const auto& requestModel = std::get<WriteSingleRegisterRequest>(requestDecode);
        const auto& responseModel = std::get<WriteSingleRegisterResponse>(responseDecode);
        if (requestModel.registerAddress != responseModel.registerAddress
            || requestModel.registerValue != responseModel.registerValue) {
            auto issue = makeIssue(TransactionIssueCode::WriteSingleRegisterEchoMismatch);
            issue.expectedRegisterAddress = requestModel.registerAddress;
            issue.actualRegisterAddress = responseModel.registerAddress;
            issue.expectedRegisterValue = requestModel.registerValue;
            issue.actualRegisterValue = responseModel.registerValue;
            return analyzed(TransactionStatus::ProtocolError, elapsed, std::nullopt,
                            std::move(issue), requestIssue);
        }
        return analyzed(TransactionStatus::Success, elapsed, std::nullopt,
                        std::nullopt, requestIssue);
    }

    // 6. The response answers the request function, but ModbusLens has no
    //    normal semantics for it yet (FC08, 0x04, 0x10 until Part C, ...).
    //    That is an explicit per-record "unsupported" fact — NOT an invalid
    //    request and NOT a TransactionStatus.
    if (response.functionCode == request.functionCode) {
        return UnsupportedObservedTransaction{
            .deviceAddress = request.address,
            .functionCode = request.functionCode,
            .reason = UnsupportedSemanticsReason::UnsupportedNormalFunctionSemantics,
        };
    }

    // 7. Any other response function cannot answer this request.
    {
        auto issue = makeIssue(TransactionIssueCode::UnexpectedResponseFunction);
        issue.actualFunctionCode = response.functionCode;
        return analyzed(TransactionStatus::ProtocolError, elapsed, std::nullopt,
                        std::move(issue), requestIssue);
    }
}

} // namespace modbuslens::core
