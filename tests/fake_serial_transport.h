#pragma once

#include <QObject>
#include <QString>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/ActiveTransactionEvidence.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/serial/SerialTransactionSession.h"
#include "ui/serial/SerialTransport.h"

// ---------------------------------------------------------------------------
// M10-A deterministic recording transport (TEST-ONLY double).
//
// Implements the SerialTransport seam for the controller/runtime tests:
//   - records start attempts, send counts and the EXACT request ADU bytes;
//   - configurable open/closed and busy state, pre-send accept/reject;
//   - scripted response bytes (normal, exception, CRC-damaged or partial);
//   - completion timing driven explicitly by the test — no Sleep, no real
//     COM port, no wall-clock race anywhere.
//
// It is deliberately NOT a device simulator: it answers "was it sent, how
// many times, what bytes, when did the result arrive" (transport layer),
// while SimulatedSlave answers "what would a device reply" (device layer).
// ---------------------------------------------------------------------------
class RecordingSerialTransport : public SerialTransport
{
    Q_OBJECT

public:
    explicit RecordingSerialTransport(QObject* parent = nullptr);

    // ---- configuration (before / between transactions) ----
    void setPortOpen(bool open);
    void setAcceptRequests(bool accept);   // pre-send rejection gate
    void setResponseBytes(std::vector<std::uint8_t> bytes);
    void setCompletionElapsed(std::chrono::milliseconds elapsed);
    // Configurable submission outcome for the NEXT start:
    //   · 0 < count < ADU size -> SHORT SUBMISSION: the transport API reports
    //     accepting only this many bytes. The start then returns
    //     {accepted=false, PossiblySent} WITH durable terminal evidence,
    //     exactly like the production adapter's short-write branch.
    //   · count == 0 (M10-D3) -> ZERO-ACCEPT: the start is REALLY attempted,
    //     but the transport API accepts none of the ADU. The start returns
    //     {accepted=false, NotSent} with NO pending request and NO terminal —
    //     the deterministic counterpart of a transport that provably put zero
    //     bytes on the wire. This is what distinguishes "guards PASS but the
    //     transport accepted nothing" from a Controller guard rejection.
    //   · count >= ADU size (or nullopt) -> normal full acceptance.
    void setSubmissionAcceptedBytes(std::optional<std::uint16_t> count);

    // ---- observation (the recording oracle) ----
    [[nodiscard]] int startAttemptCount() const;
    [[nodiscard]] int sendCount() const;   // only ACCEPTED starts
    [[nodiscard]] const std::vector<std::vector<std::uint8_t>>& sentAduLog() const;
    [[nodiscard]] modbuslens::core::ActiveStartResult lastStartResult() const;
    [[nodiscard]] bool hasPendingTransaction() const;

    // ---- controlled completion timing ----
    // Feed the configured response bytes as one chunk; emits only when the
    // core session recognizes a complete candidate (tests configure complete
    // responses; a partial configuration simply does not complete).
    void completeWithResponse();
    // T023 READ-R2: deliver the configured response across MULTIPLE readyRead
    // chunks so a real fragment boundary exists. The candidate closes on
    // whichever chunk completes it, and the accumulated evidence is the
    // concatenation of the chunks — a split must lose (or duplicate) nothing.
    void completeWithResponseInChunks(const std::vector<std::size_t>& chunkSizes);
    // Close the transaction at its response timeout (no bytes observed unless
    // feedPartialBytes() was called first).
    void completeWithTimeout();
    // Deliver some bytes WITHOUT completing the candidate (partial evidence).
    void feedPartialBytes();
    // ---- post-submission termination injection (M10-A correction) ----
    // Transport failure AFTER the request entered the transmission lifecycle:
    // emits one terminal event (TransportError) carrying the retained
    // evidence, then the existing bounded error. No Modbus outcome.
    void failTransport(const QString& message);
    // Explicit close/cancel after submission: emits one terminal event
    // (DisconnectedAfterSubmission). Without a pending request it is silent,
    // exactly like the production adapter.
    void disconnectAfterSubmission();
    // Fatal LOCAL adapter removal (M10-E4): the USB serial adapter itself was
    // unplugged. Mirrors the production adapter's fatal-port-error branch: with
    // a SUBMITTED request one terminal event (TransportError) carries the
    // retained evidence, the port then becomes CLOSED (isPortOpen() false) so
    // the owner must re-sync its connection state, and the bounded error lane
    // is emitted in BOTH cases — a removal is user-visible whether or not a
    // request was in flight. Distinct from disconnectAfterSubmission, which is
    // an INTENTIONAL close and therefore never reports an error.
    void simulateAdapterRemoval(const QString& message);

    // ---- SerialTransport implementation ----
    bool openPort(const QString& portName, qint32 baudRate) override;
    modbuslens::core::ActiveStartResult startActiveRequest(
        const modbuslens::core::ActiveRequestDescriptor& request) override;
    [[nodiscard]] bool hasActiveTransaction() const override;
    [[nodiscard]] bool isPortOpen() const override;

public slots:
    void closePort() override;

private:
    void emitCompletion(modbuslens::core::TransactionAnalysis analysis);
    // Shared termination path: captures evidence first, aborts second, emits
    // at most one terminal event (and nothing at all when nothing is pending).
    void emitTerminalIfSubmitted(
        modbuslens::core::TransportTerminalReason reason);

    modbuslens::core::SerialTransactionSession session_;
    bool portOpen_ = false;
    bool acceptRequests_ = true;
    int startAttempts_ = 0;
    int sendCount_ = 0;
    std::vector<std::vector<std::uint8_t>> sentAduLog_;
    std::optional<modbuslens::core::ActiveRequestDescriptor> pending_;
    std::vector<std::uint8_t> responseBytes_;
    std::vector<std::uint8_t> deliveredBytes_;
    std::chrono::milliseconds completionElapsed_{25};
    std::optional<std::uint16_t> submissionAcceptedBytes_;
    modbuslens::core::ActiveStartResult lastStart_{};
};