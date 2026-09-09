#pragma once

#include <cstdint>
#include <vector>

#include "core/analysis/TransactionStatistics.h"
#include "core/diagnosis/DiagnosisContext.h"

namespace modbuslens::agent {

// Immutable per-run snapshot (T012 R3): built ONCE when an Agent run starts
// from the current active deterministic batch. The dispatcher receives a
// const& to this and every tool reads ONLY this snapshot — a live
// Controller/UI state change can never make tool #1 and tool #2 of the same
// run disagree about which batch they are answering for.
//
// Runtime stale-guarding (is this snapshot's batch still the active one?)
// is a PART B concern (activeBatchRevision guard); Part A owns only the
// "single run = self-consistent facts" property.
struct AgentToolContext {
    std::vector<modbuslens::core::DiagnosisTransaction> transactions;
    modbuslens::core::TransactionStatisticsSnapshot statistics;
    std::uint64_t capturedBatchRevision{};

    bool operator==(const AgentToolContext&) const = default;
};

} // namespace modbuslens::agent