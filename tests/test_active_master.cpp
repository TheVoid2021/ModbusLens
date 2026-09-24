#include <QtTest>

#include <QUrl>
#include <QVariant>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/PreparedWriteSnapshot.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/active/ActiveTransactionEvidence.h"
#include "core/analysis/TransactionAnalysis.h"
#include "fake_serial_transport.h"
#include "ui/AnalysisController.h"
#include "ui/TransactionListModel.h"

using modbuslens::core::ActiveFunction;
using modbuslens::core::ActiveRequestDescriptor;
using modbuslens::core::ActiveRequestIntent;
using modbuslens::core::ActiveTransactionResult;
using modbuslens::core::ReadHoldingRegistersIntent;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionStatus;
using modbuslens::core::TransportDisposition;
using modbuslens::core::encodeActiveRequest;

// M10-A foundation tests over the REAL controller + a deterministic recording
// transport (no COM port, no sleep, no wall-clock race). These are transport/
// evidence/append foundation tests — NOT write-UI tests (no write UI exists).
namespace {

using ms = std::chrono::milliseconds;

// M10-C1 helper: bind the prepare/confirm outcome to a reference first, so the
// test never takes the address of a temporary variant.
bool isPrepared(const modbuslens::core::WritePrepareOutcome& outcome)
{
    return std::get_if<modbuslens::core::PreparedWrite>(&outcome) != nullptr;
}

bool isAlreadyPrepared(const modbuslens::core::WritePrepareOutcome& outcome)
{
    return std::get_if<modbuslens::core::PrepareAlreadyPrepared>(&outcome) != nullptr;
}

bool isConfirmAccepted(const modbuslens::core::ConfirmWriteOutcome& outcome)
{
    return std::get_if<modbuslens::core::ConfirmAccepted>(&outcome) != nullptr;
}

bool isConfirmRejected(const modbuslens::core::ConfirmWriteOutcome& outcome)
{
    return std::get_if<modbuslens::core::ConfirmRejected>(&outcome) != nullptr;
}


// FC03 request 01 03 00 00 00 02 + CRC (T009 golden bytes).
std::vector<std::uint8_t> goldenRequestWire(std::uint8_t unit = 0x01,
                                            std::uint16_t start = 0x0000,
                                            std::uint16_t quantity = 0x0002)
{
    const auto descriptor = std::get<ActiveRequestDescriptor>(
        encodeActiveRequest(ActiveRequestIntent{
            .function = ActiveFunction::ReadHoldingRegisters,
            .unitId = unit,
            .timeout = ms{1000},
            .payload = ReadHoldingRegistersIntent{
                .startAddress = start, .quantity = quantity}}));
    return descriptor.wire;
}

const auto kGoldenWire = goldenRequestWire();
const auto kGoodResponse9 = std::vector<std::uint8_t>{
    0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7A};
// Same shape, wrong CRC (low CRC byte flipped): a real wire-truth fact.
const auto kBadCrcResponse9 = std::vector<std::uint8_t>{
    0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7B};

// A connected controller with the fake transport, ready for one request.
struct ConnectedSession {
    AnalysisController controller;
    RecordingSerialTransport transport;

    ConnectedSession()
    {
        transport.setPortOpen(true);
        controller.setSerialTransport(&transport);
        controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    }
};

} // namespace

class ActiveMasterTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- TA matrix: recording-transport oracles (M10-A §24) ----
    void ta01_closedTransportNotSent();
    void ta02_busyTransportNotSent();
    void ta03_acceptedFc03SendCountOne();
    void ta04_repeatedBeginWhilePendingStillOneSend();
    void ta05_recordedAduMatchesEvidenceAdu();
    void ta06_corruptResponseEvidencePreserved();
    void ta07_timeoutAfterAcceptedIsPossiblySent();
    void ta08_preSendRejectIsNotSent();
    void ta09_completionTimingIsTestControlled();

    // ---- pending snapshot / session history / Clear contract ----
    void ta10_pendingSnapshotIsTheOnlyAuthority();
    void ta11_sessionRecordsAppendNeverReplace();
    void ta12_appendProjectionApi();
    void ta13_clearResultsContract();
    void ta14_typedSourceIdentity();
    void ta15_staleOrForeignCompletionIgnored();

    // ---- M10-A correction: post-submission transport termination ----
    // TF1: accepted request -> transport error before any response.
    void tf01_transportErrorAfterSubmission();
    // TF2: accepted request -> partial response -> transport error.
    void tf02_partialResponseThenTransportError();
    // TF3: accepted request -> explicit disconnect (real user path).
    void tf03_disconnectAfterSubmission();
    // TF4: pre-send rejection stays NotSent and creates NO terminal evidence.
    void tf04_preSendRejectCreatesNoTerminalEvidence();
    // TF5: a trusted response is the stronger fact (no terminal overrides it).
    void tf05_trustedResponseIsStrongerEvidence();
    // TF6: timeout keeps the observed bytes verbatim (empty vs partial).
    void tf06_timeoutKeepsObservedBytesExactly();
    // Oracle: exactly ONE terminal event per accepted request, ever.
    void tf07_noDoubleTerminalPerAcceptedRequest();
    // Clear Results clears terminal evidence too; a pending request survives
    // it and its later termination lands in the cleared session.
    void tf08_clearResultsAndTerminalEvidence();

    // ---- M10-A final closure: SHORT SUBMISSION (partial acceptance) ----
    // TS1: short submission keeps the full intended ADU + accepted-byte count.
    void ts01_shortSubmissionRetainsEvidence();
    // TS2: no ghost pending/busy afterwards; the next request works normally.
    void ts02_noGhostPendingAfterShortSubmission();
    // TS3: late drivers cannot add a second terminal or a transaction record.
    void ts03_noSecondTerminalAfterShortSubmission();
    // TS4: Clear Results clears the short-submission evidence too.
    void ts04_clearResultsClearsShortSubmissionEvidence();
    // TS5: source replacement clears it; evidence never crosses sources.
    void ts05_sourceReplacementClearsShortSubmissionEvidence();



    // ---- M10-B: FC03 unified contract migration (Active Serial history) ----
    // B01: two completed FC03 transactions in ONE session -> 2 records/rows.
    void b01_sessionHistoryAppends();
    // B02: Success + Timeout + Exception -> 3 rows in completion order.
    void b02_historyOrderStable();
    // B03: statistics cover the WHOLE session (transport terminals excluded).
    void b03_statisticsOverWholeSession();
    // B04: the deterministic diagnosis batch covers the whole session.
    void b04_diagnosisBatchCoversSession();
    // B05: append is an INSERT (never a reset/dataChanged) -> a page-local
    // selection pointing at an existing row stays valid.
    void b05_appendPreservesExistingRows();
    // B06: nothing auto-selects the newest row (no implicit selection state).
    void b06_noAutoSelectOnAppend();
    // B07: Clear Results clears the visible history and the statistics.
    void b07_clearResultsClearsVisibleHistory();
    // B08: Clear while pending keeps the pending request; its completion
    // becomes the first row of the cleared session.
    void b08_clearWhilePending();
    // B09: reconnect = new Active Serial session; old history is gone.
    void b09_reconnectStartsFreshHistory();

    // ---- M10-C1: prepared write foundation (controller context) ----
    // I01-I10: context invalidation matrix.
    void i01_disconnectInvalidatesPrepared();
    void i02_reconnectNewSessionRejectsOldToken();
    void i03_busyBecameTrueInvalidates();
    void i04_busyFalseDoesNotRevive();
    void i05_simulatorReplacementInvalidates();
    void i06_replayReplacementInvalidates();
    void i07_failedReplayKeepsPrepared();
    void i08_unrelatedActivityKeepsPrepared();
    void i09_clearResultsKeepsPrepared();
    void i10_serialErrorTextKeepsPrepared();
    // Z01-Z05: zero dispatch / zero transaction side effects.
    void z01_zeroTransportOnPrepare();
    void z02_zeroTransportOnConfirm();
    void z03_zeroTransportOnCancel();
    void z04_zeroTransportOnInvalidation();
    void z05_zeroTransactionSideEffects();
};

