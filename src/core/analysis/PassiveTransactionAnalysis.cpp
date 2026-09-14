#include "core/analysis/PassiveTransactionAnalysis.h"

#include "core/protocol/Function03.h"
#include "core/protocol/Function06.h"
#include "core/protocol/Function16.h"

#include <optional>
#include <utility>

namespace modbuslens::core {

namespace {

using ms = std::chrono::milliseconds;

constexpr std::uint8_t kReadHoldingRegistersFunction = 0x03;
constexpr std::uint8_t kWriteSingleRegisterFunction = 0x06;
constexpr std::uint8_t kWriteMultipleRegistersFunction = 0x10;
constexpr std::uint8_t kExceptionBit = 0x80;
constexpr std::uint8_t kBroadcastAddress = 0x00;
// V1.1b3 §6.3 / Function03's own kMaxQuantity (kept local; the protocol
// module does not export its constants).
constexpr std::uint16_t kFc03MinQuantity = 1;
constexpr std::uint16_t kFc03MaxQuantity = 125;
// Function16 domain (03_MODBUS_LEARNING §4.5): 1..123.
constexpr std::uint16_t kFc16MinQuantity = 1;
constexpr std::uint16_t kFc16MaxQuantity = 123;
constexpr std::uint16_t kFc16HeaderBytes = 5;

AnalyzedObservedTransaction analyzed(
    TransactionStatus status,
    ms elapsed,
    std::optional<std::uint8_t> exceptionCode = std::nullopt,
    std::optional<TransactionIssue> issue = std::nullopt,
    std::vector<TransactionRequestIssue> requestIssues = {})
{
    return AnalyzedObservedTransaction{
        .analysis = TransactionAnalysis{
            .status = status,
            .elapsed = elapsed,
            .exceptionCode = std::move(exceptionCode),
            .issue = std::move(issue),
        },
        .requestIssues = std::move(requestIssues),
    };
}

// Value-initialized issue factory: sparse payload columns start empty and
// per-branch code fills exactly the columns its code requires.
TransactionIssue makeIssue(TransactionIssueCode code)
{
    TransactionIssue issue;
    issue.code = code;
    return issue;
}

TransactionRequestIssue makeRequestIssue(TransactionRequestIssueCode code)
{
    TransactionRequestIssue issue;
    issue.code = code;
    return issue;
}

// Gate B / Part C: the ONLY place request-side issues are derived.
// Deterministic reporting order = structural readability -> quantity ->
// byteCount-vs-quantity -> payload-vs-declared; ordering is NOT a discard
// ladder: every independently provable issue is kept (anti-cascade rules
// below). InvalidBroadcastFunction is appended last when it applies.
std::vector<TransactionRequestIssue> classifyRequest(const ModbusRtuFrame& request)
{
    std::vector<TransactionRequestIssue> issues;

    switch (request.functionCode) {
    case kReadHoldingRegistersFunction: {
        const auto decoded = decodeReadHoldingRegistersRequest(request);
        if (std::holds_alternative<Function03DecodeError>(decoded)) {
            auto issue = makeRequestIssue(
                TransactionRequestIssueCode::InvalidRequestQuantity);
            issue.observedQuantity = readHoldingRegistersRequestQuantity(request);
            issue.minAllowedQuantity = kFc03MinQuantity;
            issue.maxAllowedQuantity = kFc03MaxQuantity;
            issues.push_back(std::move(issue));
        }
        break;
    }
    case kWriteSingleRegisterFunction: {
        const auto decoded = decodeWriteSingleRegisterRequest(request);
        if (std::holds_alternative<Function06DecodeError>(decoded)) {
            issues.push_back(makeRequestIssue(
                TransactionRequestIssueCode::InvalidRequestLength));
        }
        break;
    }
    case kWriteMultipleRegistersFunction: {
        const auto fields = readWriteMultipleRegistersFields(request);
        if (!fields.has_value()) {
            // Structural readability failed: nothing else can be derived.
            auto issue = makeRequestIssue(
                TransactionRequestIssueCode::InvalidRequestLength);
            issue.observedLength = static_cast<std::uint16_t>(request.data.size());
            issues.push_back(std::move(issue));
            break;
        }

        const bool quantityValid = fields->quantity >= kFc16MinQuantity
            && fields->quantity <= kFc16MaxQuantity;
        if (!quantityValid) {
            auto issue = makeRequestIssue(
                TransactionRequestIssueCode::InvalidRequestQuantity);
            issue.observedQuantity = fields->quantity;
            issue.minAllowedQuantity = kFc16MinQuantity;
            issue.maxAllowedQuantity = kFc16MaxQuantity;
            issues.push_back(std::move(issue));
        } else {
            // Anti-cascade: expectedByteCount from a VALID quantity only —
            // never derive a pseudo byte-count expectation from an invalid
            // quantity.
            const auto expectedByteCount = static_cast<std::uint8_t>(
                fields->quantity * 2);
            if (fields->byteCount != expectedByteCount) {
                auto issue = makeRequestIssue(
                    TransactionRequestIssueCode::InvalidRequestByteCount);
                issue.observedByteCount = fields->byteCount;
                issue.expectedByteCount = expectedByteCount;
                issues.push_back(std::move(issue));
            }
        }

        // Payload-length vs declared byteCount is INDEPENDENT of quantity
        // validity: as long as the byteCount field is readable, the actual
        // data length can be compared against 5 + declaredByteCount.
        const std::uint16_t expectedLength = static_cast<std::uint16_t>(
            kFc16HeaderBytes + fields->byteCount);
        if (request.data.size() != expectedLength) {
            auto issue = makeRequestIssue(
                TransactionRequestIssueCode::InvalidRequestLength);
            issue.observedLength =
                static_cast<std::uint16_t>(request.data.size());
            issue.expectedLength = expectedLength;
            issues.push_back(std::move(issue));
        }
        break;
    }
    default:
        break; // unknown functions have no request model to violate
    }

    if (request.address == kBroadcastAddress
        && request.functionCode != kWriteSingleRegisterFunction
        && request.functionCode != kWriteMultipleRegistersFunction) {
        // The broadcast-capable set is explicitly {0x06, 0x10}: a read (or
        // anything else) at address 0 is NOT a broadcast.
        issues.push_back(makeRequestIssue(
            TransactionRequestIssueCode::InvalidBroadcastFunction));
    }
    return issues;
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
    const auto requestIssues = classifyRequest(request);
    // T015 Part C: the broadcast-capable set is explicitly {0x06, 0x10}.
    // Orthogonal semantics (ADR-003): response expectation is decided by
    // address+function, NEVER gated on request semantic validity.
    const bool isBroadcast = request.address == kBroadcastAddress
        && (request.functionCode == kWriteSingleRegisterFunction
            || request.functionCode == kWriteMultipleRegistersFunction);

    // 1. NoResponse: broadcast-capable request -> the protocol itself says
    //    "no response expected" (never Pending/Timeout). Everything else
    //    keeps the shared threshold semantics.
    if (std::holds_alternative<NoResponse>(observation)) {
        if (isBroadcast) {
            return analyzed(TransactionStatus::ExpectedNoResponse, elapsed,
                            std::nullopt, std::nullopt, requestIssues);
        }
        return analyzed(elapsed < timeoutThreshold ? TransactionStatus::Pending
                                                   : TransactionStatus::Timeout,
                        elapsed, std::nullopt, std::nullopt, requestIssues);
    }

    // 2. Broadcast: ANY recorded bytes are an unexpected response, even a
    //    perfect-looking echo and even a corrupt wire (T015 §19).
    if (isBroadcast) {
        return analyzed(
            TransactionStatus::ProtocolError, elapsed, std::nullopt,
            makeIssue(TransactionIssueCode::UnexpectedResponseForBroadcast),
            requestIssues);
    }

    // 3. Wire-level failure (unicast). Exhaustive switch on purpose.
    if (auto* decodeError = std::get_if<RtuDecodeError>(&observation)) {
        switch (decodeError->code) {
        case RtuDecodeErrorCode::CrcMismatch:
            return analyzed(TransactionStatus::CrcError, elapsed, std::nullopt,
                            std::nullopt, requestIssues);
        case RtuDecodeErrorCode::FrameTooShort:
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::ResponseFrameTooShort),
                requestIssues);
        }
        return analyzed(
            TransactionStatus::ProtocolError, elapsed, std::nullopt,
            makeIssue(TransactionIssueCode::UnknownProtocolError), requestIssues);
    }

