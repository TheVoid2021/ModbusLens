#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "core/analysis/TransactionAnalysis.h"
#include "core/analysis/TransactionStatistics.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// Diagnosis view of ONE transaction. Deliberately a pure struct: no
// StatusText, no QString, no color, no sourceLabel — the deterministic core
// speaks only in protocol facts.
// ---------------------------------------------------------------------------
struct DiagnosisTransaction {
    std::uint8_t deviceAddress{};
    std::uint8_t functionCode{};
    TransactionAnalysis analysis;

    bool operator==(const DiagnosisTransaction&) const = default;
};

// Self-contained, independently testable input snapshot for the diagnosis
// layer: the transactions AND the statistics derived from exactly those
// transactions (never from the UI model, never hand-counted).
struct DiagnosisContext {
    std::vector<DiagnosisTransaction> transactions;
    TransactionStatisticsSnapshot statistics;

    bool operator==(const DiagnosisContext&) const = default;
};

// Builder: owns a copy of the transactions, extracts the TransactionAnalysis
// values and derives statistics with the ONE canonical aggregation rule
// (summarizeTransactions). Guarantees context self-consistency:
//   context.statistics == summarizeTransactions(analyses)
DiagnosisContext buildDiagnosisContext(
    std::span<const DiagnosisTransaction> transactions);

} // namespace modbuslens::core