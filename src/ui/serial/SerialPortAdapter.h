#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QTimer>

#include <chrono>
#include <cstdint>
#include <functional>
#include <vector>

#include "core/active/ActiveTransactionEvidence.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/serial/SerialTransactionSession.h"
#include "ui/serial/SerialTransport.h"

// ---------------------------------------------------------------------------
// M10-E4 (real hot-unplug correction): LOCAL PORT PRESENCE OBSERVATION.
//
// `QSerialPort::isOpen()` answers only "does this HANDLE believe it is open?".
// Nothing in the Windows backend clears that state when the USB adapter is
// physically removed, so a stale handle keeps answering "yes" — which is why an
// idle removal could leave the UI presenting a connection that no longer
// exists. Whether the LOCAL endpoint still EXISTS is a different question.
//
// On Windows it is answered by a live OS enumeration: Qt's
// QSerialPortInfo::availablePorts() runs a SetupAPI query with DIGCF_PRESENT, so
// a removed adapter drops out of the list.
//
// This is LOCAL HARDWARE PRESENCE. It is NOT remote Modbus liveness: no slave is
// ever addressed, no frame is ever sent, and a silent slave keeps the port
// present — and therefore keeps the local session open (a response timeout stays
// a response timeout).
//
// The probe is the OBSERVATION BOUNDARY and is injectable, so a deterministic
// test can drive present -> absent through the production check (never by
// invoking the teardown helper directly).
// ---------------------------------------------------------------------------
using LocalPortPresenceProbe = std::function<bool(const QString& portName)>;

// Watches EXACTLY ONE local port while a session is open. It never talks to
// QSerialPort, never touches the controller and never decides a Modbus outcome:
// it answers one question — is the local endpoint still enumerated by the OS?
//
// Anti-false-positive: a removal is only declared after `confirmations()`
// consecutive absent observations, so a single transient enumeration hiccup
// cannot tear down a healthy link. Once declared, the watch STOPS itself: one
// physical loss is reported once, and a reinserted adapter does not restart
// anything (reconnecting stays an explicit user action).
class LocalPortPresenceWatch : public QObject
{
    Q_OBJECT

public:
    explicit LocalPortPresenceWatch(QObject* parent = nullptr);

    void setProbe(LocalPortPresenceProbe probe);
    void setInterval(int milliseconds);
    void setConfirmations(int consecutiveAbsentObservations);

    [[nodiscard]] bool isWatching() const;
    [[nodiscard]] QString watchedPort() const;
    [[nodiscard]] int interval() const;

public slots:
    // Production entry point: called by the adapter right after a port was
    // successfully opened, and by nothing else.
    void start(const QString& portName);
    void stop();

signals:
    void portDisappeared(QString portName);

private slots:
    void observe();

private:
    LocalPortPresenceProbe probe_;
    QTimer timer_;
    QString portName_;
    int consecutiveAbsent_ = 0;
    int confirmations_ = 2;
};

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

    // Test seam for the ERROR DOOR only. QSerialPort::errorOccurred cannot be
    // manufactured without a real device disappearing, so a deterministic test
    // must be able to deliver the EXACT event the Qt slot receives. It forwards
    // to the same handler the signal is connected to — same classification, same
    // teardown, same terminal evidence, same bounded error lane.
    //
    // IMPORTANT (M10-E4 correction): this exercises the ERROR-response path, not
    // removal DETECTION. Real hardware showed that the shipped UI could keep
    // presenting an open port, so "given a fatal error, the teardown is right"
    // was never sufficient evidence for hot-unplug. Removal detection is proven
    // through the presence watch below, which is the path the physical event
    // actually takes when Qt reports nothing.
    void deliverPortErrorForTest(QSerialPort::SerialPortError error);

    // ---- Presence observation (M10-E4 hot-unplug correction) ----
    //
    // The watch this adapter owns: started ONLY after a successful openPort(),
    // stopped by every close path. Exposed because the observation boundary must
    // be injectable: a test replaces the probe (production default:
    // QSerialPortInfo::availablePorts()) and then calls start(portName) — the
    // same production entry point the adapter uses after a successful open —
    // because a REAL successful open cannot be manufactured without hardware.
    // Everything the watch then triggers (classification of the loss, evidence
    // capture, port close, bounded error lane, exactly-once reporting) is the
    // production implementation, never a test double.
    [[nodiscard]] LocalPortPresenceWatch& localPortPresence() { return presence_; }
    [[nodiscard]] const LocalPortPresenceWatch& localPortPresence() const
    {
        return presence_;
    }

public slots:
    void closePort() override;

private slots:
    void handleReadyRead();
    void handleTimeout();
    void handlePortError(QSerialPort::SerialPortError error);

private:
    void cancelPending();

    // The ONE local-transport-loss handler. Both doors into it — a fatal
    // QSerialPort error and the presence watch — supply an explicit CAUSE and
    // nothing else; evidence capture, the close, the bounded error lane and the
    // exactly-once guarantee live here, so the two sources can never drift into
    // two behaviours.
    void handleLocalPortLoss(const QString& cause);
    void startPresenceWatch(const QString& portName);
    void stopPresenceWatch();

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
    // one per failure event (SERIAL-I02). It is also the exactly-once guard
    // shared by BOTH loss doors: whichever detects the physical loss first, the
    // second one stays silent.
    bool suppressPortErrors_ = false;
    LocalPortPresenceWatch presence_;
};