#include "fake_serial_transport.h"

RecordingSerialTransport::RecordingSerialTransport(QObject* parent)
    : SerialTransport(parent)
{
}

void RecordingSerialTransport::setPortOpen(bool open)
{
    portOpen_ = open;
}

void RecordingSerialTransport::setAcceptRequests(bool accept)
{
    acceptRequests_ = accept;
}

void RecordingSerialTransport::setResponseBytes(std::vector<std::uint8_t> bytes)
{
    responseBytes_ = std::move(bytes);
}

void RecordingSerialTransport::setCompletionElapsed(
    std::chrono::milliseconds elapsed)
{
    completionElapsed_ = elapsed;
}

void RecordingSerialTransport::setSubmissionAcceptedBytes(
    std::optional<std::uint16_t> count)
{
    submissionAcceptedBytes_ = count;
}

int RecordingSerialTransport::startAttemptCount() const
{
    return startAttempts_;
}

int RecordingSerialTransport::sendCount() const
{
    return sendCount_;
}

const std::vector<std::vector<std::uint8_t>>& RecordingSerialTransport::sentAduLog() const
{
    return sentAduLog_;
}

modbuslens::core::ActiveStartResult RecordingSerialTransport::lastStartResult() const
{
    return lastStart_;
}

bool RecordingSerialTransport::hasPendingTransaction() const
{
    return session_.state()
        == modbuslens::core::SerialTransactionState::AwaitingResponse;
}

bool RecordingSerialTransport::hasActiveTransaction() const
{
    return hasPendingTransaction();
}

bool RecordingSerialTransport::openPort(const QString& portName, qint32 baudRate)
{
    Q_UNUSED(portName);
    Q_UNUSED(baudRate);
    portOpen_ = true;
    return true;
}

modbuslens::core::ActiveStartResult RecordingSerialTransport::startActiveRequest(
    const modbuslens::core::ActiveRequestDescriptor& request)
{
    using modbuslens::core::ActiveStartResult;
    using modbuslens::core::TransportDisposition;

    ++startAttempts_;

    if (!portOpen_) {
        lastStart_ = ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
        emit transportError(QStringLiteral("串口未连接：请先打开串口"));
        return lastStart_;
    }
    if (hasPendingTransaction()) {
        lastStart_ = ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
        emit transportError(QStringLiteral("串口忙：已有事务进行中"));
        return lastStart_;
    }
    if (!acceptRequests_) {
        // Explicit pre-send rejection: provably zero bytes left the process.
        lastStart_ = ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
        emit transportError(QStringLiteral("串口请求被拒绝"));
        return lastStart_;
    }

    // Explicit ZERO-ACCEPT (M10-D3): the attempt really happened, but the
    // transport API accepted none of the ADU. No pending request, no send
    // count, no ADU log entry and NO terminal — a terminal is the evidence of
    // a PARTIAL handover, while zero accepted bytes is provably clean, so the
    // honest disposition is NotSent. This is the deterministic counterpart of
    // "guards PASS, confirmation consumed, transport called, yet nothing
    // entered the transmission lifecycle".
    if (submissionAcceptedBytes_.has_value() && *submissionAcceptedBytes_ == 0) {
        lastStart_ = ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
        emit transportError(QStringLiteral("串口请求未被接受：0 字节"));
        return lastStart_;
    }

    // Configurable short submission: mirrors the production adapter's
    // short-write branch — the transport API accepts only PART of the ADU.
    if (submissionAcceptedBytes_.has_value()
        && *submissionAcceptedBytes_ > 0
        && *submissionAcceptedBytes_ < request.wire.size()) {
        // No pending is ever established, and the attempt still carries its
        // durable evidence out through the start result.
        lastStart_ = ActiveStartResult{
            false,
            TransportDisposition::PossiblySent,
            modbuslens::core::ActiveTransportTerminal{
                .request = request,
                .responseAdu = {},
                .disposition = TransportDisposition::PossiblySent,
                .reason = modbuslens::core::TransportTerminalReason::ShortSubmission,
                .submissionAcceptedByteCount = *submissionAcceptedBytes_,
            },
        };
        // The ADU was never fully handed over, so it does NOT enter the
        // accepted-ADU log — the terminal evidence carries the intended ADU.
        emit transportError(QStringLiteral("串口写入失败：短计数"));
        return lastStart_;
    }

    const auto begin = session_.beginActiveRequest(request);
    if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
        lastStart_ = ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
        emit transportError(QStringLiteral("串口请求无效"));
        return lastStart_;
    }

    pending_ = std::get<modbuslens::core::ActiveRequestDescriptor>(begin);
    deliveredBytes_.clear();
    ++sendCount_;
    sentAduLog_.push_back(pending_->wire);
    lastStart_ = ActiveStartResult{true, TransportDisposition::PossiblySent, std::nullopt};
    return lastStart_;
}

