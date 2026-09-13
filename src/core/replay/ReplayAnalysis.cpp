#include "core/replay/ReplayAnalysis.h"

#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"

namespace modbuslens::core {

ReplayAnalysisResult analyzeReplayLog(const ReplayLog& log)
{
    std::vector<TransactionAnalysis> analyses;
    analyses.reserve(log.transactions.size());
    std::vector<ReplayTransactionOutcome> outcomes;
    outcomes.reserve(log.transactions.size());

    for (std::size_t index = 0; index < log.transactions.size(); ++index) {
        const auto& record = log.transactions[index];

        // 1. requestWire -> trusted RTU frame. Every decode result is named
        //    before inspection; variant pointers never escape that lifetime
        //    (ISSUE-001 rule).
        const auto requestDecode = decodeRtuFrame(record.requestWire);
        if (std::get_if<RtuDecodeError>(&requestDecode) != nullptr) {
            return ReplayExecutionError{ReplayExecutionErrorCode::InvalidRequestWire, index};
        }
        const auto& request = std::get<ModbusRtuFrame>(requestDecode);

        // 2. function gate: T007's contract needs a trusted 0x03 request.
        if (request.functionCode != 0x03) {
            return ReplayExecutionError{ReplayExecutionErrorCode::InvalidRequestFunction, index};
        }

        // 3. 0x03 semantic validation (quantity / request length).
        const auto requestSemantic = decodeReadHoldingRegistersRequest(request);
        if (std::get_if<Function03DecodeError>(&requestSemantic) != nullptr) {
            return ReplayExecutionError{ReplayExecutionErrorCode::InvalidRequestData, index};
        }

        // 4. Response side: decode failure is NOT a replay failure — it is
        //    the historical fact we are diagnosing, so it flows into the
        //    analyzer as CrcError / ProtocolError.
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

        // 5. Reuse T007 classification — never re-derive statuses here.
        const auto analysis = analyzeFunction03Transaction(
            request, observation, record.elapsed, log.timeoutThreshold);
        analyses.push_back(analysis);
        outcomes.push_back(ReplayTransactionOutcome{
            .deviceAddress = request.address,
            .functionCode = request.functionCode,
            .analysis = analysis,
            .requestIssue = std::nullopt,
        });
    }

    // Same aggregation as every other mode: replay statistics ARE T007
    // statistics computed from the same function.
    auto statistics = summarizeTransactions(analyses);
    return ReplayAnalysisResult{
        ReplayBatchAnalysis{
            .transactions = std::move(outcomes),
            .unsupportedRecords = {},
            .statistics = std::move(statistics)}};
}

} // namespace modbuslens::core