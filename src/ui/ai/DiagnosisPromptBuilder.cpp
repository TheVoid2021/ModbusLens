#include "ui/ai/DiagnosisPromptBuilder.h"

#include <vector>

namespace {

constexpr int kMaxDetailTransactions = 20;

const char* kFindingNames[] = {
    "NoData", "Healthy", "PendingObserved", "ExceptionObserved",
    "CrcErrorObserved", "TimeoutObserved", "ProtocolErrorObserved",
};
const char* kSeverityNames[] = {"Info", "Warning", "Error"};

QString findingCodeName(modbuslens::core::DiagnosisFindingCode code)
{
    const auto index = static_cast<int>(code);
    if (index < 0
        || index >= static_cast<int>(sizeof(kFindingNames) / sizeof(kFindingNames[0]))) {
        return QStringLiteral("Unknown");
    }
    return QLatin1String(kFindingNames[index]);
}

QString severityName(modbuslens::core::DiagnosisSeverity severity)
{
    const auto index = static_cast<int>(severity);
    if (index < 0
        || index >= static_cast<int>(sizeof(kSeverityNames) / sizeof(kSeverityNames[0]))) {
        return QStringLiteral("Unknown");
    }
    return QLatin1String(kSeverityNames[index]);
}

QString statusName(modbuslens::core::TransactionStatus status)
{
    using modbuslens::core::TransactionStatus;
    switch (status) {
    case TransactionStatus::Pending: return QStringLiteral("Pending");
    case TransactionStatus::Success: return QStringLiteral("Success");
    case TransactionStatus::Exception: return QStringLiteral("Exception");
    case TransactionStatus::CrcError: return QStringLiteral("CrcError");
    case TransactionStatus::Timeout: return QStringLiteral("Timeout");
    case TransactionStatus::ProtocolError: return QStringLiteral("ProtocolError");
    }
    return QStringLiteral("Unknown");
}

bool isFailure(modbuslens::core::TransactionStatus status)
{
    using modbuslens::core::TransactionStatus;
    return status == TransactionStatus::ProtocolError
        || status == TransactionStatus::CrcError
        || status == TransactionStatus::Timeout
        || status == TransactionStatus::Exception;
}

// Deterministic, fixed-layout detail line: protocol facts only.
QString transactionLine(const modbuslens::core::DiagnosisTransaction& transaction)
{
    const auto& a = transaction.analysis;
    QString line = QStringLiteral("device=0x%1 function=0x%2 status=%3 elapsed=%4ms")
                       .arg(transaction.deviceAddress, 2, 16, QLatin1Char('0'))
                       .arg(transaction.functionCode, 2, 16, QLatin1Char('0'))
                       .arg(statusName(a.status))
                       .arg(a.elapsed.count());
    if (a.exceptionCode.has_value()) {
        line += QStringLiteral(" exception_code=0x%1")
                    .arg(QString::number(*a.exceptionCode, 16)
                             .toUpper()
                             .rightJustified(2, QLatin1Char('0')));
    }
    return line;
}

// Failure-first deterministic selection: one pass in ORIGINAL batch order
// for failures (Protocol/Crc/Timeout/Exception), then a second pass in
// original order filling the remainder (Pending, then Success among the
// second-pass results as they appear) up to the detail budget.
std::vector<const modbuslens::core::DiagnosisTransaction*> selectDetails(
    const modbuslens::core::DiagnosisContext& context)
{
    std::vector<const modbuslens::core::DiagnosisTransaction*> selected;
    selected.reserve(kMaxDetailTransactions);
    for (const auto& transaction : context.transactions) {
        if (selected.size() >= static_cast<std::size_t>(kMaxDetailTransactions)) {
            break;
        }
        if (isFailure(transaction.analysis.status)) {
            selected.push_back(&transaction);
        }
    }
    for (const auto& transaction : context.transactions) {
        if (selected.size() >= static_cast<std::size_t>(kMaxDetailTransactions)) {
            break;
        }
        if (!isFailure(transaction.analysis.status)) {
            selected.push_back(&transaction);
        }
    }
    return selected;
}

} // namespace