void ActiveMasterTest::ta01_closedTransportNotSent()
{
    ConnectedSession s;
    s.transport.setPortOpen(false); // port silently disappeared

    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    QCOMPARE(s.transport.sendCount(), 0);
    QCOMPARE(s.transport.lastStartResult().accepted, false);
    QCOMPARE(s.transport.lastStartResult().disposition,
             TransportDisposition::NotSent);
    QVERIFY(s.controller.hasSerialError());
    QVERIFY(!s.controller.serialConnected());
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.observedCount(), 0);
}

void ActiveMasterTest::ta02_busyTransportNotSent()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QCOMPARE(s.transport.sendCount(), 1);

    // Transport-level: a second begin while pending is refused NotSent.
    const auto descriptor = std::get<ActiveRequestDescriptor>(
        encodeActiveRequest(ActiveRequestIntent{
            .function = ActiveFunction::ReadHoldingRegisters,
            .unitId = 1,
            .timeout = ms{1000},
            .payload = ReadHoldingRegistersIntent{.startAddress = 0,
                                                  .quantity = 1}}));
    const auto direct = s.transport.startActiveRequest(descriptor);
    QCOMPARE(direct.accepted, false);
    QCOMPARE(direct.disposition, TransportDisposition::NotSent);
    QCOMPARE(s.transport.sendCount(), 1);
    QCOMPARE(s.transport.sendCount(), s.transport.startAttemptCount() - 1);
}

void ActiveMasterTest::ta03_acceptedFc03SendCountOne()
{
    ConnectedSession s;

    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 1);
    QCOMPARE(s.transport.lastStartResult().accepted, true);
    QCOMPARE(s.transport.lastStartResult().disposition,
             TransportDisposition::PossiblySent);
    QCOMPARE(s.transport.sentAduLog().size(), std::size_t{1});
    QCOMPARE(s.transport.sentAduLog()[0], kGoldenWire);

    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.successCount(), 1);
    QVERIFY(!s.controller.serialBusy());
}

void ActiveMasterTest::ta04_repeatedBeginWhilePendingStillOneSend()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QCOMPARE(s.transport.sendCount(), 1);

    // Double click / repeated key while pending: exactly one send, ever.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    QCOMPARE(s.transport.startAttemptCount(), 1); // guard never reached the wire
    QCOMPARE(s.transport.sendCount(), 1);
    QVERIFY(s.controller.serialBusy());
}

void ActiveMasterTest::ta05_recordedAduMatchesEvidenceAdu()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    // The evidence's request ADU and the transport's recorded ADU must be the
    // same bytes, byte for byte (W17).
    QCOMPARE(record.evidence.requestAdu, s.transport.sentAduLog()[0]);
    QCOMPARE(record.evidence.requestAdu, kGoldenWire);
    QCOMPARE(record.unitId(), std::uint8_t{1});
    QCOMPARE(record.functionCode(), std::uint8_t{0x03});
}

void ActiveMasterTest::ta06_corruptResponseEvidencePreserved()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kBadCrcResponse9);
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    // CRC error is a diagnosis, and the DAMAGED bytes are kept verbatim.
    QCOMPARE(record.analysis.status, TransactionStatus::CrcError);
    QCOMPARE(record.evidence.responseAdu, kBadCrcResponse9);
    QCOMPARE(record.evidence.disposition, TransportDisposition::PossiblySent);
}

void ActiveMasterTest::ta07_timeoutAfterAcceptedIsPossiblySent()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setCompletionElapsed(ms{1000});
    s.transport.completeWithTimeout();

    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    // The request entered the transmission lifecycle and nothing answered:
    // Timeout + PossiblySent (device write/read state UNKNOWN — never
    // presented as "nothing happened").
    QCOMPARE(record.analysis.status, TransactionStatus::Timeout);
    QCOMPARE(record.evidence.disposition, TransportDisposition::PossiblySent);
    QVERIFY(record.evidence.responseAdu.empty());
    QCOMPARE(record.evidence.requestAdu, kGoldenWire);
}

void ActiveMasterTest::ta08_preSendRejectIsNotSent()
{
    ConnectedSession s;
    s.transport.setAcceptRequests(false); // explicit pre-send rejection gate
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    // Zero sends, zero fabricated transactions: a pre-send local failure is
    // NOT a Modbus response (no ProtocolError row, no statistics change).
    QCOMPARE(s.transport.sendCount(), 0);
    QCOMPARE(s.transport.lastStartResult().accepted, false);
    QCOMPARE(s.transport.lastStartResult().disposition,
             TransportDisposition::NotSent);
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.observedCount(), 0);
    QVERIFY(s.controller.hasSerialError());
    QVERIFY(!s.controller.serialBusy());
    QVERIFY(s.controller.serialConnected()); // the port itself is still open
}

void ActiveMasterTest::ta09_completionTimingIsTestControlled()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    // Nothing published until the test drives the completion — no sleeps.
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QVERIFY(s.controller.serialBusy());
    QVERIFY(s.transport.hasPendingTransaction());

    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QVERIFY(!s.controller.serialBusy());
    QVERIFY(!s.transport.hasPendingTransaction());
}

void ActiveMasterTest::ta10_pendingSnapshotIsTheOnlyAuthority()
{
    ConnectedSession s;
    // Start with one request...
    s.controller.readHoldingRegistersOnce(0x11, 0x0064, 0x0002, 1000);
    const auto firstWire = goldenRequestWire(0x11, 0x0064, 0x0002);
    QCOMPARE(s.transport.sentAduLog()[0], firstWire);

    // ...attempt a DIFFERENT one while pending (refused)...
    s.controller.readHoldingRegistersOnce(0x22, 0x0100, 0x0001, 1000);
    QCOMPARE(s.transport.sendCount(), 1);

    // ...then complete: the result must describe the FIRST request.
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.request.intent.unitId, std::uint8_t{0x11});
    const auto& payload =
        std::get<ReadHoldingRegistersIntent>(record.request.intent.payload);
    QCOMPARE(payload.startAddress, std::uint16_t{0x0064});
    QCOMPARE(payload.quantity, std::uint16_t{0x0002});
    QCOMPARE(record.evidence.requestAdu, firstWire);
    QCOMPARE(s.controller.transactionModel()
                 ->data(s.controller.transactionModel()->index(0, 0),
                        TransactionListModel::DeviceAddressRole),
             QVariant{0x11});
}

