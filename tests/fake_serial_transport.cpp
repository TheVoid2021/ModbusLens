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
        lastStart_ = ActiveStartResult{false, TransportDisposition::NotSent};
        emit transportError(QStringLiteral("串口未连接：请先打开串口"));
        return lastStart_;
    }
    if (hasPendingTransaction()) {
        lastStart_ = ActiveStartResult{false, TransportDisposition::NotSent};
        emit transportError(QStringLiteral("串口忙：已有事务进行中"));
        return lastStart_;
    }
    if (!acceptRequests_) {
        // Explicit pre-send rejection: provably zero bytes left the process.
        lastStart_ = ActiveStartResult{false, TransportDisposition::NotSent};
        emit transportError(QStringLiteral("串口请求被拒绝"));
        return lastStart_;
    }

    const auto begin = session_.beginActiveRequest(request);
    if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
        lastStart_ = ActiveStartResult{false, TransportDisposition::NotSent};
        emit transportError(QStringLiteral("串口请求无效"));
        return lastStart_;
    }

    pending_ = std::get<modbuslens::core::ActiveRequestDescriptor>(begin);
    deliveredBytes_.clear();
    ++sendCount_;
    sentAduLog_.push_back(pending_->wire);
    lastStart_ = ActiveStartResult{true, TransportDisposition::PossiblySent};
    return lastStart_;
}

bool RecordingSerialTransport::isPortOpen() const
{
    return portOpen_;
}

void RecordingSerialTransport::closePort()
{
    // Intentional close: silent abort, exactly like the production adapter.
    session_.cancel();
    pending_.reset();
    deliveredBytes_.clear();
    portOpen_ = false;
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
    session_.cancel();
    pending_.reset();
    deliveredBytes_.clear();
    emit transportError(message);
}