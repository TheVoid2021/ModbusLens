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
        // Synchronous single report; the queued errorOccurred feedback that
        // follows is suppressed by the PE-4 guard (bounded at exactly one
        // user-visible emission — locked by SERIAL-I02).
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
    using modbuslens::core::TransportDisposition;

    if (!port_.isOpen()) {
        emit transportError(QStringLiteral("串口未连接：请先打开串口"));
        return ActiveStartResult{false, TransportDisposition::NotSent};
    }
    if (hasActiveTransaction()) {
        emit transportError(QStringLiteral("串口忙：已有事务进行中"));
        return ActiveStartResult{false, TransportDisposition::NotSent};
    }

    const auto begin = session_.beginActiveRequest(request);
    if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
        emit transportError(QStringLiteral("串口请求无效"));
        return ActiveStartResult{false, TransportDisposition::NotSent};
    }
    const auto& accepted = std::get<modbuslens::core::ActiveRequestDescriptor>(begin);

    observedResponseBytes_.clear();
    const auto written = port_.write(
        reinterpret_cast<const char*>(accepted.wire.data()),
        static_cast<qint64>(accepted.wire.size()));
    if (written != static_cast<qint64>(accepted.wire.size())) {
        // A short write is a local transport fact — never a 1000ms Timeout —
        // but the bytes already accepted by Qt cannot be proven absent from
        // the wire: PossiblySent, not NotSent. The port is unusable after a
        // failed write: close it and let the controller re-sync from
        // isPortOpen().
        cancelPending();
        emit transportError(
            QStringLiteral("串口写入失败：%1").arg(port_.errorString()));
        return ActiveStartResult{false, TransportDisposition::PossiblySent};
    }

    // Full write: the request is in the transmission lifecycle. Response
    // waiting starts here (the QTimer is the response-timeout callback), and
    // the threshold comes from the intent snapshot — never from a second
    // parameter that could disagree with it.
    elapsed_.start();
    timeoutTimer_.start(static_cast<int>(accepted.intent.timeout.count()));
    return ActiveStartResult{true, TransportDisposition::PossiblySent};
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

void SerialTransactionAdapter::handlePortError(QSerialPort::SerialPortError error)
{
    using modbuslens::core::ActiveTransportTerminal;
    using modbuslens::core::TransportDisposition;
    using modbuslens::core::TransportTerminalReason;

    if (error == QSerialPort::NoError || suppressPortErrors_) {
        return;
    }
    // A failed/closed port keeps re-emitting DeviceNotFoundError: handle the
    // fatal fact exactly once, then swallow the rest until the next start.
    suppressPortErrors_ = true;

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
        // Terminal transport fact, then the existing error lane (bounded,
        // presentation-only). The controller must never receive a second
        // terminal for this request.
        emit transactionTerminated(ActiveTransportTerminal{
            .request = *pending,
            .responseAdu = observed,
            .disposition = TransportDisposition::PossiblySent,
            .reason = TransportTerminalReason::TransportError,
        });
        emit transportError(
            QStringLiteral("串口错误：%1").arg(port_.errorString()));
    }
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