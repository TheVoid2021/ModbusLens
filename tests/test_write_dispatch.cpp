#include <QtTest>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/ActiveTransactionEvidence.h"
#include "core/active/PreparedWriteSnapshot.h"
#include "core/active/ProductWriteCapability.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/protocol/Function16.h"
#include "core/serial/SerialTransactionSession.h"
#include "fake_serial_transport.h"
#include "ui/AnalysisController.h"

using modbuslens::core::ActiveFunction;
using modbuslens::core::ActiveRequestDescriptor;
using modbuslens::core::ActiveRequestEncodeError;
using modbuslens::core::ActiveRequestIntent;
using modbuslens::core::ActiveStartResult;
using modbuslens::core::ConfirmRejectReason;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::PreparedDispatchLocalError;
using modbuslens::core::PreparedDispatchResult;
using modbuslens::core::PreparedWriteInvalidReason;
using modbuslens::core::PreparedWriteState;
using modbuslens::core::TransactionSourceKind;
using modbuslens::core::TransactionStatus;
using modbuslens::core::TransportDisposition;
using modbuslens::core::TransportTerminalReason;
using modbuslens::core::WriteSingleRegisterIntent;
using modbuslens::core::encodeActiveRequest;
using modbuslens::core::encodeRtuFrame;

using ms = std::chrono::milliseconds;

// ---------------------------------------------------------------------------
// M10-D3 — FC06 atomic dispatch / evidence / product capability.
//
// These oracles drive the REAL AnalysisController through the deterministic
// RecordingSerialTransport: no COM port, no sleep, no wall-clock race.
//
// The central thing this suite protects is the SEPARATION of two failure
// shapes that are easy to conflate and dangerous to merge:
//
//   A. Controller final-guard failure  -> confirmationAccepted=false,
//      dispatchAttempted=false, NO ActiveStartResult, NO TransportDisposition.
//   B. guards PASS + transport accepted 0 bytes -> confirmationAccepted=true,
//      dispatchAttempted=true, startResult{accepted=false, NotSent}.
//
// Only A is "nothing happened"; only B is a real (failed) submission attempt.
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint8_t kUnit = 0x11;
constexpr std::uint16_t kAddress = 0x0001;
constexpr std::uint16_t kValue = 0x0003;
constexpr std::int64_t kTimeoutMs = 1000;

// D1 golden vector G3, kept as an INDEPENDENT literal (same field sequence as
// the published MODBUS application example). The "exact ADU" assertions below
// compare against THIS constant, never against a re-encoding of the request,
// so a broken encoder cannot make its own expectation come true.
const std::vector<std::uint8_t> kGoldenWrite06Adu = {0x11, 0x06, 0x00, 0x01,
                                                     0x00, 0x03, 0x9A, 0x9B};

// M10-E3 review correction: the CANONICAL TWO-register FC16 request
// (values [0x000A, 0x0102] -> quantity 2, byteCount 4). Independent literal.
const std::vector<std::uint8_t> kGoldenWrite16TwoRegAdu = {
    0x11, 0x10, 0x00, 0x01, 0x00, 0x02, 0x04, 0x00,
    0x0A, 0x01, 0x02, 0xC6, 0xF0};

// M10-E3 golden literal for the FC16/0x10 request the harness prepares
// (unit 0x11, start 0x0001, values [0x0007] -> quantity 1, byteCount 2).
// Independent literal, exactly like kGoldenWrite06Adu above.
const std::vector<std::uint8_t> kGoldenWrite16Adu = {0x11, 0x10, 0x00, 0x01,
                                                     0x00, 0x01, 0x02, 0x00,
                                                     0x07, 0x2B, 0x83};

// The exact echo a conforming device returns for a 0x10 request: starting
// address + written quantity (values are NOT echoed). Built with the
// production codec on purpose: a DEVICE reply is not the thing under test.
std::vector<std::uint8_t> fc16EchoWire(std::uint8_t unit = kUnit,
                                       std::uint16_t address = kAddress,
                                       std::uint16_t quantity = 1)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit,
        .functionCode = 0x10,
        .data = {static_cast<std::uint8_t>(address >> 8),
                 static_cast<std::uint8_t>(address & 0xFF),
                 static_cast<std::uint8_t>(quantity >> 8),
                 static_cast<std::uint8_t>(quantity & 0xFF)},
    });
}

// The exact echo a conforming device returns for the request above. Built with
// the production codec on purpose: a DEVICE reply is not the thing under test
// here (the request encoder is).
std::vector<std::uint8_t> echoWire(std::uint8_t unit = kUnit,
                                   std::uint16_t address = kAddress,
                                   std::uint16_t value = kValue)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit,
        .functionCode = 0x06,
        .data = {static_cast<std::uint8_t>(address >> 8),
                 static_cast<std::uint8_t>(address & 0xFF),
                 static_cast<std::uint8_t>(value >> 8),
                 static_cast<std::uint8_t>(value & 0xFF)},
    });
}

std::vector<std::uint8_t> exceptionWire(std::uint8_t code)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = kUnit, .functionCode = 0x86, .data = {code}});
}

// A conforming FC03 answer for the read requests below: byteCount 4 = the two
// registers those requests ask for (byteCount 2 would be a QuantityMismatch
// ProtocolError, not a Success).
std::vector<std::uint8_t> fc03AnswerWire()
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = kUnit, .functionCode = 0x03,
        .data = {0x04, 0x00, 0x2A, 0x00, 0x2B}});
}

// Same echo shape, last CRC byte flipped: a real wire-truth fact.
std::vector<std::uint8_t> corruptCrcEcho()
{
    auto wire = echoWire();
    wire.back() = static_cast<std::uint8_t>(wire.back() ^ 0x01);
    return wire;
}

struct Session {
    AnalysisController controller;
    RecordingSerialTransport transport;

    Session()
    {
        transport.setPortOpen(true);
        controller.setSerialTransport(&transport);
        controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    }

    // Closes the in-flight request at a REAL response timeout. The elapsed
    // value must be at or above the request's own threshold, otherwise the
    // analyzer correctly reports Pending instead of Timeout.
    void completeAtTimeout()
    {
        transport.setCompletionElapsed(ms{kTimeoutMs});
        transport.completeWithTimeout();
    }

    // Prepares a valid 0x06 snapshot and returns its opaque token (0 means the
    // preparation failed and the calling test fails immediately).
    std::uint64_t prepare06()
    {
        (void)controller.prepareWriteSingleRegister(kUnit, kAddress, kValue, kTimeoutMs);
        const auto token = controller.preparedWriteToken();
        return token.value_or(0);
    }

    // Prepares a valid 0x10 snapshot with an explicit multi-line decimal value
    // list (N registers) and returns its token (0 = preparation failed).
    std::uint64_t prepare10Values(const QString& valuesText)
    {
        const std::string text = valuesText.toStdString();
        (void)controller.prepareWriteMultipleRegisters(kUnit, kAddress, text,
                                                       kTimeoutMs);
        const auto token = controller.preparedWriteToken();
        return token.value_or(0);
    }

    // Prepares a valid 0x10 snapshot (encoder M10-E1, session support M10-E2,
    // product capability M10-E3) and returns its token.
    std::uint64_t prepare10()
    {
        (void)controller.prepareWriteMultipleRegisters(kUnit, kAddress,
                                                       std::string_view{"7"}, kTimeoutMs);
        const auto token = controller.preparedWriteToken();
        return token.value_or(0);
    }
};

bool isGuardRejected(const PreparedDispatchResult& result,
                     ConfirmRejectReason reason)
{
    return !result.confirmationAccepted && !result.dispatchAttempted
        && result.rejectedReason.has_value() && *result.rejectedReason == reason
        && !result.startResult.has_value() && !result.localError.has_value();
}

} // namespace

class WriteDispatchTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- R1: full acceptance ----
    void r1_fullAcceptedAtomicDispatch();
    void r1_exactAduAndCompletionIntegration();

    // ---- R2 / R3: accepted but nothing (fully / partially) submitted ----
    void r2_transportZeroAcceptIsNotAGuardFailure();
    void r2_zeroAcceptLeavesObservableNonSuccess();
    void r3_shortSubmissionRetainsTerminalAndNoTransaction();

    // ---- R4: the guard matrix ----
    void r4_disconnectedGuard();
    void r4_busyGuard();
    void r4_staleSessionGuard();
    void r4_sourceChangedGuard();
    // M10-E3 INTENTIONAL TRANSITION: r4_capabilityUnavailableForFc10 asserted
    // that a prepared 0x10 was refused. The capability layer now exists, so
    // the same harness proves the POSITIVE dispatch path instead (and its
    // evidence integration), while the guard matrix itself is unchanged for
    // every other rejection reason.
    void r4_fc10DispatchAcceptedAtomically();
    void r4_fc10ExactAduAndCompletionIntegration();
    void r4_fc10NotSentMirror();
    void r4_fc10ShortSubmissionMirror();
    void fc10TimeoutEntersHistory();
    void mixedFc03Fc06Fc10ShareOneStatisticsUniverse();
    void fc10_write10SupportedIsStructuralAndRuntimeInvariant();
    void fc10_write10SupportedDoesNotRevealProductionUi();
    // M10-E3 review correction: the canonical N=2 Controller oracle plus the
    // remaining direct FC16 matrix rows the first E3 round did not cover.
    void fc10_canonicalTwoRegisterDispatch();
    void fc10_multiValueSnapshotIsImmutableThroughDispatch();
    void fc10_echoMismatchEntersSharedUniverse();
    void fc10_crcErrorEntersSharedUniverse();
    void fc10_postSubmitTransportErrorAndDisconnectKeepEvidenceOnly();
    void fc10_consumedTokenTenAttemptsProduceNoExtraWork();
    void fc10_clearWhilePendingKeepsPendingThenCompletes();
    void fc10_invalidDraftsAreRejectedBeforeDispatchNotNotSent();
    void mixedUniverseStatisticsAndDiagnosisCoverFc10();
    void r4_guardFailureIsNeverTransportNotSent();

    // ---- R5: token reuse ----
    void r5_consumedTokenNeverDispatchesAgain();

    // ---- busy / consumed stability ----
    void busyBecameTrueDoesNotOverwriteConsumed();
    void busyClearsOnSuccess();
    void busyClearsOnTimeout();
    void busyClearsOnShortSubmission();
    void busyClearsOnTransportError();

    // ---- post-submission transport outcomes ----
    void postSubmitTransportErrorKeepsEvidenceOnly();
    void postSubmitDisconnectKeepsEvidenceOnly();

    // ---- response integration (same universe as FC03) ----
    void responseExceptionEntersHistory();
    void responseCrcErrorEntersHistory();
    void responseEchoMismatchEntersHistory();
    void responseTimeoutEntersHistory();
    void historyAppendsInCompletionOrderToSameSession();

    // ---- terminal exclusion + statistics + diagnosis ----
    void terminalsStayOutOfTransactionsAndStatistics();
    void mixedFc03AndFc06ShareOneStatisticsUniverse();
    void diagnosisCoversWrite06InSameBatch();

    // ---- Clear / disconnect / source replacement while pending ----
    void clearWhileWritePendingKeepsPending();
    void disconnectWhileWritePendingKeepsEvidenceNoFakeTransaction();
    void sourceReplacementWhileWritePendingInvalidates();

    // ---- draft / capability / boundary freezes ----
    void draftAndTokenSurviveEveryOutcomeWithoutRevival();
    void write06SupportedIsStructuralAndRuntimeInvariant();
    void write06SupportedDoesNotRevealProductionUi();
    void hiddenConfirmSeamStillDispatchesNothing();
    void fc10_capabilityLayerOpenedUiStillAbsent();

    // ---- M10-D4: production dispatch entry + outcome presentation lane ----
    void d4_requestDispatchIsTheAtomicOperation();
    void d4_requestDispatchReturnsVoidToQml();
    void d4_notSentNoticeSaysNotSent();
    void d4_shortSubmissionNoticeSaysUnknown();
    void d4_writeTimeoutNoticeSaysUnknown();
    void d4_successProducesNoNotice();
    void d4_noticeClearedByNewPrepareAndByClear();
};

// ---------------------------------------------------------------------------
// R1 — full acceptance
// ---------------------------------------------------------------------------

void WriteDispatchTest::r1_fullAcceptedAtomicDispatch()
{
    Session s;
    const auto token = s.prepare06();
    QVERIFY(token != 0);
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Prepared);
    QCOMPARE(s.controller.preparedWriteFunction(), 0x06);

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    // The two orthogonal facts, both true, and a real transport attempt.
    QVERIFY(result.confirmationAccepted);
    QVERIFY(result.dispatchAttempted);
    QVERIFY(!result.rejectedReason.has_value());
    QVERIFY(!result.localError.has_value());
    QVERIFY(result.startResult.has_value());
    QVERIFY(result.startResult->accepted);
    QCOMPARE(result.startResult->disposition, TransportDisposition::PossiblySent);
    QVERIFY(!result.startResult->terminatedDuringSubmission.has_value());

    // Exactly one attempt, exactly one accepted send, exactly one ADU.
    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 1);
    QCOMPARE(s.transport.sentAduLog().size(), std::size_t{1});

    // The snapshot generation is terminal and the request is in flight.
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);
    QVERIFY(!s.controller.hasPreparedWrite());
    QVERIFY(s.controller.serialBusy());
    QCOMPARE(s.transport.hasPendingTransaction(), true);
}

void WriteDispatchTest::r1_exactAduAndCompletionIntegration()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);

    // EXACT wire bytes — against the independent golden literal.
    QCOMPARE(s.transport.sentAduLog().front(), kGoldenWrite06Adu);

    s.transport.setResponseBytes(echoWire());
    s.transport.completeWithResponse();

    // Exactly one 0x06 Success transaction, in the SAME Active Serial session.
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.successCount(), 1);
    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);

    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.functionCode(), 0x06);
    QCOMPARE(record.unitId(), kUnit);
    QCOMPARE(record.analysis.status, TransactionStatus::Success);
    QCOMPARE(record.sessionId, s.controller.activeSerialSessionId());
    // Send-time evidence: the exact request ADU and the exact response ADU.
    QCOMPARE(record.evidence.requestAdu, kGoldenWrite06Adu);
    QCOMPARE(record.evidence.responseAdu, echoWire());
    QCOMPARE(record.evidence.disposition, TransportDisposition::PossiblySent);
}

// ---------------------------------------------------------------------------
// R2 — guards PASS, transport accepts zero bytes
// ---------------------------------------------------------------------------

void WriteDispatchTest::r2_transportZeroAcceptIsNotAGuardFailure()
{
    Session s;
    const auto token = s.prepare06();
    s.transport.setSubmissionAcceptedBytes(0);

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    // The CONFIRMATION happened (it was consumed) — this is NOT a guard
    // failure, and it is NOT "nothing ever started".
    QVERIFY(result.confirmationAccepted);
    QVERIFY(result.dispatchAttempted);
    QVERIFY(!result.rejectedReason.has_value());
    QVERIFY(!result.localError.has_value());
    QVERIFY(result.startResult.has_value());

    // The transport really was called once, and accepted nothing.
    QVERIFY(!result.startResult->accepted);
    QCOMPARE(result.startResult->disposition, TransportDisposition::NotSent);
    QVERIFY(!result.startResult->terminatedDuringSubmission.has_value());
    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 0);
    QVERIFY(s.transport.sentAduLog().empty());

    // No pending transaction, not busy, no transaction, no terminal.
    QVERIFY(!s.transport.hasPendingTransaction());
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);

    // The generation is still terminal (a failed attempt never revives it).
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);
}

void WriteDispatchTest::r2_zeroAcceptLeavesObservableNonSuccess()
{
    Session s;
    const auto token = s.prepare06();
    s.transport.setSubmissionAcceptedBytes(0);
    (void)s.controller.confirmAndDispatchPreparedWrite(token);

    // The user must be able to learn that the request was NOT sent — via the
    // existing bounded serial error lane, not via a fabricated write verdict.
    QVERIFY(s.controller.hasSerialError());
    // And nothing may claim success.
    QCOMPARE(s.controller.successCount(), 0);
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.timeoutCount(), 0);
    QCOMPARE(s.controller.protocolErrorCount(), 0);
    QVERIFY(!s.controller.hasBaselineDiagnosis());
}

// ---------------------------------------------------------------------------
// R3 — short submission
// ---------------------------------------------------------------------------

