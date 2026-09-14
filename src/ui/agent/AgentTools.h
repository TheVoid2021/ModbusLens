#pragma once

#include <QJsonObject>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "core/analysis/TransactionAnalysis.h"
#include "ui/agent/AgentToolContext.h"

// T012 Part A — read-only Agent tool layer (Learning archive TD-1~TD-7 +
// Implementation Review Refinements R1~R3).
//
// Design: enum whitelist + explicit dispatcher (no ITool/ToolRegistry);
// typed deterministic tool results; validation of ALL model-provided
// arguments (the model output is untrusted input); provider-independent
// JSON-ready serialization (QJsonObject DTOs — no network, no provider
// strings, no QML, no Controller live state).
namespace modbuslens::agent {

// v1 tool whitelist — exactly three read-only tools. Any other name,
// including any write/control capability, is UnknownTool by construction:
// there is no dispatch branch, enum value, schema or name for it.
enum class AgentToolName {
    GetSessionSummary,
    GetRecentAnomalies,
    GetTransactionDetail,
};

constexpr std::size_t kMaxRecentAnomalies = 20;

// Exact-name mapping (whitelist gate). Unknown names -> std::nullopt.
std::optional<AgentToolName> agentToolNameFromString(std::string_view name);
std::string_view agentToolNameString(AgentToolName name);

// ---- typed tool results (deterministic facts only, no prose) ----

struct SessionSummaryResult {
    std::size_t observedCount{};
    std::size_t completedCount{};
    std::size_t pendingCount{};
    std::size_t successCount{};
    std::size_t crcErrorCount{};
    std::size_t timeoutCount{};
    std::size_t exceptionCount{};
    std::size_t protocolErrorCount{};
    // T015: broadcast observations are part of the completed decomposition.
    std::size_t expectedNoResponseCount{};
    // T015 Part C audit: number of analyzed transactions carrying at least
    // one request-side issue (transactions, never an issue count).
    std::size_t requestIssueTransactions{};
    std::size_t transactionCount{};
    // no value == field omitted in JSON (never a fabricated 0 / 0.0).
    std::optional<double> successRate;
    std::optional<double> averageSuccessLatencyMs;

    bool operator==(const SessionSummaryResult&) const = default;
};

struct AnomalyEntry {
    // 1-based ordinal within the captured batch (R1: "transaction_number",
    // never "transaction_id" — no stable identity exists).
    std::size_t transactionNumber{};
    std::uint8_t deviceAddress{};
    std::uint8_t functionCode{};
    modbuslens::core::TransactionStatus status{};
    long long elapsedMs{};
    std::optional<std::uint8_t> exceptionCode;
    // T014 additive: simplified issue code only (payload stays with
    // get_transaction_detail). Absent for non-ProtocolError statuses.
    std::optional<modbuslens::core::TransactionIssueCode> issueCode;
    // T015 Part C audit: request-side issue codes on the anomaly row (the
    // WHY when the status itself — e.g. Success — is not an anomaly).
    std::vector<modbuslens::core::TransactionRequestIssueCode> requestIssueCodes;

    bool operator==(const AnomalyEntry&) const = default;
};

struct RecentAnomaliesResult {
    std::size_t totalAnomalyCount{}; // all whitelisted anomaly statuses
    bool truncated{};                // total > kMaxRecentAnomalies
    // Latest 20 anomalies in ORIGINAL batch order (R2 — never reversed,
    // never randomly sampled). returned_count == entries.size().
    std::vector<AnomalyEntry> entries;

    bool operator==(const RecentAnomaliesResult&) const = default;
};

struct TransactionDetailResult {
    std::size_t transactionNumber{};
    std::uint8_t deviceAddress{};
    std::uint8_t functionCode{};
    modbuslens::core::TransactionStatus status{};
    long long elapsedMs{};
    std::optional<std::uint8_t> exceptionCode;
    // T014 additive: the FULL deterministic issue (code + sparse payload)
    // for ProtocolError rows; absent otherwise. Facts only — read from the
    // snapshot, never re-derived by the tool layer.
    std::optional<modbuslens::core::TransactionIssue> issue;
    // T015 additive: the FULL ordered request-issue collection, copied
    // verbatim from the passive analyzer; empty on valid requests and all
    // active paths. The tool layer never re-derives request facts.
    std::vector<modbuslens::core::TransactionRequestIssue> requestIssues;
    // Standard exception NAME (structured, e.g. "Illegal Data Address") when
    // the code is one of 0x01~0x04; absent otherwise. No causal prose here —
    // attribution belongs to the explanation layer, not the tool facts.

    bool operator==(const TransactionDetailResult&) const = default;
};

// Minimal Part-A error contract (Learning TD-7 / refinement §9): provider
// and round-limit errors belong to the Part B runtime, not here.
enum class AgentToolErrorCode {
    UnknownTool,
    InvalidArguments,
    TransactionNotFound,
};

struct AgentToolError {
    AgentToolErrorCode code{};

    bool operator==(const AgentToolError&) const = default;
};

using AgentToolResult = std::variant<SessionSummaryResult,
                                     RecentAnomaliesResult,
                                     TransactionDetailResult,
                                     AgentToolError>;

// Explicit dispatcher. `arguments` comes from the provider tool_call and is
// treated as UNTRUSTED input: required/type/range/unknown-field validation
// happens here, before anything reads the snapshot.
AgentToolResult dispatchAgentTool(const AgentToolContext& context,
                                  std::string_view toolName,
                                  const QJsonObject& arguments);

// T012 Part B Phase 1 seam: VALIDATE-ONLY twin of the dispatcher (same
// rules, no result produced). The runtime validates the WHOLE tool-call
// batch before executing ANY of it (transaction-like semantics, no partial
// execution); the dispatcher then re-validates internally and executes.
// Returns std::nullopt when the call is valid; otherwise the error that the
// dispatcher would produce.
std::optional<AgentToolErrorCode> validateAgentToolCall(
    const AgentToolContext& context, AgentToolName tool,
    const QJsonObject& arguments);

// Provider-independent serialization (JSON-ready DTOs). The Part B provider
// adapter only copies these objects into its wire messages.
QJsonObject toJsonObject(const SessionSummaryResult& result);
QJsonObject toJsonObject(const RecentAnomaliesResult& result);
QJsonObject toJsonObject(const TransactionDetailResult& result);
QJsonObject toJsonObject(const AgentToolError& error);

// Machine-channel status name (same convention as the T011 prompt facts).
std::string_view transactionStatusName(
    modbuslens::core::TransactionStatus status);

// Standard Modbus exception name for 0x01~0x04; std::nullopt otherwise
// (unknown codes are NEVER guessed — checked against device documentation).
std::optional<std::string_view> standardExceptionName(std::uint8_t code);

} // namespace modbuslens::agent