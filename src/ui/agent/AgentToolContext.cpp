#include "ui/agent/AgentToolContext.h"

#include "core/diagnosis/DiagnosisContext.h"

namespace modbuslens::agent {

AgentToolContext makeAgentToolContext(
    std::span<const modbuslens::core::DiagnosisTransaction> transactions,
    std::uint64_t capturedBatchRevision)
{
    // P0 self-consistency contract: statistics are re-derived from the SAME
    // copied transactions by the canonical summarizer — a snapshot can
    // never mix transactions of one batch with statistics of another.
    const auto coreContext = modbuslens::core::buildDiagnosisContext(transactions);
    return AgentToolContext{
        .transactions = coreContext.transactions,
        .statistics = coreContext.statistics,
        .capturedBatchRevision = capturedBatchRevision,
    };
}

} // namespace modbuslens::agent
