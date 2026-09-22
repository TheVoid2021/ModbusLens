#include "ui/serial/SerialPortAdapter.h"

#include <QByteArray>

#include <utility>
#include <vector>

namespace {

// QByteArray -> span-friendly buffer (the adapter's only data conversion).
std::vector<std::uint8_t> toBytes(const QByteArray& data)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(static_cast<std::size_t>(data.size()));
    for (const char byte : data) {
        bytes.push_back(static_cast<std::uint8_t>(byte));
    }
    return bytes;
}

} // namespace

SerialTransactionAdapter::SerialTransactionAdapter(QObject* parent)
    : SerialTransport(parent)
{
    // 8N1 — the v1 fixed configuration (baud is caller-provided).
    port_.setDataBits(QSerialPort::Data8);
    port_.setParity(QSerialPort::NoParity);
    port_.setStopBits(QSerialPort::OneStop);
    port_.setFlowControl(QSerialPort::NoFlowControl);

    timeoutTimer_.setSingleShot(true);

    connect(&port_, &QSerialPort::readyRead, this, &SerialTransactionAdapter::handleReadyRead);
    connect(&timeoutTimer_, &QTimer::timeout, this, &SerialTransactionAdapter::handleTimeout);
    // Queued on purpose: errorOccurred fires SYNCHRONOUSLY inside open() on
    // failure, and re-entering the half-open port from that call stack
    // corrupts Qt state. Queued delivery defers handling until the event
    // loop; the synchronous open-failure path in startActiveRequest already
    // reports the transport error itself.
    connect(&port_, &QSerialPort::errorOccurred,
            this, &SerialTransactionAdapter::handlePortError,
            Qt::QueuedConnection);
}

bool SerialTransactionAdapter::openPort(const QString& portName, qint32 baudRate)
{
    // Reject a port switch while a transaction runs: the transaction owns
    // the wire for its whole lifetime.
    if (hasActiveTransaction()) {
        emit transportError(
            QStringLiteral("串口忙：已有事务进行中"));
        return false;
    }

    suppressPortErrors_ = false;
    port_.setPortName(portName);
    port_.setBaudRate(baudRate);
    if (!port_.open(QIODevice::ReadWrite)) {
        // Synchronous single report. The queued errorOccurred delivery that
        // follows is the SAME failure, already reported here, so the PE-4
        // guard is armed now: exactly one user-visible emission per failed
        // open (locked by SERIAL-I02).
        suppressPortErrors_ = true;
        emit transportError(
            QStringLiteral("串口打开失败：%1").arg(port_.errorString()));
        return false;
    }
    return true;
}

modbuslens::core::ActiveStartResult SerialTransactionAdapter::startActiveRequest(
    const modbuslens::core::ActiveRequestDescriptor& request)
{
    using modbuslens::core::ActiveStartResult;
    using modbuslens::core::ActiveTransportTerminal;
    using modbuslens::core::TransportDisposition;
    using modbuslens::core::TransportTerminalReason;

    if (!port_.isOpen()) {
        emit transportError(QStringLiteral("串口未连接：请先打开串口"));
        return ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
    }
    if (hasActiveTransaction()) {
        emit transportError(QStringLiteral("串口忙：已有事务进行中"));
        return ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
    }

    const auto begin = session_.beginActiveRequest(request);
    if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
        emit transportError(QStringLiteral("串口请求无效"));
        return ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
    }
    const auto& accepted = std::get<modbuslens::core::ActiveRequestDescriptor>(begin);

    observedResponseBytes_.clear();
    const auto written = port_.write(
        reinterpret_cast<const char*>(accepted.wire.data()),
        static_cast<qint64>(accepted.wire.size()));
    if (written != static_cast<qint64>(accepted.wire.size())) {
        // A short write is a local transport fact — never a 1000ms Timeout.
        // Two distinct dispositions, because the wire facts differ:
        //   <= 0 bytes accepted -> provably nothing left the process: NotSent
        //   >  0 bytes accepted -> PART of the ADU may already be on the wire:
        //                          PossiblySent, and the attempt must not
        //                          evaporate without durable evidence.
        // The port is unusable after a failed write: close it and let the
        // controller re-sync from isPortOpen().
        if (written <= 0) {
            cancelPending();
            emit transportError(
                QStringLiteral("串口写入失败：%1").arg(port_.errorString()));
            return ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
        }

        // Evidence BEFORE the abort: the session cleanup below clears the
        // buffers, so the snapshot and the accepted-byte count are captured
        // first. No Modbus outcome is produced for this attempt.
        ActiveStartResult result{
            false,
            TransportDisposition::PossiblySent,
            ActiveTransportTerminal{
                .request = accepted,
                .responseAdu = {},
                .disposition = TransportDisposition::PossiblySent,
                .reason = TransportTerminalReason::ShortSubmission,
                .submissionAcceptedByteCount =
                    static_cast<std::uint16_t>(written),
            },
        };
        cancelPending();
        emit transportError(
            QStringLiteral("串口写入失败：%1").arg(port_.errorString()));
        return result;
    }

    // Full write: the request is in the transmission lifecycle. Response
    // waiting starts here (the QTimer is the response-timeout callback), and
    // the threshold comes from the intent snapshot — never from a second
    // parameter that could disagree with it.
    elapsed_.start();
    timeoutTimer_.start(static_cast<int>(accepted.intent.timeout.count()));
    return ActiveStartResult{true, TransportDisposition::PossiblySent, std::nullopt};
}