void WriteDispatchTest::r3_shortSubmissionRetainsTerminalAndNoTransaction()
{
    Session s;
    const auto token = s.prepare06();
    s.transport.setSubmissionAcceptedBytes(3); // 0 < 3 < 8

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    QVERIFY(result.confirmationAccepted);
    QVERIFY(result.dispatchAttempted);
    QVERIFY(result.startResult.has_value());
    QVERIFY(!result.startResult->accepted);
    QCOMPARE(result.startResult->disposition, TransportDisposition::PossiblySent);
    QVERIFY(result.startResult->terminatedDuringSubmission.has_value());

    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 0);
    QVERIFY(s.transport.sentAduLog().empty());
    QVERIFY(!s.controller.serialBusy());

    // Exactly one ShortSubmission terminal, carrying the intended request.
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    const auto& terminal = s.controller.activeSerialTerminations().front();
    QCOMPARE(terminal.reason, TransportTerminalReason::ShortSubmission);
    QCOMPARE(terminal.request.wire, kGoldenWrite06Adu);
    QVERIFY(terminal.responseAdu.empty());
    QCOMPARE(terminal.disposition, TransportDisposition::PossiblySent);
    QCOMPARE(terminal.submissionAcceptedByteCount, std::optional<std::uint16_t>{3});

    // A short submission is NOT a Modbus transaction.
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
}

// ---------------------------------------------------------------------------
// R4 — the guard matrix
// ---------------------------------------------------------------------------

void WriteDispatchTest::r4_disconnectedGuard()
{
    Session s;
    const auto token = s.prepare06();

    // Disconnecting INVALIDATES a still-Prepared snapshot (M10-C1 contract), so
    // by the time dispatch runs the generation is already terminal and the
    // observable guard answer is NotPrepared. The dedicated NotConnected branch
    // is therefore DEFENSIVE: preparation itself requires a live connection and
    // every real disconnect funnels through the invalidation above, so no
    // public sequence can reach it. This oracle asserts the reachable truth
    // instead of inventing a path to a branch that exists for safety.
    s.controller.disconnectSerial();
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Invalidated);

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    QVERIFY(isGuardRejected(result, ConfirmRejectReason::NotPrepared));
    // The terminal reason is the real one and is never rewritten.
    QCOMPARE(s.controller.preparedWriteInvalidReason(),
             std::optional{PreparedWriteInvalidReason::Disconnected});
    // Nothing happened at all: no attempt, no send, no bytes on the wire.
    QCOMPARE(s.transport.startAttemptCount(), 0);
    QCOMPARE(s.transport.sendCount(), 0);
    QVERIFY(s.transport.sentAduLog().empty());
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Invalidated);
}

void WriteDispatchTest::r4_busyGuard()
{
    Session s;
    const auto token = s.prepare06();
    // A real FC03 read entering flight makes serialBusy true (and invalidates
    // the still-Prepared snapshot).
    s.controller.readHoldingRegistersOnce(kUnit, 0, 2, kTimeoutMs);
    QVERIFY(s.controller.serialBusy());

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    QVERIFY(isGuardRejected(result, ConfirmRejectReason::NotPrepared));
    QCOMPARE(s.controller.preparedWriteInvalidReason(),
             std::optional{PreparedWriteInvalidReason::BusyBecameTrue});
    // The read's own attempt is the only one; the write added nothing.
    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 1);
}

void WriteDispatchTest::r4_staleSessionGuard()
{
    Session s;
    const auto token = s.prepare06();
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{1});

    // Same port, same baud, brand-new session: identity is (sourceKind,
    // sessionId), never the port string.
    s.controller.disconnectSerial();
    s.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{2});

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    QVERIFY(!result.confirmationAccepted);
    QVERIFY(!result.dispatchAttempted);
    QVERIFY(result.rejectedReason.has_value());
    QCOMPARE(s.transport.startAttemptCount(), 0);
    QCOMPARE(s.transport.sendCount(), 0);
}

void WriteDispatchTest::r4_sourceChangedGuard()
{
    Session s;
    const auto token = s.prepare06();

    // A successful Simulator/Replay replacement makes the source authoritative
    // again; the Active Serial confirmation can no longer act.
    s.controller.runDemoBatch();

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    QVERIFY(!result.confirmationAccepted);
    QVERIFY(!result.dispatchAttempted);
    QVERIFY(result.rejectedReason.has_value());
    QCOMPARE(s.transport.startAttemptCount(), 0);
    QCOMPARE(s.transport.sendCount(), 0);
}

void WriteDispatchTest::r4_fc10DispatchAcceptedAtomically()
{
    // M10-E3 INTENTIONAL TRANSITION: this oracle used to assert that a
    // prepared 0x10 was refused with CapabilityUnavailable. The capability
    // layer now exists, so it proves the POSITIVE path instead — the exact
    // atomic contract r1_fullAcceptedAtomicDispatch locks for 0x06.
    Session s;
    const auto token = s.prepare10();
    QVERIFY(token != 0);
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Prepared);
    QCOMPARE(s.controller.preparedWriteFunction(), 0x10);

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    QVERIFY(result.confirmationAccepted);
    QVERIFY(result.dispatchAttempted);
    QVERIFY(!result.rejectedReason.has_value());
    QVERIFY(!result.localError.has_value());
    QVERIFY(result.startResult.has_value());
    QVERIFY(result.startResult->accepted);
    QCOMPARE(result.startResult->disposition, TransportDisposition::PossiblySent);
    QVERIFY(!result.startResult->terminatedDuringSubmission.has_value());

    // Exactly one attempt, exactly one accepted send, exactly one ADU.
    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 1);
    QCOMPARE(s.transport.sentAduLog().size(), std::size_t{1});

    // The snapshot generation is terminal and the request is in flight.
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);
    QVERIFY(!s.controller.hasPreparedWrite());
    QVERIFY(s.controller.serialBusy());
    QCOMPARE(s.transport.hasPendingTransaction(), true);
}

void WriteDispatchTest::r4_fc10ExactAduAndCompletionIntegration()
{
    Session s;
    const auto token = s.prepare10();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);

    // EXACT wire bytes — against the independent golden literal.
    QCOMPARE(s.transport.sentAduLog().front(), kGoldenWrite16Adu);

    // A conforming 0x10 echo: starting address + written quantity.
    s.transport.setResponseBytes(fc16EchoWire());
    s.transport.completeWithResponse();

    // Exactly one 0x10 Success transaction, in the SAME Active Serial session.
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.successCount(), 1);
    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);

    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.functionCode(), 0x10);
    QCOMPARE(record.unitId(), kUnit);
    QCOMPARE(record.analysis.status, TransactionStatus::Success);
    QCOMPARE(record.sessionId, s.controller.activeSerialSessionId());
    // Send-time evidence: the exact request ADU and the exact response ADU.
    QCOMPARE(record.evidence.requestAdu, kGoldenWrite16Adu);
    QCOMPARE(record.evidence.responseAdu, fc16EchoWire());
    QCOMPARE(record.evidence.disposition, TransportDisposition::PossiblySent);
}

void WriteDispatchTest::r4_fc10NotSentMirror()
{
    // 0x10 reuses the generic transport lifecycle: a zero-byte acceptance is
    // NotSent (NOT a guard failure), with the same user-visible lane as 0x06.
    Session s;
    const auto token = s.prepare10();
    s.transport.setSubmissionAcceptedBytes(0);

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    QVERIFY(result.confirmationAccepted);
    QVERIFY(result.dispatchAttempted);
    QVERIFY(result.startResult.has_value());
    QVERIFY(!result.startResult->accepted);
    QCOMPARE(result.startResult->disposition, TransportDisposition::NotSent);
    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 0);
    QVERIFY(s.transport.sentAduLog().empty());
    QVERIFY(!s.transport.hasPendingTransaction());
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);
}

void WriteDispatchTest::r4_fc10ShortSubmissionMirror()
{
    // A short submission (0 < accepted < ADU) keeps the PossiblySent terminal
    // with the intended request, and never fabricates a transaction.
    Session s;
    const auto token = s.prepare10();
    s.transport.setSubmissionAcceptedBytes(3); // 0 < 3 < 11

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);

    QVERIFY(result.confirmationAccepted);
    QVERIFY(result.dispatchAttempted);
    QVERIFY(result.startResult.has_value());
    QVERIFY(!result.startResult->accepted);
    QCOMPARE(result.startResult->disposition, TransportDisposition::PossiblySent);
    QVERIFY(result.startResult->terminatedDuringSubmission.has_value());
    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 0);

    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    const auto& terminal = s.controller.activeSerialTerminations().front();
    QCOMPARE(terminal.reason, TransportTerminalReason::ShortSubmission);
    QCOMPARE(terminal.request.wire, kGoldenWrite16Adu);
    QVERIFY(terminal.responseAdu.empty());
    QCOMPARE(terminal.disposition, TransportDisposition::PossiblySent);
    QCOMPARE(terminal.submissionAcceptedByteCount, std::optional<std::uint16_t>{3});

    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
}

