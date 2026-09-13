#pragma once

#include <cstdint>
#include <variant>
#include <vector>

#include "core/analysis/PassiveTransactionAnalysis.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/analysis/TransactionStatistics.h"
#include "core/replay/ReplayLog.h"

namespace modbuslens::core {

// Why a replay batch can fail BEFORE any per-record analysis: T015 Gate E
// (Scope A) keeps the OLD contract for corrupted request WIRES only — a
// record whose request cannot even be RTU-decoded cannot be classified as a
// transaction, and per-record wire-corruption observation is explicitly
// deferred (conceptually it IS a captured anomaly, but interpreting corrupt
// requests is its own design problem). Everything AFTER a valid RTU request
// (semantic-invalid / supported / unsupported) is per-record and can never
// poison the rest of the batch.
enum class ReplayExecutionErrorCode {
    InvalidRequestWire,     // decodeRtuFrame failed (CRC / too short)
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
    // T015: request-side fact copied from the passive analyzer (nullopt when
    // the captured request was valid for its function).
    std::optional<TransactionRequestIssue> requestIssue;

    bool operator==(const ReplayTransactionOutcome&) const = default;
};

struct ReplayBatchAnalysis {
    // Analyzed records only (these feed TransactionStatisticsSnapshot).
    std::vector<ReplayTransactionOutcome> transactions;
    // T015 Gate F: valid-wire records whose NORMAL semantics are not
    // supported yet. They are neither invalid nor hidden: they are kept as
    // explicit per-record facts so no log line is silently dropped.
    std::vector<UnsupportedObservedTransaction> unsupportedRecords;
    // Statistics are computed ONLY from the analyzed subset above.
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