#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "core/analysis/TransactionAnalysis.h"
#include "core/analysis/TransactionStatistics.h"
#include "core/replay/ReplayLog.h"

namespace modbuslens::core {

// Why a replay batch can fail BEFORE transaction analysis: the T007 analyzer
// contract requires an already-trusted Function 0x03 request, so the replay
// pipeline must prove that chain first (requestWire -> RTU frame -> 0x03 ->
// valid 0x03 semantics). A response-side problem is deliberately NOT here:
// a bad response is usually the historical fault we are diagnosing, and it
// flows into the analyzer as CrcError/ProtocolError instead.
enum class ReplayExecutionErrorCode {
    InvalidRequestWire,     // decodeRtuFrame failed (CRC / too short)
    InvalidRequestFunction, // frame OK but functionCode != 0x03
    InvalidRequestData      // 0x03 semantic validation failed (quantity etc.)
};

struct ReplayExecutionError {
    ReplayExecutionErrorCode code{};
    // 0-based index into ReplayLog::transactions. Distinct from the parser's
    // 1-based physical lineNumber: this one tells the programmer which record.
    std::size_t transactionIndex{};

    bool operator==(const ReplayExecutionError&) const = default;
};

// One analyzed transaction plus the identity needed to display it (device
// address / function code). UI concerns (text, colors) stay out of Core.
struct ReplayTransactionOutcome {
    std::uint8_t deviceAddress{};
    std::uint8_t functionCode{};
    TransactionAnalysis analysis;

    bool operator==(const ReplayTransactionOutcome&) const = default;
};

struct ReplayBatchAnalysis {
    std::vector<ReplayTransactionOutcome> transactions;
    TransactionStatisticsSnapshot statistics;

    bool operator==(const ReplayBatchAnalysis&) const = default;
};

using ReplayAnalysisResult = std::variant<ReplayBatchAnalysis, ReplayExecutionError>;

// Batch / analytical replay: processes an entire ReplayLog instantaneously.
// elapsed values are historical facts passed straight to the T007 analyzer —
// no sleeping, no clock. Reuses decodeRtuFrame,
// decodeReadHoldingRegistersRequest, analyzeFunction03Transaction and
// summarizeTransactions; never touches SimulatedSlave or the fault injector.
ReplayAnalysisResult analyzeReplayLog(const ReplayLog& log);

} // namespace modbuslens::core