void WriteDispatchTest::fc10TimeoutEntersHistory()
{
    // A 0x10 write timeout means the DEVICE WRITE STATE IS UNKNOWN — the same
    // frozen wording/semantics as 0x06, with no auto retry and no terminal.
    Session s;
    const auto token = s.prepare10();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.completeAtTimeout();

    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.timeoutCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.functionCode(), 0x10);
    QCOMPARE(record.analysis.status, TransactionStatus::Timeout);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
}

void WriteDispatchTest::mixedFc03Fc06Fc10ShareOneStatisticsUniverse()
{
    // The 0x10 write joins the SAME transaction/statistics universe: one
    // session, completion-order append, no second write authority.
    Session s;
    s.controller.readHoldingRegistersOnce(kUnit, 0, 2, kTimeoutMs);
    s.transport.setResponseBytes(fc03AnswerWire());
    s.transport.completeWithResponse();

    const auto token06 = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token06);
    s.transport.setResponseBytes(echoWire());
    s.transport.completeWithResponse();

    const auto token10 = s.prepare10();
    (void)s.controller.confirmAndDispatchPreparedWrite(token10);
    s.transport.setResponseBytes(fc16EchoWire());
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.activeSerialRecordCount(), 3);
    const auto& records = s.controller.activeSerialRecords();
    QCOMPARE(records.at(0).functionCode(), 0x03);
    QCOMPARE(records.at(1).functionCode(), 0x06);
    QCOMPARE(records.at(2).functionCode(), 0x10);
    QCOMPARE(records.at(0).analysis.status, TransactionStatus::Success);
    QCOMPARE(records.at(1).analysis.status, TransactionStatus::Success);
    QCOMPARE(records.at(2).analysis.status, TransactionStatus::Success);
    QCOMPARE(records.at(0).sessionId, records.at(2).sessionId);
    QCOMPARE(records.at(2).sessionId, s.controller.activeSerialSessionId());
    QCOMPARE(s.controller.successCount(), 3);
}

void WriteDispatchTest::fc10_write10SupportedIsStructuralAndRuntimeInvariant()
{
    Session s;
    QVERIFY(s.controller.write10Supported());

    // The property is the SAME single source of truth the dispatch guard uses,
    // cross-checked against the real runtime predicates.
    QVERIFY(modbuslens::core::kProductWrite10Supported);
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteMultipleRegisters));
    // The encoder really exists for 0x10 (M10-E1).
    const auto encoded = encodeActiveRequest(ActiveRequestIntent{
        .function = ActiveFunction::WriteMultipleRegisters,
        .unitId = kUnit,
        .timeout = ms{kTimeoutMs},
        .payload = modbuslens::core::WriteMultipleRegistersIntent{
            .startAddress = kAddress, .values = {7}}});
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&encoded) != nullptr);

    // NOT runtime availability: every ACTION-state transition leaves it alone.
    s.controller.clearResults();
    QVERIFY(s.controller.write10Supported());
    s.controller.runBaselineDiagnosis();
    QVERIFY(s.controller.write10Supported());
    s.controller.disconnectSerial();
    QVERIFY(s.controller.write10Supported());
    s.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    s.controller.readHoldingRegistersOnce(kUnit, 0, 2, kTimeoutMs);
    QVERIFY(s.controller.serialBusy());
    QVERIFY(s.controller.write10Supported());
    s.transport.completeWithTimeout();
    QVERIFY(s.controller.write10Supported());
    s.controller.runDemoBatch();
    QVERIFY(s.controller.write10Supported());
}

void WriteDispatchTest::fc10_write10SupportedDoesNotRevealProductionUi()
{
    // Capability ready != presentation rollout (mirrors the 0x06 test): the
    // property is a CONSTANT with no setter and no notify, so it exposes no
    // runtime switch that could reveal a 0x10 UI before M10-E4. The QML
    // runtime gate (qml_focus_check's prod-hidden oracle) is the other half.
    Session s;
    QVERIFY(s.controller.write10Supported());
    const QMetaObject* meta = s.controller.metaObject();
    const int index = meta->indexOfProperty("write10Supported");
    QVERIFY(index >= 0);
    const QMetaProperty property = meta->property(index);
    QVERIFY(property.isConstant());
    QVERIFY(property.isReadable());
    QVERIFY(!property.isWritable());
    QVERIFY(!property.hasNotifySignal());
}

void WriteDispatchTest::r4_guardFailureIsNeverTransportNotSent()
{
    // THE distinction this suite exists for. A guard rejection has NO
    // ActiveStartResult at all — there is no TransportDisposition to report,
    // because the transport was never consulted.
    Session s;
    const auto token = s.prepare06();
    s.controller.disconnectSerial();

    const auto guardFailure = s.controller.confirmAndDispatchPreparedWrite(token);
    QVERIFY(!guardFailure.confirmationAccepted);
    QVERIFY(!guardFailure.dispatchAttempted);
    QVERIFY(!guardFailure.startResult.has_value()); // <- no disposition exists
    QVERIFY(guardFailure.rejectedReason.has_value());

    // In contrast, guards PASS + zero accepted bytes DOES produce a start
    // result with an explicit NotSent disposition.
    Session t;
    const auto token2 = t.prepare06();
    t.transport.setSubmissionAcceptedBytes(0);
    const auto zeroAccept = t.controller.confirmAndDispatchPreparedWrite(token2);
    QVERIFY(zeroAccept.confirmationAccepted);
    QVERIFY(zeroAccept.dispatchAttempted);
    QVERIFY(zeroAccept.startResult.has_value());
    QCOMPARE(zeroAccept.startResult->disposition, TransportDisposition::NotSent);
    QVERIFY(!zeroAccept.rejectedReason.has_value());
}

// ---------------------------------------------------------------------------
// R5 — token reuse
// ---------------------------------------------------------------------------

void WriteDispatchTest::r5_consumedTokenNeverDispatchesAgain()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.transport.setResponseBytes(echoWire());
    s.transport.completeWithResponse();

    const int attemptsAfterFirst = s.transport.startAttemptCount();
    const int sendsAfterFirst = s.transport.sendCount();

    // Ten more calls with the SAME consumed token: not one extra attempt, not
    // one extra byte, and never a second confirmation. No debounce involved —
    // the store generation is simply terminal.
    for (int i = 0; i < 10; ++i) {
        const auto repeated = s.controller.confirmAndDispatchPreparedWrite(token);
        QVERIFY(!repeated.confirmationAccepted);
        QVERIFY(!repeated.dispatchAttempted);
        QVERIFY(!repeated.startResult.has_value());
        QVERIFY(repeated.rejectedReason.has_value());
    }
    QCOMPARE(s.transport.startAttemptCount(), attemptsAfterFirst);
    QCOMPARE(s.transport.sendCount(), sendsAfterFirst);
    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
}

// ---------------------------------------------------------------------------
// busy / consumed stability
// ---------------------------------------------------------------------------

void WriteDispatchTest::busyBecameTrueDoesNotOverwriteConsumed()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    QVERIFY(s.controller.serialBusy());

    // Entering flight invalidates a still-PREPARED generation, but this one is
    // already Consumed — a terminal reason must never be rewritten.
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);
    QVERIFY(!s.controller.preparedWriteInvalidReason().has_value());
}

void WriteDispatchTest::busyClearsOnSuccess()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    QVERIFY(s.controller.serialBusy());
    s.transport.setResponseBytes(echoWire());
    s.transport.completeWithResponse();
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.successCount(), 1);
}

void WriteDispatchTest::busyClearsOnTimeout()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    QVERIFY(s.controller.serialBusy());
    s.completeAtTimeout();
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.timeoutCount(), 1);
}

void WriteDispatchTest::busyClearsOnShortSubmission()
{
    Session s;
    const auto token = s.prepare06();
    s.transport.setSubmissionAcceptedBytes(3);
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    // A submission that terminated during the call never enters flight.
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
}

void WriteDispatchTest::busyClearsOnTransportError()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    QVERIFY(s.controller.serialBusy());
    s.transport.failTransport(QStringLiteral("端口失效"));
    QVERIFY(!s.controller.serialBusy());
}

// ---------------------------------------------------------------------------
// post-submission transport outcomes
// ---------------------------------------------------------------------------

void WriteDispatchTest::postSubmitTransportErrorKeepsEvidenceOnly()
{
    Session s;
    const auto token = s.prepare06();
    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);
    QVERIFY(result.confirmationAccepted);
    QVERIFY(result.dispatchAttempted);
    QCOMPARE(s.transport.sendCount(), 1);

    s.transport.failTransport(QStringLiteral("端口失效"));

    // One terminal with retained evidence, and NOT a fabricated Modbus result:
    // the device mutation state is unknown, so nothing may claim it changed or
    // did not change.
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    QCOMPARE(s.controller.activeSerialTerminations().front().reason,
             TransportTerminalReason::TransportError);
    QCOMPARE(s.controller.activeSerialTerminations().front().request.wire,
             kGoldenWrite06Adu);
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QVERIFY(!s.controller.serialBusy());
    // The confirmation stays consumed: no retry path is opened by the failure.
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);
}

