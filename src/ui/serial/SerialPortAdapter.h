#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>

#include "core/analysis/TransactionAnalysis.h"
#include "core/serial/SerialTransactionSession.h"

// ---------------------------------------------------------------------------
// Serial Transaction Adapter (T010 Part A): the ONLY Qt-side serial layer.
// Thin shell that owns QSerialPort + a single-shot response-timeout QTimer +
// QElapsedTimer around a Pure SerialTransactionSession. All protocol
// reasoning (framing, decode, analysis) stays in the core session.
//
// Async contract: never waitForReadyRead/waitForBytesWritten/sleep. The
// QTimer is used solely as the response-timeout callback — not a polling
// loop.
// ---------------------------------------------------------------------------
class SerialTransactionAdapter : public QObject
{
    Q_OBJECT

public:
    explicit SerialTransactionAdapter(QObject* parent = nullptr);

    // Open/configure the port (8N1 + caller baud), begin one FC03 transaction
    // and write the request wire. Returns false and emits transportError() on
    // any immediate transport/usage failure (open failed, session busy or
    // invalid begin, write failed) — in those cases the session is cancelled
    // and nothing is left pending. Success means the response will arrive via
    // transactionCompleted() or a transportError() signal later.
    bool startTransaction(const QString& portName, int baudRate,
                          std::uint8_t slaveAddress, std::uint16_t startAddress,
                          std::uint16_t quantity,
                          std::chrono::milliseconds timeout);

    [[nodiscard]] bool hasActiveTransaction() const;
    [[nodiscard]] bool isPortOpen() const;

public slots:
    // Local close: cancels any pending transaction WITHOUT emitting a
    // transaction result (a user-requested close is not a Modbus diagnosis).
    void closePort();

signals:
    void transactionCompleted(modbuslens::core::TransactionAnalysis analysis);
    void transportError(const QString& message);

private slots:
    void handleReadyRead();
    void handleTimeout();
    void handlePortError(QSerialPort::SerialPortError error);

private:
    void cancelPending();

    QSerialPort port_;
    QTimer timeoutTimer_;
    QElapsedTimer elapsed_;
    modbuslens::core::SerialTransactionSession session_;
    // Breaks the errorOccurred feedback loop (see T010 archive): closing an
    // already-failed port re-emits DeviceNotFoundError forever, so once a
    // fatal error is handled, further emissions are ignored until the next
    // startTransaction.
    bool suppressPortErrors_ = false;
};