#include "core/active/ActiveTransactionEvidence.h"

namespace modbuslens::core {

std::string_view transportDispositionName(TransportDisposition disposition)
{
    switch (disposition) {
    case TransportDisposition::NotSent:
        return "not_sent";
    case TransportDisposition::PossiblySent:
        return "possibly_sent";
    }
    return "unknown";
}

ActiveTransactionEvidence ActiveTransactionResult::evidence() const
{
    return ActiveTransactionEvidence{
        .requestAdu = request.wire,
        .responseAdu = responseAdu,
        .disposition = disposition,
    };
}

std::string_view transportTerminalReasonName(TransportTerminalReason reason)
{
    switch (reason) {
    case TransportTerminalReason::TransportError:
        return "transport_error";
    case TransportTerminalReason::DisconnectedAfterSubmission:
        return "disconnected_after_submission";
    }
    return "unknown";
}

ActiveTransactionEvidence ActiveTransportTerminal::evidence() const
{
    return ActiveTransactionEvidence{
        .requestAdu = request.wire,
        .responseAdu = responseAdu,
        .disposition = disposition,
    };
}

std::uint8_t ActiveTransactionRecord::unitId() const
{
    return request.intent.unitId;
}

std::uint8_t ActiveTransactionRecord::functionCode() const
{
    return activeFunctionCode(request.intent.function);
}

} // namespace modbuslens::core