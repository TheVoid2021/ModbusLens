#include "core/serial/SerialTransactionSession.h"

#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"

namespace modbuslens::core {

namespace {

constexpr std::uint8_t kMinSlaveAddress = 1;
constexpr std::uint8_t kMaxSlaveAddress = 247; // 0=broadcast, 248+ reserved

} // namespace

SerialStartResult SerialTransactionSession::beginReadHoldingRegisters(
    std::uint8_t slaveAddress,
    std::uint16_t startAddress,
    std::uint16_t quantity,
    std::chrono::milliseconds timeoutThreshold)
{
    // One-outstanding rule: a pending transaction stays 100% intact.
    if (state_ != SerialTransactionState::Idle) {
        return SerialTransactionError{SerialTransactionErrorCode::Busy};
    }
    // Unicast-only: FC03 requires a reply, broadcast has no reply semantics.
    if (slaveAddress < kMinSlaveAddress || slaveAddress > kMaxSlaveAddress) {
        return SerialTransactionError{SerialTransactionErrorCode::InvalidAddress};
    }

    // Trusted request: built by the semantic encoder (quantity validated
    // there), wire + CRC by the RTU codec. Build every local BEFORE mutating
    // any member so a failure never leaves a half-initialized state.
    const auto encoded =
        encodeReadHoldingRegistersRequest(slaveAddress, startAddress, quantity);
    if (std::get_if<Function03EncodeError>(&encoded) != nullptr) {
        return SerialTransactionError{SerialTransactionErrorCode::InvalidQuantity};
    }
    const auto requestFrame = std::get<ModbusRtuFrame>(encoded);
    auto requestWire = encodeRtuFrame(requestFrame);

    request_ = requestFrame;
    timeoutThreshold_ = timeoutThreshold;
    buffer_.clear();
    state_ = SerialTransactionState::AwaitingResponse;
    return SerialRequestStart{
        .requestFrame = requestFrame,
        .requestWire = std::move(requestWire),
    };
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
        // No bytes at all: "no response" — T007 decides Timeout vs Pending
        // from elapsed/threshold.
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
    // from the request quantity (A16: the mismatch is T007's verdict).
    if (function == 0x03) {
        if (buffer_.size() < 3) {
            return std::nullopt; // byteCount not arrived yet
        }
        return static_cast<std::size_t>(5) + buffer_[2];
    }
    // Any other normal function code: v1 does not invent length parsers for
    // them. Keep accumulating until the timeout closes the transaction over
    // the whole buffer.
    return std::nullopt;
}

void SerialTransactionSession::resetToIdle()
{
    buffer_.clear();
    request_ = ModbusRtuFrame{};
    state_ = SerialTransactionState::Idle;
}

void SerialTransactionSession::analyzeAndReset(
    const ResponseObservation& observation,
    std::chrono::milliseconds elapsed,
    TransactionAnalysis& out)
{
    // Reuse the existing analyzer — the session never re-derives statuses.
    out = analyzeFunction03Transaction(request_, observation, elapsed,
                                       timeoutThreshold_);
    resetToIdle();
}

} // namespace modbuslens::core