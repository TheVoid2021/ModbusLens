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
    : QObject(parent)
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
    // loop; the synchronous open-failure path in startTransaction already
    // reports the transport error itself.
    connect(&port_, &QSerialPort::errorOccurred,
            this, &SerialTransactionAdapter::handlePortError,
            Qt::QueuedConnection);
}

bool SerialTransactionAdapter::startTransaction(
    const QString& portName, int baudRate, std::uint8_t slaveAddress,
    std::uint16_t startAddress, std::uint16_t quantity,
    std::chrono::milliseconds timeout)
{
    if (hasActiveTransaction()) {
        emit transportError(QStringLiteral("Serial busy: a transaction is already pending"));
        return false;
    }

    suppressPortErrors_ = false;
    port_.setPortName(portName);
    port_.setBaudRate(baudRate);
    if (!port_.open(QIODevice::ReadWrite)) {
        emit transportError(
            QStringLiteral("Serial open failed: %1").arg(port_.errorString()));
        return false;
    }

    const auto begin = session_.beginReadHoldingRegisters(
        slaveAddress, startAddress, quantity, timeout);
    const auto* beginError = std::get_if<modbuslens::core::SerialTransactionError>(&begin);
    if (beginError != nullptr) {
        port_.close();
        emit transportError(QStringLiteral("Serial begin failed: invalid request"));
        return false;
    }
    const auto& start = std::get<modbuslens::core::SerialRequestStart>(begin);

    const auto written = port_.write(
        reinterpret_cast<const char*>(start.requestWire.data()),
        static_cast<qint64>(start.requestWire.size()));
    if (written != static_cast<qint64>(start.requestWire.size())) {
        // Write failure is a local transport fact — never a 1000ms Timeout.
        cancelPending();
        emit transportError(
            QStringLiteral("Serial write failed: %1").arg(port_.errorString()));
        return false;
    }

    elapsed_.start();
    timeoutTimer_.start(static_cast<int>(timeout.count()));
    return true;
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
    // User-requested close: abort silently — not a Modbus diagnosis.
    suppressPortErrors_ = true; // closing triggers error emissions; ignore
    session_.cancel();
    timeoutTimer_.stop();
    port_.close();
    elapsed_.invalidate();
}

void SerialTransactionAdapter::handleReadyRead()
{
    const auto bytes = toBytes(port_.readAll());
    const auto result = session_.feedResponseBytes(
        bytes, std::chrono::milliseconds{elapsed_.elapsed()});

    if (const auto* analysis =
            std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
        timeoutTimer_.stop();
        emit transactionCompleted(*analysis);
    }
}

void SerialTransactionAdapter::handleTimeout()
{
    const auto result = session_.onResponseTimeout(
        std::chrono::milliseconds{elapsed_.elapsed()});

    if (const auto* analysis =
            std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
        emit transactionCompleted(*analysis);
    }
    // NotActive can only mean a stray timer fire after completion — the
    // timer is stopped on completion, nothing to do here.
}

void SerialTransactionAdapter::handlePortError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || suppressPortErrors_) {
        return;
    }
    // A failed/closed port keeps re-emitting DeviceNotFoundError: handle the
    // fatal fact exactly once, then swallow the rest until the next start.
    suppressPortErrors_ = true;
    if (hasActiveTransaction()) {
        // Transport failure aborts the transaction WITHOUT fabricating a
        // Modbus status (Timeout stays a purely "no response" fact).
        session_.cancel();
        timeoutTimer_.stop();
        emit transportError(
            QStringLiteral("Serial port error: %1").arg(port_.errorString()));
    }
    port_.close();
    elapsed_.invalidate();
}

void SerialTransactionAdapter::cancelPending()
{
    suppressPortErrors_ = true; // the close below re-emits errors; ignore
    session_.cancel();
    timeoutTimer_.stop();
    port_.close();
    elapsed_.invalidate();
}