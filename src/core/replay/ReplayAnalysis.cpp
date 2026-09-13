#include "core/replay/ReplayAnalysis.h"

#include "core/protocol/ModbusRtuCodec.h"

namespace modbuslens::core {

ReplayAnalysisResult analyzeReplayLog(const ReplayLog& log)
{
    std::vector<ReplayTransactionOutcome> outcomes;
    std::vector<UnsupportedObservedTransaction> unsupportedRecords;
    std::vector<TransactionAnalysis> analyses;
    outcomes.reserve(log.transactions.size());
    analyses.reserve(log.transactions.size());

    for (std::size_t index = 0; index < log.transactions.size(); ++index) {
        const auto& record = log.transactions[index];

        // 1. Gate E (Scope A): a request wire that is not even a valid RTU
        //    frame keeps the old whole-batch failure contract (deferred).
        const auto requestDecode = decodeRtuFrame(record.requestWire);
        if (std::get_if<RtuDecodeError>(&requestDecode) != nullptr) {
            return ReplayExecutionError{ReplayExecutionErrorCode::InvalidRequestWire, index};
        }
        const auto& request = std::get<ModbusRtuFrame>(requestDecode);

        // 2. Response side: decode failure is NOT a replay failure — it is
        //    the historical fact being diagnosed, so it flows into the
        //    passive analyzer as CrcError / ProtocolError.
        ResponseObservation observation;
        if (!record.responseWire.has_value()) {
            observation = NoResponse{};
        } else {
            const auto responseDecode = decodeRtuFrame(*record.responseWire);
            if (const auto* error = std::get_if<RtuDecodeError>(&responseDecode)) {
                observation = *error;
            } else {
                observation = std::get<ModbusRtuFrame>(responseDecode);
            }
        }

        // 3. Gate F: every record after a valid RTU request becomes its own
        //    outcome — semantic-invalid requests and unsupported normal
        //    semantics can never poison later records.
        const auto result = analyzeObservedTransaction(
            request, observation, record.elapsed, log.timeoutThreshold);
        if (const auto* analyzed = std::get_if<AnalyzedObservedTransaction>(&result)) {
            analyses.push_back(analyzed->analysis);
            outcomes.push_back(ReplayTransactionOutcome{
                .deviceAddress = request.address,
                .functionCode = request.functionCode,
                .analysis = analyzed->analysis,
                .requestIssue = analyzed->requestIssue,
            });
        } else {
            unsupportedRecords.push_back(
                std::get<UnsupportedObservedTransaction>(result));
        }
    }

    // Statistics describe exactly the ANALYZED subset; unsupported records
    // are reported separately (never silently dropped — Gate F).
    auto statistics = summarizeTransactions(analyses);
    return ReplayAnalysisResult{ReplayBatchAnalysis{
        .transactions = std::move(outcomes),
        .unsupportedRecords = std::move(unsupportedRecords),
        .statistics = std::move(statistics)}};
}

} // namespace modbuslens::core
