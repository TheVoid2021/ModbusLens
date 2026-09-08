#include "core/diagnosis/DiagnosisContext.h"

namespace modbuslens::core {

DiagnosisContext buildDiagnosisContext(
    std::span<const DiagnosisTransaction> transactions)
{
    DiagnosisContext context;
    context.transactions.assign(transactions.begin(), transactions.end());

    // Statistics come from EXACTLY the transactions in this context, through
    // the one canonical aggregation (T007) — never hand-counted, never read
    // back from the UI model.
    std::vector<TransactionAnalysis> analyses;
    analyses.reserve(transactions.size());
    for (const auto& transaction : transactions) {
        analyses.push_back(transaction.analysis);
    }
    context.statistics = summarizeTransactions(analyses);
    return context;
}

} // namespace modbuslens::core