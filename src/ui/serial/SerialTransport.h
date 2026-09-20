#pragma once

#include <QObject>
#include <QString>

#include <chrono>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/ActiveTransactionEvidence.h"

// ---------------------------------------------------------------------------
// M10-A: the transport seam.
//
// The Controller/runtime talks to a SerialTransport, never to a concrete
// QSerialPort object. Production uses SerialTransactionAdapter (unchanged
// QSerialPort behaviour); deterministic tests inject a recording transport
// that can prove send counts and exact ADU bytes without any COM port, sleep
// or wall-clock race.
//
// The interface deliberately speaks CORE types (intent/descriptor/evidence/
// analysis): a transport states what the wire may have seen — it never
// decides what a Modbus outcome is, and it never re-reads a UI draft.
// ---------------------------------------------------------------------------
class SerialTransport : public QObject
{
    Q_OBJECT

public:
    explicit SerialTransport(QObject* parent = nullptr);
    ~SerialTransport() override;

    // Open and configure the port ONLY (8N1 + caller baud). No Modbus state
    // is touched. On failure returns false and emits a BOUNDED
    // transportError() — never a fabricated TransactionStatus.
    virtual bool openPort(const QString& portName, qint32 baudRate) = 0;

    // Hand one ALREADY-ENCODED request to the wire. The timeout threshold is
    // NOT a separate parameter: descriptor.intent.timeout is the single
    // authority for both the response-timeout callback and the analyzer.
    //   accepted == true  -> the full request entered the transmission
    //                        lifecycle; a completion will follow later.
    //   accepted == false -> nothing will complete; the disposition states
    //                        whether the wire can be proven clean (NotSent)
    //                        or not (PossiblySent — a short write).
    // Failures emit exactly one bounded transportError().
    virtual modbuslens::core::ActiveStartResult startActiveRequest(
        const modbuslens::core::ActiveRequestDescriptor& request) = 0;

    [[nodiscard]] virtual bool hasActiveTransaction() const = 0;
    [[nodiscard]] virtual bool isPortOpen() const = 0;

public slots:
    // Intentional disconnect: cancels any pending transaction WITHOUT
    // emitting a transaction result or a transport error (a user close is
    // neither a Modbus diagnosis nor a transport failure).
    virtual void closePort() = 0;

signals:
    // Completion travels WITH its evidence: the send-time request snapshot,
    // the exact observed response bytes and the transport disposition.
    void transactionCompleted(modbuslens::core::ActiveTransactionResult result);
    // A SUBMITTED request ended without a trusted Modbus response (port
    // failure or explicit close/cancel). Separate from transactionCompleted
    // exactly because it carries no Modbus outcome — and separate from
    // transportError because it must carry the retained evidence.
    // Contract: at most one terminal event per accepted request.
    void transactionTerminated(modbuslens::core::ActiveTransportTerminal terminal);
    void transportError(const QString& message);
};