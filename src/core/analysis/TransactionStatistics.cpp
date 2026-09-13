#include "core/analysis/TransactionStatistics.h"

namespace modbuslens::core {

TransactionStatisticsSnapshot summarizeTransactions(
    std::span<const TransactionAnalysis> transactions)
{
    TransactionStatisticsSnapshot snapshot{};
    snapshot.observedCount = transactions.size();

    // Integer milliseconds accumulate exactly; only the average converts to
    // double (implementation rule A).
    std::int64_t successLatencyTotalMs = 0;

    for (const auto& transaction : transactions) {
        // Exhaustive switch, no default: a new TransactionStatus trips
        // -Wswitch and forces this aggregation to be extended explicitly.
        switch (transaction.status) {
        case TransactionStatus::Pending:
            ++snapshot.pendingCount;
            break;
        case TransactionStatus::Success:
            ++snapshot.successCount;
            successLatencyTotalMs += transaction.elapsed.count();
            break;
        case TransactionStatus::Exception:
            ++snapshot.exceptionCount;
            break;
        case TransactionStatus::CrcError:
            ++snapshot.crcErrorCount;
            break;
        case TransactionStatus::Timeout:
            ++snapshot.timeoutCount;
            break;
        case TransactionStatus::ProtocolError:
            ++snapshot.protocolErrorCount;
            break;
        case TransactionStatus::ExpectedNoResponse:
            // T015: a completed observation, but never a success and never a
            // failure (broadcast has no response by protocol).
            ++snapshot.expectedNoResponseCount;
            break;
        }
    }

    // Invariant B by construction: completed = sum of the six final states
    // (and with observedCount set above, invariant A holds automatically).
    snapshot.completedCount = snapshot.successCount + snapshot.exceptionCount
        + snapshot.crcErrorCount + snapshot.timeoutCount
        + snapshot.protocolErrorCount + snapshot.expectedNoResponseCount;

    // Invariant C (T015/ADR-003 approved formula): the rate is computed over
    // rate-eligible completed transactions only — a legal broadcast must not
    // dilute the success rate; "no eligible completed transaction" is
    // nullopt, never a fabricated 0%.
    const std::size_t rateEligibleCompleted =
        snapshot.completedCount - snapshot.expectedNoResponseCount;
    if (rateEligibleCompleted > 0) {
        snapshot.successRate = static_cast<double>(snapshot.successCount)
            / static_cast<double>(rateEligibleCompleted);
    }

    // Invariant D: latency averages only successful transactions; a
    // zero-success batch has no data, not a zero average.
    if (snapshot.successCount > 0) {
        snapshot.averageSuccessLatencyMs =
            static_cast<double>(successLatencyTotalMs)
            / static_cast<double>(snapshot.successCount);
    }

    return snapshot;
}

} // namespace modbuslens::core