    // 4. A decoded frame.
    const auto& response = std::get<ModbusRtuFrame>(observation);

    if (response.address != request.address) {
        auto issue = makeIssue(TransactionIssueCode::ResponseAddressMismatch);
        issue.expectedAddress = request.address;
        issue.actualAddress = response.address;
        return analyzed(TransactionStatus::ProtocolError, elapsed, std::nullopt,
                        std::move(issue), requestIssues);
    }

    // 5. Generic exception path — written ONCE for every function code.
    //    Boundary guard (T015 semantic audit): only a request function
    //    WITHOUT the exception bit can be answered by (fn | 0x80).
    const bool requestHasExceptionBit =
        (request.functionCode & kExceptionBit) != 0;
    if (!requestHasExceptionBit
        && response.functionCode
            == static_cast<std::uint8_t>(request.functionCode | kExceptionBit)) {
        if (response.data.size() != 1) {
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::MalformedExceptionResponse),
                requestIssues);
        }
        return analyzed(TransactionStatus::Exception, elapsed, response.data[0],
                        std::nullopt, requestIssues);
    }

    if (request.functionCode == kReadHoldingRegistersFunction
        && response.functionCode == kReadHoldingRegistersFunction) {
        if (requestIssues.empty()) {
            // Valid FC03 request: reuse T007 verbatim — single source of
            // truth for the whole FC03 pair (T014 issue semantics included).
            return AnalyzedObservedTransaction{
                .analysis = analyzeFunction03Transaction(
                    request, observation, elapsed, timeoutThreshold),
                .requestIssues = {},
            };
        }
        // Semantic-invalid FC03 request: the response side is still
        // classified, but the invalid request must not be trusted for the
        // cross-frame quantity expectation either.
        const auto responseDecode = decodeReadHoldingRegistersResponse(response);
        if (std::holds_alternative<Function03DecodeError>(responseDecode)) {
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::MalformedNormalResponse),
                requestIssues);
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
            return analyzed(TransactionStatus::ProtocolError, elapsed,
                            std::nullopt, std::move(issue), requestIssues);
        }
        return analyzed(TransactionStatus::Success, elapsed, std::nullopt,
                        std::nullopt, requestIssues);
    }

    if (request.functionCode == kWriteSingleRegisterFunction
        && response.functionCode == kWriteSingleRegisterFunction) {
        const auto responseDecode = decodeWriteSingleRegisterResponse(response);
        if (std::holds_alternative<Function06DecodeError>(responseDecode)) {
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::MalformedNormalResponse),
                requestIssues);
        }
        const auto requestDecode = decodeWriteSingleRegisterRequest(request);
        if (std::holds_alternative<Function06DecodeError>(requestDecode)) {
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::UnknownProtocolError),
                requestIssues);
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
            return analyzed(TransactionStatus::ProtocolError, elapsed,
                            std::nullopt, std::move(issue), requestIssues);
        }
        return analyzed(TransactionStatus::Success, elapsed, std::nullopt,
                        std::nullopt, requestIssues);
    }

    if (request.functionCode == kWriteMultipleRegistersFunction
        && response.functionCode == kWriteMultipleRegistersFunction) {
        // Function 0x10 normal semantics (T015 Part C). Format errors stay
        // MalformedNormalResponse; a well-formed reply whose start address or
        // written quantity disagrees with the request is an ECHO-CONTRACT
        // mismatch, never a malformed reply.
        const auto responseDecode = decodeWriteMultipleRegistersResponse(response);
        if (std::holds_alternative<Function16DecodeError>(responseDecode)) {
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::MalformedNormalResponse),
                requestIssues);
        }
        const auto requestFields = readWriteMultipleRegistersFields(request);
        if (!requestFields.has_value()) {
            // Request header not readable: no echo comparison possible.
            return analyzed(
                TransactionStatus::ProtocolError, elapsed, std::nullopt,
                makeIssue(TransactionIssueCode::UnknownProtocolError),
                requestIssues);
        }
        const auto& responseModel =
            std::get<WriteMultipleRegistersResponse>(responseDecode);
        if (requestFields->startingAddress != responseModel.startingAddress
            || requestFields->quantity != responseModel.quantityWritten) {
            auto issue = makeIssue(
                TransactionIssueCode::WriteMultipleRegistersEchoMismatch);
            issue.expectedRegisterAddress = requestFields->startingAddress;
            issue.actualRegisterAddress = responseModel.startingAddress;
            issue.expectedQuantity = requestFields->quantity;
            issue.actualQuantity = responseModel.quantityWritten;
            return analyzed(TransactionStatus::ProtocolError, elapsed,
                            std::nullopt, std::move(issue), requestIssues);
        }
        // Orthogonality: a matching normal reply is Success even when the
        // request carried invalid semantics — those facts ride in
        // requestIssues, never in the response-side status.
        return analyzed(TransactionStatus::Success, elapsed, std::nullopt,
                        std::nullopt, requestIssues);
    }

    // 6. The response answers the request function, but ModbusLens has no
    //    normal semantics for it yet (FC08, 0x04, Function 0x10 until
    //    Part C wires it, ...). That is an explicit per-record "unsupported"
    //    fact — NOT an invalid request and NOT a TransactionStatus.
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
                        std::move(issue), requestIssues);
    }
}

} // namespace modbuslens::core