void WriteDispatchTest::postSubmitDisconnectKeepsEvidenceOnly()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    QCOMPARE(s.transport.sendCount(), 1);

    s.transport.disconnectAfterSubmission();

    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    QCOMPARE(s.controller.activeSerialTerminations().front().reason,
             TransportTerminalReason::DisconnectedAfterSubmission);
    QCOMPARE(s.controller.observedCount(), 0);
    QVERIFY(!s.controller.serialBusy());
}

// ---------------------------------------------------------------------------
// response integration — 0x06 joins the SAME transaction universe as FC03
// ---------------------------------------------------------------------------

void WriteDispatchTest::responseExceptionEntersHistory()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.transport.setResponseBytes(exceptionWire(0x02));
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.exceptionCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.functionCode(), 0x06);
    QCOMPARE(record.analysis.status, TransactionStatus::Exception);
    QCOMPARE(record.analysis.exceptionCode, std::optional<std::uint8_t>{0x02});
}

void WriteDispatchTest::responseCrcErrorEntersHistory()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.transport.setResponseBytes(corruptCrcEcho());
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.crcErrorCount(), 1);
    // Damaged bytes are retained, never discarded.
    QCOMPARE(s.controller.activeSerialRecords().front().evidence.responseAdu,
             corruptCrcEcho());
}

void WriteDispatchTest::responseEchoMismatchEntersHistory()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    // A perfectly shaped 0x06 echo that reports a DIFFERENT value.
    s.transport.setResponseBytes(echoWire(kUnit, kAddress, 0x0099));
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.protocolErrorCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(record.analysis.issue.has_value());
    QCOMPARE(record.analysis.issue->code,
             modbuslens::core::TransactionIssueCode::WriteSingleRegisterEchoMismatch);
}

void WriteDispatchTest::responseTimeoutEntersHistory()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.completeAtTimeout();

    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.timeoutCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.functionCode(), 0x06);
    QCOMPARE(record.analysis.status, TransactionStatus::Timeout);
    // A write timeout means the DEVICE WRITE STATE IS UNKNOWN. The analysis
    // deliberately carries no device-mutation claim, and no terminal is
    // fabricated for it.
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
}

void WriteDispatchTest::historyAppendsInCompletionOrderToSameSession()
{
    Session s;
    // FC03 first.
    s.controller.readHoldingRegistersOnce(kUnit, 0, 2, kTimeoutMs);
    s.transport.setResponseBytes(fc03AnswerWire());
    s.transport.completeWithResponse();

    // Then a 0x06 write.
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.transport.setResponseBytes(echoWire());
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.activeSerialRecordCount(), 2);
    const auto& records = s.controller.activeSerialRecords();
    QCOMPARE(records.at(0).functionCode(), 0x03);
    QCOMPARE(records.at(1).functionCode(), 0x06);
    // Both really succeeded (a wrong FC03 fixture would otherwise still show
    // two rows and quietly weaken this oracle).
    QCOMPARE(records.at(0).analysis.status, TransactionStatus::Success);
    QCOMPARE(records.at(1).analysis.status, TransactionStatus::Success);
    // Both belong to the one session — no second universe was created.
    QCOMPARE(records.at(0).sessionId, records.at(1).sessionId);
    QCOMPARE(records.at(0).sessionId, s.controller.activeSerialSessionId());
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{1});
}

// ---------------------------------------------------------------------------
// terminal exclusion + statistics + diagnosis
// ---------------------------------------------------------------------------

void WriteDispatchTest::terminalsStayOutOfTransactionsAndStatistics()
{
    Session s;
    const auto token = s.prepare06();
    s.transport.setSubmissionAcceptedBytes(3);
    (void)s.controller.confirmAndDispatchPreparedWrite(token);

    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    // A transport terminal is evidence, not a Modbus outcome: it must not
    // become a row and must not move a single counter.
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.completedCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QVERIFY(s.controller.activeSerialTerminations().front().evidence().requestAdu
            == kGoldenWrite06Adu);
}

void WriteDispatchTest::mixedFc03AndFc06ShareOneStatisticsUniverse()
{
    Session s;
    // 1x FC03 success.
    s.controller.readHoldingRegistersOnce(kUnit, 0, 2, kTimeoutMs);
    s.transport.setResponseBytes(fc03AnswerWire());
    s.transport.completeWithResponse();

    // 1x FC06 success.
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.transport.setResponseBytes(echoWire());
    s.transport.completeWithResponse();

    // 1x FC06 timeout.
    const auto token2 = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token2);
    s.completeAtTimeout();

    // Frozen formulas over the MIXED batch: observed = pending + completed.
    QCOMPARE(s.controller.observedCount(), 3);
    QCOMPARE(s.controller.completedCount(), 3);
    QCOMPARE(s.controller.pendingCount(), 0);
    QCOMPARE(s.controller.successCount(), 2);
    QCOMPARE(s.controller.timeoutCount(), 1);
    QCOMPARE(s.controller.expectedNoResponseCount(), 0);

    // successRate = Success / (completed - ExpectedNoResponse) = 2/3.
    QVERIFY(s.controller.hasSuccessRate());
    QCOMPARE(s.controller.successRate(), 2.0 / 3.0);
    // Latency averages SUCCESS only — the timeout must not dilute it.
    QVERIFY(s.controller.hasAverageSuccessLatency());
}

void WriteDispatchTest::diagnosisCoversWrite06InSameBatch()
{
    Session s;
    // A 0x06 success...
    const auto t1 = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(t1);
    s.transport.setResponseBytes(echoWire());
    s.transport.completeWithResponse();

    // ...and a 0x06 timeout, in the same deterministic batch.
    const auto t2 = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(t2);
    s.completeAtTimeout();

    s.controller.runBaselineDiagnosis();
    QVERIFY(s.controller.hasBaselineDiagnosis());
    const QString text = s.controller.baselineDiagnosisText();
    // Whole-batch proof: the timeout fact is reported, and the FC06 write
    // transaction is inside the same analysis universe as FC03 (no separate
    // write diagnosis pipeline).
    QVERIFY(text.contains(QStringLiteral("无响应超时")));
    QCOMPARE(s.controller.timeoutCount(), 1);
    QCOMPARE(s.controller.successCount(), 1);
}

// ---------------------------------------------------------------------------
// Clear / disconnect / source replacement while a write is pending
// ---------------------------------------------------------------------------

void WriteDispatchTest::clearWhileWritePendingKeepsPending()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.transport.setResponseBytes(echoWire());
    QVERIFY(s.controller.serialBusy());

    s.controller.clearResults();

    // Clear is not Cancel and not Disconnect: the in-flight write survives and
    // its completion lands as the FIRST transaction of the emptied view.
    QVERIFY(s.controller.serialBusy());
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    // Clear must not touch the write's own terminal confirmation state.
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);

    s.transport.completeWithResponse();
    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QCOMPARE(s.controller.activeSerialRecords().front().functionCode(), 0x06);
}

void WriteDispatchTest::disconnectWhileWritePendingKeepsEvidenceNoFakeTransaction()
{
    Session s;
    const auto token = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    QCOMPARE(s.transport.sendCount(), 1);

    // An explicit user disconnect on an in-flight write: the adapter emits the
    // post-submission terminal, then the transport is torn down.
    s.controller.disconnectSerial();

    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    QCOMPARE(s.controller.activeSerialTerminations().front().reason,
             TransportTerminalReason::DisconnectedAfterSubmission);
    // A disconnect is NOT a Modbus verdict: no Timeout row may be invented,
    // and nothing may claim the device was left unchanged.
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QVERIFY(!s.controller.serialBusy());
    QVERIFY(!s.controller.serialConnected());
}

void WriteDispatchTest::sourceReplacementWhileWritePendingInvalidates()
{
    Session s;
    const auto token = s.prepare06();

    // A successful source replacement while a write is pending: SourceChanged
    // must be recorded BEFORE the teardown, so the reason is the real one.
    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);
    QVERIFY(result.confirmationAccepted);
    QVERIFY(s.controller.serialBusy());

    s.controller.runDemoBatch();
    QCOMPARE(s.controller.sourceKind(), TransactionSourceKind::Simulator);

    // The write's terminal confirmation state is preserved (it is not a
    // Prepared generation any more, so no reason may be written onto it).
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);
    QVERIFY(!s.controller.preparedWriteInvalidReason().has_value());
}

