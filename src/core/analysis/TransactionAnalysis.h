#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>

#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/protocol/ModbusRtuFrame.h"

namespace modbuslens::core {

enum class TransactionStatus {
    Pending,       // no response yet, elapsed < timeoutThreshold
    Success,       // legal, matching normal response
    Exception,     // legal, matching Modbus exception response
    CrcError,      // wire arrived but decode failed with CrcMismatch
    Timeout,       // no response and elapsed >= timeoutThreshold
    ProtocolError, // data arrived but cannot be a valid match for this request
};

// A transaction is a request plus its response/failure observation — never a
// single frame. NoResponse is deliberately decoupled from T006's
// DroppedResponse: the analyzer speaks the abstract observation language.
struct NoResponse {
    bool operator==(const NoResponse&) const = default;
};

using ResponseObservation = std::variant<ModbusRtuFrame, RtuDecodeError, NoResponse>;

struct TransactionAnalysis {
    TransactionStatus status{};
    std::chrono::milliseconds elapsed{0};
    std::optional<std::uint8_t> exceptionCode;   // set only for Exception

    bool operator==(const TransactionAnalysis&) const = default;
};

// Analyze one Function 0x03 transaction. Pure function: elapsed and the
// timeout threshold are caller-provided facts — no clock, no waiting. The
// request contract is "an already-validated Function 0x03 request".
TransactionAnalysis analyzeFunction03Transaction(
    const ModbusRtuFrame& request,
    const ResponseObservation& observation,
    std::chrono::milliseconds elapsed,
    std::chrono::milliseconds timeoutThreshold);

} // namespace modbuslens::core