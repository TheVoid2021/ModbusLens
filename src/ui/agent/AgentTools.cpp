// T012 Part A — read-only Agent tool layer implementation.
//
// Policy anchors (docs/tasks/T012-agent-tools.md, ADR002):
//   - three whitelisted read-only tools, explicit dispatcher (no registry);
//   - tools read ONLY the immutable AgentToolContext snapshot (R3);
//   - recent anomalies = latest-20 in ORIGINAL order (R2);
//   - transaction_number = 1-based batch ordinal, no stable id claim (R1);
//   - tool results are typed deterministic facts; serialization is a
//     provider-independent JSON DTO (no QML text, no filenames, no prose).
#include "ui/agent/AgentTools.h"

#include <QJsonArray>
#include <QJsonObject>

#include <cmath>
#include <string>
#include <utility>

namespace modbuslens::agent {

namespace {

using modbuslens::core::TransactionStatus;

constexpr std::string_view kEvidenceScope = "current_observed_batch";

// T011 deterministic contract: Pending means "not finished yet" — it is
// neither Success nor a completed failure, and it is NOT an anomaly.
// The anomaly whitelist is exactly the four completed failure statuses
// (same semantic as the T011 baseline failure definition).
bool isAnomalyStatus(TransactionStatus status)
{
    switch (status) {
    case TransactionStatus::Exception:
    case TransactionStatus::CrcError:
    case TransactionStatus::Timeout:
    case TransactionStatus::ProtocolError:
        return true;
    case TransactionStatus::Success:
    case TransactionStatus::Pending:
        return false;
    }
    return false;
}

SessionSummaryResult makeSessionSummary(const AgentToolContext& context)
{
    SessionSummaryResult summary;
    summary.observedCount = context.statistics.observedCount;
    summary.completedCount = context.statistics.completedCount;
    summary.pendingCount = context.statistics.pendingCount;
    summary.successCount = context.statistics.successCount;
    summary.crcErrorCount = context.statistics.crcErrorCount;
    summary.timeoutCount = context.statistics.timeoutCount;
    summary.exceptionCount = context.statistics.exceptionCount;
    summary.protocolErrorCount = context.statistics.protocolErrorCount;
    summary.successRate = context.statistics.successRate;
    summary.averageSuccessLatencyMs = context.statistics.averageSuccessLatencyMs;
    summary.transactionCount = context.transactions.size();
    return summary;
}

AnomalyEntry makeAnomalyEntry(std::size_t transactionNumber,
                              const modbuslens::core::DiagnosisTransaction& tx)
{
    return AnomalyEntry{
        .transactionNumber = transactionNumber,
        .deviceAddress = tx.deviceAddress,
        .functionCode = tx.functionCode,
        .status = tx.analysis.status,
        .elapsedMs = tx.analysis.elapsed.count(),
        .exceptionCode = tx.analysis.exceptionCode,
    };
}

RecentAnomaliesResult makeRecentAnomalies(const AgentToolContext& context)
{
    // 1-based ordinals of every whitelisted anomaly transaction, in batch order.
    std::vector<std::size_t> anomalyOrdinals;
    anomalyOrdinals.reserve(context.transactions.size());
    for (std::size_t i = 0; i < context.transactions.size(); ++i) {
        if (isAnomalyStatus(context.transactions[i].analysis.status)) {
            anomalyOrdinals.push_back(i + 1);
        }
    }

    RecentAnomaliesResult result;
    result.totalAnomalyCount = anomalyOrdinals.size();
    result.truncated = anomalyOrdinals.size() > kMaxRecentAnomalies;
    // R2: "recent" means the LAST up-to-20 anomalies — still emitted in
    // original batch order (never reversed, never sampled).
    const std::size_t begin =
        anomalyOrdinals.size() > kMaxRecentAnomalies
            ? anomalyOrdinals.size() - kMaxRecentAnomalies
            : 0;
    for (std::size_t i = begin; i < anomalyOrdinals.size(); ++i) {
        const std::size_t ordinal = anomalyOrdinals[i];
        result.entries.push_back(
            makeAnomalyEntry(ordinal, context.transactions[ordinal - 1]));
    }
    return result;
}

} // namespace

std::optional<AgentToolName> agentToolNameFromString(std::string_view name)
{
    if (name == "get_session_summary") {
        return AgentToolName::GetSessionSummary;
    }
    if (name == "get_recent_anomalies") {
        return AgentToolName::GetRecentAnomalies;
    }
    if (name == "get_transaction_detail") {
        return AgentToolName::GetTransactionDetail;
    }
    return std::nullopt;
}

std::string_view agentToolNameString(AgentToolName name)
{
    switch (name) {
    case AgentToolName::GetSessionSummary: return "get_session_summary";
    case AgentToolName::GetRecentAnomalies: return "get_recent_anomalies";
    case AgentToolName::GetTransactionDetail: return "get_transaction_detail";
    }
    return {};
}

std::string_view transactionStatusName(
    modbuslens::core::TransactionStatus status)
{
    using modbuslens::core::TransactionStatus;
    switch (status) {
    case TransactionStatus::Pending: return "Pending";
    case TransactionStatus::Success: return "Success";
    case TransactionStatus::Exception: return "Exception";
    case TransactionStatus::CrcError: return "CrcError";
    case TransactionStatus::Timeout: return "Timeout";
    case TransactionStatus::ProtocolError: return "ProtocolError";
    }
    return "Unknown";
}

std::optional<std::string_view> standardExceptionName(std::uint8_t code)
{
    switch (code) {
    case 0x01: return "Illegal Function";
    case 0x02: return "Illegal Data Address";
    case 0x03: return "Illegal Data Value";
    case 0x04: return "Slave Device Failure";
    default: return std::nullopt; // unknown: never guessed
    }
}

namespace {

std::string_view errorCodeName(AgentToolErrorCode code)
{
    switch (code) {
    case AgentToolErrorCode::UnknownTool: return "UnknownTool";
    case AgentToolErrorCode::InvalidArguments: return "InvalidArguments";
    case AgentToolErrorCode::TransactionNotFound: return "TransactionNotFound";
    }
    return "Unknown";
}

// The model's arguments object is UNTRUSTED input. Returns nullopt when the
// numeric transaction_number is missing, non-integral, or extra fields
// exist. Range violations in [1..N] are NOT decided here (they are facts
// against the snapshot: a well-formed number that names no transaction is
// TransactionNotFound, not InvalidArguments).
bool validatedTransactionNumber(const QJsonObject& arguments,
                                const AgentToolContext& context,
                                std::size_t& outNumber,
                                bool& outRangeViolation)
{
    if (arguments.size() != 1
        || !arguments.contains(QLatin1String("transaction_number"))) {
        return false;
    }
    const QJsonValue value = arguments.value(QLatin1String("transaction_number"));
    if (!value.isDouble()) {
        return false;
    }
    const double d = value.toDouble();
    if (std::floor(d) != d) {
        // Non-integral numbers cannot name an ordinal: malformed input.
        return false;
    }
    const std::uint64_t number = static_cast<std::uint64_t>(d);
    if (number < 1 || number > context.transactions.size()) {
        // A well-formed ordinal that names no transaction in THIS batch:
        // "transaction number N does not exist" — not a malformed argument.
        outRangeViolation = true;
        return true;
    }
    outNumber = static_cast<std::size_t>(number);
    return true;
}

} // namespace

AgentToolResult dispatchAgentTool(const AgentToolContext& context,
                                  std::string_view toolName,
                                  const QJsonObject& arguments)
{
    const auto tool = agentToolNameFromString(toolName);
    if (!tool.has_value()) {
        return AgentToolError{AgentToolErrorCode::UnknownTool};
    }
    switch (*tool) {
    case AgentToolName::GetSessionSummary:
        if (!arguments.isEmpty()) {
            return AgentToolError{AgentToolErrorCode::InvalidArguments};
        }
        return makeSessionSummary(context);
    case AgentToolName::GetRecentAnomalies:
        if (!arguments.isEmpty()) {
            return AgentToolError{AgentToolErrorCode::InvalidArguments};
        }
        return makeRecentAnomalies(context);
    case AgentToolName::GetTransactionDetail: {
        std::size_t number = 0;
        bool rangeViolation = false;
        if (!validatedTransactionNumber(arguments, context, number,
                                        rangeViolation)) {
            return AgentToolError{AgentToolErrorCode::InvalidArguments};
        }
        if (rangeViolation) {
            return AgentToolError{AgentToolErrorCode::TransactionNotFound};
        }
        const auto& tx = context.transactions[number - 1];
        return TransactionDetailResult{
            .transactionNumber = number,
            .deviceAddress = tx.deviceAddress,
            .functionCode = tx.functionCode,
            .status = tx.analysis.status,
            .elapsedMs = tx.analysis.elapsed.count(),
            .exceptionCode = tx.analysis.exceptionCode,
        };
    }
    }
    return AgentToolError{AgentToolErrorCode::UnknownTool};
}

QJsonObject toJsonObject(const SessionSummaryResult& result)
{
    QJsonObject json;
    json.insert(QStringLiteral("observed_count"),
                static_cast<qint64>(result.observedCount));
    json.insert(QStringLiteral("completed_count"),
                static_cast<qint64>(result.completedCount));
    json.insert(QStringLiteral("pending_count"),
                static_cast<qint64>(result.pendingCount));
    json.insert(QStringLiteral("success_count"),
                static_cast<qint64>(result.successCount));
    json.insert(QStringLiteral("crc_error_count"),
                static_cast<qint64>(result.crcErrorCount));
    json.insert(QStringLiteral("timeout_count"),
                static_cast<qint64>(result.timeoutCount));
    json.insert(QStringLiteral("exception_count"),
                static_cast<qint64>(result.exceptionCount));
    json.insert(QStringLiteral("protocol_error_count"),
                static_cast<qint64>(result.protocolErrorCount));
    json.insert(QStringLiteral("transaction_count"),
                static_cast<qint64>(result.transactionCount));
    if (result.successRate.has_value()) {
        json.insert(QStringLiteral("success_rate"), *result.successRate);
    }
    if (result.averageSuccessLatencyMs.has_value()) {
        json.insert(QStringLiteral("average_success_latency_ms"),
                    *result.averageSuccessLatencyMs);
    }
    json.insert(QStringLiteral("evidence_scope"),
                QString::fromUtf8(kEvidenceScope.data(),
                                  static_cast<qsizetype>(kEvidenceScope.size())));
    return json;
}

QJsonObject toJsonObject(const RecentAnomaliesResult& result)
{
    QJsonObject json;
    json.insert(QStringLiteral("total_anomaly_count"),
                static_cast<qint64>(result.totalAnomalyCount));
    json.insert(QStringLiteral("returned_count"),
                static_cast<qint64>(result.entries.size()));
    json.insert(QStringLiteral("truncated"), result.truncated);

    QJsonArray anomalies;
    for (const auto& entry : result.entries) {
        QJsonObject item;
        item.insert(QStringLiteral("transaction_number"),
                    static_cast<qint64>(entry.transactionNumber));
        item.insert(QStringLiteral("device_address"),
                    static_cast<int>(entry.deviceAddress));
        item.insert(QStringLiteral("function_code"),
                    static_cast<int>(entry.functionCode));
        item.insert(QStringLiteral("status"),
                    QString::fromUtf8(transactionStatusName(entry.status)));
        item.insert(QStringLiteral("elapsed_ms"), entry.elapsedMs);
        if (entry.exceptionCode.has_value()) {
            item.insert(QStringLiteral("exception_code"),
                        static_cast<int>(*entry.exceptionCode));
        }
        anomalies.append(item);
    }
    json.insert(QStringLiteral("anomalies"), anomalies);
    json.insert(QStringLiteral("evidence_scope"),
                QString::fromUtf8(kEvidenceScope.data(),
                                  static_cast<qsizetype>(kEvidenceScope.size())));
    return json;
}

QJsonObject toJsonObject(const TransactionDetailResult& result)
{
    QJsonObject json;
    json.insert(QStringLiteral("transaction_number"),
                static_cast<qint64>(result.transactionNumber));
    json.insert(QStringLiteral("device_address"),
                static_cast<int>(result.deviceAddress));
    json.insert(QStringLiteral("function_code"),
                static_cast<int>(result.functionCode));
    json.insert(QStringLiteral("status"),
                QString::fromUtf8(transactionStatusName(result.status)));
    json.insert(QStringLiteral("elapsed_ms"), result.elapsedMs);
    if (result.exceptionCode.has_value()) {
        json.insert(QStringLiteral("exception_code"),
                    static_cast<int>(*result.exceptionCode));
        const auto name = standardExceptionName(*result.exceptionCode);
        if (name.has_value()) {
            json.insert(QStringLiteral("exception_name"),
                        QString::fromUtf8(name->data(),
                                          static_cast<qsizetype>(name->size())));
        }
    }
    json.insert(QStringLiteral("evidence_scope"),
                QString::fromUtf8(kEvidenceScope.data(),
                                  static_cast<qsizetype>(kEvidenceScope.size())));
    return json;
}

QJsonObject toJsonObject(const AgentToolError& error)
{
    QJsonObject json;
    json.insert(QStringLiteral("error"),
                QString::fromUtf8(errorCodeName(error.code)));
    return json;
}

} // namespace modbuslens::agent

namespace modbuslens::agent {

std::optional<AgentToolErrorCode> validateAgentToolCall(
    const AgentToolContext& context, AgentToolName tool,
    const QJsonObject& arguments)
{
    switch (tool) {
    case AgentToolName::GetSessionSummary:
    case AgentToolName::GetRecentAnomalies:
        return arguments.isEmpty()
                   ? std::nullopt
                   : std::optional{AgentToolErrorCode::InvalidArguments};
    case AgentToolName::GetTransactionDetail: {
        std::size_t number = 0;
        bool rangeViolation = false;
        if (!validatedTransactionNumber(arguments, context, number,
                                        rangeViolation)) {
            return AgentToolErrorCode::InvalidArguments;
        }
        if (rangeViolation) {
            return AgentToolErrorCode::TransactionNotFound;
        }
        return std::nullopt;
    }
    }
    return AgentToolErrorCode::UnknownTool;
}

} // namespace modbuslens::agent
