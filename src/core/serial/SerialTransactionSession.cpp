#include "core/serial/SerialTransactionSession.h"

#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"

namespace modbuslens::core {

namespace {

// Local (pre-send) intent validation -> session error vocabulary. These are
// NOT Modbus diagnoses: they are refused before any byte leaves the process.
std::optional<SerialTransactionErrorCode> mapValidationError(
    std::optional<ActiveRequestValidationError> error)
{
    if (!error.has_value()) {
        return std::nullopt;
    }
    switch (*error) {
    case ActiveRequestValidationError::UnitIdNotUnicast:
        return SerialTransactionErrorCode::InvalidAddress;
    case ActiveRequestValidationError::QuantityOutOfRange:
        return SerialTransactionErrorCode::InvalidQuantity;
    case ActiveRequestValidationError::TimeoutNotPositive:
        return SerialTransactionErrorCode::InvalidTimeout;
    case ActiveRequestValidationError::PayloadFunctionMismatch:
        return SerialTransactionErrorCode::InvalidRequestDescriptor;
    }
    return SerialTransactionErrorCode::InvalidRequestDescriptor;
}

// A descriptor must be self-consistent: the semantic frame must describe the
// same request as the intent, and the wire bytes must be exactly that frame's
// encoding. This is what makes "the bytes handed to the transport ARE the
// encoded intent" a property of the type system instead of a convention.
bool descriptorIsConsistent(const ActiveRequestDescriptor& request)
{
    if (request.wire.empty()) {
        return false;
    }
    if (request.frame.address != request.intent.unitId) {
        return false;
    }
    if (request.frame.functionCode != activeFunctionCode(request.intent.function)) {
        return false;
    }
    const auto decoded = decodeRtuFrame(request.wire);
    const auto* frame = std::get_if<ModbusRtuFrame>(&decoded);
    return frame != nullptr && *frame == request.frame;
}

} // namespace

bool activeFunctionSupported(ActiveFunction function)
{
    // M10-D2: two active analyzers are wired — Function 0x03 and Function 0x06
    // (its framing rule, shared echo analyzer and session tests all exist).
    // 0x10 remains refused before any send: it has a response-shape
    // RECOGNITION rule for framing, but no encoder and no active analyzer.
    return function == ActiveFunction::ReadHoldingRegisters
           || function == ActiveFunction::WriteSingleRegister;
}

SerialStartResult SerialTransactionSession::beginActiveRequest(
    const ActiveRequestDescriptor& request)
{
    // One-outstanding rule: a pending transaction stays 100% intact.
    if (state_ != SerialTransactionState::Idle) {
        return SerialTransactionError{SerialTransactionErrorCode::Busy};
    }

    // Reuse the shared validation (single source for both the Controller and
    // the session) — never re-derive ranges here.
    if (const auto error = mapValidationError(
            validateActiveRequestIntent(request.intent));
        error.has_value()) {
        return SerialTransactionError{*error};
    }

    if (!activeFunctionSupported(request.intent.function)) {
        return SerialTransactionError{
            SerialTransactionErrorCode::UnsupportedFunction};
    }
    if (!descriptorIsConsistent(request)) {
        return SerialTransactionError{
            SerialTransactionErrorCode::InvalidRequestDescriptor};
    }

    // Commit: everything local first, a single mutation point, no half state.
    pending_ = request;
    timeoutThreshold_ = request.intent.timeout;
    buffer_.clear();
    state_ = SerialTransactionState::AwaitingResponse;
    return request;
}

SerialStartResult SerialTransactionSession::beginReadHoldingRegisters(
    std::uint8_t slaveAddress,
    std::uint16_t startAddress,
    std::uint16_t quantity,
    std::chrono::milliseconds timeoutThreshold)
{
    // Thin FC03 convenience: the unified intent is the only request model, so
    // this path cannot drift from the generic one.
    const ActiveRequestIntent intent{
        .function = ActiveFunction::ReadHoldingRegisters,
        .unitId = slaveAddress,
        .timeout = timeoutThreshold,
        .payload = ReadHoldingRegistersIntent{
            .startAddress = startAddress,
            .quantity = quantity,
        },
    };
    // Local validation first so the historical error codes survive verbatim
    // (InvalidAddress / InvalidQuantity) before the encoder adds its own.
    if (const auto error = mapValidationError(validateActiveRequestIntent(intent));
        error.has_value()) {
        return SerialTransactionError{*error};
    }
    const auto encoded = encodeActiveRequest(intent);
    if (std::get_if<ActiveRequestEncodeError>(&encoded) != nullptr) {
        return SerialTransactionError{SerialTransactionErrorCode::InvalidQuantity};
    }
    return beginActiveRequest(std::get<ActiveRequestDescriptor>(encoded));
}

SerialFeedResult SerialTransactionSession::feedResponseBytes(
    std::span<const std::uint8_t> bytes,
    std::chrono::milliseconds elapsed)
{
    // Bytes fed without a transaction are ignored rather than queued: the
    // session models one transaction at a time, and stale bytes are a
    // transport-context artifact, not a Modbus fact.
    if (state_ != SerialTransactionState::AwaitingResponse) {
        return AwaitingMoreData{};
    }

    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());