DiagnosisPrompt buildDiagnosisPrompt(
    const modbuslens::core::DiagnosisContext& context,
    const modbuslens::core::DiagnosisReport& report)
{
    DiagnosisPrompt prompt;
    prompt.systemInstructions = QStringLiteral(
        "You are an industrial Modbus RTU diagnostic explainer.\n"
        "The supplied deterministic protocol facts and baseline are authoritative.\n"
        "Do not recalculate or contradict: CRC status, transaction status, exception codes, statistics.\n"
        "Clearly distinguish: observed facts, possible explanations, suggested checks.\n"
        "Do not claim a certain root cause unless the supplied facts prove it.\n"
        "Do not claim access to information that is not supplied.\n"
        "Do not propose automatic actions.\n"
        "Do not re-analyze raw Modbus packets.\n"
        // ---- ISSUE-006: attribution discipline (appended AFTER the original
        // authority rules; the original rules are never weakened or deleted).
        "Evidence scope: the supplied evidence describes only the current observed batch, never a history or a long-run population.\n"
        "Do not generalize this batch into long-term device, link, wiring, or communication reliability with wording such as: chronically unstable, long-term unstable, persistent instability, intermittent connection failure — unless longitudinal evidence is explicitly supplied (v1 supplies none).\n"
        "Deterministic status semantics:\n"
        "- CRC Error: the received response bytes failed Modbus RTU CRC validation. Serial settings, wiring, grounding, noise/EMI, capture or frame corruption are only possible explanations or suggested checks, never stated causes.\n"
        "- Timeout: no valid response was observed before the configured timeout threshold. Do not claim the device is offline, broken, disconnected, or that the link was interrupted.\n"
        "- Exception 0x02: Illegal Data Address — the requested register address is outside the device register map. Relate it to the requested register address, the register map, device documentation or request configuration.\n"
        "- Standard exception semantics for phrasing: 0x01 Illegal Function (function support), 0x02 Illegal Data Address (register map), 0x03 Illegal Data Value (request parameters), 0x04 Slave Device Failure (device health). For an unknown exception code, do not guess: direct the user to check the device documentation.\n"
        "Never explain an exception code as a wiring failure, CRC problem, link interruption or electrical interference.\n"
        "Anomaly types are independent observations. Multiple anomaly types in one batch do NOT imply a shared root cause.\n"
        "Summarize 'multiple anomaly types were observed' when true, but never conclude 'therefore the connection is intermittently failing', never 'all failures share one cause such as signal integrity', and never comparisons like 'rather than configuration errors' unless supplied evidence proves the comparison.\n"
        "Explain each anomaly (CRC, Timeout, Exception 0x02, ...) against its own deterministic facts above.\n"
        "In the final answer: 观察事实 only from the supplied deterministic facts; 可能原因 always with explicit uncertainty wording (可能、可能与……有关、可考虑、may、may indicate、possible); 建议检查 only human troubleshooting suggestions.\n"
        "Never rewrite a recommendation into an observed fact, and never present a possible explanation as a confirmed root cause.\n"
        "Return the explanation in concise Simplified Chinese.\n"
        "Keep protocol terms in English as-is: Modbus, RTU, CRC, function codes, register addresses, exception codes.\n"
        "Do not use Markdown formatting. Use plain text only.\n"
        "Structure the explanation with sections: 概述, 观测事实, 可能原因, 建议检查.\n"
        "Keep the answer concise (roughly 250 words or less).");

    const auto& stats = context.statistics;
    const auto detail = selectDetails(context);

    QString user;
    user += QStringLiteral("Deterministic protocol facts:\n");
    user += QStringLiteral("total_transactions=%1\n").arg(context.transactions.size());
    // ISSUE-006: unconditional evidence-scope marker — count is NEVER used as
    // a threshold for long-run inference (observed=4 and observed=30 must be
    // guarded identically; v1 has no longitudinal evidence at all).
    user += QStringLiteral("evidence_scope=current_observed_batch\n");
    user += QStringLiteral("detailed_transactions=%1\n").arg(detail.size());
    user += QStringLiteral("details_truncated=%1\n")
                .arg(detail.size() < context.transactions.size()
                         ? QStringLiteral("true")
                         : QStringLiteral("false"));
    user += QStringLiteral(
                "statistics: observed=%1 completed=%2 pending=%3 success=%4 exception=%5 crc_error=%6 timeout=%7 protocol_error=%8\n")
                .arg(stats.observedCount)
                .arg(stats.completedCount)
                .arg(stats.pendingCount)
                .arg(stats.successCount)
                .arg(stats.exceptionCount)
                .arg(stats.crcErrorCount)
                .arg(stats.timeoutCount)
                .arg(stats.protocolErrorCount);
    if (stats.successRate.has_value()) {
        user += QStringLiteral("success_rate=%1\n").arg(*stats.successRate, 0, 'f', 4);
    }
    if (stats.averageSuccessLatencyMs.has_value()) {
        user += QStringLiteral("average_success_latency_ms=%1\n")
                    .arg(*stats.averageSuccessLatencyMs, 0, 'f', 2);
    }

    user += QStringLiteral("baseline findings:\n");
    for (const auto& finding : report.findings) {
        QString line = QStringLiteral("- code=%1 severity=%2 count=%3")
                           .arg(findingCodeName(finding.code))
                           .arg(severityName(finding.severity))
                           .arg(finding.affectedCount);
        if (finding.exceptionCode.has_value()) {
            line += QStringLiteral(" exception_code=0x%1")
                        .arg(QString::number(*finding.exceptionCode, 16)
                                 .toUpper()
                                 .rightJustified(2, QLatin1Char('0')));
        }
        user += line + QLatin1Char('\n');
    }

    user += QStringLiteral("transaction details:\n");
    std::size_t index = 1;
    for (const auto* transaction : detail) {
        user += QStringLiteral("%1. %2\n")
                    .arg(index++)
                    .arg(transactionLine(*transaction));
    }

    prompt.userPrompt = user;
    return prompt;
}