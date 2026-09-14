#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "core/analysis/TransactionAnalysis.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// T015 Gate A: generic PASSIVE transaction analyzer (Core, Zero Qt).
//
// Why this exists next to T007's analyzeFunction03Transaction:
//   Active Serial/Simulator AUTHOR their requests, so T007's "already-valid
//   FC03 request" contract is correct there and stays untouched. Passive
//   Replay analyses traffic it did not author: a malformed/invalid request
//   is itself a diagnosable historical fact. This analyzer therefore
//   dispatches by the OBSERVED request function and keeps the same shared
//   TransactionAnalysis/TransactionIssue vocabulary (Gate B: request-side
//   facts travel as an independent TransactionRequestIssue).
//
// Wire-level request corruption (CRC/frame-too-short) is NOT handled here:
// Gate E approved Scope A — that stays the replay caller's old failure
// contract for this phase (recorded as deferred).
// ---------------------------------------------------------------------------

enum class UnsupportedSemanticsReason {
    // The request was valid, but no normal-response matcher exists for its
    // function yet (e.g. FC08 normal, Function 0x10 normal until Part C).
    // Deliberately NOT an invalid request and NOT a TransactionStatus.
    UnsupportedNormalFunctionSemantics,
};

std::string_view unsupportedSemanticsReasonName(UnsupportedSemanticsReason reason);

struct UnsupportedObservedTransaction {
    std::uint8_t deviceAddress{};
    std::uint8_t functionCode{};
    UnsupportedSemanticsReason reason{};

    bool operator==(const UnsupportedObservedTransaction&) const = default;
};

struct AnalyzedObservedTransaction {
    TransactionAnalysis analysis;
    // Produced ONCE here; every downstream layer copies, never re-derives.
    // T015 Part C: an ordered COLLECTION — a single request can carry
    // several independently provable semantic issues, and every one of them
    // must survive (ordering is deterministic reporting order, NOT a
    // discard-priority ladder). Empty for a request that violated nothing.
    std::vector<TransactionRequestIssue> requestIssues;

    bool operator==(const AnalyzedObservedTransaction&) const = default;
};

using PassiveObservedTransactionResult =
    std::variant<AnalyzedObservedTransaction, UnsupportedObservedTransaction>;

// Analyze ONE observed transaction. `request` must already be a valid
// RTU-decoded frame; `observation` is the same abstract response language
// T007 uses (frame / decode error / no response); elapsed and the timeout
// threshold are caller-provided historical facts (no clock, no waiting).
PassiveObservedTransactionResult analyzeObservedTransaction(
    const ModbusRtuFrame& request,
    const ResponseObservation& observation,
    std::chrono::milliseconds elapsed,
    std::chrono::milliseconds timeoutThreshold);

} // namespace modbuslens::core
