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

    // ---- Fatal local port failure (M10-E4) ----
    //
    // A QSerialPort error is a FATAL LOCAL PORT FAILURE when the LOCAL port
    // itself disappeared or became unusable (the USB serial adapter was
    // unplugged, the handle went invalid) — as opposed to a transaction-level
    // failure, and as opposed to remote-slave silence. The distinction is a
    // frozen product rule, because the three are NOT the same event:
    //   NoError       -> not an error at all;
    //   TimeoutError  -> NOT a removal. QSerialPort emits it only from the
    //                    blocking waitFor*() calls this adapter never uses, so
    //                    it cannot be a response timeout either; presenting
    //                    silence as an unplug would be a fabricated claim;
    //   NotOpenError  -> NOT a removal. "The device is not open" is a STATE
    //                    statement about an operation on a closed handle, not
    //                    evidence that a live port vanished;
    //   everything else (ResourceError, DeviceNotFoundError, PermissionError,
    //                    ReadError, WriteError, OpenError,
    //                    UnsupportedOperationError, UnknownError) -> the local
    //                    port is no longer usable, so the session must end and
    //                    the owner must be told.
    [[nodiscard]] static bool isFatalLocalPortFailure(
        QSerialPort::SerialPortError error);

    // Test seam: QSerialPort::errorOccurred cannot be manufactured without a
    // real device disappearing, so a deterministic test must be able to
    // deliver the EXACT event the Qt slot receives. This forwards to the same
    // handler the signal is connected to — same classification, same teardown,
    // same terminal evidence, same bounded error lane. Product code never
    // calls it, and it adds no second path: it IS the slot body.
    void deliverPortErrorForTest(QSerialPort::SerialPortError error);

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
    // failure is handled further emissions are ignored until the next
    // openPort/start attempt. M10-E4: the failed-open branch arms this too,
    // because the queued errorOccurred that follows it is the SAME failure
    // already reported synchronously — the user-visible emission stays exactly
    // one per failure event (SERIAL-I02).
    bool suppressPortErrors_ = false;
};