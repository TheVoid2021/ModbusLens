#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/WritePrepareValidation.h"
#include "core/analysis/TransactionProvenance.h"

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M10-C1: PreparedWriteSnapshot + its one-shot state machine (Pure C++20).
//
// Three authorities are deliberately kept apart (M10-C Phase 1 §K1):
//   · draft            — QML page-local, mutable, never seen here;
//   · prepared snapshot — Controller/runtime, immutable, token-identified;
//   · dispatch         — Controller -> encoder -> transport (M10-D/E).
//
// The snapshot stores NO wire bytes: M10-C1 has no 0x06/0x10 encoder, and the
// raw request ADU only ever becomes evidence when a real dispatch encodes it.
// It also stores no derived quantity: `preparedQuantity()` derives it from the
// intent, so a second truth cannot exist.
// ---------------------------------------------------------------------------

enum class PreparedWriteState {
    None,        // nothing prepared
    Prepared,    // an immutable snapshot awaits confirmation
    Consumed,    // confirmed (terminal for this generation)
    Invalidated, // cancelled / context changed (terminal for this generation)
};

enum class PreparedWriteInvalidReason {
    UserCancelled,          // explicit Cancel / Escape
    Disconnected,           // the serial connection was closed or lost
    SessionChanged,         // a new Active Serial session was established
    SourceChanged,          // the data source was replaced
    BusyBecameTrue,         // another request entered flight (single in-flight)
    CapabilityUnavailable,  // dispatch capability missing (reserved for D/E)
};

// Immutable validated write intent captured at click-Write time.
struct PreparedWriteSnapshot {
    // Opaque monotonic generation. Not a session id, not a row index, not a
    // pointer: QML will only hand this value back to the Controller.
    std::uint64_t token{};
    ActiveRequestIntent intent{};
    TransactionSourceKind sourceKind{TransactionSourceKind::ActiveSerial};
    std::uint64_t sessionId{};
    // Immutable display-only label ("COM3 @ 9600"). Never used for
    // source/session equality — those use sourceKind + sessionId.
    std::string connectionLabel;

    bool operator==(const PreparedWriteSnapshot&) const = default;
};

// Derived read-only projection of the 0x10 value count (0 for other functions).
[[nodiscard]] std::uint16_t preparedQuantity(const ActiveRequestIntent& intent);

// ---- store results (typed, never a bare bool / never a UI string) ----

struct PreparedWrite {
    bool operator==(const PreparedWrite&) const = default;
};

struct PrepareAlreadyPrepared {
    bool operator==(const PrepareAlreadyPrepared&) const = default;
};

enum class PrepareRejectReason {
    Busy,                   // serialBusy was true when prepare was requested
    NotConnected,           // no Active Serial connection
    SourceNotActiveSerial,  // the authoritative source is not Active Serial
    ValidationFailed,       // see WriteValidationError (validation failure)
};

struct PrepareRejected {
    PrepareRejectReason reason{};
    // Present only when reason == ValidationFailed (line/value context for the
    // UI, still a local-preparation fact — never a Modbus outcome).
    std::optional<WriteValidationError> validationError;

    bool operator==(const PrepareRejected&) const = default;
};

// Controller-level prepare outcome: the store's two answers plus the context
// rejections the runtime owns (source / connection / busy / validation).
using WritePrepareOutcome =
    std::variant<PreparedWrite, PrepareAlreadyPrepared, PrepareRejected>;

struct ConfirmAccepted {
    bool operator==(const ConfirmAccepted&) const = default;
};

enum class ConfirmRejectReason {
    NotPrepared,      // no snapshot / already terminal
    TokenMismatch,    // stale or foreign token
    NotConnected,
    SourceNotActiveSerial,
    SessionChanged,
    Busy,
};

struct ConfirmRejected {
    ConfirmRejectReason reason{};

    bool operator==(const ConfirmRejected&) const = default;
};

// Runtime-level confirmation outcome (token/generation lifecycle only — the
// dispatch decision is a separate layer, M10-D/E).
using ConfirmWriteOutcome = std::variant<ConfirmAccepted, ConfirmRejected>;

// ---------------------------------------------------------------------------
// The one-shot store: at most ONE prepared generation at a time.
//
//   None -> Prepared -> Consumed | Invalidated     (both terminal)
//
// A terminal generation is never revived, never re-consumed and never
// re-labeled: once Consumed or Invalidated, later context changes are no-ops,
// so the historical terminal reason cannot be overwritten.
// ---------------------------------------------------------------------------
class PreparedWriteStore
{
public:
    // Replaces nothing: an active Prepared snapshot is kept and the caller is
    // told (repeated Write must not create a second snapshot).
    std::variant<PreparedWrite, PrepareAlreadyPrepared>
    prepare(PreparedWriteSnapshot snapshot);

    // Token match + state == Prepared. Context guards (connection, session,
    // source, busy) belong to the Controller: the store only owns the
    // generation lifecycle.
    std::variant<ConfirmAccepted, ConfirmRejected> confirm(std::uint64_t token);

    // Explicit cancel: Prepared -> Invalidated(UserCancelled).
    bool cancel(std::uint64_t token);

    // Context-driven invalidation (disconnect / session change / source change
    // / busy became true). Only a currently Prepared generation is affected;
    // terminal states are preserved.
    bool invalidate(PreparedWriteInvalidReason reason);

    [[nodiscard]] PreparedWriteState state() const;
    [[nodiscard]] std::optional<std::uint64_t> token() const;
    [[nodiscard]] std::optional<PreparedWriteSnapshot> snapshot() const;
    [[nodiscard]] std::optional<PreparedWriteInvalidReason> invalidReason() const;

private:
    void reset();

    PreparedWriteState state_ = PreparedWriteState::None;
    std::optional<PreparedWriteSnapshot> snapshot_;
    std::optional<PreparedWriteInvalidReason> invalidReason_;
};

} // namespace modbuslens::core