void ActiveMasterTest::ta11_sessionRecordsAppendNeverReplace()
{
    ConnectedSession s;
    // Transaction 1: success.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    // Transaction 2: timeout.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes({});
    s.transport.setCompletionElapsed(ms{1000});
    s.transport.completeWithTimeout();

    // Authority: BOTH transactions retained in order, each with its evidence.
    QCOMPARE(s.controller.activeSerialRecordCount(), 2);
    const auto& records = s.controller.activeSerialRecords();
    QCOMPARE(records[0].analysis.status, TransactionStatus::Success);
    QCOMPARE(records[1].analysis.status, TransactionStatus::Timeout);
    QVERIFY(records[0].evidence.requestAdu == records[1].evidence.requestAdu);
    QVERIFY(!records[0].evidence.responseAdu.empty());
    QVERIFY(records[1].evidence.responseAdu.empty());

    // M10-B CONTRACT MIGRATION: the Active Serial presentation is no longer
    // latest-only (that was the M10-A state this test used to lock). Both
    // transactions of the session are visible and counted; the RECORD
    // invariants asserted above are unchanged.
    QCOMPARE(s.controller.transactionModel()->rowCount(), 2);
    QCOMPARE(s.controller.observedCount(), 2);
    QCOMPARE(s.controller.timeoutCount(), 1);
}

void ActiveMasterTest::ta12_appendProjectionApi()
{
    AnalysisController controller; // no transport needed for the projection

    const auto appendRecord = [&](std::uint8_t unit, TransactionStatus status) {
        auto descriptor = std::get<ActiveRequestDescriptor>(
            encodeActiveRequest(ActiveRequestIntent{
                .function = ActiveFunction::ReadHoldingRegisters,
                .unitId = unit,
                .timeout = ms{1000},
                .payload = ReadHoldingRegistersIntent{
                    .startAddress = 0, .quantity = 2}}));
        controller.appendActiveSerialTransaction(
            modbuslens::core::ActiveTransactionRecord{
                .sessionId = 5,
                .request = descriptor,
                .evidence = modbuslens::core::ActiveTransactionEvidence{
                    .requestAdu = descriptor.wire,
                    .responseAdu = status == TransactionStatus::Success
                        ? kGoodResponse9
                        : std::vector<std::uint8_t>{},
                    .disposition = TransportDisposition::PossiblySent,
                },
                .analysis = TransactionAnalysis{
                    .status = status,
                    .elapsed = ms{25},
                    .exceptionCode = std::nullopt,
                    .issue = std::nullopt,
                    .values = {}},
            });
    };

    appendRecord(1, TransactionStatus::Success);
    appendRecord(2, TransactionStatus::Timeout);
    appendRecord(3, TransactionStatus::Success);

    // APPEND, never replacement: three records -> three rows, aggregate stats.
    QCOMPARE(controller.activeSerialRecordCount(), 3);
    QCOMPARE(controller.transactionModel()->rowCount(), 3);
    QCOMPARE(controller.observedCount(), 3);
    QCOMPARE(controller.successCount(), 2);
    QCOMPARE(controller.timeoutCount(), 1);
    QCOMPARE(controller.modeLabel(), QStringLiteral("串口模式"));
    QCOMPARE(controller.activeSerialSessionId(), std::uint64_t{0});
    // Rows project the recorded unit ids in append order.
    for (int row = 0; row < 3; ++row) {
        QCOMPARE(controller.transactionModel()
                     ->data(controller.transactionModel()->index(row, 0),
                            TransactionListModel::DeviceAddressRole),
                 QVariant{row + 1});
    }
}

void ActiveMasterTest::ta13_clearResultsContract()
{
    // Part 1: completed history is cleared, source identity stays.
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.completeWithTimeout();
    QCOMPARE(s.controller.activeSerialRecordCount(), 2);

    s.controller.clearResults();

    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.modeLabel(), QStringLiteral("串口模式"));
    QCOMPARE(s.controller.sourceLabel(), QStringLiteral("COM_TEST @ 9600"));
    QVERIFY(s.controller.serialConnected()); // Clear != Disconnect

    // Part 2: a PENDING request is not canceled by Clear; its completion
    // enters the now-cleared view as a NEW transaction.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QVERIFY(s.transport.hasPendingTransaction());
    s.controller.clearResults();
    QVERIFY(s.transport.hasPendingTransaction()); // still in flight

    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QCOMPARE(s.controller.observedCount(), 1);
}

