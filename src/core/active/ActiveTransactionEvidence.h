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

// ---------------------------------------------------------------------------
// M10-A correction: post-submission TRANSPORT TERMINATION.
//
// A request that was already handed to the transmission lifecycle can end
// WITHOUT any trusted Modbus response — the port fails, the user
// disconnects, the source is torn down. That ending is a TRANSPORT fact, so
// it must never be squeezed into the Modbus outcome taxonomy (no fabricated
// ProtocolError / Timeout / Exception / Success), and its evidence must
// never be discarded together with the pending transaction.
// ---------------------------------------------------------------------------

enum class TransportTerminalReason {
    TransportError,              // port/transport failure after submission
    DisconnectedAfterSubmission, // explicit close / cancel / source teardown
};

[[nodiscard]] std::string_view transportTerminalReasonName(TransportTerminalReason reason);

// Evidence of one submitted request whose transaction ended without a trusted
// Modbus response. Deliberately carries NO TransactionAnalysis: for a future
// write this is exactly the "device mutation state UNKNOWN" case, and
// claiming NotSent / "device unchanged" / "write definitely failed" would all
// be unsupported statements.
//
// Authority is this record + the typed reason; the human-readable transport
// error text stays where it already lives (the serial error lane) and is
// never the evidence authority.
struct ActiveTransportTerminal {
    // Send-time snapshot: the request as it was handed to the transport.
    ActiveRequestDescriptor request{};
    // Every byte the transport actually observed before the termination
    // (empty when nothing arrived). Never cleared as part of the abort.
    std::vector<std::uint8_t> responseAdu{};
    // Always PossiblySent on this path: the request entered the transmission
    // lifecycle, so the wire cannot be proven clean.
    TransportDisposition disposition{TransportDisposition::PossiblySent};
    TransportTerminalReason reason{TransportTerminalReason::TransportError};

    [[nodiscard]] ActiveTransactionEvidence evidence() const;

    bool operator==(const ActiveTransportTerminal&) const = default;
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