bool RecordingSerialTransport::isPortOpen() const
{
    return portOpen_;
}

void RecordingSerialTransport::closePort()
{
    // Intentional close: mirrors the production adapter exactly — with a
    // SUBMITTED request the attempt's evidence is emitted as one terminal
    // event; with nothing pending the close stays completely silent.
    emitTerminalIfSubmitted(modbuslens::core::TransportTerminalReason::
                                DisconnectedAfterSubmission);
    portOpen_ = false;
}

void RecordingSerialTransport::emitTerminalIfSubmitted(
    modbuslens::core::TransportTerminalReason reason)
{
    // Snapshot and observed bytes are captured BEFORE the abort clears them.
    if (!pending_.has_value()) {
        session_.cancel();
        return;
    }
    const auto terminal = modbuslens::core::ActiveTransportTerminal{
        .request = *pending_,
        .responseAdu = deliveredBytes_,
        .disposition = modbuslens::core::TransportDisposition::PossiblySent,
        .reason = reason,
        .submissionAcceptedByteCount = std::nullopt,
    };
    session_.cancel();
    pending_.reset();
    deliveredBytes_.clear();
    emit transactionTerminated(terminal);
}

void RecordingSerialTransport::emitCompletion(
    modbuslens::core::TransactionAnalysis analysis)
{
    if (!pending_.has_value()) {
        return;
    }
    emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
        .request = *pending_,
        .responseAdu = deliveredBytes_,
        .disposition = modbuslens::core::TransportDisposition::PossiblySent,
        .analysis = std::move(analysis),
    });
    pending_.reset();
    deliveredBytes_.clear();
}

void RecordingSerialTransport::completeWithResponse()
{
    if (!hasPendingTransaction()) {
        return;
    }
    deliveredBytes_ = responseBytes_;
    const auto result = session_.feedResponseBytes(
        responseBytes_, completionElapsed_);
    if (const auto* analysis =
            std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
        emitCompletion(*analysis);
    }
    // AwaitingMoreData: an incomplete candidate simply does not complete yet.
}

void RecordingSerialTransport::completeWithTimeout()
{
    if (!hasPendingTransaction()) {
        return;
    }
    const auto result = session_.onResponseTimeout(completionElapsed_);
    if (const auto* analysis =
            std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
        emitCompletion(*analysis);
    }
}

void RecordingSerialTransport::feedPartialBytes()
{
    if (!hasPendingTransaction()) {
        return;
    }
    session_.feedResponseBytes(responseBytes_, completionElapsed_);
    deliveredBytes_.insert(deliveredBytes_.end(),
                           responseBytes_.begin(), responseBytes_.end());
}

void RecordingSerialTransport::failTransport(const QString& message)
{
    // Post-submission transport failure: ONE terminal event with the retained
    // evidence (request snapshot + exact request ADU + any bytes observed so
    // far + PossiblySent), then the existing bounded error lane. No Modbus
    // outcome is invented.
    emitTerminalIfSubmitted(
        modbuslens::core::TransportTerminalReason::TransportError);
    emit transportError(message);
}

void RecordingSerialTransport::disconnectAfterSubmission()
{
    emitTerminalIfSubmitted(modbuslens::core::TransportTerminalReason::
                                DisconnectedAfterSubmission);
}