void ActiveMasterTest::ta14_typedSourceIdentity()
{
    AnalysisController controller;
    RecordingSerialTransport transport;
    transport.setPortOpen(true);
    controller.setSerialTransport(&transport);

    // Initial state is the Simulator source (typed, not inferred from text).
    QCOMPARE(controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::Simulator);
    QCOMPARE(controller.activeSerialSessionId(), std::uint64_t{0});

    controller.runDemoBatch();
    QCOMPARE(controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::Simulator);

    // First successful connect opens Active Serial session #1.
    controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    QCOMPARE(controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::ActiveSerial);
    QCOMPARE(controller.activeSerialSessionId(), std::uint64_t{1});

    // Second connect = a NEW session (histories never merge).
    controller.disconnectSerial();
    controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    QCOMPARE(controller.activeSerialSessionId(), std::uint64_t{2});

    // Replay switches the typed source away from Active Serial.
    controller.loadReplayFile(
        QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    QCOMPARE(controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::Replay);
    QCOMPARE(controller.modeLabel(), QStringLiteral("回放模式"));
}

void ActiveMasterTest::ta15_staleOrForeignCompletionIgnored()
{
    // Part 1: a completion with NO pending snapshot must be ignored entirely.
    AnalysisController controller;
    controller.runDemoBatch();
    const auto rowsBefore = controller.transactionModel()->rowCount();
    const auto observedBefore = controller.observedCount();

    const auto makeResult = [](const ActiveRequestDescriptor& request) {
        return ActiveTransactionResult{
            .request = request,
            .responseAdu = kGoodResponse9,
            .disposition = TransportDisposition::PossiblySent,
            .analysis = TransactionAnalysis{
                .status = TransactionStatus::Success,
                .elapsed = ms{25},
                .exceptionCode = std::nullopt,
                .issue = std::nullopt,
                .values = {}},
        };
    };
    const auto someRequest = std::get<ActiveRequestDescriptor>(
        encodeActiveRequest(ActiveRequestIntent{
            .function = ActiveFunction::ReadHoldingRegisters,
            .unitId = 1,
            .timeout = ms{1000},
            .payload = ReadHoldingRegistersIntent{.startAddress = 0,
                                                  .quantity = 2}}));
    controller.handleSerialTransactionCompleted(makeResult(someRequest));
    QCOMPARE(controller.transactionModel()->rowCount(), rowsBefore);
    QCOMPARE(controller.observedCount(), observedBefore);

    // Part 2: while pending, a completion answering a DIFFERENT request is
    // ignored too — the send-time snapshot is the only accepted identity.
    RecordingSerialTransport transport;
    transport.setPortOpen(true);
    controller.setSerialTransport(&transport);
    controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    const auto foreignRequest = std::get<ActiveRequestDescriptor>(
        encodeActiveRequest(ActiveRequestIntent{
            .function = ActiveFunction::ReadHoldingRegisters,
            .unitId = 9,
            .timeout = ms{1000},
            .payload = ReadHoldingRegistersIntent{.startAddress = 0,
                                                  .quantity = 1}}));
    controller.handleSerialTransactionCompleted(makeResult(foreignRequest));
    QCOMPARE(controller.transactionModel()->rowCount(), 0); // foreign ignored
    QCOMPARE(controller.activeSerialRecordCount(), 0);
    QVERIFY(controller.serialBusy()); // the real pending is untouched

    // The REAL pending request's completion is the one that lands.
    transport.setResponseBytes(kGoodResponse9);
    transport.completeWithResponse();
    QCOMPARE(controller.activeSerialRecordCount(), 1);
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
}

// ---- M10-A correction: post-submission transport termination ----

void ActiveMasterTest::tf01_transportErrorAfterSubmission()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QCOMPARE(s.transport.sendCount(), 1);

    s.transport.failTransport(QStringLiteral("测试传输失败"));

    // The attempt is retained: snapshot + exact ADU + empty response + the
    // conservative submission disposition + the typed reason.
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    const auto& terminal = s.controller.activeSerialTerminations().front();
    QCOMPARE(terminal.request.intent.unitId, std::uint8_t{1});
    QCOMPARE(std::get<ReadHoldingRegistersIntent>(terminal.request.intent.payload)
                 .startAddress,
             std::uint16_t{0});
    QCOMPARE(terminal.request.wire, kGoldenWire);
    QCOMPARE(terminal.evidence().requestAdu, kGoldenWire);
    QCOMPARE(terminal.evidence().requestAdu, s.transport.sentAduLog()[0]);
    QVERIFY(terminal.responseAdu.empty());
    QCOMPARE(terminal.disposition, TransportDisposition::PossiblySent);
    QCOMPARE(terminal.reason,
             modbuslens::core::TransportTerminalReason::TransportError);
    QCOMPARE(modbuslens::core::transportTerminalReasonName(terminal.reason),
             std::string_view{"transport_error"});

    // No Modbus outcome was fabricated and no row/statistics appeared.
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.observedCount(), 0);
    QVERIFY(!s.controller.serialBusy());
    QVERIFY(s.controller.hasSerialError());
    QCOMPARE(s.transport.sendCount(), 1); // exactly one send, ever
}

void ActiveMasterTest::tf02_partialResponseThenTransportError()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    const std::vector<std::uint8_t> partial = {0x01, 0x03, 0x04, 0x00};
    s.transport.setResponseBytes(partial);
    s.transport.feedPartialBytes(); // observed, candidate not complete
    s.transport.failTransport(QStringLiteral("测试传输失败"));

    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    const auto& terminal = s.controller.activeSerialTerminations().front();
    // The partial bytes survive the abort BYTE FOR BYTE — the abort must not
    // clear them before the evidence is built.
    QCOMPARE(terminal.responseAdu, partial);
    QCOMPARE(terminal.evidence().responseAdu, partial);
    QCOMPARE(terminal.request.wire, kGoldenWire);
    QCOMPARE(terminal.disposition, TransportDisposition::PossiblySent);
    QCOMPARE(s.controller.activeSerialRecordCount(), 0); // never a fake verdict
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
}

void ActiveMasterTest::tf03_disconnectAfterSubmission()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QVERIFY(s.transport.hasPendingTransaction());

    // Real user path: disconnect -> controller teardown -> transport close.
    s.controller.disconnectSerial();

    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    const auto& terminal = s.controller.activeSerialTerminations().front();
    QCOMPARE(terminal.reason,
             modbuslens::core::TransportTerminalReason::DisconnectedAfterSubmission);
    QCOMPARE(modbuslens::core::transportTerminalReasonName(terminal.reason),
             std::string_view{"disconnected_after_submission"});
    QCOMPARE(terminal.disposition, TransportDisposition::PossiblySent);
    QCOMPARE(terminal.request.wire, kGoldenWire);
    QVERIFY(terminal.responseAdu.empty());
    QCOMPARE(s.transport.sendCount(), 1);
    QVERIFY(!s.controller.serialConnected());
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.activeSerialRecordCount(), 0); // not a transaction
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);

    // A new connection is a NEW Active Serial session: the previous session's
    // termination evidence is cleared by the source-replacement contract
    // (evidence never leaks across sessions).
    s.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);

    // Exactly like the production adapter: a close with NOTHING in flight is
    // a pure connection change and adds no terminal evidence.
    s.transport.disconnectAfterSubmission();
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
}

void ActiveMasterTest::tf04_preSendRejectCreatesNoTerminalEvidence()
{
    ConnectedSession s;
    s.transport.setAcceptRequests(false);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    QCOMPARE(s.transport.sendCount(), 0);
    QCOMPARE(s.transport.lastStartResult().disposition,
             TransportDisposition::NotSent);
    // A pre-send rejection produces NO completed transaction AND no terminal
    // evidence: it never entered the transmission lifecycle.
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QVERIFY(s.controller.hasSerialError());
}

void ActiveMasterTest::tf05_trustedResponseIsStrongerEvidence()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    // The response is the stronger fact: a completed Modbus transaction with
    // its verdict, and NO terminal event on top of it. The recorded
    // submission disposition is still PossiblySent (a transport fact) — it
    // must never be presented as a final error state.
    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
    const auto& record = s.controller.activeSerialRecords().front();
    QCOMPARE(record.analysis.status, TransactionStatus::Success);
    QCOMPARE(record.evidence.disposition, TransportDisposition::PossiblySent);
    QCOMPARE(record.evidence.responseAdu, kGoodResponse9);
    QCOMPARE(s.controller.successCount(), 1);
}

void ActiveMasterTest::tf06_timeoutKeepsObservedBytesExactly()
{
    // Scenario A: nothing was ever observed -> pure no-response Timeout with
    // an EMPTY response ADU (never a fabricated byte).
    {
        ConnectedSession s;
        s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
        s.transport.setCompletionElapsed(ms{1000});
        s.transport.completeWithTimeout();

        QCOMPARE(s.controller.activeSerialRecordCount(), 1);
        QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
        const auto& record = s.controller.activeSerialRecords().front();
        QCOMPARE(record.analysis.status, TransactionStatus::Timeout);
        QCOMPARE(record.evidence.disposition, TransportDisposition::PossiblySent);
        QVERIFY(record.evidence.responseAdu.empty());
        QCOMPARE(record.evidence.requestAdu, kGoldenWire);
    }
    // Scenario B: bytes DID arrive -> the wire-truth verdict over the exact
    // observed bytes (3 bytes cannot be a frame -> ProtocolError), still with
    // PossiblySent and still no terminal event.
    {
        ConnectedSession s;
        s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
        const std::vector<std::uint8_t> partial = {0x01, 0x03, 0x04};
        s.transport.setResponseBytes(partial);
        s.transport.feedPartialBytes();
        s.transport.setCompletionElapsed(ms{1000});
        s.transport.completeWithTimeout();

        QCOMPARE(s.controller.activeSerialRecordCount(), 1);
        QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
        const auto& record = s.controller.activeSerialRecords().front();
        QCOMPARE(record.analysis.status, TransactionStatus::ProtocolError);
        QCOMPARE(record.evidence.responseAdu, partial);
        QCOMPARE(record.evidence.disposition, TransportDisposition::PossiblySent);
    }
}

