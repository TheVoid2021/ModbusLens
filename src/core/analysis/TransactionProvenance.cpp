#include "core/analysis/TransactionProvenance.h"

namespace modbuslens::core {

std::string_view transactionSourceKindName(TransactionSourceKind kind)
{
    switch (kind) {
    case TransactionSourceKind::Simulator:
        return "simulator";
    case TransactionSourceKind::Replay:
        return "replay";
    case TransactionSourceKind::ActiveSerial:
        return "active_serial";
    }
    return "unknown";
}

} // namespace modbuslens::core