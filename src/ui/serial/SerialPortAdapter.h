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

    // ---- Transport lifecycle (T010 Part B evolution) ----
    // Open and configure the port ONLY (8N1 + caller baud). No Modbus state
    // is touched. On failure returns false, emits a BOUNDED transportError()
    // (exactly one user-visible emission — see PE-4) and leaves the port
    // closed. Never fabricates a TransactionStatus.
    bool openPort(const QString& portName, qint32 baudRate);

    // ---- Transaction lifecycle (T010 Part B evolution) ----
    // REQUIRES an already-open port (see openPort): runs exactly ONE FC03
    // transaction and writes the request wire. Returns false + a bounded
    // transportError() on any failure (port closed, busy, invalid begin,
    // write failed) — the session is never left half-pending and no
    // TransactionStatus is invented. Success means the result arrives via
    // transactionCompleted() or a transportError() later.
    bool startTransaction(std::uint8_t slaveAddress, std::uint16_t startAddress,
                          std::uint16_t quantity,
                          std::chrono::milliseconds timeout);

    [[nodiscard]] bool hasActiveTransaction() const;
    [[nodiscard]] bool isPortOpen() const;

public slots:
    // Intentional disconnect: cancels any pending transaction WITHOUT
    // emitting a transaction result or a transport error (a user close is
    // neither a Modbus diagnosis nor a transport failure).
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
    // Breaks the errorOccurred feedback loop (see T010 archive PE-4): closing
    // an already-failed port re-emits DeviceNotFoundError forever, so once a
    // fatal error is handled, further emissions are ignored until the next
    // openPort/start attempt.
    bool suppressPortErrors_ = false;
};