    // "Exact candidate" rule: complete ONLY at the exact boundary. An
    // oversized buffer is never truncated into a Success — it waits for the
    // timeout to decode the entire buffer and surface the trailing garbage
    // as a real wire diagnosis.
    const auto candidate = candidateFrameLength();
    if (!candidate.has_value() || buffer_.size() != *candidate) {
        return AwaitingMoreData{};
    }

    const auto decodeResult = decodeRtuFrame(buffer_);
    ResponseObservation observation;
    if (const auto* frame = std::get_if<ModbusRtuFrame>(&decodeResult)) {
        observation = *frame;
    } else {
        observation = std::get<RtuDecodeError>(decodeResult);
    }

    TransactionAnalysis analysis{};
    analyzeAndReset(observation, elapsed, analysis);
    return SerialFeedResult{analysis};
}

SerialTimeoutResult SerialTransactionSession::onResponseTimeout(
    std::chrono::milliseconds elapsed)
{
    if (state_ != SerialTransactionState::AwaitingResponse) {
        return SerialTransactionError{SerialTransactionErrorCode::NotActive};
    }

    TransactionAnalysis analysis{};
    if (buffer_.empty()) {
        // No bytes at all: "no response" — the analyzer decides Timeout vs
        // Pending from elapsed/threshold.
        analyzeAndReset(ResponseObservation{NoResponse{}}, elapsed, analysis);
    } else {
        // Partial or oversized bytes: the device DID answer something. Decode
        // the ENTIRE buffer and let the analyzer produce the wire-truth
        // diagnosis (FrameTooShort -> ProtocolError, garbage -> CrcError...).
        // Never labeled Timeout.
        const auto decodeResult = decodeRtuFrame(buffer_);
        ResponseObservation observation;
        if (const auto* frame = std::get_if<ModbusRtuFrame>(&decodeResult)) {
            observation = *frame;
        } else {
            observation = std::get<RtuDecodeError>(decodeResult);
        }
        analyzeAndReset(observation, elapsed, analysis);
    }
    return SerialTimeoutResult{analysis};
}

void SerialTransactionSession::cancel()
{
    // Local abort (transport disconnect/user close): NOT a Modbus
    // transaction diagnosis — no TransactionStatus is produced.
    resetToIdle();
}

SerialTransactionState SerialTransactionSession::state() const
{
    return state_;
}

std::optional<ActiveRequestDescriptor> SerialTransactionSession::pendingRequest() const
{
    return pending_;
}

