#include "core/diagnosis/RuleBasedDiagnosis.h"

#include <cstddef>
#include <map>

namespace modbuslens::core {

namespace {

// Standard exception-code recommendation mapping (archived decision).
DiagnosisActionCode actionForExceptionCode(std::uint8_t code)
{
    switch (code) {
    case 0x01: return DiagnosisActionCode::CheckFunctionSupport;
    case 0x02: return DiagnosisActionCode::CheckRegisterMap;
    case 0x03: return DiagnosisActionCode::CheckRequestParameters;
    case 0x04: return DiagnosisActionCode::CheckDeviceHealth;
    default:   return DiagnosisActionCode::CheckDeviceDocumentation;
    }
}

} // namespace

DiagnosisReport diagnoseTransactions(const DiagnosisContext& context)
{
    // Empty batch: the only honest statement is "no data" — health needs
    // positive evidence (archived rule: NoData != Healthy).
    if (context.transactions.empty()) {
        return DiagnosisReport{{
            DiagnosisFinding{
                .code = DiagnosisFindingCode::NoData,
                .severity = DiagnosisSeverity::Info,
                .affectedCount = 0,
                .exceptionCode = std::nullopt,
                .recommendedActions = {},
            },
        }};
    }

    const auto& s = context.statistics;
    std::vector<DiagnosisFinding> findings;

    // Fixed presentation order: Protocol -> CRC -> Timeout -> Exception
    // (code ascending) -> Pending -> Healthy/NoData. This order is NOT a
    // root-cause or confidence ranking — only deterministic presentation.
    if (s.protocolErrorCount > 0) {
        findings.push_back(DiagnosisFinding{
            .code = DiagnosisFindingCode::ProtocolErrorObserved,
            .severity = DiagnosisSeverity::Error,
            .affectedCount = s.protocolErrorCount,
            .exceptionCode = std::nullopt,
            .recommendedActions = {DiagnosisActionCode::InspectProtocolConsistency,
                                   DiagnosisActionCode::CheckDeviceDocumentation},
        });
    }
    if (s.crcErrorCount > 0) {
        findings.push_back(DiagnosisFinding{
            .code = DiagnosisFindingCode::CrcErrorObserved,
            .severity = DiagnosisSeverity::Warning,
            .affectedCount = s.crcErrorCount,
            .exceptionCode = std::nullopt,
            .recommendedActions = {DiagnosisActionCode::CheckSerialSettings,
                                   DiagnosisActionCode::CheckWiring,
                                   DiagnosisActionCode::CheckNoiseAndGrounding},
        });
    }
    if (s.timeoutCount > 0) {
        findings.push_back(DiagnosisFinding{
            .code = DiagnosisFindingCode::TimeoutObserved,
            .severity = DiagnosisSeverity::Warning,
            .affectedCount = s.timeoutCount,
            .exceptionCode = std::nullopt,
            .recommendedActions = {DiagnosisActionCode::CheckDevicePower,
                                   DiagnosisActionCode::CheckSlaveAddress,
                                   DiagnosisActionCode::CheckSerialSettings,
                                   DiagnosisActionCode::CheckWiring},
        });
    }

    // Exception grouping: ascending by code (std::map guarantees it). The
    // T007 invariant says Exception implies an exception code; defensively,
    // a code-less Exception is simply not grouped (no new error framework).
    std::map<std::uint8_t, std::size_t> exceptionCounts;
    for (const auto& transaction : context.transactions) {
        if (transaction.analysis.status == TransactionStatus::Exception
            && transaction.analysis.exceptionCode.has_value()) {
            ++exceptionCounts[*transaction.analysis.exceptionCode];
        }
    }
    for (const auto& [code, count] : exceptionCounts) {
        findings.push_back(DiagnosisFinding{
            .code = DiagnosisFindingCode::ExceptionObserved,
            .severity = DiagnosisSeverity::Warning,
            .affectedCount = count,
            .exceptionCode = code,
            .recommendedActions = {actionForExceptionCode(code)},
        });
    }

    if (s.pendingCount > 0) {
        findings.push_back(DiagnosisFinding{
            .code = DiagnosisFindingCode::PendingObserved,
            .severity = DiagnosisSeverity::Info,
            .affectedCount = s.pendingCount,
            .exceptionCode = std::nullopt,
            .recommendedActions = {DiagnosisActionCode::WaitForCompletion},
        });
    }

    // T015: broadcast observations are informational facts only — no check
    // is suggested from "no response was expected", and no device/write
    // conclusion may be derived. Placed after Pending, before Healthy.
    if (s.expectedNoResponseCount > 0) {
        findings.push_back(DiagnosisFinding{
            .code = DiagnosisFindingCode::ExpectedNoResponseObserved,
            .severity = DiagnosisSeverity::Info,
            .affectedCount = s.expectedNoResponseCount,
            .exceptionCode = std::nullopt,
            .recommendedActions = {},
        });
    }

    // Healthy needs ALL FOUR conditions: something completed, everything
    // completed successfully, nothing in flight, and no broadcast
    // observation (T015: a broadcast must never, on its own, let a batch be
    // declared Healthy).
    if (findings.empty() && s.completedCount > 0
        && s.successCount == s.completedCount && s.pendingCount == 0
        && s.expectedNoResponseCount == 0) {
        findings.push_back(DiagnosisFinding{
            .code = DiagnosisFindingCode::Healthy,
            .severity = DiagnosisSeverity::Info,
            .affectedCount = s.successCount,
            .exceptionCode = std::nullopt,
            .recommendedActions = {},
        });
    }

    return DiagnosisReport{.findings = std::move(findings)};
}

} // namespace modbuslens::core