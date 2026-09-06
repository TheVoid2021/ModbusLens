#include "ui/AnalysisController.h"

#include "core/analysis/TransactionStatistics.h"

namespace {

// Bounded display contract: the UI adapter narrows core size_t counters to
// int for QML. Demo/history batches are far below INT_MAX; core stays size_t.
int toInt(std::size_t value)
{
    return static_cast<int>(value);
}

} // namespace

AnalysisController::AnalysisController(QObject* parent)
    : QObject(parent)
{
    // Same source of truth as T007: an empty batch produces the zeroed
    // snapshot with undefined successRate/latency (hasX == false).
    applySnapshot(summarizeTransactions(
        std::span<const modbuslens::core::TransactionAnalysis>{}));
}

int AnalysisController::observedCount() const
{
    return toInt(statistics_.observedCount);
}

int AnalysisController::pendingCount() const
{
    return toInt(statistics_.pendingCount);
}

int AnalysisController::completedCount() const
{
    return toInt(statistics_.completedCount);
}

int AnalysisController::successCount() const
{
    return toInt(statistics_.successCount);
}

int AnalysisController::exceptionCount() const
{
    return toInt(statistics_.exceptionCount);
}

int AnalysisController::crcErrorCount() const
{
    return toInt(statistics_.crcErrorCount);
}

int AnalysisController::timeoutCount() const
{
    return toInt(statistics_.timeoutCount);
}

int AnalysisController::protocolErrorCount() const
{
    return toInt(statistics_.protocolErrorCount);
}

bool AnalysisController::hasSuccessRate() const
{
    return statistics_.successRate.has_value();
}

double AnalysisController::successRate() const
{
    // 0.0 is only a safe placeholder when hasSuccessRate() is false — QML
    // must branch on hasSuccessRate first (never show "0%" for no data).
    return statistics_.successRate.value_or(0.0);
}

bool AnalysisController::hasAverageSuccessLatency() const
{
    return statistics_.averageSuccessLatencyMs.has_value();
}

double AnalysisController::averageSuccessLatencyMs() const
{
    return statistics_.averageSuccessLatencyMs.value_or(0.0);
}

QAbstractItemModel* AnalysisController::transactionModel()
{
    return &transactionModel_;
}

void AnalysisController::applySnapshot(
    const modbuslens::core::TransactionStatisticsSnapshot& snapshot)
{
    statistics_ = snapshot;
    emit statisticsChanged();
}

void AnalysisController::setTransactionEntries(
    std::vector<TransactionListEntry> entries)
{
    transactionModel_.setEntries(std::move(entries));
}