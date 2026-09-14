#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/diagnosis/DiagnosisContext.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// Deterministic rule-based diagnosis baseline (T011 Part A).
//
// The rules ONLY interpret facts that the deterministic core already
// established (statuses + statistics). They never judge CRC, framing,
// consistency or timing themselves, and they never claim a root cause:
// every output is an OBSERVATION plus possible checks (structured action
// codes — human text belongs to the application layer).
// ---------------------------------------------------------------------------

enum class DiagnosisFindingCode {
    NoData,
    Healthy,
    PendingObserved,
    ExceptionObserved,
    CrcErrorObserved,
    TimeoutObserved,
    ProtocolErrorObserved,
    // T015 (ADR-003): broadcast transactions that completed with no
    // (expected) response. Purely observational — it proves nothing about
    // device write success or device health.
    ExpectedNoResponseObserved,
    // T015 Part C audit: one or more transactions carry request-side
    // protocol semantic issues (orthogonal to their TransactionStatus —
    // a Success row may carry them). Affected count = TRANSACTIONS with a
    // non-empty request-issue collection, never the issue count.
    RequestIssueObserved,
};

enum class DiagnosisSeverity {
    Info,
    Warning,
    Error, // reserved for protocol-contract violations (archived decision)
};

enum class DiagnosisActionCode {
    WaitForCompletion,
    CheckDevicePower,
    CheckSlaveAddress,
    CheckSerialSettings,
    CheckWiring,
    CheckNoiseAndGrounding,
    CheckFunctionSupport,
    CheckRegisterMap,
    CheckRequestParameters,
    CheckDeviceHealth,
    CheckDeviceDocumentation,
    InspectProtocolConsistency,
};

struct DiagnosisFinding {
    DiagnosisFindingCode code{};
    DiagnosisSeverity severity{};

    std::size_t affectedCount{};

    std::optional<std::uint8_t> exceptionCode; // set only for Exception findings

    // Fixed deterministic order, no duplicates (archived rule).
    std::vector<DiagnosisActionCode> recommendedActions;

    bool operator==(const DiagnosisFinding&) const = default;
};

struct DiagnosisReport {
    std::vector<DiagnosisFinding> findings;

    bool operator==(const DiagnosisReport&) const = default;
};

// Pure function: deterministic, no I/O, no Qt, no clock, no random, no
// network. Finding order is FIXED (deterministic presentation order — not a
// root-cause/confidence ranking):
//   Protocol -> CRC -> Timeout -> Exception (code ascending) -> Pending
//   -> Healthy / NoData
DiagnosisReport diagnoseTransactions(const DiagnosisContext& context);

} // namespace modbuslens::core