// ---------------------------------------------------------------------------
// draft / capability / boundary freezes
// ---------------------------------------------------------------------------

void WriteDispatchTest::draftAndTokenSurviveEveryOutcomeWithoutRevival()
{
    // After EVERY terminal outcome the token stays dead and the only way to
    // write again is a brand-new Write -> prepare -> confirm cycle. No retry,
    // no automatic resend, no revival.
    const auto runOutcome = [](int kind) {
        Session s;
        const auto token = s.prepare06();
        if (kind == 0) { // success
            (void)s.controller.confirmAndDispatchPreparedWrite(token);
            s.transport.setResponseBytes(echoWire());
            s.transport.completeWithResponse();
        } else if (kind == 1) { // exception
            (void)s.controller.confirmAndDispatchPreparedWrite(token);
            s.transport.setResponseBytes(exceptionWire(0x03));
            s.transport.completeWithResponse();
        } else if (kind == 2) { // crc
            (void)s.controller.confirmAndDispatchPreparedWrite(token);
            s.transport.setResponseBytes(corruptCrcEcho());
            s.transport.completeWithResponse();
        } else if (kind == 3) { // timeout
            (void)s.controller.confirmAndDispatchPreparedWrite(token);
            s.transport.completeWithTimeout();
        } else if (kind == 4) { // zero accept
            s.transport.setSubmissionAcceptedBytes(0);
            (void)s.controller.confirmAndDispatchPreparedWrite(token);
        } else if (kind == 5) { // short
            s.transport.setSubmissionAcceptedBytes(3);
            (void)s.controller.confirmAndDispatchPreparedWrite(token);
        } else { // transport error
            (void)s.controller.confirmAndDispatchPreparedWrite(token);
            s.transport.failTransport(QStringLiteral("端口失效"));
        }
        const int attempts = s.transport.startAttemptCount();
        const auto again = s.controller.confirmAndDispatchPreparedWrite(token);
        return !again.confirmationAccepted && !again.dispatchAttempted
            && s.transport.startAttemptCount() == attempts;
    };

    for (int kind = 0; kind < 7; ++kind) {
        QVERIFY2(runOutcome(kind), qPrintable(QStringLiteral("outcome %1").arg(kind)));
    }
}

void WriteDispatchTest::write06SupportedIsStructuralAndRuntimeInvariant()
{
    Session s;
    QVERIFY(s.controller.write06Supported());

    // The property is the SAME single source of truth the dispatch guard uses,
    // and it is cross-checked here against the real runtime predicates — so it
    // can never drift away from the code it describes.
    QVERIFY(modbuslens::core::kProductWrite06Supported);
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteSingleRegister));
    // The encoder really exists for 0x06.
    const auto encoded = encodeActiveRequest(ActiveRequestIntent{
        .function = ActiveFunction::WriteSingleRegister,
        .unitId = kUnit,
        .timeout = ms{kTimeoutMs},
        .payload = WriteSingleRegisterIntent{.registerAddress = kAddress,
                                             .value = kValue}});
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&encoded) != nullptr);

    // NOT runtime availability. Every one of these is an ACTION-state
    // transition and must leave the capability untouched.
    s.controller.clearResults();
    QVERIFY(s.controller.write06Supported());
    s.controller.runBaselineDiagnosis();
    QVERIFY(s.controller.write06Supported());
    s.controller.disconnectSerial();          // disconnected
    QVERIFY(s.controller.write06Supported());
    s.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    s.controller.readHoldingRegistersOnce(kUnit, 0, 2, kTimeoutMs);
    QVERIFY(s.controller.serialBusy());       // busy
    QVERIFY(s.controller.write06Supported());
    // Invalid draft (no snapshot at all) must not move it either.
    s.transport.completeWithTimeout();
    QVERIFY(s.controller.write06Supported());
    s.controller.runDemoBatch();              // Simulator source
    QVERIFY(s.controller.write06Supported());
}

void WriteDispatchTest::write06SupportedDoesNotRevealProductionUi()
{
    // Capability ready != presentation rollout. The property may be true while
    // the production Write UI stays entirely absent; the QML runtime gate
    // (qml_focus_check's prod-hidden oracle) is the other half of this proof.
    Session s;
    QVERIFY(s.controller.write06Supported());
    // The capability is a CONSTANT with no setter and no notify: it exposes no
    // runtime switch that could reveal a UI early.
    const QMetaObject* meta = s.controller.metaObject();
    const int index = meta->indexOfProperty("write06Supported");
    QVERIFY(index >= 0);
    const QMetaProperty property = meta->property(index);
    QVERIFY(property.isConstant());
    QVERIFY(property.isReadable());
    QVERIFY(!property.isWritable());
    QVERIFY(!property.hasNotifySignal());
}

void WriteDispatchTest::hiddenConfirmSeamStillDispatchesNothing()
{
    // The QML-facing confirm seam is NOT the dispatch operation: in D3 the
    // production Confirm button stays confirmation-only, so it must never
    // touch the transport.
    Session s;
    const auto token = s.prepare06();

    QVERIFY(s.controller.confirmPreparedWriteToken(static_cast<qulonglong>(token)));

    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);
    QCOMPARE(s.transport.startAttemptCount(), 0);
    QCOMPARE(s.transport.sendCount(), 0);
    QVERIFY(s.transport.sentAduLog().empty());
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.observedCount(), 0);
}

// ---------------------------------------------------------------------------
// M10-E3 review correction — FC16 (0x10) Controller-level evidence matrix
// ---------------------------------------------------------------------------

void WriteDispatchTest::fc10_canonicalTwoRegisterDispatch()
{
    // The CANONICAL multi-value case through the real Controller: two
    // registers, the F16-G6 field sequence, proven end to end at dispatch
    // level (the first E3 round only exercised a SINGLE-register snapshot, so
    // this exact 13-byte ADU was never proven through the Controller).
    Session s;
    const auto token = s.prepare10Values(QStringLiteral("10\n258"));
    QVERIFY(token != 0);
    QCOMPARE(s.controller.preparedWriteFunction(), 0x10);
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Prepared);

    const auto result = s.controller.confirmAndDispatchPreparedWrite(token);
    QVERIFY(result.confirmationAccepted);
    QVERIFY(result.dispatchAttempted);
    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 1);
    QCOMPARE(s.transport.sentAduLog().size(), std::size_t{1});

    // EXACT wire bytes against the independent literal; 13 = 9 + 2*2.
    const auto& adu = s.transport.sentAduLog().front();
    QCOMPARE(adu, kGoldenWrite16TwoRegAdu);
    QCOMPARE(adu.size(), std::size_t{13});

    // quantity = values.size() = 2, byteCount = 2 * quantity = 4, and the
    // value ORDER is the prepared order (no sorting, no word swap).
    const auto decoded = modbuslens::core::decodeRtuFrame(adu);
    const auto* frame = std::get_if<ModbusRtuFrame>(&decoded);
    QVERIFY(frame != nullptr);
    const auto fields = modbuslens::core::readWriteMultipleRegistersFields(*frame);
    QVERIFY(fields.has_value());
    QCOMPARE(fields->quantity, std::uint16_t{2});
    QCOMPARE(fields->byteCount, std::uint8_t{4});
    const auto request =
        modbuslens::core::decodeWriteMultipleRegistersRequest(*frame);
    const auto* model =
        std::get_if<modbuslens::core::WriteMultipleRegistersRequest>(&request);
    QVERIFY(model != nullptr);
    QCOMPARE(model->startingAddress, kAddress);
    QCOMPARE(model->values, (std::vector<std::uint16_t>{0x000A, 0x0102}));

    // And the real lifecycle completes as one 0x10 Success record.
    s.transport.setResponseBytes(fc16EchoWire(kUnit, kAddress, 2));
    s.transport.completeWithResponse();
    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.functionCode(), 0x10);
    QCOMPARE(record.analysis.status, TransactionStatus::Success);
    QCOMPARE(record.evidence.requestAdu, kGoldenWrite16TwoRegAdu);
}

