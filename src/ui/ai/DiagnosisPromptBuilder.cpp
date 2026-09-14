#include "ui/ai/DiagnosisPromptBuilder.h"

#include <vector>

namespace {

constexpr int kMaxDetailTransactions = 20;

const char* kFindingNames[] = {
    "NoData", "Healthy", "PendingObserved", "ExceptionObserved",
    "CrcErrorObserved", "TimeoutObserved", "ProtocolErrorObserved",
    "ExpectedNoResponseObserved",
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
    case TransactionStatus::ExpectedNoResponse: return QStringLiteral("ExpectedNoResponse");
    }
    return QStringLiteral("Unknown");
}

QString issueToken(modbuslens::core::TransactionIssueCode code)
{
    const auto name = modbuslens::core::transactionIssueName(code);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

QString requestIssueToken(modbuslens::core::TransactionRequestIssueCode code)
{
    const auto name = modbuslens::core::transactionRequestIssueName(code);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
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
    // T014 additive: deterministic issue facts (machine channel, observed
    // values only). A ProtocolError WITHOUT an issue (defensive input) stays
    // detail-less — the builder never guesses a reason (refinement A).
    if (a.issue.has_value()) {
        line += QStringLiteral(" issue=%1").arg(issueToken(a.issue->code));
        if (a.issue->expectedAddress.has_value()) {
            line += QStringLiteral(" expected_address=%1")
                        .arg(*a.issue->expectedAddress);
        }
        if (a.issue->actualAddress.has_value()) {
            line += QStringLiteral(" actual_address=%1")
                        .arg(*a.issue->actualAddress);
        }
        if (a.issue->actualFunctionCode.has_value()) {
            line += QStringLiteral(" actual_function_code=0x%1")
                        .arg(QString::number(*a.issue->actualFunctionCode, 16)
                                 .toUpper()
                                 .rightJustified(2, QLatin1Char('0')));
        }
        if (a.issue->expectedQuantity.has_value()) {
            line += QStringLiteral(" expected_quantity=%1")
                        .arg(*a.issue->expectedQuantity);
        }
        if (a.issue->actualQuantity.has_value()) {
            line += QStringLiteral(" actual_quantity=%1")
                        .arg(*a.issue->actualQuantity);
        }
    }
    // T015 Part C additive: EVERY deterministic request issue, in Core order.
    for (const auto& requestIssue : transaction.requestIssues) {
        line += QStringLiteral(" request_issue=%1")
                    .arg(requestIssueToken(requestIssue.code));
        if (requestIssue.observedQuantity.has_value()) {
            line += QStringLiteral(" observed_quantity=%1")
                        .arg(*requestIssue.observedQuantity);
        }
        if (requestIssue.minAllowedQuantity.has_value()) {
            line += QStringLiteral(" min_allowed_quantity=%1")
                        .arg(*requestIssue.minAllowedQuantity);
        }
        if (requestIssue.maxAllowedQuantity.has_value()) {
            line += QStringLiteral(" max_allowed_quantity=%1")
                        .arg(*requestIssue.maxAllowedQuantity);
        }
        if (requestIssue.observedByteCount.has_value()) {
            line += QStringLiteral(" observed_byte_count=%1")
                        .arg(*requestIssue.observedByteCount);
        }
        if (requestIssue.expectedByteCount.has_value()) {
            line += QStringLiteral(" expected_byte_count=%1")
                        .arg(*requestIssue.expectedByteCount);
        }
        if (requestIssue.observedLength.has_value()) {
            line += QStringLiteral(" observed_length=%1")
                        .arg(*requestIssue.observedLength);
        }
        if (requestIssue.expectedLength.has_value()) {
            line += QStringLiteral(" expected_length=%1")
                        .arg(*requestIssue.expectedLength);
        }
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
        "User-facing terminology: describe a device Exception as 'Modbus 异常响应', a CRC Error as 'CRC 校验失败', a Timeout as '响应超时'; describe Exception 0x02 as '非法数据地址' and add that the register address range / register mapping should be checked; avoid unfounded layer jargon such as '链路层完整性', '传输层无响应', '功能码异常'.\n"
        "Express internal evidence fields naturally in Chinese in the answer: evidence scope as '当前观测批次', multiple anomaly types as '观察到多种异常', a shared root cause as '共同根因'.\n"
        "Return the explanation in concise Simplified Chinese.\n"
        "Keep protocol terms in English as-is: Modbus, RTU, CRC, function codes, register addresses, exception codes.\n"
        "Do not use Markdown formatting. Use plain text only.\n"
        "Structure the explanation with sections: 概述, 观测事实, 可能原因, 建议检查.\n"
        // ---- T014: deterministic protocol-error issue semantics ----
        "Deterministic protocol error detail (observed transaction facts only):\n"
        "- issue=response_frame_too_short: the received response was shorter than the minimum RTU frame; nothing beyond that is implied.\n"
        "- issue=response_address_mismatch: the response address byte differs from the request address byte; this does NOT prove slave address misconfiguration.\n"
        "- issue=unexpected_response_function: the response function code is neither the requested function nor its exception form.\n"
        "- issue=malformed_exception_response / malformed_normal_response: the response carried the expected function form but with an invalid data shape.\n"
        "- issue=quantity_mismatch: the response register count differs from the requested quantity — both are observed values.\n"
        // ---- T015: broadcast and request-side fact semantics ----
        "Deterministic broadcast and request facts:\n"
        "- status=ExpectedNoResponse (expected_no_response statistics): a broadcast-capable request was observed and no response was observed, which the protocol does not expect — this status describes the response expectation/outcome only; request semantic validity is expressed SEPARATELY through request_issue facts, never folded into this status. This does NOT prove that any device applied the write, that all devices executed it, or that any device is healthy. Never describe it as success, failure, or timeout.\n"
        "- request_issue=invalid_request_quantity: the captured request asked for a quantity outside its function's protocol constraint (observed/min/max values are supplied); it says nothing about why the requester sent it (never claim a program or operator error).\n"
        "- request_issue=invalid_request_length (observed_length/expected_length in the function's request-data bytes): the captured request's data length does not match its function's protocol shape; never claim truncation causes.\n"
        "- request_issue=invalid_request_byte_count (observed/expected byte count): the declared byte count disagrees with the quantity-based expectation; it is a declarative mismatch fact.\n"
        "- request_issue=invalid_broadcast_function: address 0 was observed with a function that is not broadcast-capable (a read cannot be broadcast). It is NOT an expected-no-response transaction.\n"
        "- issue=write_multiple_registers_echo_mismatch: a well-formed Write Multiple Registers reply whose starting address or written quantity disagrees with the request (expected/actual register-address and quantity pairs are supplied); it is an echo-contract mismatch, NOT a malformed reply.\n"
        "- Multiple request_issue entries may exist for ONE transaction when several independent request facts were observed; keep every one of them and never report only the first.\n"
        "- Never convert these observed facts into root causes (wiring, device defects, configuration, software bugs) and never claim the cause; keep them as observed facts and give possible explanations only with explicit uncertainty.\n"
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
                "statistics: observed=%1 completed=%2 pending=%3 success=%4 exception=%5 crc_error=%6 timeout=%7 protocol_error=%8 expected_no_response=%9\n")
                .arg(stats.observedCount)
                .arg(stats.completedCount)
                .arg(stats.pendingCount)
                .arg(stats.successCount)
                .arg(stats.exceptionCount)
                .arg(stats.crcErrorCount)
                .arg(stats.timeoutCount)
                .arg(stats.protocolErrorCount)
                .arg(stats.expectedNoResponseCount);
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