void ActiveMasterTest::tf07_noDoubleTerminalPerAcceptedRequest()
{
    // Part 1: terminal first, then every late driver must stay silent.
    {
        ConnectedSession s;
        s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
        s.transport.failTransport(QStringLiteral("测试传输失败"));
        QCOMPARE(s.controller.activeSerialTerminalCount(), 1);

        // A late timeout callback (the stopped timer's equivalent), a late
        // response completion, a second disconnect and a repeated error may
        // not add anything.
        s.transport.completeWithTimeout();
        s.transport.setResponseBytes(kGoodResponse9);
        s.transport.completeWithResponse();
        s.transport.disconnectAfterSubmission();
        s.transport.failTransport(QStringLiteral("重复错误"));

        QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
        QCOMPARE(s.controller.activeSerialRecordCount(), 0);
        QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
        QCOMPARE(s.transport.sendCount(), 1);
    }
    // Part 2: the controller boundary is a guard too — a duplicate terminal
    // for the same request is ignored, and a terminal with no pending request
    // never lands.
    {
        AnalysisController controller;
        RecordingSerialTransport transport;
        transport.setPortOpen(true);
        controller.setSerialTransport(&transport);
        controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
        controller.readHoldingRegistersOnce(1, 0, 2, 1000);

        const auto descriptor = std::get<ActiveRequestDescriptor>(
            encodeActiveRequest(ActiveRequestIntent{
                .function = ActiveFunction::ReadHoldingRegisters,
                .unitId = 1,
                .timeout = ms{1000},
                .payload = ReadHoldingRegistersIntent{.startAddress = 0,
                                                      .quantity = 2}}));
        const modbuslens::core::ActiveTransportTerminal terminal{
            .request = descriptor,
            .responseAdu = {},
            .disposition = TransportDisposition::PossiblySent,
            .reason = modbuslens::core::TransportTerminalReason::TransportError,
            .submissionAcceptedByteCount = std::nullopt,
        };
        controller.handleSerialTransactionTerminated(terminal);
        controller.handleSerialTransactionTerminated(terminal); // duplicate
        QCOMPARE(controller.activeSerialTerminalCount(), 1);

        // No pending request any more -> a late terminal is ignored whole.
        controller.handleSerialTransactionTerminated(terminal);
        QCOMPARE(controller.activeSerialTerminalCount(), 1);
    }
}

void ActiveMasterTest::tf08_clearResultsAndTerminalEvidence()
{
    ConnectedSession s;
    // One completed transaction + one terminated attempt in the session.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.failTransport(QStringLiteral("测试传输失败"));
    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);

    s.controller.clearResults();

    // Both kinds of completed evidence are cleared together; the source
    // identity survives and nothing was cancelled.
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
    QCOMPARE(s.controller.modeLabel(), QStringLiteral("串口模式"));
    QVERIFY(s.controller.serialConnected());

    // Clear -> pending -> post-submission error: the pending request was NOT
    // cancelled, and its termination evidence enters the cleared session.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QVERIFY(s.transport.hasPendingTransaction());
    s.controller.clearResults();
    QVERIFY(s.transport.hasPendingTransaction());
    s.transport.failTransport(QStringLiteral("测试传输失败"));

    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1); // idempotent read
    const auto& terminal = s.controller.activeSerialTerminations().front();
    QCOMPARE(terminal.reason,
             modbuslens::core::TransportTerminalReason::TransportError);
    QCOMPARE(terminal.request.wire, kGoldenWire);
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
}

// ---- M10-A final closure: short submission ----

void ActiveMasterTest::ts01_shortSubmissionRetainsEvidence()
{
    ConnectedSession s;
    // 8-byte FC03 ADU; the transport API reports accepting only 4 bytes.
    s.transport.setSubmissionAcceptedBytes(4);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    // The attempt happened once, but it was NEVER a fully accepted send and
    // never became accepted sendCount.
    QCOMPARE(s.transport.startAttemptCount(), 1);
    QCOMPARE(s.transport.sendCount(), 0);
    QCOMPARE(s.transport.lastStartResult().accepted, false);
    QCOMPARE(s.transport.lastStartResult().disposition,
             TransportDisposition::PossiblySent);
    QVERIFY(!s.transport.hasPendingTransaction());

    // Durable evidence: exactly one terminal, typed as ShortSubmission.
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    const auto& terminal = s.controller.activeSerialTerminations().front();
    QCOMPARE(terminal.reason,
             modbuslens::core::TransportTerminalReason::ShortSubmission);
    QCOMPARE(modbuslens::core::transportTerminalReasonName(terminal.reason),
             std::string_view{"short_submission"});
    QCOMPARE(terminal.disposition, TransportDisposition::PossiblySent);
    // requestAdu = the COMPLETE intended ADU of this attempt (not the part
    // the API accepted, and not "what the device received").
    QCOMPARE(terminal.request.wire, kGoldenWire);
    QCOMPARE(terminal.request.wire.size(), std::size_t{8});
    QCOMPARE(terminal.evidence().requestAdu, kGoldenWire);
    QVERIFY(terminal.responseAdu.empty());
    // The accepted-byte count describes the transport API boundary only.
    QVERIFY(terminal.submissionAcceptedByteCount.has_value());
    QCOMPARE(*terminal.submissionAcceptedByteCount, std::uint16_t{4});

    // No Modbus verdict, no row, no statistics entry.
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.observedCount(), 0);
    QVERIFY(!s.controller.serialBusy());
    QVERIFY(s.controller.hasSerialError()); // the existing human-readable lane
}

void ActiveMasterTest::ts02_noGhostPendingAfterShortSubmission()
{
    ConnectedSession s;
    s.transport.setSubmissionAcceptedBytes(3);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    // No ghost state: nothing pending anywhere, and the runtime is idle.
    QVERIFY(!s.controller.serialBusy());
    QVERIFY(!s.transport.hasPendingTransaction());
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);

    // The very next request works normally: full acceptance + completion.
    s.transport.setSubmissionAcceptedBytes(std::nullopt);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QCOMPARE(s.transport.startAttemptCount(), 2);
    QCOMPARE(s.transport.sendCount(), 1); // only the fully accepted one
    QVERIFY(s.controller.serialBusy());
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1); // unchanged
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QVERIFY(!s.controller.serialBusy());
}

