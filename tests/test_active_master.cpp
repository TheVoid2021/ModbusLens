#include <QtTest>

#include <QUrl>
#include <QVariant>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
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

    // Presentation stays the FROZEN FC03 contract in M10-A: latest-only row
    // and single-transaction statistics (append presentation = M10-B).
    QCOMPARE(s.controller.transactionModel()->rowCount(), 1);
    QCOMPARE(s.controller.observedCount(), 1);
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
        controller.appendSerialTransaction(
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
                    .issue = std::nullopt},
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
                .issue = std::nullopt},
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

QTEST_GUILESS_MAIN(ActiveMasterTest)
#include "test_active_master.moc"