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
    // Close the transaction at its response timeout (no bytes observed unless
    // feedPartialBytes() was called first).
    void completeWithTimeout();
    // Deliver some bytes WITHOUT completing the candidate (partial evidence).
    void feedPartialBytes();
    // Transport failure while pending: abort locally + one bounded error.
    void failTransport(const QString& message);

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
    modbuslens::core::ActiveStartResult lastStart_{};
};