void ActiveMasterTest::ts03_noSecondTerminalAfterShortSubmission()
{
    ConnectedSession s;
    s.transport.setSubmissionAcceptedBytes(4);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    QCOMPARE(s.transport.sendCount(), 0);

    // Every late driver must stay silent: there is no pending request to
    // terminate and no session transaction to complete.
    s.transport.completeWithTimeout();
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.transport.failTransport(QStringLiteral("迟到错误"));
    s.transport.disconnectAfterSubmission();

    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.observedCount(), 0);
}

void ActiveMasterTest::ts04_clearResultsClearsShortSubmissionEvidence()
{
    ConnectedSession s;
    s.transport.setSubmissionAcceptedBytes(4);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);

    s.controller.clearResults();

    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    // Source identity is untouched by Clear (Clear != Disconnect).
    QCOMPARE(s.controller.modeLabel(), QStringLiteral("串口模式"));
    QCOMPARE(s.controller.sourceLabel(), QStringLiteral("COM_TEST @ 9600"));
    QVERIFY(s.controller.serialConnected());
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{1});
}

void ActiveMasterTest::ts05_sourceReplacementClearsShortSubmissionEvidence()
{
    ConnectedSession s;

    // (a) a NEW Active Serial session clears the previous session's evidence.
    s.transport.setSubmissionAcceptedBytes(4);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    s.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{2});

    // (b) Simulator replacement clears it and the source is typed Simulator.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    s.controller.runDemoBatch();
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
    QCOMPARE(s.controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::Simulator);

    // (c) Replay replacement clears it as well.
    s.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QCOMPARE(s.controller.activeSerialTerminalCount(), 1);
    s.controller.loadReplayFile(
        QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    QCOMPARE(s.controller.activeSerialTerminalCount(), 0);
    QCOMPARE(s.controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::Replay);
    QCOMPARE(s.controller.modeLabel(), QStringLiteral("回放模式"));
}

// ---- M10-B: FC03 unified contract migration ----

void ActiveMasterTest::b01_sessionHistoryAppends()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    // Same session: BOTH transactions are retained and BOTH are visible.
    QCOMPARE(s.controller.activeSerialRecordCount(), 2);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 2);
    QCOMPARE(s.controller.observedCount(), 2);
    QCOMPARE(s.controller.successCount(), 2);
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{1});
}

void ActiveMasterTest::b02_historyOrderStable()
{
    ConnectedSession s;
    // R1 Success, R2 Timeout, R3 Exception 0x02 — in this order.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.controller.readHoldingRegistersOnce(2, 0, 2, 1000);
    s.transport.setResponseBytes({});
    s.transport.setCompletionElapsed(ms{1000});
    s.transport.completeWithTimeout();
    s.controller.readHoldingRegistersOnce(3, 0, 2, 1000);
    s.transport.setResponseBytes(modbuslens::core::encodeRtuFrame(
        modbuslens::core::ModbusRtuFrame{.address = 0x03, .functionCode = 0x83, .data = {0x02}}));
    s.transport.setCompletionElapsed(ms{18});
    s.transport.completeWithResponse();

    const auto* model = s.controller.transactionModel();
    QCOMPARE(model->rowCount(), 3);
    // Oldest -> newest, appended at the end; identity (unit) follows the
    // request order and is never re-sorted.
    for (int row = 0; row < 3; ++row) {
        QCOMPARE(model->data(model->index(row, 0),
                             TransactionListModel::DeviceAddressRole),
                 QVariant{row + 1});
        QCOMPARE(model->data(model->index(row, 0),
                             TransactionListModel::FunctionCodeRole),
                 QVariant{3});
    }
    QCOMPARE(model->data(model->index(0, 0), TransactionListModel::StatusTextRole).toString(),
             QStringLiteral("成功"));
    QCOMPARE(model->data(model->index(1, 0), TransactionListModel::StatusTextRole).toString(),
             QStringLiteral("超时"));
    QCOMPARE(model->data(model->index(2, 0), TransactionListModel::StatusTextRole).toString(),
             QStringLiteral("异常"));
    QCOMPARE(model->data(model->index(2, 0), TransactionListModel::ExceptionCodeRole),
             QVariant{0x02});
}

void ActiveMasterTest::b03_statisticsOverWholeSession()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.controller.readHoldingRegistersOnce(2, 0, 2, 1000);
    s.transport.setResponseBytes({});
    s.transport.setCompletionElapsed(ms{1000});
    s.transport.completeWithTimeout();
    s.controller.readHoldingRegistersOnce(3, 0, 2, 1000);
    s.transport.setResponseBytes(modbuslens::core::encodeRtuFrame(
        modbuslens::core::ModbusRtuFrame{.address = 0x03, .functionCode = 0x83, .data = {0x02}}));
    s.transport.setCompletionElapsed(ms{18});
    s.transport.completeWithResponse();

    // Whole-session aggregation (not latest-only).
    QCOMPARE(s.controller.observedCount(), 3);
    QCOMPARE(s.controller.completedCount(), 3);
    QCOMPARE(s.controller.successCount(), 1);
    QCOMPARE(s.controller.timeoutCount(), 1);
    QCOMPARE(s.controller.exceptionCount(), 1);
    QVERIFY(s.controller.hasSuccessRate());
    QVERIFY(qFuzzyCompare(s.controller.successRate(), 1.0 / 3.0));

    // Transport terminals never enter the Modbus outcome counts (M10-A
    // invariant continued by M10-B): a short submission and a port error
    // add evidence, not statistics.
    s.transport.setSubmissionAcceptedBytes(4);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setSubmissionAcceptedBytes(std::nullopt);
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.failTransport(QStringLiteral("测试传输失败"));

    QCOMPARE(s.controller.activeSerialTerminalCount(), 2);
    QCOMPARE(s.controller.observedCount(), 3);   // unchanged
    QCOMPARE(s.controller.completedCount(), 3);  // unchanged
    QCOMPARE(s.controller.activeSerialRecordCount(), 3);
    QCOMPARE(s.controller.transactionModel()->rowCount(), 3); // no fake rows
}

void ActiveMasterTest::b04_diagnosisBatchCoversSession()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.controller.readHoldingRegistersOnce(2, 0, 2, 1000);
    s.transport.setResponseBytes({});
    s.transport.setCompletionElapsed(ms{1000});
    s.transport.completeWithTimeout();
    s.controller.readHoldingRegistersOnce(3, 0, 2, 1000);
    s.transport.setResponseBytes(modbuslens::core::encodeRtuFrame(
        modbuslens::core::ModbusRtuFrame{.address = 0x03, .functionCode = 0x83, .data = {0x02}}));
    s.transport.setCompletionElapsed(ms{18});
    s.transport.completeWithResponse();

    s.controller.runBaselineDiagnosis();

    QVERIFY(s.controller.hasBaselineDiagnosis());
    const QString text = s.controller.baselineDiagnosisText();
    // The report covers the WHOLE session: the timeout AND the device
    // exception both appear as facts. A latest-only batch could only ever
    // report the last one, so this pair is the whole-batch proof.
    QVERIFY(text.contains(QStringLiteral("无响应超时")));
    QVERIFY(text.contains(QStringLiteral("设备异常 0x02")));
}