bool SerialTransactionAdapter::hasActiveTransaction() const
{
    return session_.state() == modbuslens::core::SerialTransactionState::AwaitingResponse;
}

bool SerialTransactionAdapter::isPortOpen() const
{
    return port_.isOpen();
}

void SerialTransactionAdapter::closePort()
{
    using modbuslens::core::ActiveTransportTerminal;
    using modbuslens::core::TransportDisposition;
    using modbuslens::core::TransportTerminalReason;

    // An explicit close with a SUBMITTED request is a post-submission
    // termination: the bytes may already be on the wire, so the attempt's
    // evidence is emitted before anything is cleared. Without a pending
    // transaction a close stays completely silent (pure connection change).
    const auto pending = session_.pendingRequest();
    const auto observed = observedResponseBytes_;
    const bool wasSubmitted = pending.has_value();

    suppressPortErrors_ = true; // closing triggers error emissions; ignore
    session_.cancel();
    timeoutTimer_.stop();
    port_.close();
    elapsed_.invalidate();
    observedResponseBytes_.clear();

    if (wasSubmitted) {
        emit transactionTerminated(ActiveTransportTerminal{
            .request = *pending,
            .responseAdu = observed,
            .disposition = TransportDisposition::PossiblySent,
            .reason = TransportTerminalReason::DisconnectedAfterSubmission,
            .submissionAcceptedByteCount = std::nullopt,
        });
    }
}

void SerialTransactionAdapter::handleReadyRead()
{
    const auto bytes = toBytes(port_.readAll());
    // Evidence first: whatever arrived is retained verbatim, even when it
    // turns out to be an incomplete or corrupt candidate.
    observedResponseBytes_.insert(
        observedResponseBytes_.end(), bytes.begin(), bytes.end());

    // The send-time snapshot must be captured BEFORE feeding: a completed
    // feed resets the session to Idle.
    const auto pending = session_.pendingRequest();

    const auto result = session_.feedResponseBytes(
        bytes, std::chrono::milliseconds{elapsed_.elapsed()});

    if (const auto* analysis =
            std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
        timeoutTimer_.stop();
        if (pending.has_value()) {
            emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                .request = *pending,
                .responseAdu = observedResponseBytes_,
                .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                .analysis = *analysis,
            });
        }
        observedResponseBytes_.clear();
    }
}

void SerialTransactionAdapter::handleTimeout()
{
    const auto pending = session_.pendingRequest();

    const auto result = session_.onResponseTimeout(
        std::chrono::milliseconds{elapsed_.elapsed()});

    if (const auto* analysis =
            std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
        if (pending.has_value()) {
            emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                .request = *pending,
                // Empty when nothing was ever observed: the honest
                // representation of a pure no-response timeout.
                .responseAdu = observedResponseBytes_,
                .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                .analysis = *analysis,
            });
        }
        observedResponseBytes_.clear();
    }
    // NotActive can only mean a stray timer fire after completion — the
    // timer is stopped on completion, nothing to do here.
}

