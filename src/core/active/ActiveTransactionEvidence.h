#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/analysis/TransactionAnalysis.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M10-A: transport disposition + wire evidence (Pure C++20, Zero Qt).
//
// The disposition is an ORTHOGONAL TRANSPORT FACT, never a second public
// transaction outcome: the outcome axis stays the frozen
// Pending/Success/Exception/CrcError/Timeout/ProtocolError/ExpectedNoResponse
// taxonomy (M10 Phase 1 §34 / FC31, M10-A §28).
//
//   NotSent      — provably never entered the transmission lifecycle
//                  (validation/not-connected/busy/pre-send rejection, and
//                  the future confirmation cancel).
//   PossiblySent — handed to the transmission lifecycle, but the bytes
//                  reaching the DEVICE is unprovable (a complete QSerialPort
//                  write only proves Qt accepted them; a short write may
//                  already have put bytes on the wire). Whenever a trusted
//                  response WAS observed, that response is the stronger
//                  fact — the UI must never claim "may have been sent" over
//                  an observed reply.
// ---------------------------------------------------------------------------

enum class TransportDisposition {
    NotSent,
    PossiblySent,
};

// Stable machine token (e.g. "possibly_sent") — adapter/tool serialization
// only, never human UI prose.
[[nodiscard]] std::string_view transportDispositionName(TransportDisposition disposition);

// The wire evidence of one active transaction. Retained by the runtime as
// authoritative core/runtime facts; a QML model is only ever a projection of
// them (M10 Phase 1 §21 / FC19).
struct ActiveTransactionEvidence {
    // EXACT bytes handed to the transport (CRC included) — copied from the
    // send-time descriptor, never re-encoded from the intent afterwards.
    std::vector<std::uint8_t> requestAdu;
    // EXACT response bytes the transport observed: normal, exception, CRC
    // error candidate, protocol error candidate or partial bytes. Empty (with
    // disposition PossiblySent) is the honest representation of "no bytes
    // were observed at all" — damaged bytes are never discarded.
    std::vector<std::uint8_t> responseAdu;
    TransportDisposition disposition{TransportDisposition::NotSent};

    bool operator==(const ActiveTransactionEvidence&) const = default;
};

// The outcome of one ATTEMPT to start a transaction. `accepted` means the
// full request entered the transmission lifecycle and a completion will
// follow; `disposition` states what the wire may have seen. A short write
// leaves accepted == false WITH PossiblySent (the wire cannot be cleared).
struct ActiveStartResult {
    bool accepted{false};
    TransportDisposition disposition{TransportDisposition::NotSent};

    bool operator==(const ActiveStartResult&) const = default;
};

// Transport completion envelope: everything one finished active transaction
// produced, travelling together so a completion can never lose its evidence.
struct ActiveTransactionResult {
    // Send-time request snapshot (descriptor as handed to the transport).
    ActiveRequestDescriptor request{};
    std::vector<std::uint8_t> responseAdu{};
    TransportDisposition disposition{TransportDisposition::PossiblySent};
    TransactionAnalysis analysis{};

    [[nodiscard]] ActiveTransactionEvidence evidence() const;

    bool operator==(const ActiveTransactionResult&) const = default;
};

// Provenance of one PUBLISHED transaction: which runtime session authored it
// plus, for Active Serial, the send-time request snapshot and the wire
// evidence. A presentation row may carry this; it never becomes the
// authority (the runtime records do).
struct ActiveSerialProvenance {
    std::uint64_t sessionId{};
    ActiveRequestDescriptor request{};
    ActiveTransactionEvidence evidence{};

    bool operator==(const ActiveSerialProvenance&) const = default;
};

// One completed Active Serial transaction as the runtime retains it: the
// session it belongs to, the send-time request snapshot, the wire evidence
// and the analysis verdict. The record — not a QML row — is the authority.
struct ActiveTransactionRecord {
    std::uint64_t sessionId{};
    ActiveRequestDescriptor request{};
    ActiveTransactionEvidence evidence{};
    TransactionAnalysis analysis{};

    [[nodiscard]] std::uint8_t unitId() const;
    [[nodiscard]] std::uint8_t functionCode() const;

    bool operator==(const ActiveTransactionRecord&) const = default;
};

} // namespace modbuslens::core