void ActiveMasterTest::b05_appendPreservesExistingRows()
{
    ConnectedSession s;
    // Snapshot the first row (all roles) plus the model's signal behaviour.
    int resetCount = 0;
    int insertCount = 0;
    int dataChangedCount = 0;
    const auto* model = s.controller.transactionModel();
    connect(model, &QAbstractItemModel::modelReset, this,
            [&resetCount]() { ++resetCount; });
    connect(model, &QAbstractItemModel::rowsInserted, this,
            [&insertCount]() { ++insertCount; });
    connect(model, &QAbstractItemModel::dataChanged, this,
            [&dataChangedCount]() { ++dataChangedCount; });

    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    const int resetsAfterFirst = resetCount;
    const int insertsAfterFirst = insertCount;
    const auto rowZeroBefore = model->data(model->index(0, 0),
                                          TransactionListModel::StatusTextRole);

    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    // The second completion INSERTS (append): no reset, no dataChanged, so a
    // page-local selection pointing at row 0 is still valid and still shows
    // the same transaction (M9-D selection contract, M10-B §12/§13).
    QCOMPARE(resetCount, resetsAfterFirst);
    QCOMPARE(insertCount, insertsAfterFirst + 1);
    QCOMPARE(dataChangedCount, 0);
    QCOMPARE(model->data(model->index(0, 0), TransactionListModel::StatusTextRole),
             rowZeroBefore);
    QCOMPARE(model->rowCount(), 2);
}

void ActiveMasterTest::b06_noAutoSelectOnAppend()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.completeWithTimeout();

    // The runtime owns NO selection state: appending a transaction changes
    // the history, the statistics and the diagnosis batch — and nothing else.
    // (The Transactions page keeps selection in page-local state and only
    // clears it on modelReset, which append never emits; the qml harness
    // continues to guard no-select-on-focus for the keyboard path.)
    QCOMPARE(s.controller.transactionModel()->rowCount(), 2);
    QCOMPARE(s.controller.modeLabel(), QStringLiteral("串口模式"));
    QCOMPARE(s.controller.sourceLabel(), QStringLiteral("COM_TEST @ 9600"));
    QVERIFY(!s.controller.serialBusy());
    QVERIFY(!s.controller.hasSerialError());
}

void ActiveMasterTest::b07_clearResultsClearsVisibleHistory()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.completeWithTimeout();
    QCOMPARE(s.controller.transactionModel()->rowCount(), 2);
    QCOMPARE(s.controller.observedCount(), 2);

    s.controller.clearResults();

    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.observedCount(), 0);
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QVERIFY(!s.controller.hasSuccessRate());
    QVERIFY(!s.controller.hasAverageSuccessLatency());
    // Clear != Disconnect: source identity and connection survive.
    QCOMPARE(s.controller.modeLabel(), QStringLiteral("串口模式"));
    QCOMPARE(s.controller.sourceLabel(), QStringLiteral("COM_TEST @ 9600"));
    QVERIFY(s.controller.serialConnected());
}

void ActiveMasterTest::b08_clearWhilePending()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);

    // R2 goes in flight, THEN Clear Results: the completed R1 disappears and
    // the pending request is NOT cancelled.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QVERIFY(s.transport.hasPendingTransaction());
    s.controller.clearResults();
    QVERIFY(s.transport.hasPendingTransaction());
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);

    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    // R2's completion is the FIRST row of the cleared session; R1 is gone for
    // good and never resurrects.
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QCOMPARE(s.controller.observedCount(), 1);
    QCOMPARE(s.controller.successCount(), 1);
    QCOMPARE(s.controller.activeSerialRecordCount(), 1);
}

void ActiveMasterTest::b09_reconnectStartsFreshHistory()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    QCOMPARE(s.controller.transactionModel()->rowCount(), 2);
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{1});

    // Disconnect + reconnect = a NEW Active Serial session: the visible
    // history starts empty and the old records never leak into it.
    s.controller.disconnectSerial();
    s.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{2});
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);
    QCOMPARE(s.controller.activeSerialRecordCount(), 0);
    QCOMPARE(s.controller.observedCount(), 0);

    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{2});
}

// ---- M10-C1: prepared write foundation (controller context) ----

void ActiveMasterTest::i01_disconnectInvalidatesPrepared()
{
    ConnectedSession s;
    const auto prepared = s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000);
    QVERIFY(isPrepared(prepared));
    const auto token = s.controller.preparedWriteToken();
    QVERIFY(token.has_value());

    // Repeated Write with an active snapshot: rejected as AlreadyPrepared, and
    // the original generation is kept (no second snapshot, no new token).
    QVERIFY(isAlreadyPrepared(
        s.controller.prepareWriteSingleRegister(22, 0x0100, 0x00FF, 2000)));
    QCOMPARE(s.controller.preparedWriteToken(), token);
    QCOMPARE(s.controller.preparedWriteSnapshot()->intent.unitId, std::uint8_t{11});

    s.controller.disconnectSerial();

    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Invalidated);
    QCOMPARE(s.controller.preparedWriteInvalidReason(),
             std::optional{modbuslens::core::PreparedWriteInvalidReason::Disconnected});
    const auto confirmed = s.controller.confirmPreparedWrite(*token);
    QVERIFY(isConfirmRejected(confirmed));
}

void ActiveMasterTest::i02_reconnectNewSessionRejectsOldToken()
{
    ConnectedSession s;
    const auto prepared = s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000);
    QVERIFY(isPrepared(prepared));
    const auto oldToken = *s.controller.preparedWriteToken();
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{1});

    s.controller.disconnectSerial();
    s.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    QCOMPARE(s.controller.activeSerialSessionId(), std::uint64_t{2});

    // The terminal reason from the disconnect is preserved (never rewritten),
    // and the old token can never act in the new session.
    QCOMPARE(s.controller.preparedWriteInvalidReason(),
             std::optional{modbuslens::core::PreparedWriteInvalidReason::Disconnected});
    const auto confirmed = s.controller.confirmPreparedWrite(oldToken);
    QVERIFY(isConfirmRejected(confirmed));
    QVERIFY(!s.controller.preparedWriteSnapshot().has_value());
}

void ActiveMasterTest::i03_busyBecameTrueInvalidates()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));

    // A normal FC03 read entering flight makes serialBusy true.
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    QVERIFY(s.controller.serialBusy());

    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Invalidated);
    QCOMPARE(s.controller.preparedWriteInvalidReason(),
             std::optional{modbuslens::core::PreparedWriteInvalidReason::BusyBecameTrue});
}

void ActiveMasterTest::i04_busyFalseDoesNotRevive()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));
    const auto token = *s.controller.preparedWriteToken();

    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();
    QVERIFY(!s.controller.serialBusy());

    // Busy is false again, but the invalidated snapshot stays dead.
    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Invalidated);
    const auto confirmed = s.controller.confirmPreparedWrite(token);
    QVERIFY(isConfirmRejected(confirmed));
    QVERIFY(!s.controller.preparedWriteSnapshot().has_value());
}

void ActiveMasterTest::i05_simulatorReplacementInvalidates()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));

    s.controller.runDemoBatch();

    QCOMPARE(s.controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::Simulator);
    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Invalidated);
    QCOMPARE(s.controller.preparedWriteInvalidReason(),
             std::optional{modbuslens::core::PreparedWriteInvalidReason::SourceChanged});
}

