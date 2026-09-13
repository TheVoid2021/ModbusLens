#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>

#include "core/analysis/TransactionAnalysis.h"

namespace modbuslens::core {

// Pure batch -> snapshot aggregation over Part A results. No mutable state,
// no history, no manager: the same batch always yields the same snapshot.
// Invariants (locked by tests):
//   A: observedCount == pendingCount + completedCount
//   B: completedCount == success + exception + crcError + timeout
//                       + protocolError + expectedNoResponse
//   C: successRate has a value  <=>  rateEligibleCompleted > 0, where
//      rateEligibleCompleted = completedCount - expectedNoResponseCount
//      (T015/ADR-003: a legal broadcast must not dilute the success rate)
//   D: averageSuccessLatencyMs has a value  <=>  successCount > 0
struct TransactionStatisticsSnapshot {
    std::size_t observedCount{};
    std::size_t pendingCount{};
    std::size_t completedCount{};

    std::size_t successCount{};
    std::size_t exceptionCount{};
    std::size_t crcErrorCount{};
    std::size_t timeoutCount{};
    std::size_t protocolErrorCount{};
    // T015 Gate C: broadcast transactions are completed OBSERVATIONS but are
    // excluded from the success-rate denominator (they cannot be "successful"
    // and must not be counted as failures either).
    std::size_t expectedNoResponseCount{};

    std::optional<double> successRate;
    std::optional<double> averageSuccessLatencyMs;

    bool operator==(const TransactionStatisticsSnapshot&) const = default;
};

// Aggregates a batch of Part A analyses into one snapshot. Success rate is
// successCount / completedCount (Pending never enters the denominator);
// latency averages only successful transactions' elapsed, accumulated in
// integer milliseconds.
TransactionStatisticsSnapshot summarizeTransactions(
    std::span<const TransactionAnalysis> transactions);

} // namespace modbuslens::core