#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "core/analysis/TransactionAnalysis.h"
#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// Serial Transaction Session (T010 Part A): Pure C++20, Zero Qt.
//
// Owns ONE FC03 request-response transaction over a serial byte stream:
// begin -> accumulate arbitrary readyRead chunks -> frame candidate ->
// delegate to the existing codec + transaction analyzer -> Idle.
//
// It deliberately does NOT know about COM ports, baud rates or timers:
// transport state belongs to the Qt adapter (src/ui/serial/), timing facts
// (elapsed) arrive as plain milliseconds from the caller. No QObject,
// no QSerialPort, no blocking anywhere.
// ---------------------------------------------------------------------------

enum class SerialTransactionState {
    Idle,             // no transaction in flight (also before the first begin)
    AwaitingResponse, // request written; accumulating response bytes
};

enum class SerialTransactionErrorCode {
    Busy,            // begin while a transaction is already AwaitingResponse
    InvalidAddress,  // slave address outside the unicast range 1..247
    InvalidQuantity, // quantity outside 1..125 (or 0x03 encode rejection)
    NotActive,       // onResponseTimeout called with no transaction pending
};

struct SerialTransactionError {
    SerialTransactionErrorCode code{};

    bool operator==(const SerialTransactionError&) const = default;
};

// What the Qt adapter needs to transmit one request: the semantic frame
// (identity for bookkeeping/UI) plus the ready-to-write wire bytes. The
// adapter must write the wire as-is — never re-encode.
struct SerialRequestStart {
    ModbusRtuFrame requestFrame;
    std::vector<std::uint8_t> requestWire;

    bool operator==(const SerialRequestStart&) const = default;
};

using SerialStartResult =
    std::variant<SerialRequestStart, SerialTransactionError>;

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
    // Begin one FC03 read transaction. On success the session becomes
    // AwaitingResponse with a cleared receive buffer and returns the request
    // frame + wire; on failure the state is left untouched (Busy keeps the
    // in-flight transaction fully intact; validation errors change nothing).
    SerialStartResult beginReadHoldingRegisters(
        std::uint8_t slaveAddress,
        std::uint16_t startAddress,
        std::uint16_t quantity,
        std::chrono::milliseconds timeoutThreshold);

    // Accumulate an arbitrary chunk of response bytes and, once a complete
    // wire candidate has formed, decode it (existing codec), analyze it
    // (existing T007 analyzer) and reset to Idle. Returns AwaitingMoreData
    // while the buffer has not reached a candidate boundary — including the
    // oversized case, which is NEVER truncated: it waits for the timeout to
    // close the transaction over the entire buffer.
    SerialFeedResult feedResponseBytes(
        std::span<const std::uint8_t> bytes,
        std::chrono::milliseconds elapsed);

    // Close the transaction at response timeout. Empty buffer => NoResponse
    // (T007 decides Timeout vs Pending from elapsed/threshold). Non-empty
    // (partial or oversized) => decode the ENTIRE buffer and let the
    // analyzer produce the wire-truth diagnosis (CrcError/ProtocolError...)
    // — partial bytes are NOT labeled Timeout. Returns NotActive when no
    // transaction is pending. Always resets to Idle on the active path.
    SerialTimeoutResult onResponseTimeout(std::chrono::milliseconds elapsed);

    // Abort the pending transaction without producing any
    // TransactionStatus: transport disconnects are local transport facts,
    // not Modbus diagnoses.
    void cancel();

    [[nodiscard]] SerialTransactionState state() const;

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
    ModbusRtuFrame request_{};
    std::chrono::milliseconds timeoutThreshold_{1000};
};

} // namespace modbuslens::core