void ActiveMasterTest::i06_replayReplacementInvalidates()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));

    s.controller.loadReplayFile(
        QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));

    QVERIFY(!s.controller.hasReplayError());
    QCOMPARE(s.controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::Replay);
    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Invalidated);
    QCOMPARE(s.controller.preparedWriteInvalidReason(),
             std::optional{modbuslens::core::PreparedWriteInvalidReason::SourceChanged});
}

void ActiveMasterTest::i07_failedReplayKeepsPrepared()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));
    const auto token = *s.controller.preparedWriteToken();

    const QString missing = QDir::tempPath()
        + QStringLiteral("/modbuslens_m10c1_missing.mlog");
    QVERIFY(!QFile::exists(missing));
    s.controller.loadReplayFile(QUrl::fromLocalFile(missing));

    // Rule A: a FAILED replacement changes no authoritative source fact, so the
    // prepared snapshot survives and can still be confirmed.
    QVERIFY(s.controller.hasReplayError());
    QCOMPARE(s.controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::ActiveSerial);
    QVERIFY(s.controller.serialConnected());
    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Prepared);
    const auto confirmed = s.controller.confirmPreparedWrite(token);
    QVERIFY(isConfirmAccepted(confirmed));
}

void ActiveMasterTest::i08_unrelatedActivityKeepsPrepared()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));

    // Activities that are neither source nor session nor busy changes must not
    // touch the snapshot (navigation itself is presentation-only and already
    // guarded by the QML harness; this proves the runtime side has no
    // hidden invalidation path).
    s.controller.refreshSerialPorts();
    s.controller.runBaselineDiagnosis();
    s.controller.clearDiagnosis();
    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Prepared);
    QVERIFY(s.controller.preparedWriteSnapshot().has_value());
}

void ActiveMasterTest::i09_clearResultsKeepsPrepared()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));
    QCOMPARE(s.controller.transactionModel()->rowCount(), 0);

    s.controller.clearResults();

    // Clear Results is a result-domain operation: it never consumes,
    // invalidates or sends a prepared write.
    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Prepared);
    QCOMPARE(s.controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::ActiveSerial);
    QVERIFY(s.controller.serialConnected());
    QVERIFY(!s.controller.serialBusy());
}

void ActiveMasterTest::i10_serialErrorTextKeepsPrepared()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));

    // A rejected read request only raises the serial error lane: no state that
    // the safety authority depends on changed, so the snapshot lives on.
    s.controller.readHoldingRegistersOnce(1, 0, 999, 1000);
    QVERIFY(s.controller.hasSerialError());
    QVERIFY(!s.controller.serialBusy());
    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Prepared);
}

void ActiveMasterTest::z01_zeroTransportOnPrepare()
{
    ConnectedSession s;
    const int attemptsBefore = s.transport.startAttemptCount();
    const int sendsBefore = s.transport.sendCount();

    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));

    QCOMPARE(s.transport.startAttemptCount(), attemptsBefore);
    QCOMPARE(s.transport.sendCount(), sendsBefore);
}

void ActiveMasterTest::z02_zeroTransportOnConfirm()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteMultipleRegisters(11, 0, "1\n2\n3", 1000)));
    const auto token = *s.controller.preparedWriteToken();
    const int attemptsBefore = s.transport.startAttemptCount();
    const int sendsBefore = s.transport.sendCount();

    const auto confirmed = s.controller.confirmPreparedWrite(token);

    QVERIFY(isConfirmAccepted(confirmed));
    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Consumed);
    QCOMPARE(s.transport.startAttemptCount(), attemptsBefore);
    QCOMPARE(s.transport.sendCount(), sendsBefore);
    QVERIFY(!s.transport.hasPendingTransaction());
}

void ActiveMasterTest::z03_zeroTransportOnCancel()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));
    const auto token = *s.controller.preparedWriteToken();
    const int attemptsBefore = s.transport.startAttemptCount();
    const int sendsBefore = s.transport.sendCount();

    QVERIFY(s.controller.cancelPreparedWrite(token));

    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Invalidated);
    QCOMPARE(s.controller.preparedWriteInvalidReason(),
             std::optional{modbuslens::core::PreparedWriteInvalidReason::UserCancelled});
    QCOMPARE(s.transport.startAttemptCount(), attemptsBefore);
    QCOMPARE(s.transport.sendCount(), sendsBefore);
    // A cancel is not a serial error either.
    QVERIFY(!s.controller.hasSerialError());
}

void ActiveMasterTest::z04_zeroTransportOnInvalidation()
{
    ConnectedSession s;
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));
    const int attemptsBefore = s.transport.startAttemptCount();
    const int sendsBefore = s.transport.sendCount();

    s.controller.disconnectSerial();

    QCOMPARE(s.controller.preparedWriteState(),
             modbuslens::core::PreparedWriteState::Invalidated);
    QCOMPARE(s.transport.startAttemptCount(), attemptsBefore);
    QCOMPARE(s.transport.sendCount(), sendsBefore);
}

void ActiveMasterTest::z05_zeroTransactionSideEffects()
{
    ConnectedSession s;
    s.controller.readHoldingRegistersOnce(1, 0, 2, 1000);
    s.transport.setResponseBytes(kGoodResponse9);
    s.transport.completeWithResponse();

    const int recordsBefore = s.controller.activeSerialRecordCount();
    const int terminalsBefore = s.controller.activeSerialTerminalCount();
    const int rowsBefore = s.controller.transactionModel()->rowCount();
    const int observedBefore = s.controller.observedCount();

    // prepare -> cancel -> prepare -> confirm -> invalidate
    QVERIFY(isPrepared(s.controller.prepareWriteSingleRegister(11, 0x0064, 0x0064, 1000)));
    const auto first = *s.controller.preparedWriteToken();
    QVERIFY(s.controller.cancelPreparedWrite(first));
    QVERIFY(isPrepared(s.controller.prepareWriteMultipleRegisters(11, 0x0010, "10\n20", 1000)));
    const auto second = *s.controller.preparedWriteToken();
    QVERIFY(second != first);
    QVERIFY(isConfirmAccepted(s.controller.confirmPreparedWrite(second)));

    // prepare / cancel / confirm added NO record, terminal, row or statistic.
    QCOMPARE(s.controller.activeSerialRecordCount(), recordsBefore);
    QCOMPARE(s.controller.activeSerialTerminalCount(), terminalsBefore);
    QCOMPARE(s.controller.transactionModel()->rowCount(), rowsBefore);
    QCOMPARE(s.controller.observedCount(), observedBefore);

    // ... and an invalidation path does not touch them either.
    s.controller.disconnectSerial();
    QCOMPARE(s.controller.activeSerialRecordCount(), recordsBefore);
    QCOMPARE(s.controller.activeSerialTerminalCount(), terminalsBefore);
    QCOMPARE(s.controller.transactionModel()->rowCount(), rowsBefore);
    QCOMPARE(s.controller.observedCount(), observedBefore);
}

QTEST_GUILESS_MAIN(ActiveMasterTest)
#include "test_active_master.moc"