#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// Serial Transaction Session (T010 Part A; M10-A generalization).
//
// Pure C++20, Zero Qt. Owns ONE active request-response transaction over a
// serial byte stream:
//
//   Idle -> begin(descriptor) -> AwaitingResponse
//        -> feedResponseBytes(...)  (arbitrary chunks; exact candidate closes)
//        -> onResponseTimeout(...)  (no/partial/oversized bytes)
//        -> Idle (cancel() aborts locally with no Modbus verdict)
//
// M10-A made the lifecycle function-GENERIC: the session stores the whole
// send-time REQUEST DESCRIPTOR (unified intent + semantic frame + exact wire)
// and dispatches response meaning through a function-specific analyzer seam.
// It deliberately does NOT grow one state machine per function: there is no
// SerialWrite06Session / SerialWrite10Session and there never will be.
//
// What it still must NOT know: COM ports, baud rates, timers, QObject, QML
// drafts. Timing facts (elapsed) arrive as plain milliseconds from the
// transport; transmission facts (whether bytes may have reached the wire)
// belong to the transport, not here.
// ---------------------------------------------------------------------------

enum class SerialTransactionState {
    Idle,             // no transaction in flight (also before the first begin)
    AwaitingResponse, // request accepted; accumulating response bytes
};

enum class SerialTransactionErrorCode {
    Busy,            // begin while a transaction is already AwaitingResponse
    InvalidAddress,  // unit id outside the unicast range 1..247
    InvalidQuantity, // quantity outside the function's legal domain
    NotActive,       // onResponseTimeout called with no transaction pending
    InvalidTimeout,  // intent timeout must be positive
    // The function is representable in the unified intent but has no ACTIVE
    // analyzer in this milestone (0x06 / 0x10 = M10-D/E). Rejected at begin,
    // before any send: an honest "not implemented yet", never a fabricated
    // Modbus verdict.
    UnsupportedFunction,
    // Descriptor inconsistent with its own intent (frame/wire disagree with
    // the intent, or the wire bytes are empty/undecodable).
    InvalidRequestDescriptor,
};

struct SerialTransactionError {
    SerialTransactionErrorCode code{};

    bool operator==(const SerialTransactionError&) const = default;
};

// Start result: the accepted send-time descriptor (semantic frame + exact
// wire bytes + intent snapshot). The transport must write `wire` as-is —
// never re-encode, never re-read a UI draft.
using SerialStartResult =
    std::variant<ActiveRequestDescriptor, SerialTransactionError>;

// "Not a complete candidate yet — wait for more bytes (or the timeout)".
struct AwaitingMoreData {
    bool operator==(const AwaitingMoreData&) const = default;
};

using SerialFeedResult = std::variant<AwaitingMoreData, TransactionAnalysis>;
using SerialTimeoutResult =
    std::variant<TransactionAnalysis, SerialTransactionError>;

class SerialTransactionSession
{
public:
    // Begin one transaction from an already-encoded descriptor. The timeout
    // threshold and the request identity both come from descriptor.intent —
    // a single authority, so a late response is matched against the SEND-TIME
    // snapshot and never against anything the caller may have edited since.
    //
    // On success the session becomes AwaitingResponse with a cleared receive
    // buffer and returns the descriptor; on failure the state is left
    // untouched (Busy keeps the in-flight transaction fully intact;
    // validation/descriptor errors change nothing at all).
    SerialStartResult beginActiveRequest(const ActiveRequestDescriptor& request);

    // Thin Function 0x03 convenience entry kept for the existing callers and
    // regression suite: builds the unified intent, encodes it and delegates to
    // beginActiveRequest (identical failure modes).
    SerialStartResult beginReadHoldingRegisters(
        std::uint8_t slaveAddress,
        std::uint16_t startAddress,
        std::uint16_t quantity,
        std::chrono::milliseconds timeoutThreshold);

    // Accumulate an arbitrary chunk of response bytes and, once a complete
    // wire candidate has formed, decode it (existing codec), analyze it
    // (existing analyzer) and reset to Idle. Returns AwaitingMoreData while
    // the buffer has not reached a candidate boundary — including the
    // oversized case, which is NEVER truncated: it waits for the timeout to
    // close the transaction over the entire buffer.
    SerialFeedResult feedResponseBytes(
        std::span<const std::uint8_t> bytes,
        std::chrono::milliseconds elapsed);

    // Close the transaction at response timeout. Empty buffer => NoResponse
    // (the analyzer decides Timeout vs Pending from elapsed/threshold).
    // Non-empty (partial or oversized) => decode the ENTIRE buffer and let
    // the analyzer produce the wire-truth diagnosis (CrcError/ProtocolError...)
    // — partial bytes are NOT labeled Timeout. Returns NotActive when no
    // transaction is pending. Always resets to Idle on the active path.
    SerialTimeoutResult onResponseTimeout(std::chrono::milliseconds elapsed);

    // Abort the pending transaction without producing any TransactionStatus:
    // transport disconnects are local transport facts, not Modbus diagnoses.
    void cancel();

    [[nodiscard]] SerialTransactionState state() const;

    // Send-time request snapshot (nullopt while Idle). The transport attaches
    // it to the completion envelope so a result can never be published
    // without the request it actually answered.
    [[nodiscard]] std::optional<ActiveRequestDescriptor> pendingRequest() const;

private:
    // Candidate length from the framing rules (T010 design):
    //   buffer[1] & 0x80        -> 5       (any exception-format reply)
    //   buffer[1] == 0x03       -> 5 + buffer[2]  (needs >=3 bytes first)
    //   other normal functions  -> none   (unknown length: wait for timeout)
    [[nodiscard]] std::optional<std::size_t> candidateFrameLength() const;

    void resetToIdle();

    void analyzeAndReset(const ResponseObservation& observation,
                         std::chrono::milliseconds elapsed,
                         TransactionAnalysis& out);

    SerialTransactionState state_ = SerialTransactionState::Idle;
    std::vector<std::uint8_t> buffer_;
    std::optional<ActiveRequestDescriptor> pending_;
    std::chrono::milliseconds timeoutThreshold_{1000};
};

// Function-specific ACTIVE response semantics: the single dispatch point of
// the generic lifecycle. Implemented for Function 0x03 (T007 analyzer);
// 0x06 / 0x10 are rejected at begin, so they cannot reach here today.
TransactionAnalysis analyzeActiveResponse(
    const ActiveRequestDescriptor& request,
    const ResponseObservation& observation,
    std::chrono::milliseconds elapsed,
    std::chrono::milliseconds timeoutThreshold);

// True when an active response analyzer exists for this function in the
// current milestone (the session's begin gate uses it).
[[nodiscard]] bool activeFunctionSupported(ActiveFunction function);

} // namespace modbuslens::core