bool SerialTransactionAdapter::isFatalLocalPortFailure(
    QSerialPort::SerialPortError error)
{
    switch (error) {
    case QSerialPort::NoError:
        // Not an error at all.
        return false;
    case QSerialPort::TimeoutError:
        // Only the blocking waitFor*() calls produce this, and this adapter
        // never makes one. A response timeout is decided by the session's own
        // threshold callback, NOT by a QSerialPort error — so nothing here can
        // legitimately mean "the device was removed".
        return false;
    case QSerialPort::NotOpenError:
        // "The device is not open" describes an operation attempted on a
        // closed handle. It states nothing about a port that WAS open, so it
        // cannot prove a removal.
        return false;
    default:
        // ResourceError / DeviceNotFoundError / PermissionError / ReadError /
        // WriteError / OpenError / UnsupportedOperationError / UnknownError:
        // the LOCAL port is no longer usable (device removed, handle invalid,
        // or a real I/O failure). The connection is over.
        return true;
    }
}

void SerialTransactionAdapter::deliverPortErrorForTest(
    QSerialPort::SerialPortError error)
{
    handlePortError(error);
}

void SerialTransactionAdapter::handlePortError(QSerialPort::SerialPortError error)
{
    using modbuslens::core::ActiveTransportTerminal;
    using modbuslens::core::TransportDisposition;
    using modbuslens::core::TransportTerminalReason;

    if (error == QSerialPort::NoError || suppressPortErrors_) {
        return;
    }
    if (!isFatalLocalPortFailure(error)) {
        // NOT a removal — see the classification. Tearing the port down on a
        // timeout/not-open state would convert a transaction-level fact into a
        // fabricated "device removed" claim, and would drop a healthy port.
        return;
    }
    suppressPortErrors_ = true;

    // Capture the cause BEFORE the close: QSerialPort::close() resets the
    // port's error, so reading errorString() afterwards would report "no
    // error" instead of the reason the user needs to see.
    const QString cause = port_.errorString();

    // Evidence FIRST, abort second: the port error is NOT a Modbus response,
    // so no TransactionAnalysis is fabricated — but a request that already
    // entered the transmission lifecycle keeps its snapshot, its exact wire
    // bytes and any response bytes observed so far. Clearing the abort first
    // would destroy exactly the evidence a future write needs.
    const auto pending = session_.pendingRequest();
    const auto observed = observedResponseBytes_;
    const bool wasSubmitted = pending.has_value();

    if (wasSubmitted) {
        session_.cancel();
        timeoutTimer_.stop();
    }
    port_.close();
    elapsed_.invalidate();
    observedResponseBytes_.clear();

    if (wasSubmitted) {
        // Terminal transport fact: the frozen post-submission evidence
        // semantics (taxonomy unchanged — a port failure is TransportError,
        // and no Modbus outcome is invented for it).
        emit transactionTerminated(ActiveTransportTerminal{
            .request = *pending,
            .responseAdu = observed,
            .disposition = TransportDisposition::PossiblySent,
            .reason = TransportTerminalReason::TransportError,
            .submissionAcceptedByteCount = std::nullopt,
        });
    }
    // M10-E4: the LOCAL port is gone. That is a user-visible CONNECTION fact
    // whether or not a request was in flight — before this correction an idle
    // removal closed the port SILENTLY and the owner kept presenting a
    // connection that no longer existed (stale "已连接", enabled actions, and
    // a confirmation snapshot outliving its session). The message names the
    // LOCAL device only: a port failure never claims anything about a remote
    // Modbus slave.
    emit transportError(QStringLiteral("串口设备不可用：%1").arg(cause));
}

void SerialTransactionAdapter::cancelPending()
{
    suppressPortErrors_ = true; // the close below re-emits errors; ignore
    session_.cancel();
    timeoutTimer_.stop();
    port_.close();
    elapsed_.invalidate();
    observedResponseBytes_.clear();
}