std::optional<std::size_t> SerialTransactionSession::candidateFrameLength() const
{
    // T010 framing rules:
    if (buffer_.size() < 2) {
        return std::nullopt; // cannot even see the function code yet
    }
    const auto function = buffer_[1];
    // Any function with the exception bit set is an exception-FORMAT reply:
    // Address | Fn|0x80 | Code | CRC(2) = 5 bytes. Deliberately not
    // hardcoded to 0x83 — e.g. a stray 0x84 must also close at 5 bytes so
    // the analyzer can report ProtocolError instead of us waiting forever.
    if ((function & 0x80) != 0) {
        return 5;
    }
    // Normal FC03 reply: Address | 03 | ByteCount | data | CRC(2) whose
    // total length is derived from the RESPONSE's own byteCount — never
    // from the request quantity (A16: the mismatch is the analyzer's verdict).
    if (function == 0x03) {
        if (buffer_.size() < 3) {
            return std::nullopt; // byteCount not arrived yet
        }
        return static_cast<std::size_t>(5) + buffer_[2];
    }
    // Write Single Register (0x06) normal reply: Address | 06 | addr hi/lo |
    // value hi/lo | CRC(2) = 8 bytes, a FIXED size (M10-D2). The rule keys on
    // the RESPONSE function byte, never on what the request happened to be, so
    // a wrong-function reply with a known shape can still be framed and judged.
    if (function == 0x06) {
        return 8;
    }
    // Function 0x10 normal reply: Address | 10 | addr hi/lo | qty hi/lo |
    // CRC(2) = 8 bytes. This is KNOWN-RESPONSE-SHAPE RECOGNITION ONLY: it lets
    // such a reply be framed for the analyzer to reject as an unexpected
    // function. It grants 0x10 no encoder, no active analyzer and no dispatch.
    if (function == 0x10) {
        return 8;
    }
    // Any other normal function code: v1 does not invent length parsers for
    // them (a KNOWN LIMITATION: without a reliable rule the bytes accumulate
    // until the timeout, which then decodes the whole buffer). Never guess a
    // length just to produce a nicer status.
    return std::nullopt;
}

void SerialTransactionSession::resetToIdle()
{
    buffer_.clear();
    pending_.reset();
    state_ = SerialTransactionState::Idle;
}

void SerialTransactionSession::analyzeAndReset(
    const ResponseObservation& observation,
    std::chrono::milliseconds elapsed,
    TransactionAnalysis& out)
{
    // The response verdict comes from the shared analyzer — the session never
    // re-derives statuses, and it dispatches on the SEND-TIME snapshot.
    if (pending_.has_value()) {
        out = analyzeActiveResponse(*pending_, observation, elapsed, timeoutThreshold_);
    }
    resetToIdle();
}

TransactionAnalysis analyzeActiveResponse(
    const ActiveRequestDescriptor& request,
    const ResponseObservation& observation,
    std::chrono::milliseconds elapsed,
    std::chrono::milliseconds timeoutThreshold)
{
    switch (request.intent.function) {
    case ActiveFunction::ReadHoldingRegisters:
        // Trusted-request contract of the active path: the request was
        // validated and encoded locally, so T007's analyzer is the authority.
        return analyzeFunction03Transaction(
            request.frame, observation, elapsed, timeoutThreshold);
    case ActiveFunction::WriteSingleRegister:
        // M10-D2: the active 0x06 path reuses the SAME core analyzer the
        // passive path uses, with the send-time request frame as the trusted
        // request. Response validation (unit, function, echo, CRC, exception,
        // timeout) is therefore complete and identical on both paths.
        return analyzeWriteSingleRegisterTransaction(
            request.frame, observation, elapsed, timeoutThreshold);
    case ActiveFunction::WriteMultipleRegisters:
        // Unreachable: beginActiveRequest refuses unsupported functions
        // before any send. Kept as a deterministic defensive branch (the same
        // discipline as TransactionIssueCode::UnknownProtocolError) instead
        // of inventing a verdict for a function nobody can send yet.
        break;
    }

    TransactionIssue defensiveIssue{};
    defensiveIssue.code = TransactionIssueCode::UnknownProtocolError;
    return TransactionAnalysis{
        .status = TransactionStatus::ProtocolError,
        .elapsed = elapsed,
        .exceptionCode = std::nullopt,
        .issue = defensiveIssue,
    };
}

} // namespace modbuslens::core