void WriteDispatchTest::fc10_multiValueSnapshotIsImmutableThroughDispatch()
{
    // Multi-value snapshots are IMMUTABLE: after preparing [10, 258], a second
    // preparation with different values must not replace the generation, and
    // dispatch must send the PREPARED values - never a re-read draft.
    Session s;
    const auto token = s.prepare10Values(QStringLiteral("10\n258"));
    QVERIFY(token != 0);

    // Draft-side mutation attempt: a second prepare with a different value.
    (void)s.controller.prepareWriteMultipleRegisters(kUnit, kAddress,
                                                     std::string_view{"9"},
                                                     kTimeoutMs);
    QCOMPARE(s.controller.preparedWriteToken().value_or(0), token);
    const auto snapshot = s.controller.preparedWriteSnapshot();
    QVERIFY(snapshot.has_value());
    const auto* preparedValues =
        std::get_if<modbuslens::core::WriteMultipleRegistersIntent>(
            &snapshot->intent.payload);
    QVERIFY(preparedValues != nullptr);
    QCOMPARE(preparedValues->values,
             (std::vector<std::uint16_t>{0x000A, 0x0102}));

    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    QCOMPARE(s.transport.sentAduLog().size(), std::size_t{1});
    QCOMPARE(s.transport.sentAduLog().front(), kGoldenWrite16TwoRegAdu);
}

void WriteDispatchTest::fc10_echoMismatchEntersSharedUniverse()
{
    // How a Controller-dispatched 0x10 lifecycle reaches the E2 authority: the
    // session completes the transaction through analyzeActiveResponse -> the
    // shared analyzeWriteMultipleRegistersTransaction, and the resulting record
    // carries the SHARED issue code into the common history.
    Session s;
    const auto token = s.prepare10Values(QStringLiteral("10\n258"));
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    // Well-formed 0x10 echo reporting a DIFFERENT quantity (3 != 2).
    s.transport.setResponseBytes(fc16EchoWire(kUnit, kAddress, 3));
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.protocolErrorCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.functionCode(), 0x10);
    QCOMPARE(record.analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(record.analysis.issue.has_value());
    QCOMPARE(record.analysis.issue->code,
             modbuslens::core::TransactionIssueCode::
                 WriteMultipleRegistersEchoMismatch);
    QCOMPARE(record.analysis.issue->expectedQuantity, std::uint16_t{2});
    QCOMPARE(record.analysis.issue->actualQuantity, std::uint16_t{3});
}

void WriteDispatchTest::fc10_crcErrorEntersSharedUniverse()
{
    Session s;
    const auto token = s.prepare10Values(QStringLiteral("10\n258"));
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    auto wire = fc16EchoWire(kUnit, kAddress, 2);
    wire.back() = static_cast<std::uint8_t>(wire.back() ^ 0x01);
    s.transport.setResponseBytes(wire);
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.crcErrorCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.functionCode(), 0x10);
    QCOMPARE(record.analysis.status, TransactionStatus::CrcError);
}

void WriteDispatchTest::fc10_postSubmitTransportErrorAndDisconnectKeepEvidenceOnly()
{
    // Phase 1: a transport error after submission keeps ONE terminal with the
    // intended 0x10 request and fabricates no Modbus result.
    {
        Session s;
        const auto token = s.prepare10();
        (void)s.controller.confirmAndDispatchPreparedWrite(token);
        QCOMPARE(s.transport.sendCount(), 1);
        s.transport.failTransport(QStringLiteral("端口失效"));

        QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
        QCOMPARE(s.controller.activeSerialTerminations().front().reason,
                 TransportTerminalReason::TransportError);
        QCOMPARE(s.controller.activeSerialTerminations().front().request.wire,
                 kGoldenWrite16Adu);
        QCOMPARE(s.controller.observedCount(), 0);
        QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
        QVERIFY(!s.controller.serialBusy());
        QCOMPARE(s.controller.preparedWriteState(),
                 PreparedWriteState::Consumed);
    }
    // Phase 2: a post-submission disconnect is DisconnectedAfterSubmission and
    // likewise fabricates nothing.
    {
        Session s;
        const auto token = s.prepare10();
        (void)s.controller.confirmAndDispatchPreparedWrite(token);
        QCOMPARE(s.transport.sendCount(), 1);
        s.transport.disconnectAfterSubmission();

        QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
        QCOMPARE(s.controller.activeSerialTerminations().front().reason,
                 TransportTerminalReason::DisconnectedAfterSubmission);
        QCOMPARE(s.controller.observedCount(), 0);
        QVERIFY(!s.controller.serialBusy());
    }
}

void WriteDispatchTest::fc10_consumedTokenTenAttemptsProduceNoExtraWork()
{
    // The one-shot generation holds for 0x10 exactly as for 0x06: ten further
    // confirmations of the SAME consumed token produce no attempt, no send and
    // no second transaction.
    Session s;
    const auto token = s.prepare10();
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.transport.setResponseBytes(fc16EchoWire());
    s.transport.completeWithResponse();

    const int attemptsAfterFirst = s.transport.startAttemptCount();
    const int sendsAfterFirst = s.transport.sendCount();
    for (int i = 0; i < 10; ++i) {
        const auto repeated =
            s.controller.confirmAndDispatchPreparedWrite(token);
        QVERIFY(!repeated.confirmationAccepted);
        QVERIFY(!repeated.dispatchAttempted);
        QVERIFY(!repeated.startResult.has_value());
        QVERIFY(repeated.rejectedReason.has_value());
    }
    QCOMPARE(s.transport.startAttemptCount(), attemptsAfterFirst);
    QCOMPARE(s.transport.sendCount(), sendsAfterFirst);
    QCOMPARE(s.transport.sentAduLog().size(), std::size_t{1});
    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
}

void WriteDispatchTest::fc10_clearWhilePendingKeepsPendingThenCompletes()
{
    // Clear is neither Cancel nor Disconnect: an in-flight 0x10 survives it and
    // its completion lands as the FIRST transaction of the emptied view.
    Session s;
    const auto token = s.prepare10Values(QStringLiteral("10\n258"));
    (void)s.controller.confirmAndDispatchPreparedWrite(token);
    s.transport.setResponseBytes(fc16EchoWire(kUnit, kAddress, 2));
    QVERIFY(s.controller.serialBusy());

    s.controller.clearResults();

    QVERIFY(s.controller.serialBusy());
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);

    s.transport.completeWithResponse();
    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.functionCode(), 0x10);
    QCOMPARE(record.analysis.status, TransactionStatus::Success);
}

void WriteDispatchTest::fc10_invalidDraftsAreRejectedBeforeDispatchNotNotSent()
{
    // Pre-send validation REJECTION is not a transport outcome: every invalid
    // 0x10 draft must fail to prepare, leaving nothing to dispatch - and it
    // must never be reported through the NotSent lane.
    struct BadDraft {
        const char* name;
        int unitId;
        int startAddress;
        const char* valuesText; // nullptr => the 124-value payload
    };
    const BadDraft drafts[] = {
        {"empty values", 1, 0, "   "},
        {"124 values", 1, 0, nullptr},
        {"span overflow", 1, 65535, "1\n2"},
        {"invalid decimal", 1, 0, "0x1"},
        {"unit 0 (broadcast)", 0, 0, "1"},
    };
    std::string many;
    for (int i = 0; i < 124; ++i) {
        if (i != 0) {
            many.push_back('\n');
        }
        many += "1";
    }

    for (const BadDraft& draft : drafts) {
        Session s;
        const std::string text =
            draft.valuesText != nullptr ? std::string{draft.valuesText} : many;
        (void)s.controller.prepareWriteMultipleRegisters(
            draft.unitId, draft.startAddress, text, kTimeoutMs);

        // Nothing was prepared, so nothing can dispatch.
        QVERIFY2(!s.controller.preparedWriteToken().has_value(), draft.name);
        QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::None);
        const auto rejected = s.controller.confirmAndDispatchPreparedWrite(1);
        QVERIFY2(!rejected.confirmationAccepted, draft.name);
        QVERIFY2(!rejected.dispatchAttempted, draft.name);
        QVERIFY2(!rejected.startResult.has_value(), draft.name);
        // A validation rejection is NEVER dressed up as a transport NotSent.
        QVERIFY2(!s.controller.hasWriteDispatchNotice(), draft.name);
        QCOMPARE(s.transport.startAttemptCount(), 0);
        QCOMPARE(s.transport.sendCount(), 0);
        QVERIFY(s.transport.sentAduLog().empty());
        QCOMPARE(s.controller.observedCount(), 0);
    }
}

