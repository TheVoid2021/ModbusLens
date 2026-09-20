#pragma once

#include <QElapsedTimer>
#include <QSerialPort>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <vector>

#include "core/active/ActiveTransactionEvidence.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/serial/SerialTransactionSession.h"
#include "ui/serial/SerialTransport.h"

// ---------------------------------------------------------------------------
// Serial Transaction Adapter (T010 Part A): the ONLY Qt-side serial layer.
// Thin shell that owns QSerialPort + a single-shot response-timeout QTimer +
// QElapsedTimer around a Pure SerialTransactionSession. All protocol
// reasoning (framing, decode, analysis) stays in the core session.
//
// M10-A: this class is the PRODUCTION implementation of the SerialTransport
// seam — its real-port behaviour (open/close, readyRead, timeout, port error)
// is unchanged; the seam only inverted the dependency so tests can inject a
// recording transport instead.
//
// Async contract: never waitForReadyRead/waitForBytesWritten/sleep. The
// QTimer is used solely as the response-timeout callback — not a polling
// loop.
// ---------------------------------------------------------------------------
class SerialTransactionAdapter : public SerialTransport
{
    Q_OBJECT

public:
    explicit SerialTransactionAdapter(QObject* parent = nullptr);

    // ---- Transport lifecycle (T010 Part B evolution) ----
    bool openPort(const QString& portName, qint32 baudRate) override;

    // ---- Transaction lifecycle (T010 Part B evolution + M10-A seam) ----
    // REQUIRES an already-open port (see openPort): writes the descriptor's
    // EXACT wire bytes and waits for the response. The disposition is a
    // transport fact:
    //   not open / busy / invalid descriptor -> {false, NotSent}
    //   short write (unusable port)          -> {false, PossiblySent}
    //   full write                           -> {true,  PossiblySent}
    // A full write only proves Qt accepted the bytes — never that the device
    // received them.
    modbuslens::core::ActiveStartResult startActiveRequest(
        const modbuslens::core::ActiveRequestDescriptor& request) override;

    [[nodiscard]] bool hasActiveTransaction() const override;
    [[nodiscard]] bool isPortOpen() const override;

public slots:
    void closePort() override;

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
    // Every byte this transport actually observed for the pending
    // transaction, kept verbatim (damaged/partial bytes included) so the
    // completion envelope never has to reconstruct evidence.
    std::vector<std::uint8_t> observedResponseBytes_;
    // Breaks the errorOccurred feedback loop (see T010 archive PE-4): closing
    // an already-failed port re-emits DeviceNotFoundError forever, so once a
    // fatal error is handled, further emissions are ignored until the next
    // openPort/start attempt.
    bool suppressPortErrors_ = false;
};