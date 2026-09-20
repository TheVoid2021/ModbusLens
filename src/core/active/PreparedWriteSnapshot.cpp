#include "core/active/PreparedWriteSnapshot.h"

namespace modbuslens::core {

std::uint16_t preparedQuantity(const ActiveRequestIntent& intent)
{
    // Derived on demand — never stored, so it cannot disagree with the intent.
    // 0x06 writes exactly one register; 0x10 derives from values.size(), the
    // single value authority. Read requests keep their read quantity for
    // completeness (the write path never projects them).
    if (const auto* payload =
            std::get_if<WriteMultipleRegistersIntent>(&intent.payload)) {
        return static_cast<std::uint16_t>(payload->values.size());
    }
    if (std::holds_alternative<WriteSingleRegisterIntent>(intent.payload)) {
        return 1;
    }
    if (const auto* read =
            std::get_if<ReadHoldingRegistersIntent>(&intent.payload)) {
        return read->quantity;
    }
    return 0;
}

std::variant<PreparedWrite, PrepareAlreadyPrepared>
PreparedWriteStore::prepare(PreparedWriteSnapshot snapshot)
{
    if (state_ == PreparedWriteState::Prepared) {
        // Repeated Write activation: keep the original generation and tell the
        // caller, so no second snapshot / second dialog can appear.
        return PrepareAlreadyPrepared{};
    }
    snapshot_ = std::move(snapshot);
    invalidReason_.reset();
    state_ = PreparedWriteState::Prepared;
    return PreparedWrite{};
}

std::variant<ConfirmAccepted, ConfirmRejected>
PreparedWriteStore::confirm(std::uint64_t token)
{
    if (state_ != PreparedWriteState::Prepared || !snapshot_.has_value()) {
        // Already Consumed / Invalidated / nothing prepared: a second
        // confirmation of the same token can never succeed.
        return ConfirmRejected{ConfirmRejectReason::NotPrepared};
    }
    if (snapshot_->token != token) {
        return ConfirmRejected{ConfirmRejectReason::TokenMismatch};
    }
    reset();
    state_ = PreparedWriteState::Consumed;
    return ConfirmAccepted{};
}

bool PreparedWriteStore::cancel(std::uint64_t token)
{
    if (state_ != PreparedWriteState::Prepared || !snapshot_.has_value()) {
        return false;
    }
    if (snapshot_->token != token) {
        return false;
    }
    reset();
    state_ = PreparedWriteState::Invalidated;
    invalidReason_ = PreparedWriteInvalidReason::UserCancelled;
    return true;
}

bool PreparedWriteStore::invalidate(PreparedWriteInvalidReason reason)
{
    // Only the currently prepared generation can be invalidated: a terminal
    // state (and its historical reason) is never rewritten.
    if (state_ != PreparedWriteState::Prepared) {
        return false;
    }
    reset();
    state_ = PreparedWriteState::Invalidated;
    invalidReason_ = reason;
    return true;
}

PreparedWriteState PreparedWriteStore::state() const
{
    return state_;
}

std::optional<std::uint64_t> PreparedWriteStore::token() const
{
    if (!snapshot_.has_value()) {
        return std::nullopt;
    }
    return snapshot_->token;
}

std::optional<PreparedWriteSnapshot> PreparedWriteStore::snapshot() const
{
    return snapshot_;
}

std::optional<PreparedWriteInvalidReason> PreparedWriteStore::invalidReason() const
{
    return invalidReason_;
}

void PreparedWriteStore::reset()
{
    snapshot_.reset();
}

} // namespace modbuslens::core