void WriteDispatchTest::mixedUniverseStatisticsAndDiagnosisCoverFc10()
{
    // More than successCount: the 0x10 rows join the same history, in
    // completion order, in one session, and the SAME statistics formulas and
    // the SAME deterministic diagnosis batch cover them.
    Session s;
    s.controller.readHoldingRegistersOnce(kUnit, 0, 2, kTimeoutMs);
    s.transport.setResponseBytes(fc03AnswerWire());
    s.transport.completeWithResponse();

    const auto token06 = s.prepare06();
    (void)s.controller.confirmAndDispatchPreparedWrite(token06);
    s.transport.setResponseBytes(echoWire());
    s.transport.completeWithResponse();

    const auto token10 = s.prepare10Values(QStringLiteral("10\n258"));
    (void)s.controller.confirmAndDispatchPreparedWrite(token10);
    s.transport.setResponseBytes(fc16EchoWire(kUnit, kAddress, 2));
    s.transport.completeWithResponse();

    // ...and one 0x10 timeout, so the batch also exercises the failure side.
    const auto token10b = s.prepare10();
    (void)s.controller.confirmAndDispatchPreparedWrite(token10b);
    s.completeAtTimeout();

    // History: four rows, completion order, ONE Active Serial session.
    QCOMPARE(s.controller.activeSerialRecordCount(), 4);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 4);
    const auto& records = s.controller.activeSerialRecords();
    QCOMPARE(records.at(0).functionCode(), 0x03);
    QCOMPARE(records.at(1).functionCode(), 0x06);
    QCOMPARE(records.at(2).functionCode(), 0x10);
    QCOMPARE(records.at(3).functionCode(), 0x10);
    QCOMPARE(records.at(0).sessionId, records.at(3).sessionId);
    QCOMPARE(records.at(3).sessionId, s.controller.activeSerialSessionId());

    // Statistics: frozen formulas over the mixed batch.
    QCOMPARE(s.controller.observedCount(), 4);
    QCOMPARE(s.controller.completedCount(), 4);
    QCOMPARE(s.controller.pendingCount(), 0);
    QCOMPARE(s.controller.successCount(), 3);
    QCOMPARE(s.controller.timeoutCount(), 1);
    QVERIFY(s.controller.hasSuccessRate());
    QCOMPARE(s.controller.successRate(), 3.0 / 4.0);
    QVERIFY(s.controller.hasAverageSuccessLatency());
    // A timeout is not a transport terminal.
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);

    // Diagnosis: the same deterministic batch covers the 0x10 facts.
    s.controller.runBaselineDiagnosis();
    QVERIFY(s.controller.hasBaselineDiagnosis());
    QVERIFY(s.controller.baselineDiagnosisText().contains(
        QStringLiteral("无响应超时")));
}

void WriteDispatchTest::fc10_capabilityLayerOpenedUiStillAbsent()
{
    // M10-E3 INTENTIONAL TRANSITION: the capability layer for 0x10 is now
    // OPEN (write10Supported exists and is true; a prepared 0x10 dispatches
    // atomically — see the r4_fc10_* oracles). What stays frozen is the
    // PRESENTATION layer: the production 0x10 Write UI remains absent until
    // M10-E4, which the qml_focus_check prod-hidden oracle asserts at runtime.
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteMultipleRegisters));
    QVERIFY(modbuslens::core::kProductWrite10Supported);

    // The superseded runtime-name shape stays absent (same discipline as 0x06).
    Session s;
    QCOMPARE(s.controller.metaObject()->indexOfProperty("write10Available"), -1);
}

// ---------------------------------------------------------------------------
// M10-D4 — the production dispatch entry and the write OUTCOME lane.
// ---------------------------------------------------------------------------

void WriteDispatchTest::d4_requestDispatchIsTheAtomicOperation()
{
    // The production entry performs the WHOLE atomic operation: consume, encode
    // and start in one call, with an opaque token as the only input.
    Session s;
    const auto token = s.prepare06();
    s.controller.requestPreparedWriteDispatch(static_cast<qulonglong>(token));

    QCOMPARE(s.controller.preparedWriteState(), PreparedWriteState::Consumed);
    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 1);
    QCOMPARE(s.transport.sentAduLog().size(), std::size_t{1});
    QCOMPARE(s.transport.sentAduLog().front(), kGoldenWrite06Adu);
    QVERIFY(s.controller.serialBusy());
}

void WriteDispatchTest::d4_requestDispatchReturnsVoidToQml()
{
    // It returns NOTHING on purpose: a bool handed back to QML invites
    // "if (dispatch(token)) close()", which would quietly promote one of two
    // different facts (confirmation consumed, or transport accepted the bytes)
    // into a send-success authority.
    Session s;
    const QMetaObject *meta = s.controller.metaObject();
    const int index = meta->indexOfMethod("requestPreparedWriteDispatch(qulonglong)");
    QVERIFY(index >= 0);
    const QMetaMethod method = meta->method(index);
    QVERIFY(method.access() == QMetaMethod::Public);
    // In Qt 6 returnType() is the metatype id, so compare against the id.
    QCOMPARE(method.returnType(), static_cast<int>(QMetaType::Void));
    // And the confirmation-only seam still exists and still returns a bool —
    // the two are different doors with different contracts.
    QVERIFY(meta->indexOfMethod("confirmPreparedWriteToken(qulonglong)") >= 0);
}

void WriteDispatchTest::d4_notSentNoticeSaysNotSent()
{
    Session s;
    const auto token = s.prepare06();
    s.transport.setSubmissionAcceptedBytes(0);
    s.controller.requestPreparedWriteDispatch(static_cast<qulonglong>(token));

    QVERIFY(s.controller.hasWriteDispatchNotice());
    const QString text = s.controller.writeDispatchNotice();
    QVERIFY(text.contains(QStringLiteral("未发送")));
    QCOMPARE(s.controller.writeDispatchNoticeTone(), QStringLiteral("warning"));
    // Never framed as a protocol outcome.
    QVERIFY(!text.contains(QStringLiteral("写入成功")));
    QVERIFY(!text.contains(QStringLiteral("设备已写入")));
    QVERIFY(!text.contains(QStringLiteral("超时")));
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
}

void WriteDispatchTest::d4_shortSubmissionNoticeSaysUnknown()
{
    Session s;
    const auto token = s.prepare06();
    s.transport.setSubmissionAcceptedBytes(3);
    s.controller.requestPreparedWriteDispatch(static_cast<qulonglong>(token));

    const QString text = s.controller.writeDispatchNotice();
    QVERIFY(text.contains(QStringLiteral("设备写入状态未知")));
    // The forbidden claim: a partial handover cannot prove the device is
    // unchanged.
    QVERIFY(!text.contains(QStringLiteral("设备未写入")));
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    QCOMPARE(s.controller.observedCount(), 0);
}

void WriteDispatchTest::d4_writeTimeoutNoticeSaysUnknown()
{
    Session s;
    const auto token = s.prepare06();
    s.controller.requestPreparedWriteDispatch(static_cast<qulonglong>(token));
    s.completeAtTimeout();

    QCOMPARE(s.controller.timeoutCount(), 1);
    const QString text = s.controller.writeDispatchNotice();
    QVERIFY(text.contains(QStringLiteral("响应超时")));
    QVERIFY(text.contains(QStringLiteral("设备写入状态未知")));
    QVERIFY(!text.contains(QStringLiteral("设备未写入")));
}

void WriteDispatchTest::d4_successProducesNoNotice()
{
    // Only a TRUSTED response may look like success, and it must NOT produce a
    // non-success notice.
    Session s;
    const auto token = s.prepare06();
    s.controller.requestPreparedWriteDispatch(static_cast<qulonglong>(token));
    s.transport.setResponseBytes(echoWire());
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.successCount(), 1);
    QVERIFY(!s.controller.hasWriteDispatchNotice());
}

void WriteDispatchTest::d4_noticeClearedByNewPrepareAndByClear()
{
    Session s;
    auto token = s.prepare06();
    s.transport.setSubmissionAcceptedBytes(0);
    s.controller.requestPreparedWriteDispatch(static_cast<qulonglong>(token));
    QVERIFY(s.controller.hasWriteDispatchNotice());

    // A new Write starts a fresh attempt (and must not disturb the draft).
    s.transport.setSubmissionAcceptedBytes(std::nullopt);
    const auto token2 = s.prepare06();
    QVERIFY(token2 != 0);
    QVERIFY(!s.controller.hasWriteDispatchNotice());

    // Clear Results is result state and clears it too.
    s.transport.setSubmissionAcceptedBytes(0);
    s.controller.requestPreparedWriteDispatch(static_cast<qulonglong>(token2));
    QVERIFY(s.controller.hasWriteDispatchNotice());
    s.controller.clearResults();
    QVERIFY(!s.controller.hasWriteDispatchNotice());
}

QTEST_MAIN(WriteDispatchTest)
#include "test_write_dispatch.moc"
