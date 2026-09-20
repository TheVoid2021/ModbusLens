#pragma once

#include <string_view>

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M10-A: transaction provenance (Pure C++20, Zero Qt).
//
// Which data source authored a transaction is a FACT about the transaction,
// so it travels as a typed enum — never inferred from a workspace index, a
// modeLabel string or a filename (M10-A §22).
// ---------------------------------------------------------------------------

enum class TransactionSourceKind {
    Simulator,    // deterministic demo batch (SimulatedSlave + fault seam)
    Replay,       // captured .mlog batch
    ActiveSerial, // master-initiated transactions over a live serial session
};

[[nodiscard]] std::string_view transactionSourceKindName(TransactionSourceKind kind);

} // namespace modbuslens::core