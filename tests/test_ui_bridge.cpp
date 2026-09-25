#include <QtTest>

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryFile>
#include <QUrl>
#include <QVariant>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/ActiveTransactionEvidence.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/active/ActiveRequestIntent.h"
#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "fake_chat_completions_server.h"
#include "fake_serial_transport.h"
#include "ui/AnalysisController.h"
#include "core/active/ActiveRequestIntent.h"
#include "ui/TransactionListModel.h"

#include <QSet>

#include <algorithm>
#include <span>
#include <string>
#include <utility>

namespace {

// UI-A04/05 fixture rows.
TransactionListEntry successEntry()
{
    return TransactionListEntry{
        .deviceAddress = 1,
        .functionCode = 0x03,
        .status = modbuslens::core::TransactionStatus::Success,
        .elapsedMs = 25,
        .exceptionCode = std::nullopt,
        .issueText = QStringLiteral(""),
        .activeSerialProvenance = std::nullopt,
    };
}

TransactionListEntry exceptionEntry()
{
    return TransactionListEntry{
        .deviceAddress = 1,
        .functionCode = 0x03,
        .status = modbuslens::core::TransactionStatus::Exception,
        .elapsedMs = 18,
        .exceptionCode = std::uint8_t{0x02},
        .issueText = QStringLiteral(""),
        .activeSerialProvenance = std::nullopt,
    };
}

class UiBridgeTest : public QObject
{
    Q_OBJECT

private slots:
    // UI-A01 (P0): controller initial counts are all zero.
    void a01_controllerInitialCounts();
    // UI-A02 (P0): optional fields start undefined (never a fake 0%).
    void a02_optionalInitialState();
    // UI-A03 (P0): transaction model starts empty.
    void a03_emptyTransactionModel();
    // UI-A04 (P0): single success entry exposes all roles.
    void a04_modelRolesForSuccessEntry();
    // UI-A05 (P0): exception entry exposes has/code and label.
    void a05_exceptionEntry();
    // UI-A06 (P1): setEntries replaces the whole batch.
    void a06_replaceModel();

    // ---- Part B: deterministic demo ----
    // UI-B01 (P0): runDemoBatch statistics.
    void b01_runDemoStatistics();
    // UI-B02 (P0): transaction model rows.
    void b02_demoTransactionRows();
    // UI-B03 (P0): exception detail on row 1.
    void b03_exceptionDetail();
    // UI-B04 (P0): deterministic re-run.
    void b04_deterministicReRun();
    // UI-B05 (P0): clear.
    void b05_clear();
    // UI-B06 (P1): clear then re-run.
    void b06_clearThenReRun();

    // ---- T009 Part B: replay UI integration ----
    // UI-R01 (P0): golden replay load publishes the full batch.
    void r01_goldenReplay();
    // UI-R02 (P0): golden replay rows carry status/elapsed/exception.
    void r02_goldenReplayRows();
    // UI-R03 (P0): parse failure keeps the old batch + source intact.
    void r03_parseErrorPreservesState();
    // UI-R04 (P0): execution failure (quantity=0) maps to a human message.
    void r04_executionError();
    // UI-R05 (P1): missing file reports a file error, state preserved.
    void r05_fileOpenFailure();
    // UI-R06 (P0): valid load after failure clears the error.
    void r06_errorRecovery();
    // UI-R07 (P0): replay/demo/replay all replace, never append.
    void r07_sourceReplace();
    // UI-R08 (P1): clearResults empties results but keeps the source.
    void r08_clearKeepsSource();

    // ---- T010 Part B: serial UI integration ----
    // UI-S01 (P1): initial serial state + safe discovery.
    void s01_initialSerialState();
    // UI-S02 (P0): failed connect preserves the whole old batch/source.
    void s02_failedConnectAtomicPreservation();
    // UI-S03 (P0): read while disconnected errors without a Timeout.
    void s03_readWhileDisconnected();
    // UI-S04 (P1): out-of-range inputs are rejected before narrowing.
    void s04_inputValidation();
    // UI-S05 (P0): serial Success maps to the shared dashboard.
    void s05_publishSerialSuccess();
    // UI-S06 (P0): serial Timeout maps with a VALID 0% rate and no avg.
    void s06_publishSerialTimeout();
    // UI-S07 (P0): serial results replace, never append (always 1 row).
    void s07_serialReplace();
    // UI-S08 (P1): clearResults empties serial results but keeps source.
    void s08_clearSerialResults();
    // UI-S09 (P1): a successful source switch clears a stale serial error.
    void s09_serialErrorRecovery();
    // UI-S10 (P1): stale completion without pending metadata is ignored.
    void s10_staleCompletionGuard();
    // UI-S11 (M10-E4): an OPEN local port is not a claim about a remote slave;
    // a silent slave must stay a Timeout.
    void s11_openPortIsNotDeviceOnline();
    // UI-S12 (M10-E4, hot-unplug): a removed USB adapter takes the LOCAL
    // connection state down with it, and fabricates no Modbus outcome.
    void s12_adapterRemovalClearsLocalConnectionState();
    // UI-S13 (M10-E4): a removed adapter invalidates a prepared confirmation,
    // and the old token never revives after reconnect.
    void s13_adapterRemovalInvalidatesPreparedWrite();

    // ---- M10-B: source boundaries around the Active Serial history ----
    // B10: switching to Simulator replaces the visible set (no Active rows).
    void b10_simulatorReplacementHasNoActiveRows();
    // B11: a successful Replay load replaces it as well.
    void b11_replayReplacementHasNoActiveRows();
    // B12: a FAILED Replay load preserves the Active Serial source (M9 rule A).
    void b12_failedReplayPreservesActiveSerialSource();

    // ---- T011 Part A: deterministic baseline diagnosis ----
    // UI-D01 (P0): demo batch -> baseline contains the three failure facts.
    void d01_demoBaseline();
    // UI-D02 (P0): clearResults invalidates the diagnosis.
    void d02_clearResultsInvalidates();
    // UI-D03 (P0): a new successful batch invalidates the diagnosis.
    void d03_newBatchInvalidates();
    // UI-D04 (P1): a failed source switch keeps the diagnosis.
    void d04_failedSwitchKeeps();
    // UI-D05 (P1): diagnosing an empty batch is a VALID NoData report.
    void d05_emptyDiagnosis();
    // UI-D06 (P0): Simulator golden and Replay golden yield identical text.
    void d06_sameFactsSameBaseline();
    // UI-D07 (P1): serial Timeout maps to a lone Timeout finding.
    void d07_serialSingleResult();
    // UI-D08 (P1): clearDiagnosis clears only the diagnosis, not the batch.
    void d08_clearDiagnosisOnly();

    // ---- T011 Part B: LLM explanation (ModelScope, fake localhost) ----
    void ai01_notConfigured();
    void ai02_baselineRequired();
    void ai03_emptyBatch();
    void ai04_success();
    void ai05_providerFailurePreservesBaseline();
    void ai06_newBatchInvalidatesAi();
    void ai07_failedSwitchKeepsAi();
    void ai08_crossBatchStaleGuard();
    void ai09_clearDiagnosis();
    void ai10_aiNeverChangesFacts();
    void ai11_sameBatchCancelRestartGuard();

    // ---- T014: issue presentation through the model/controller ----
    // UI-T01 (P0): ProtocolError rows expose deterministic issueText;
    // ordinary rows stay empty (additive presentation, no new columns).
    void t01_protocolErrorIssueText();
    // UI-T02 (P0, T015): broadcast rows present 预期无响应 + the dashboard
    // counter + the non-fatal unsupported-records notice.
    void t02_t015Presentation();
    // UI-T03 (P0, T015): invalid-request + legal Exception row shows the
    // request-side deterministic secondary text.
    void t03_requestIssueSecondaryText();
    // UI-T04 (P0, T015 Part C audit): a Success row carrying request issues
    // still displays 成功 (status is never rewritten) with the FULL
    // secondary request-issue text.
    void t04_successRowKeepsBothDimensions();
    // UI-T05 (P0, presentation gap): the formatted baseline MUST contain the
    // RequestIssueObserved line (transaction-count based, deterministic
    // position after ExpectedNoResponse), never just its side-actions.
    void t05_baselineRequestIssueFindingVisible();
    // ---- M10-D1 staging: product capability is still ABSENT ----
    void d3_productWriteCapabilityExposedForFc06Only();
    void d1_typedPrepareApiStillAuthoritative();
    void pv1_readPreviewMatchesEncoder();
    void pv2_write06PreviewDecHex();
    void pv3_write10PreviewQuantityByteCountAndTable();
    void pv4_previewRejectsInvalidDrafts();
    // ---- T023 / M10 correction: FC03 read-result observability ----
    // READ-R1..R8 (T023 §12). READ-R9 (geometry) is the QML gate
    // (--qml-read-result-check) because it needs a real window.
    void rr01_classificationMatrixCompleteness();
    void rr02_txIdentityEqualsPreviewAndRecord();
    void rr03_rxByteFidelity();
    void rr04_valuesComeFromTheSameRxBytes();
    void rr05_noValuesOutsideSuccess();
    void rr06_dispositionWording();
    void rr07_noBytesIsStatedHonestly();
    void rr08_noWireEvidenceSourceIsNeverFaked();
    // ---- M11 first slice: single-register decode view (READ-D1..D4) ----
    void d1_decodeDefaultsAreUInt16Normal();
    void d2_decodedColumnFollowsConfiguration();
    void d3_decodeNeverMutatesRawColumns();
    void d4_fc04AndCustomSourcesAreDecodeEligible();
    // ---- M11 second slice: 32-bit decode + word order (READ-D5..D11) ----
    void d5_decodeTypeBoundsExtendTo32Bit();
    void d6_wordOrderDefaultsAndBounds();
    void d7_wordOrderEnabledGate();
    void d8_uint32SlidingWindowProjection();
    void d9_float32Projection();
    void d10_int32Projection();
    void d11_wordOrderFlipChangesDerivedNotRaw();

    // ---- M10 correction: the read function code is editable ----
    // READ-FC1..FC8. FC9 (UI -> preview -> actual TX identity) lives in the
    // --qml-read-result-check QML gate, which owns the real field widgets.
    void fc1_readFc03CompatibilityIsUntouched();
    void fc2_readFc04UsesTheSelectedFunctionByte();
    void fc3_readCustomFc41UsesTheSelectedFunctionByte();
    void fc4_dynamicExceptionFunctionForm();
    void fc5_wrongResponseFunctionIsDynamic();
    void fc6_invalidFunctionTextIsRejectedLocally();
    void fc7_typedInputParsing();
    void fc8_baudOptionsIncludeLowSpeedRates();
};

void UiBridgeTest::a01_controllerInitialCounts()
{
    AnalysisController controller;
    QCOMPARE(controller.observedCount(), 0);
    QCOMPARE(controller.pendingCount(), 0);
    QCOMPARE(controller.completedCount(), 0);
    QCOMPARE(controller.successCount(), 0);
    QCOMPARE(controller.exceptionCount(), 0);
    QCOMPARE(controller.crcErrorCount(), 0);
    QCOMPARE(controller.timeoutCount(), 0);
    QCOMPARE(controller.protocolErrorCount(), 0);
}

void UiBridgeTest::a02_optionalInitialState()
{
    AnalysisController controller;
    QVERIFY(!controller.hasSuccessRate());
    QVERIFY(!controller.hasAverageSuccessLatency());
    // The getters may return a 0.0 placeholder, but that must never flip the
    // hasX flags — business meaning lives in hasX only.
    QCOMPARE(controller.successRate(), 0.0);
    QCOMPARE(controller.averageSuccessLatencyMs(), 0.0);
    QVERIFY(!controller.hasSuccessRate());
    QVERIFY(!controller.hasAverageSuccessLatency());
}

void UiBridgeTest::a03_emptyTransactionModel()
{
    AnalysisController controller;
    QVERIFY(controller.transactionModel() != nullptr);
    QCOMPARE(controller.transactionModel()->rowCount(), 0);

    TransactionListModel model;
    QCOMPARE(model.rowCount(), 0);
}

void UiBridgeTest::a04_modelRolesForSuccessEntry()
{
    TransactionListModel model;
    model.setEntries({successEntry()});

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, TransactionListModel::DeviceAddressRole), QVariant{1});
    QCOMPARE(model.data(index, TransactionListModel::FunctionCodeRole), QVariant{3});
    QCOMPARE(model.data(index, TransactionListModel::StatusTextRole), QStringLiteral("成功"));
    QCOMPARE(model.data(index, TransactionListModel::ElapsedMsRole), QVariant{qint64{25}});
    QCOMPARE(model.data(index, TransactionListModel::HasExceptionCodeRole), QVariant{false});
    // Safe placeholder: 0, but business logic must use hasExceptionCode.
    QCOMPARE(model.data(index, TransactionListModel::ExceptionCodeRole), QVariant{0});
}

void UiBridgeTest::a05_exceptionEntry()
{
    TransactionListModel model;
    model.setEntries({exceptionEntry()});

    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, TransactionListModel::StatusTextRole), QStringLiteral("异常"));
    QCOMPARE(model.data(index, TransactionListModel::HasExceptionCodeRole), QVariant{true});
    QCOMPARE(model.data(index, TransactionListModel::ExceptionCodeRole), QVariant{2});
    QCOMPARE(model.data(index, TransactionListModel::ElapsedMsRole), QVariant{qint64{18}});
}

void UiBridgeTest::a06_replaceModel()
{
    TransactionListModel model;
    model.setEntries({successEntry(), exceptionEntry()});
    QCOMPARE(model.rowCount(), 2);

    const TransactionListEntry secondBatchRow{
        .deviceAddress = 5,
        .functionCode = 0x03,
        .status = modbuslens::core::TransactionStatus::Success,
        .elapsedMs = 12,
        .exceptionCode = std::nullopt,
        .issueText = QStringLiteral(""),
        .activeSerialProvenance = std::nullopt,
    };
    model.setEntries({secondBatchRow});

    QCOMPARE(model.rowCount(), 1);
    const QModelIndex index = model.index(0, 0);
    QCOMPARE(model.data(index, TransactionListModel::DeviceAddressRole), QVariant{5});
    QCOMPARE(model.data(index, TransactionListModel::ElapsedMsRole), QVariant{qint64{12}});
}

// ---- Part B test implementations ----

void UiBridgeTest::b01_runDemoStatistics()
{
    AnalysisController controller;
    controller.runDemoBatch();
    QCOMPARE(controller.observedCount(), 4);
    QCOMPARE(controller.pendingCount(), 0);
    QCOMPARE(controller.completedCount(), 4);
    QCOMPARE(controller.successCount(), 1);
    QCOMPARE(controller.exceptionCount(), 1);
    QCOMPARE(controller.crcErrorCount(), 1);
    QCOMPARE(controller.timeoutCount(), 1);
    QCOMPARE(controller.protocolErrorCount(), 0);
    QVERIFY(controller.hasSuccessRate());
    QVERIFY(qFuzzyCompare(controller.successRate(), 0.25));
    QVERIFY(controller.hasAverageSuccessLatency());
    QVERIFY(qFuzzyCompare(controller.averageSuccessLatencyMs(), 25.0));
}

void UiBridgeTest::b02_demoTransactionRows()
{
    AnalysisController controller;
    controller.runDemoBatch();
    auto* model = controller.transactionModel();
    QVERIFY(model != nullptr);
    QCOMPARE(model->rowCount(), 4);

    struct Row { QString status; qint64 elapsed; };
    const Row expected[] = {
        {QStringLiteral("成功"), 25},
        {QStringLiteral("异常"), 18},
        {QStringLiteral("CRC 错误"), 17},
        {QStringLiteral("超时"), 1000}
    };
    for (int row = 0; row < 4; ++row) {
        const auto idx = model->index(row, 0);
        QCOMPARE(model->data(idx, TransactionListModel::DeviceAddressRole), QVariant{1});
        QCOMPARE(model->data(idx, TransactionListModel::FunctionCodeRole), QVariant{3});
        QCOMPARE(model->data(idx, TransactionListModel::StatusTextRole).toString(),
                 QString(expected[row].status));
        QCOMPARE(model->data(idx, TransactionListModel::ElapsedMsRole),
                 QVariant{expected[row].elapsed});
    }
}

void UiBridgeTest::b03_exceptionDetail()
{
    AnalysisController controller;
    controller.runDemoBatch();
    auto* model = controller.transactionModel();

    // Row 1 (Exception): has code 0x02.
    const auto idx1 = model->index(1, 0);
    QCOMPARE(model->data(idx1, TransactionListModel::HasExceptionCodeRole), QVariant{true});
    QCOMPARE(model->data(idx1, TransactionListModel::ExceptionCodeRole), QVariant{2});

    // Rows 0/2/3: no exception code.
    for (int row : {0, 2, 3}) {
        const auto idx = model->index(row, 0);
        QCOMPARE(model->data(idx, TransactionListModel::HasExceptionCodeRole), QVariant{false});
    }
}

void UiBridgeTest::b04_deterministicReRun()
{
    AnalysisController controller;
    controller.runDemoBatch();
    const int obs1 = controller.observedCount();
    const int succ1 = controller.successCount();
    const double rate1 = controller.successRate();
    const double avg1 = controller.averageSuccessLatencyMs();
    auto* model1 = controller.transactionModel();
    const int rows1 = model1->rowCount();

    controller.runDemoBatch();
    QCOMPARE(controller.observedCount(), obs1);
    QCOMPARE(controller.successCount(), succ1);
    QVERIFY(qFuzzyCompare(controller.successRate(), rate1));
    QVERIFY(qFuzzyCompare(controller.averageSuccessLatencyMs(), avg1));
    auto* model2 = controller.transactionModel();
    QCOMPARE(model2->rowCount(), rows1);
    // Compare row content
    for (int row = 0; row < rows1; ++row) {
        const auto idx = model2->index(row, 0);
        QCOMPARE(model2->data(idx, TransactionListModel::DeviceAddressRole), QVariant{1});
        QCOMPARE(model2->data(idx, TransactionListModel::FunctionCodeRole), QVariant{3});
    }
}

void UiBridgeTest::b05_clear()
{
    AnalysisController controller;
    controller.runDemoBatch();
    controller.clearResults();
    QCOMPARE(controller.observedCount(), 0);
    QCOMPARE(controller.pendingCount(), 0);
    QCOMPARE(controller.completedCount(), 0);
    QCOMPARE(controller.successCount(), 0);
    QCOMPARE(controller.exceptionCount(), 0);
    QCOMPARE(controller.crcErrorCount(), 0);
    QCOMPARE(controller.timeoutCount(), 0);
    QCOMPARE(controller.protocolErrorCount(), 0);
    QVERIFY(!controller.hasSuccessRate());
    QVERIFY(!controller.hasAverageSuccessLatency());
    QCOMPARE(controller.transactionModel()->rowCount(), 0);
}

void UiBridgeTest::b06_clearThenReRun()
{
    AnalysisController controller;
    controller.runDemoBatch();
    controller.clearResults();
    controller.runDemoBatch();
    QCOMPARE(controller.observedCount(), 4);
    QCOMPARE(controller.completedCount(), 4);
    QCOMPARE(controller.successCount(), 1);
    QCOMPARE(controller.exceptionCount(), 1);
    QCOMPARE(controller.crcErrorCount(), 1);
    QCOMPARE(controller.timeoutCount(), 1);
    QVERIFY(controller.hasSuccessRate());
    QVERIFY(qFuzzyCompare(controller.successRate(), 0.25));
    QVERIFY(controller.hasAverageSuccessLatency());
    QVERIFY(qFuzzyCompare(controller.averageSuccessLatencyMs(), 25.0));
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
}

// ---- T009 Part B test implementations ----

namespace {

// Writes `content` to a temporary .mlog-named file and returns its path
// (empty on failure — callers QVERIFY it). The QTemporaryFile must outlive
// loadReplayFile's synchronous read, so the helper keeps the object alive
// via the holder out-parameter.
QString writeTempMlog(const QString& content,
                      std::optional<QTemporaryFile>& holder)
{
    holder.emplace(QDir::tempPath() + QStringLiteral("/modbuslens_ui_XXXXXX.mlog"));
    if (!holder->open()) {
        return {};
    }
    if (holder->write(content.toUtf8()) < 0) {
        return {};
    }
    holder->flush();
    return holder->fileName();
}

const QString kBadHexLog = QStringLiteral(
    "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
    "TXN|1|GG|01 03 04 00 64 00 C8 BA 7A\n");

// quantity = 0 with a CORRECT CRC (01 03 00 00 00 00 45 CA): frame and
// function both valid. T015 Gate F: this is now a PER-RECORD request issue
// (InvalidRequestQuantity), not a batch load failure.
const QString kZeroQuantityLog = QStringLiteral(
    "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
    "TXN|1|01 03 00 00 00 00 45 CA|NO_RESPONSE\n");

// T015 helpers: build replay log text from real codec-built wires.
QString wireHex(const std::vector<std::uint8_t>& bytes)
{
    QStringList parts;
    for (const auto byte : bytes) {
        parts << QStringLiteral("%1")
                     .arg(byte, 2, 16, QLatin1Char('0'))
                     .toUpper();
    }
    return parts.join(QLatin1Char(' '));
}

QString mlogOf(const QString& records)
{
    return QStringLiteral("MODBUSLENS_MLOG|1|timeout_ms=1000\n") + records;
}

} // namespace

void UiBridgeTest::r01_goldenReplay()
{
    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));

    QCOMPARE(controller.modeLabel(), QStringLiteral("回放模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("demo_v1.mlog"));
    QVERIFY(!controller.hasReplayError());
    QVERIFY(controller.replayErrorMessage().isEmpty());

    QCOMPARE(controller.observedCount(), 4);
    QCOMPARE(controller.completedCount(), 4);
    QCOMPARE(controller.pendingCount(), 0);
    QCOMPARE(controller.successCount(), 1);
    QCOMPARE(controller.exceptionCount(), 1);
    QCOMPARE(controller.crcErrorCount(), 1);
    QCOMPARE(controller.timeoutCount(), 1);
    QCOMPARE(controller.protocolErrorCount(), 0);
    QVERIFY(controller.hasSuccessRate());
    QVERIFY(qFuzzyCompare(controller.successRate(), 0.25));
    QVERIFY(controller.hasAverageSuccessLatency());
    QVERIFY(qFuzzyCompare(controller.averageSuccessLatencyMs(), 25.0));
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
}

void UiBridgeTest::r02_goldenReplayRows()
{
    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    auto* model = controller.transactionModel();
    QCOMPARE(model->rowCount(), 4);

    struct Row { QString status; qint64 elapsed; bool hasException; int exception; };
    const Row expected[] = {
        {QStringLiteral("成功"), 25, false, 0},
        {QStringLiteral("异常"), 18, true, 2},
        {QStringLiteral("CRC 错误"), 17, false, 0},
        {QStringLiteral("超时"), 1000, false, 0},
    };
    for (int row = 0; row < 4; ++row) {
        const auto idx = model->index(row, 0);
        QCOMPARE(model->data(idx, TransactionListModel::DeviceAddressRole), QVariant{1});
        QCOMPARE(model->data(idx, TransactionListModel::FunctionCodeRole), QVariant{3});
        QCOMPARE(model->data(idx, TransactionListModel::StatusTextRole).toString(),
                 QString(expected[row].status));
        QCOMPARE(model->data(idx, TransactionListModel::ElapsedMsRole),
                 QVariant{expected[row].elapsed});
        QCOMPARE(model->data(idx, TransactionListModel::HasExceptionCodeRole),
                 QVariant{expected[row].hasException});
        QCOMPARE(model->data(idx, TransactionListModel::ExceptionCodeRole),
                 QVariant{expected[row].exception});
    }
}

void UiBridgeTest::r03_parseErrorPreservesState()
{
    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    const auto rowsBefore = controller.transactionModel()->rowCount();
    const auto observedBefore = controller.observedCount();

    std::optional<QTemporaryFile> holder;
    const QString badPath = writeTempMlog(kBadHexLog, holder);
    QVERIFY(!badPath.isEmpty());
    controller.loadReplayFile(QUrl::fromLocalFile(badPath));

    // Error is visible and human-readable (line + phrase).
    QVERIFY(controller.hasReplayError());
    QVERIFY(controller.replayErrorMessage().contains(QStringLiteral("第 2 行")));
    QVERIFY(controller.replayErrorMessage().contains(QStringLiteral("十六进制数据无效")));

    // The WHOLE previous successful state is preserved (rule A).
    QCOMPARE(controller.observedCount(), observedBefore);
    QCOMPARE(controller.transactionModel()->rowCount(), rowsBefore);
    QCOMPARE(controller.modeLabel(), QStringLiteral("回放模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("demo_v1.mlog"));
    QVERIFY(controller.hasSuccessRate());
}

void UiBridgeTest::r04_executionError()
{
    // T015 Gate F (declared in the T015 Phase A archive §21): a semantically
    // invalid but wire-valid request is no longer a batch execution failure —
    // it becomes a per-record analyzed outcome carrying a request issue, so
    // the OLD "请求数据无效 load error" expectation is replaced. Request
    // WIRE corruption keeps the legacy failure contract (core REPLAY-I03 and
    // the file-level error paths still cover it).
    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    QCOMPARE(controller.transactionModel()->rowCount(), 4);

    std::optional<QTemporaryFile> holder;
    const QString badPath = writeTempMlog(kZeroQuantityLog, holder);
    QVERIFY(!badPath.isEmpty());
    controller.loadReplayFile(QUrl::fromLocalFile(badPath));

    // The quantity=0 record now loads and publishes as a Pending observation
    // (no response recorded, elapsed below the threshold).
    QVERIFY(!controller.hasReplayError());
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    const QModelIndex index = controller.transactionModel()->index(0, 0);
    QCOMPARE(controller.transactionModel()
                 ->data(index, TransactionListModel::StatusCodeRole)
                 .toInt(),
             static_cast<int>(modbuslens::core::TransactionStatus::Pending));
    QCOMPARE(controller.modeLabel(), QStringLiteral("回放模式"));
}

void UiBridgeTest::r05_fileOpenFailure()
{
    AnalysisController controller;
    controller.runDemoBatch();

    const QString missing =
        QDir::tempPath() + QStringLiteral("/modbuslens_definitely_missing_") +
        QString::number(reinterpret_cast<quintptr>(&controller)) + QStringLiteral(".mlog");
    QVERIFY(!QFile::exists(missing));
    controller.loadReplayFile(QUrl::fromLocalFile(missing));

    QVERIFY(controller.hasReplayError());
    QVERIFY(controller.replayErrorMessage().contains(QStringLiteral("回放加载失败")));

    // Demo batch + Simulator source preserved (rule A).
    QCOMPARE(controller.observedCount(), 4);
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("模拟器模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("确定性演示"));
}

void UiBridgeTest::r06_errorRecovery()
{
    AnalysisController controller;
    QVERIFY(!controller.hasReplayError());

    std::optional<QTemporaryFile> holder;
    const QString badPath = writeTempMlog(kBadHexLog, holder);
    QVERIFY(!badPath.isEmpty());
    controller.loadReplayFile(QUrl::fromLocalFile(badPath));
    QVERIFY(controller.hasReplayError());

    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    QVERIFY(!controller.hasReplayError());
    QVERIFY(controller.replayErrorMessage().isEmpty());
    QCOMPARE(controller.modeLabel(), QStringLiteral("回放模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("demo_v1.mlog"));
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.observedCount(), 4);
}

void UiBridgeTest::r07_sourceReplace()
{
    AnalysisController controller;

    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    QVERIFY(!controller.hasReplayError());
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("回放模式"));

    controller.runDemoBatch();
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("模拟器模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("确定性演示"));
    QVERIFY(!controller.hasReplayError());

    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("回放模式"));
    QVERIFY(!controller.hasReplayError());
}

void UiBridgeTest::r08_clearKeepsSource()
{
    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));

    // Force an error first; clear must drop it too.
    std::optional<QTemporaryFile> holder;
    const QString badPath = writeTempMlog(kBadHexLog, holder);
    QVERIFY(!badPath.isEmpty());
    controller.loadReplayFile(QUrl::fromLocalFile(badPath));
    QVERIFY(controller.hasReplayError());

    controller.clearResults();

    QCOMPARE(controller.observedCount(), 0);
    QCOMPARE(controller.completedCount(), 0);
    QVERIFY(!controller.hasSuccessRate());
    QVERIFY(!controller.hasAverageSuccessLatency());
    QCOMPARE(controller.transactionModel()->rowCount(), 0);
    QVERIFY(!controller.hasReplayError());
    QVERIFY(controller.replayErrorMessage().isEmpty());

    // Source identity survives the clear (only runDemoBatch switches it).
    QCOMPARE(controller.modeLabel(), QStringLiteral("回放模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("demo_v1.mlog"));
}


// ---- T010 Part B test implementations ----

namespace {

using ms = std::chrono::milliseconds;

modbuslens::core::TransactionAnalysis makeAnalysis(
    modbuslens::core::TransactionStatus status, long long elapsedMs,
    std::optional<std::uint8_t> exceptionCode = std::nullopt)
{
    return modbuslens::core::TransactionAnalysis{
        .status = status,
        .elapsed = ms{elapsedMs},
        .exceptionCode = exceptionCode,
        .issue = std::nullopt,
        .values = {},
    };
}

// M10-B: the serial bridge tests drive the REAL Active Serial path through
// the deterministic recording transport (no COM port, no sleeps, no wall
// clock). The M10-A synthetic "publish one analysis" seam is gone: mapping
// correctness is now proven on the same path production uses.
class ActiveSerialFixture
{
public:
    ActiveSerialFixture()
    {
        transport.setPortOpen(true);
        controller.setSerialTransport(&transport);
        controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    }

    // One accepted FC03 request (fails the test if the transport refused).
    void request(std::uint8_t unit = 1, std::uint16_t quantity = 2)
    {
        controller.readHoldingRegistersOnce(unit, 0, quantity, 1000);
    }

    void completeWith(std::vector<std::uint8_t> responseBytes,
                      long long elapsedMs = 25)
    {
        transport.setResponseBytes(std::move(responseBytes));
        transport.setCompletionElapsed(std::chrono::milliseconds{elapsedMs});
        transport.completeWithResponse();
    }

    void completeWithTimeout(long long elapsedMs = 1000)
    {
        transport.setResponseBytes({});
        transport.setCompletionElapsed(std::chrono::milliseconds{elapsedMs});
        transport.completeWithTimeout();
    }

    AnalysisController controller;
    RecordingSerialTransport transport;
};

// FC03 golden response: unit 1, two registers 100 / 200.
std::vector<std::uint8_t> goodFc03Response()
{
    return {0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7A};
}

// T023: a conforming FC03 answer carrying `values` as big-endian pairs, built
// by the SHIPPED encoder (so the CRC is the production CRC).
// M10 correction: the reply builder takes the register-read function byte
// (0x03 default) — one builder for FC03/FC04/custom-code fixtures.
std::vector<std::uint8_t> fc03Response(
    int unit, const std::vector<std::uint16_t>& values,
    std::uint8_t readFunctionCode = 0x03)
{
    std::vector<std::uint8_t> data;
    data.push_back(static_cast<std::uint8_t>(values.size() * 2));
    for (const std::uint16_t value : values) {
        data.push_back(static_cast<std::uint8_t>(value >> 8));
        data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }
    return modbuslens::core::encodeRtuFrame(modbuslens::core::ModbusRtuFrame{
        .address = static_cast<std::uint8_t>(unit),
        .functionCode = readFunctionCode,
        .data = std::move(data)});
}

// T023 READ-R4: parse the DISPLAYED hex run back into bytes — the invariant is
// about the bytes the user can actually see, not about the injected vector.
std::vector<std::uint8_t> bytesFromHexText(const QString& hex)
{
    std::vector<std::uint8_t> bytes;
    const QStringList parts =
        hex.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        bool ok = false;
        const uint value = part.toUInt(&ok, 16);
        if (!ok) {
            return {};
        }
        bytes.push_back(static_cast<std::uint8_t>(value));
    }
    return bytes;
}

modbuslens::core::TransactionIssue issueWith(
    modbuslens::core::TransactionIssueCode code)
{
    modbuslens::core::TransactionIssue issue;
    issue.code = code;
    return issue;
}

} // namespace

void UiBridgeTest::s01_initialSerialState()
{
    AnalysisController controller;
    QVERIFY(!controller.serialConnected());
    QVERIFY(!controller.serialBusy());
    QVERIFY(!controller.hasSerialError());
    QVERIFY(controller.serialErrorMessage().isEmpty());

    controller.refreshSerialPorts(); // must never crash, never open anything
    const QStringList names = controller.serialPortNames();
    for (const QString& name : names) {
        QVERIFY(!name.isEmpty());
    }
    QVERIFY(!controller.hasSerialError()); // empty list is NOT an error
}

void UiBridgeTest::s02_failedConnectAtomicPreservation()
{
    AnalysisController controller;
    controller.runDemoBatch();
    const auto rowsBefore = controller.transactionModel()->rowCount();
    const auto observedBefore = controller.observedCount();
    QVERIFY(rowsBefore > 0);

    controller.connectSerial(
        QStringLiteral("MODBUSLENS_TEST_NONEXISTENT_PORT"), 9600);

    QVERIFY(controller.hasSerialError());
    QVERIFY(!controller.serialErrorMessage().isEmpty());
    QVERIFY(!controller.serialConnected());
    // The ENTIRE old source state survives the failed connect.
    QCOMPARE(controller.observedCount(), observedBefore);
    QCOMPARE(controller.transactionModel()->rowCount(), rowsBefore);
    QCOMPARE(controller.modeLabel(), QStringLiteral("模拟器模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("确定性演示"));
}

void UiBridgeTest::s03_readWhileDisconnected()
{
    AnalysisController controller;
    controller.readHoldingRegistersOnce(1, 0, 2, 1000);

    QVERIFY(controller.hasSerialError());
    QCOMPARE(controller.observedCount(), 0);
    QCOMPARE(controller.transactionModel()->rowCount(), 0);
    QVERIFY(!controller.serialBusy()); // no Timeout, no pending, no rows
    // Source untouched.
    QCOMPARE(controller.modeLabel(), QStringLiteral("模拟器模式"));
}

void UiBridgeTest::s04_inputValidation()
{
    AnalysisController controller;
    // Each of these must be rejected BEFORE any narrowing cast.
    controller.readHoldingRegistersOnce(0, 0, 2, 1000);       // slave 0
    QVERIFY(controller.hasSerialError());
    controller.readHoldingRegistersOnce(1, 65536, 2, 1000);   // start 65536
    QVERIFY(controller.hasSerialError());
    controller.readHoldingRegistersOnce(1, 0, 126, 1000);     // quantity 126
    QVERIFY(controller.hasSerialError());
    controller.readHoldingRegistersOnce(1, 0, 2, 0);          // timeout 0
    QVERIFY(controller.hasSerialError());
    // Unsupported baud on the connect path is rejected the same way.
    controller.connectSerial(QStringLiteral("COM1"), 12345);
    QVERIFY(controller.hasSerialError());

    // Nothing was ever written, connected or transacted.
    QVERIFY(!controller.serialConnected());
    QVERIFY(!controller.serialBusy());
    QCOMPARE(controller.transactionModel()->rowCount(), 0);
    QCOMPARE(controller.observedCount(), 0);
    QCOMPARE(controller.modeLabel(), QStringLiteral("模拟器模式"));
}

void UiBridgeTest::s05_publishSerialSuccess()
{
    // M10-B: driven through the production Active Serial path (the old
    // synthetic publish seam was removed with the latest-only contract).
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWith(goodFc03Response(), 25);

    QCOMPARE(controller.modeLabel(), QStringLiteral("串口模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("COM_TEST @ 9600"));

    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    const auto idx = controller.transactionModel()->index(0, 0);
    QCOMPARE(controller.transactionModel()
                 ->data(idx, TransactionListModel::StatusTextRole)
                 .toString(),
             QStringLiteral("成功"));
    QCOMPARE(controller.transactionModel()
                 ->data(idx, TransactionListModel::ElapsedMsRole),
             QVariant{qint64{25}});
    QCOMPARE(controller.transactionModel()
                 ->data(idx, TransactionListModel::DeviceAddressRole),
             QVariant{1});
    QCOMPARE(controller.transactionModel()
                 ->data(idx, TransactionListModel::FunctionCodeRole),
             QVariant{3});

    QCOMPARE(controller.observedCount(), 1);
    QCOMPARE(controller.completedCount(), 1);
    QCOMPARE(controller.pendingCount(), 0);
    QCOMPARE(controller.successCount(), 1);
    QVERIFY(controller.hasSuccessRate());
    QVERIFY(qFuzzyCompare(controller.successRate(), 1.0));
    QVERIFY(controller.hasAverageSuccessLatency());
    QVERIFY(qFuzzyCompare(controller.averageSuccessLatencyMs(), 25.0));
    QVERIFY(!controller.hasSerialError());
}

void UiBridgeTest::s06_publishSerialTimeout()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWithTimeout(1000);

    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    const auto idx = controller.transactionModel()->index(0, 0);
    QCOMPARE(controller.transactionModel()
                 ->data(idx, TransactionListModel::StatusTextRole)
                 .toString(),
             QStringLiteral("超时"));

    QCOMPARE(controller.observedCount(), 1);
    QCOMPARE(controller.completedCount(), 1);
    QCOMPARE(controller.timeoutCount(), 1);
    QCOMPARE(controller.successCount(), 0);
    // 0% is a VALID rate (has=true), only the average is absent.
    QVERIFY(controller.hasSuccessRate());
    QVERIFY(qFuzzyCompare(controller.successRate(), 0.0));
    QVERIFY(!controller.hasAverageSuccessLatency());
}

void UiBridgeTest::s07_serialReplace()
{
    // M10-B CONTRACT MIGRATION. This test used to lock "Replace, never
    // append: still exactly one row, latest-only statistics" — that WAS the
    // frozen FC03 presentation contract and it is exactly what M10-B was
    // reviewed to change. The Active Serial session now reports its WHOLE
    // session: two completed transactions are two rows and two counted
    // outcomes, in completion order. Source replacement (Simulator/Replay,
    // Clear Results) still replaces wholesale — see s08 and r07.
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWith(goodFc03Response(), 25);
    QCOMPARE(controller.transactionModel()->rowCount(), 1);

    f.request(1);
    f.completeWithTimeout(1000);

    // Append, never replace: both transactions stay visible and counted.
    QCOMPARE(controller.transactionModel()->rowCount(), 2);
    QCOMPARE(controller.observedCount(), 2);
    QCOMPARE(controller.successCount(), 1);
    QCOMPARE(controller.timeoutCount(), 1);
    QCOMPARE(controller.activeSerialRecordCount(), 2);
}

void UiBridgeTest::s08_clearSerialResults()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWith(goodFc03Response(), 25);
    QCOMPARE(controller.transactionModel()->rowCount(), 1);

    controller.clearResults();

    QCOMPARE(controller.observedCount(), 0);
    QCOMPARE(controller.transactionModel()->rowCount(), 0);
    QVERIFY(!controller.hasSuccessRate());
    QVERIFY(!controller.hasAverageSuccessLatency());
    QVERIFY(!controller.hasSerialError());
    // Source identity survives a Clear (Clear != Disconnect).
    QCOMPARE(controller.modeLabel(), QStringLiteral("串口模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("COM_TEST @ 9600"));
    QVERIFY(controller.serialConnected());
    QCOMPARE(controller.activeSerialRecordCount(), 0);
}

void UiBridgeTest::s09_serialErrorRecovery()
{
    AnalysisController controller;
    controller.connectSerial(
        QStringLiteral("MODBUSLENS_TEST_NONEXISTENT_PORT"), 9600);
    QVERIFY(controller.hasSerialError());

    controller.runDemoBatch();

    // A successful source switch must not leave the stale error behind.
    QVERIFY(!controller.hasSerialError());
    QVERIFY(controller.serialErrorMessage().isEmpty());
    QCOMPARE(controller.modeLabel(), QStringLiteral("模拟器模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("确定性演示"));
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
}

void UiBridgeTest::s10_staleCompletionGuard()
{
    AnalysisController controller;
    controller.runDemoBatch();
    const auto rowsBefore = controller.transactionModel()->rowCount();
    const auto observedBefore = controller.observedCount();

    // A late serial completion with NO pending snapshot (source has moved
    // on) must be ignored entirely, even when it is a well-formed Active
    // Serial result for a real request.
    const auto descriptor = std::get<modbuslens::core::ActiveRequestDescriptor>(
        modbuslens::core::encodeActiveRequest(
            modbuslens::core::ActiveRequestIntent{
                .function = modbuslens::core::ActiveFunction::ReadHoldingRegisters,
                .unitId = 1,
                .timeout = ms{1000},
                .payload = modbuslens::core::ReadHoldingRegistersIntent{
                    .startAddress = 0, .quantity = 2}}));
    controller.handleSerialTransactionCompleted(
        modbuslens::core::ActiveTransactionResult{
            .request = descriptor,
            .responseAdu = {},
            .disposition = modbuslens::core::TransportDisposition::PossiblySent,
            .analysis = makeAnalysis(
                modbuslens::core::TransactionStatus::Success, 25)});

    QCOMPARE(controller.transactionModel()->rowCount(), rowsBefore);
    QCOMPARE(controller.observedCount(), observedBefore);
    QCOMPARE(controller.modeLabel(), QStringLiteral("模拟器模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("确定性演示"));
}

// ---- M10-E4: LOCAL serial port state semantics ----

void UiBridgeTest::s11_openPortIsNotDeviceOnline()
{
    // An OPEN port with a silent remote: Modbus RTU has no connection
    // handshake, so serialConnected means "the LOCAL port is open" and must
    // stay true while the slave says nothing.
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    QVERIFY(controller.serialConnected());

    f.request(9); // no slave exists for unit 9 in this fixture
    QVERIFY(controller.serialBusy());
    f.completeWithTimeout(1000);

    QCOMPARE(controller.timeoutCount(), 1);
    // Silence is a TRANSACTION fact: no transport error, no port teardown.
    QVERIFY(!controller.hasSerialError());
    QVERIFY(controller.serialErrorMessage().isEmpty());
    QVERIFY(controller.serialConnected());
    QVERIFY(!controller.serialBusy());
    QCOMPARE(controller.modeLabel(), QStringLiteral("串口模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("COM_TEST @ 9600"));
}

void UiBridgeTest::s12_adapterRemovalClearsLocalConnectionState()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWith(goodFc03Response());
    QVERIFY(controller.serialConnected());
    QVERIFY(!controller.hasSerialError());
    const int recordsBefore = controller.activeSerialRecordCount();
    const int terminalsBefore = controller.activeSerialTerminalCount();

    // The USB serial adapter ITSELF is removed while the session is idle: the
    // local port is gone, so the connection state must follow it — and the
    // remote-device rows are NOT reinterpreted (an adapter removal is not a
    // Modbus outcome).
    f.transport.simulateAdapterRemoval(QStringLiteral("串口设备不可用：适配器已移除"));

    QVERIFY(!controller.serialConnected());
    QVERIFY(!controller.serialBusy());
    QVERIFY(controller.hasSerialError());
    QVERIFY(controller.serialErrorMessage().contains(QStringLiteral("适配器已移除")));
    QCOMPARE(controller.activeSerialRecordCount(), recordsBefore);
    QCOMPARE(controller.activeSerialTerminalCount(), terminalsBefore);
    QCOMPARE(controller.observedCount(), 1);

    // A read attempt now hits the existing "not connected" guard and sends
    // nothing (the port is really closed — isPortOpen() is the authority).
    f.request(1);
    QCOMPARE(f.transport.startAttemptCount(), 1);
}

void UiBridgeTest::s13_adapterRemovalInvalidatesPreparedWrite()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    QVERIFY(controller.prepareWrite06(7, 100, 5, 1000));
    QCOMPARE(controller.preparedWriteStateToken(), QStringLiteral("prepared"));
    const qulonglong token = controller.preparedWriteTokenValue();
    QVERIFY(token != 0);

    f.transport.simulateAdapterRemoval(QStringLiteral("串口设备不可用：适配器已移除"));

    // The confirmation was captured for a session that no longer exists.
    QCOMPARE(controller.preparedWriteStateToken(), QStringLiteral("invalidated"));
    QCOMPARE(controller.preparedWriteInvalidReasonToken(),
             QStringLiteral("disconnected"));

    // Reconnecting creates a NEW session: the old token must not revive, and
    // it must not be usable.
    controller.connectSerial(QStringLiteral("COM_TEST_2"), 9600);
    QVERIFY(controller.serialConnected());
    QVERIFY(controller.preparedWriteStateToken() != QStringLiteral("prepared"));
    QVERIFY(!controller.confirmPreparedWriteToken(token));
    QCOMPARE(controller.activeSerialRecordCount(), 0);
}

// ---- T011 Part A test implementations ----

// ---- M10-B: source boundaries ----

void UiBridgeTest::b10_simulatorReplacementHasNoActiveRows()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWith(goodFc03Response(), 25);
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    QCOMPARE(controller.activeSerialRecordCount(), 1);

    controller.runDemoBatch();

    // Simulator is a source REPLACEMENT: its own batch only, and the Active
    // Serial session (records + wires) is gone with the transition.
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.observedCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("模拟器模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("确定性演示"));
    QCOMPARE(controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::Simulator);
    QCOMPARE(controller.activeSerialRecordCount(), 0);
    QCOMPARE(controller.activeSerialTerminalCount(), 0);
}

void UiBridgeTest::b11_replayReplacementHasNoActiveRows()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWith(goodFc03Response(), 25);
    QCOMPARE(controller.transactionModel()->rowCount(), 1);

    controller.loadReplayFile(
        QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));

    QVERIFY(!controller.hasReplayError());
    QCOMPARE(controller.transactionModel()->rowCount(), 4); // replay batch only
    QCOMPARE(controller.observedCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("回放模式"));
    QCOMPARE(controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::Replay);
    QCOMPARE(controller.activeSerialRecordCount(), 0);
    // A successful source switch also leaves the serial transport.
    QVERIFY(!controller.serialConnected());
}

void UiBridgeTest::b12_failedReplayPreservesActiveSerialSource()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWith(goodFc03Response(), 25);
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    QCOMPARE(controller.observedCount(), 1);

    const QString missing =
        QDir::tempPath() + QStringLiteral("/modbuslens_m10b_missing_") +
        QString::number(reinterpret_cast<quintptr>(&controller)) + QStringLiteral(".mlog");
    QVERIFY(!QFile::exists(missing));
    controller.loadReplayFile(QUrl::fromLocalFile(missing));

    // Rule A (M9-B/D frozen): a FAILED replacement changes nothing about the
    // previous authoritative source — the Active Serial session survives,
    // including its connection and its visible history.
    QVERIFY(controller.hasReplayError());
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    QCOMPARE(controller.observedCount(), 1);
    QCOMPARE(controller.activeSerialRecordCount(), 1);
    QCOMPARE(controller.modeLabel(), QStringLiteral("串口模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("COM_TEST @ 9600"));
    QCOMPARE(controller.sourceKind(),
             modbuslens::core::TransactionSourceKind::ActiveSerial);
    QVERIFY(controller.serialConnected());
}

void UiBridgeTest::d01_demoBaseline()
{
    AnalysisController controller;
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();

    QVERIFY(controller.hasBaselineDiagnosis());
    const QString text = controller.baselineDiagnosisText();
    QVERIFY(text.contains(QStringLiteral("CRC 错误")));
    QVERIFY(text.contains(QStringLiteral("无响应超时")));
    QVERIFY(text.contains(QStringLiteral("设备异常 0x02")));
    QVERIFY(!text.contains(QStringLiteral("全部")));
    QVERIFY(!text.contains(QStringLiteral("协议错误")));
}

void UiBridgeTest::d02_clearResultsInvalidates()
{
    AnalysisController controller;
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    QVERIFY(controller.hasBaselineDiagnosis());

    controller.clearResults();

    QVERIFY(!controller.hasBaselineDiagnosis());
    QVERIFY(controller.baselineDiagnosisText().isEmpty());
    QCOMPARE(controller.transactionModel()->rowCount(), 0);
}

void UiBridgeTest::d03_newBatchInvalidates()
{
    AnalysisController controller;
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    QVERIFY(controller.hasBaselineDiagnosis());

    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));

    // New successful batch: the old diagnosis must be gone.
    QVERIFY(!controller.hasBaselineDiagnosis());
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
}

void UiBridgeTest::d04_failedSwitchKeeps()
{
    AnalysisController controller;
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    const QString before = controller.baselineDiagnosisText();

    std::optional<QTemporaryFile> holder;
    const QString badPath = writeTempMlog(kBadHexLog, holder);
    QVERIFY(!badPath.isEmpty());
    controller.loadReplayFile(QUrl::fromLocalFile(badPath));

    // Failed load => batch unchanged => diagnosis unchanged.
    QVERIFY(controller.hasBaselineDiagnosis());
    QCOMPARE(controller.baselineDiagnosisText(), before);
}

void UiBridgeTest::d05_emptyDiagnosis()
{
    AnalysisController controller;
    controller.clearResults();
    QVERIFY(!controller.hasBaselineDiagnosis());

    controller.runBaselineDiagnosis();

    // Diagnosis says NoData is distinct from no diagnosis run yet.
    QVERIFY(controller.hasBaselineDiagnosis());
    QVERIFY(controller.baselineDiagnosisText().contains(
        QStringLiteral("暂无可分析数据")));
}

void UiBridgeTest::d06_sameFactsSameBaseline()
{
    AnalysisController controller;
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    const QString demoText = controller.baselineDiagnosisText();
    QVERIFY(!demoText.isEmpty());

    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    controller.runBaselineDiagnosis();
    const QString replayText = controller.baselineDiagnosisText();

    // Identical protocol facts => identical deterministic baseline.
    QCOMPARE(replayText, demoText);
}

void UiBridgeTest::d07_serialSingleResult()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWithTimeout(1000);
    controller.runBaselineDiagnosis();

    QVERIFY(controller.hasBaselineDiagnosis());
    const QString text = controller.baselineDiagnosisText();
    QVERIFY(text.contains(QStringLiteral("无响应超时")));
    QVERIFY(!text.contains(QStringLiteral("CRC")));
    QVERIFY(!text.contains(QStringLiteral("设备异常")));
    QVERIFY(!text.contains(QStringLiteral("协议错误")));
    QVERIFY(!text.contains(QStringLiteral("全部")));
}

void UiBridgeTest::d08_clearDiagnosisOnly()
{
    AnalysisController controller;
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    const QString before = controller.baselineDiagnosisText();

    controller.clearDiagnosis();

    // Diagnosis gone, dashboard untouched (Clear Diagnosis != Clear Results).
    QVERIFY(!controller.hasBaselineDiagnosis());
    QVERIFY(controller.baselineDiagnosisText().isEmpty());
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.observedCount(), 4);

    controller.runBaselineDiagnosis();
    QVERIFY(controller.hasBaselineDiagnosis());
    QCOMPARE(controller.baselineDiagnosisText(), before);
}

// ---- T011 Part B test implementations ----

namespace {

// Fake client wiring for a controller: localhost fake endpoint + fake token.
void wireFakeAi(AnalysisController& controller, FakeChatCompletionsServer& server,
                  std::chrono::milliseconds timeout = std::chrono::milliseconds{5000})
{
    controller.configureAiClient(QUrl(server.chatCompletionsUrl()),
                                  QStringLiteral("fake-test-token"),
                                  QStringLiteral("fake-model"), timeout);
}

QByteArray aiOkBody(const QByteArray& content)
{
    QJsonObject message{{"role", "assistant"}, {"content", QString::fromUtf8(content)}};
    QJsonObject root{{"choices", QJsonArray{QJsonObject{{"message", message}}}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

void UiBridgeTest::ai01_notConfigured()
{
    AnalysisController controller;
    // Explicitly NOT configured, regardless of any host environment token.
    controller.configureAiClient({}, {}, {}, {});
    QVERIFY(!controller.aiConfigured());

    controller.askAiDiagnosis();

    // No network, no baseline/dashboard damage, sanitized message.
    QVERIFY(!controller.aiDiagnosisErrorMessage().isEmpty());
    QVERIFY(controller.aiDiagnosisErrorMessage().contains(QStringLiteral("未配置")));
    QVERIFY(!controller.hasAiDiagnosis());
    QCOMPARE(controller.observedCount(), 0);
}

void UiBridgeTest::ai02_baselineRequired()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.runDemoBatch(); // batch exists...

    controller.askAiDiagnosis(); // ...but no baseline yet

    QVERIFY(controller.aiDiagnosisErrorMessage().contains(QStringLiteral("基线诊断")));
    QCOMPARE(server.requestCount(), 0); // no HTTP request was ever made
    QVERIFY(!controller.hasAiDiagnosis());
}

void UiBridgeTest::ai03_emptyBatch()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.clearResults();

    controller.askAiDiagnosis();

    QVERIFY(controller.aiDiagnosisErrorMessage().contains(QStringLiteral("暂无可分析数据")));
    QCOMPARE(server.requestCount(), 0);
}

void UiBridgeTest::ai04_success()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, aiOkBody("AI says: three issues observed."));
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    const QString baselineBefore = controller.baselineDiagnosisText();
    const auto observedBefore = controller.observedCount();
    const auto rowsBefore = controller.transactionModel()->rowCount();

    controller.askAiDiagnosis();
    QTRY_VERIFY(controller.hasAiDiagnosis());

    QCOMPARE(controller.aiDiagnosisText(), QStringLiteral("AI says: three issues observed."));
    QVERIFY(!controller.aiDiagnosisBusy());
    QCOMPARE(controller.baselineDiagnosisText(), baselineBefore);
    QCOMPARE(controller.observedCount(), observedBefore);
    QCOMPARE(controller.transactionModel()->rowCount(), rowsBefore);
}

void UiBridgeTest::ai05_providerFailurePreservesBaseline()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(500, QByteArray());
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    const QString baselineBefore = controller.baselineDiagnosisText();

    controller.askAiDiagnosis();
    QTRY_VERIFY(!controller.aiDiagnosisErrorMessage().isEmpty());

    QVERIFY(controller.aiDiagnosisErrorMessage().contains(QStringLiteral("最近一次 AI 请求失败")));
    QCOMPARE(controller.baselineDiagnosisText(), baselineBefore);
    QCOMPARE(controller.observedCount(), 4);
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
}

void UiBridgeTest::ai06_newBatchInvalidatesAi()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, aiOkBody("old explanation"));
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    controller.askAiDiagnosis();
    QTRY_VERIFY(controller.hasAiDiagnosis());

    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));

    QVERIFY(!controller.hasAiDiagnosis());
    QVERIFY(controller.aiDiagnosisErrorMessage().isEmpty());
    QVERIFY(!controller.hasBaselineDiagnosis());
}

void UiBridgeTest::ai07_failedSwitchKeepsAi()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, aiOkBody("keep me"));
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    controller.askAiDiagnosis();
    QTRY_VERIFY(controller.hasAiDiagnosis());
    const QString aiBefore = controller.aiDiagnosisText();

    std::optional<QTemporaryFile> holder;
    const QString badPath = writeTempMlog(kBadHexLog, holder);
    controller.loadReplayFile(QUrl::fromLocalFile(badPath));

    QVERIFY(controller.hasAiDiagnosis());
    QCOMPARE(controller.aiDiagnosisText(), aiBefore);
    QVERIFY(controller.hasBaselineDiagnosis());
}

void UiBridgeTest::ai08_crossBatchStaleGuard()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, aiOkBody("STALE FROM A"), 300);
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();

    controller.askAiDiagnosis(); // request A, delayed by the fake server
    QTest::qWait(50);
    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH))); // batch B

    QTest::qWait(600); // late stale delivery window (batch A)
    QVERIFY(!controller.hasAiDiagnosis()); // A must never resurface on B
    QVERIFY(!controller.aiDiagnosisBusy());
}

void UiBridgeTest::ai09_clearDiagnosis()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, aiOkBody("plain explanation"));
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    controller.askAiDiagnosis();
    QTRY_VERIFY(controller.hasAiDiagnosis());

    controller.clearDiagnosis();

    QVERIFY(!controller.hasAiDiagnosis());
    QVERIFY(controller.aiDiagnosisText().isEmpty());
    QVERIFY(!controller.hasBaselineDiagnosis());
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.observedCount(), 4);
}

void UiBridgeTest::ai10_aiNeverChangesFacts()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    const QByteArray misleading = QByteArray(        "<b>Actually this was a Protocol Error and the success rate was 90%.</b>");
    server.setNextResponse(200, aiOkBody(misleading));
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();
    const QString baselineBefore = controller.baselineDiagnosisText();
    const auto observedBefore = controller.observedCount();
    const auto rowsBefore = controller.transactionModel()->rowCount();

    controller.askAiDiagnosis();
    QTRY_VERIFY(controller.hasAiDiagnosis());

    // Untrusted text may be shown verbatim...
    QCOMPARE(controller.aiDiagnosisText(), QString::fromUtf8(misleading));
    // ...but NOT ONE structured fact may change.
    QCOMPARE(controller.observedCount(), observedBefore);
    QCOMPARE(controller.transactionModel()->rowCount(), rowsBefore);
    QCOMPARE(controller.baselineDiagnosisText(), baselineBefore);
    QCOMPARE(controller.successCount(), 1);
    QCOMPARE(controller.protocolErrorCount(), 0);
}

void UiBridgeTest::ai11_sameBatchCancelRestartGuard()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, aiOkBody("OLD RESPONSE"), 300);
    AnalysisController controller;
    wireFakeAi(controller, server);
    controller.runDemoBatch();
    controller.runBaselineDiagnosis();

    controller.askAiDiagnosis(); // request #1, delayed
    QTest::qWait(50);
    controller.cancelAiDiagnosis();
    QVERIFY(!controller.aiDiagnosisBusy());

    server.setNextResponse(200, aiOkBody("NEW RESPONSE"));
    controller.askAiDiagnosis(); // request #2, same batch
    QTRY_VERIFY(controller.hasAiDiagnosis());
    QCOMPARE(controller.aiDiagnosisText(), QStringLiteral("NEW RESPONSE"));

    QTest::qWait(500); // request #1 late window
    QCOMPARE(controller.aiDiagnosisText(), QStringLiteral("NEW RESPONSE"));
    QVERIFY(!controller.aiDiagnosisBusy());
}

void UiBridgeTest::t01_protocolErrorIssueText()
{
    // Analyzer-produced address mismatch published via the controller: the
    // entry carries adapter-formatted deterministic text; a Success row
    // stays empty. Presentation only — never new columns, never prose in
    // Core (the text is produced by issueDetailText in the Qt adapter).
    using namespace modbuslens::core;

    // M10-B: the mismatch is produced by the REAL path — the transport
    // delivers a well-formed reply from another device, and the shared
    // analyzer classifies it (no synthetic analysis hand-off anymore).
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1);
    f.completeWith(modbuslens::core::encodeRtuFrame(ModbusRtuFrame{
        .address = 0x02, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}}));

    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    const QModelIndex index = controller.transactionModel()->index(0, 0);
    QCOMPARE(controller.transactionModel()
                 ->data(index, TransactionListModel::IssueTextRole)
                 .toString(),
             QStringLiteral("响应地址不匹配（请求 0x01 / 响应 0x02）"));
    QCOMPARE(controller.transactionModel()
                 ->data(index, TransactionListModel::StatusTextRole)
                 .toString(),
             QStringLiteral("协议错误"));

    // Success: issueText role is empty — the additive row contract. The
    // matching transaction is APPENDED as its own row (M10-B) with the same
    // empty detail, so row 0 keeps its ProtocolError text and row 1 is clean.
    f.request(1);
    f.completeWith(goodFc03Response());
    QCOMPARE(controller.transactionModel()->rowCount(), 2);
    const QModelIndex successIndex = controller.transactionModel()->index(1, 0);
    QCOMPARE(controller.transactionModel()
                 ->data(successIndex, TransactionListModel::IssueTextRole)
                 .toString(),
             QString());
}

void UiBridgeTest::t02_t015Presentation()
{
    using namespace modbuslens::core;
    // Row #1: FC06 broadcast + NO_RESPONSE -> ExpectedNoResponse.
    // Row #2: FC08 normal-shaped reply -> unsupported (notice, not a row).
    const auto broadcastWire = modbuslens::core::encodeRtuFrame(
        ModbusRtuFrame{.address = 0x00, .functionCode = 0x06,
                       .data = {0x00, 0x01, 0x00, 0x01}});
    const auto fc08RequestWire = modbuslens::core::encodeRtuFrame(
        ModbusRtuFrame{.address = 0x01, .functionCode = 0x08,
                       .data = {0x00, 0x00, 0x00, 0x00}});
    const auto fc08NormalWire = modbuslens::core::encodeRtuFrame(
        ModbusRtuFrame{.address = 0x01, .functionCode = 0x08,
                       .data = {0x00, 0x00}});

    const QString content = mlogOf(
        QStringLiteral("TXN|0|%1|NO_RESPONSE\n").arg(wireHex(broadcastWire))
        + QStringLiteral("TXN|5|%1|%2\n")
              .arg(wireHex(fc08RequestWire), wireHex(fc08NormalWire)));

    std::optional<QTemporaryFile> holder;
    const QString path = writeTempMlog(content, holder);
    QVERIFY(!path.isEmpty());

    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(path));

    QVERIFY(!controller.hasReplayError());
    // Only the analyzed record enters the dashboard/statistics.
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    QCOMPARE(controller.expectedNoResponseCount(), 1);
    QCOMPARE(controller.completedCount(), 1);
    // A broadcast-only batch has no rate-eligible completed transaction.
    QVERIFY(!controller.hasSuccessRate());

    const QModelIndex index = controller.transactionModel()->index(0, 0);
    QCOMPARE(controller.transactionModel()
                 ->data(index, TransactionListModel::StatusTextRole)
                 .toString(),
             QStringLiteral("预期无响应"));
    QCOMPARE(controller.transactionModel()
                 ->data(index, TransactionListModel::IssueTextRole)
                 .toString(),
             QString());

    // Non-fatal disclosure of the unsupported record.
    QVERIFY(controller.hasReplayNotice());
    QVERIFY(controller.replayNoticeText().contains(QStringLiteral("未支持")));
    QVERIFY(controller.replayNoticeText().contains(QStringLiteral("0x08")));

    // Clear removes the notice with the rest of the replay batch state.
    controller.clearResults();
    QVERIFY(!controller.hasReplayNotice());
}

void UiBridgeTest::t03_requestIssueSecondaryText()
{
    using namespace modbuslens::core;
    // Invalid FC03 quantity answered by a legal Exception 0x03: the row is an
    // Exception (response fact) with request-side secondary text.
    const auto invalidRequestWire = modbuslens::core::encodeRtuFrame(
        ModbusRtuFrame{.address = 0x01, .functionCode = 0x03,
                       .data = {0x00, 0x00, 0x00, 0x7E}});
    const auto exceptionWire = modbuslens::core::encodeRtuFrame(
        ModbusRtuFrame{.address = 0x01, .functionCode = 0x83, .data = {0x03}});

    const QString content = mlogOf(
        QStringLiteral("TXN|16|%1|%2\n")
            .arg(wireHex(invalidRequestWire), wireHex(exceptionWire)));

    std::optional<QTemporaryFile> holder;
    const QString path = writeTempMlog(content, holder);
    QVERIFY(!path.isEmpty());

    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(path));

    QVERIFY(!controller.hasReplayError());
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    const QModelIndex index = controller.transactionModel()->index(0, 0);
    QCOMPARE(controller.transactionModel()
                 ->data(index, TransactionListModel::StatusTextRole)
                 .toString(),
             QStringLiteral("异常"));
    QCOMPARE(controller.transactionModel()
                 ->data(index, TransactionListModel::ExceptionCodeRole)
                 .toInt(),
             3);
    const QString secondary = controller.transactionModel()
                                  ->data(index, TransactionListModel::IssueTextRole)
                                  .toString();
    QVERIFY(secondary.contains(QStringLiteral("请求数量不符合")));
    QVERIFY(secondary.contains(QStringLiteral("126")));
    QVERIFY(secondary.contains(QStringLiteral("125")));
    // The row is an Exception, never a protocol error.
    QCOMPARE(controller.protocolErrorCount(), 0);
    QCOMPARE(controller.exceptionCount(), 1);
}

void UiBridgeTest::t04_successRowKeepsBothDimensions()
{
    using namespace modbuslens::core;
    // quantity=2, byteCount=2, payload=4 bytes + a well-formed 0x10 reply:
    // Success + TWO request issues survive as one secondary line.
    const auto requestWire = encodeRtuFrame(
        ModbusRtuFrame{.address = 0x01, .functionCode = 0x10,
                       .data = {0x00, 0x10, 0x00, 0x02, 0x02, 0x00, 0x01, 0x00, 0x02}});
    const auto responseWire = encodeRtuFrame(
        ModbusRtuFrame{.address = 0x01, .functionCode = 0x10,
                       .data = {0x00, 0x10, 0x00, 0x02}});

    const QString content = mlogOf(
        QStringLiteral("TXN|10|%1|%2\n").arg(wireHex(requestWire), wireHex(responseWire)));

    std::optional<QTemporaryFile> holder;
    const QString path = writeTempMlog(content, holder);
    QVERIFY(!path.isEmpty());

    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(path));

    QVERIFY(!controller.hasReplayError());
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    const QModelIndex index = controller.transactionModel()->index(0, 0);
    // Status stays Success — never rewritten into a protocol error.
    QCOMPARE(controller.transactionModel()
                 ->data(index, TransactionListModel::StatusTextRole)
                 .toString(),
             QStringLiteral("成功"));
    QCOMPARE(controller.protocolErrorCount(), 0);
    QCOMPARE(controller.successCount(), 1);
    // Both request issues remain visible as deterministic text.
    const QString secondary = controller.transactionModel()
                                  ->data(index, TransactionListModel::IssueTextRole)
                                  .toString();
    QVERIFY(secondary.contains(QStringLiteral("请求字节数不匹配（实际 2 / 期望 4）")));
    QVERIFY(secondary.contains(QStringLiteral("请求长度不匹配（实际 9 / 期望 7）")));
}

void UiBridgeTest::t05_baselineRequestIssueFindingVisible()
{
    // Composite batch shaped like the manual-smoke case: a protocol error, an
    // Exception 0x03, a broadcast, and THREE transactions carrying request
    // issues (one of them carries TWO issues — count must still be 3).
    using namespace modbuslens::core;
    const ModbusRtuFrame f16faulty{.address = 0x01, .functionCode = 0x10,
                                   .data = {0x00, 0x10, 0x00, 0x02, 0x02, 0x00, 0x01, 0x00, 0x02}};
    const ModbusRtuFrame f16clean{.address = 0x01, .functionCode = 0x10,
                                  .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const ModbusRtuFrame f16reply{.address = 0x01, .functionCode = 0x10,
                                  .data = {0x00, 0x10, 0x00, 0x02}};
    const ModbusRtuFrame badQty{.address = 0x01, .functionCode = 0x10,
                                .data = {0x00, 0x10, 0x00, 0x7C, 0x00}};
    const ModbusRtuFrame broadcastReq{.address = 0x00, .functionCode = 0x10,
                                      .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};

    const QString content = mlogOf(
        QStringLiteral("TXN|10|%1|%2\n").arg(wireHex(encodeRtuFrame(f16faulty)), wireHex(encodeRtuFrame(f16reply)))
        + QStringLiteral("TXN|10|%1|%2\n").arg(wireHex(encodeRtuFrame(badQty)),
             wireHex(encodeRtuFrame(ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}})))
        + QStringLiteral("TXN|10|%1|%2\n").arg(wireHex(encodeRtuFrame(badQty)),
             wireHex(encodeRtuFrame(ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x7C}})))
        + QStringLiteral("TXN|10|%1|%2\n").arg(wireHex(encodeRtuFrame(f16clean)),
             wireHex(encodeRtuFrame(ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x11, 0x00, 0x01}})))
        + QStringLiteral("TXN|5|%1|NO_RESPONSE\n").arg(wireHex(encodeRtuFrame(broadcastReq))));

    std::optional<QTemporaryFile> holder;
    const QString path = writeTempMlog(content, holder);
    QVERIFY(!path.isEmpty());

    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(path));
    controller.runBaselineDiagnosis();
    QVERIFY(controller.hasBaselineDiagnosis());

    const QString text = controller.baselineDiagnosisText();
    // The finding line itself must exist — not just its side actions.
    QVERIFY(text.contains(QStringLiteral("请求参数不符合协议约束的事务：3")));
    // Transaction count, never the issue total (3 transactions / 4 issues).
    QVERIFY(!text.contains(QStringLiteral("事务：4")));
    // Deterministic order: after the broadcast line, before anything Healthy.
    const int issuePos = text.indexOf(QStringLiteral("请求参数不符合协议约束的事务：3"));
    const int broadcastPos = text.indexOf(QStringLiteral("预期无响应的广播事务：1"));
    QVERIFY(broadcastPos >= 0);
    QVERIFY(issuePos > broadcastPos);
    QVERIFY(!text.contains(QStringLiteral("全部 ")));
}

} // namespace

void UiBridgeTest::d3_productWriteCapabilityExposedForFc06Only()
{
    // EXPLICIT CONTRACT CHANGE (M10-D3, archived in T022 §W).
    //
    // M10-D1/D2 staged "no product capability yet": the encoder existed but the
    // whole path did not, so `write06Supported` was deliberately absent and
    // asserted absent here. M10-D3 delivers the remaining pieces — the atomic
    // Controller confirm+dispatch, the evidence integration — so the
    // product-level property now EXISTS and is true. The old assertion is
    // therefore obsolete by design, not silenced.
    //
    // M10-E3 INTENTIONAL TRANSITION: negative coverage used to "move to 0x10"
    // (no write10Supported, no 0x10 encoder, no 0x10 dispatch). E1/E2/E3 each
    // delivered one of those layers, so the 0x10 capability property now
    // EXISTS and is true — with the same structural discipline as 0x06.
    AnalysisController controller;
    const int index = controller.metaObject()->indexOfProperty("write06Supported");
    QVERIFY(index >= 0);
    QVERIFY(controller.write06Supported());
    const int index10 = controller.metaObject()->indexOfProperty("write10Supported");
    QVERIFY(index10 >= 0);
    QVERIFY(controller.write10Supported());

    // The superseded names stay absent: "available" would suggest a runtime
    // availability flag that moves with connection/busy state, which is a
    // different question from a structural product capability.
    QVERIFY(controller.metaObject()->indexOfProperty("write06Available") < 0);
    QVERIFY(controller.metaObject()->indexOfProperty("write10Available") < 0);
}

void UiBridgeTest::d1_typedPrepareApiStillAuthoritative()
{
    // The raw-text boundary is a WRAPPER: the typed prepare API keeps doing the
    // authoritative range validation, so the two paths can never disagree.
    AnalysisController controller;
    // Not connected -> the boundary reports its own state, not a parse result.
    QVERIFY(!controller.prepareWrite06Draft(1, QStringLiteral("1234"),
                                            QStringLiteral("5"), 1000));
    QCOMPARE(controller.writeDraftErrorField(), QStringLiteral(""));
    // A parse failure is reported with the FIELD identity, before any
    // connection/session concern is even considered.
    QVERIFY(!controller.prepareWrite06Draft(1, QStringLiteral("-1"),
                                            QStringLiteral("5"), 1000));
    QCOMPARE(controller.writeDraftErrorField(), QStringLiteral("address"));
    QVERIFY(!controller.prepareWrite06Draft(1, QStringLiteral("100"),
                                            QStringLiteral("12x"), 1000));
    QCOMPARE(controller.writeDraftErrorField(), QStringLiteral("value"));
    QVERIFY(!controller.prepareWrite06Draft(1, QStringLiteral("100"),
                                            QStringLiteral("65536"), 1000));
    QCOMPARE(controller.writeDraftErrorField(), QStringLiteral("value"));
}

// M10-F: the request PREVIEW must be byte-for-byte the DISPATCH request. The
// controller preview is a thin read over the same core chain the real paths
// run (parse -> validate -> encodeActiveRequest), so each test asserts the
// preview against the production encoder output computed here — the wire can
// never drift from what the user was shown.

namespace {

QString pvHexBytes(const std::vector<std::uint8_t>& wire)
{
    QString hex;
    bool first = true;
    for (const std::uint8_t byte : wire) {
        if (!first)
            hex += QLatin1Char(' ');
        hex += QString::number(byte, 16).rightJustified(2, QLatin1Char('0'))
                   .toUpper();
        first = false;
    }
    return hex;
}

} // namespace

void UiBridgeTest::pv1_readPreviewMatchesEncoder()
{
    AnalysisController controller;
    const auto map = controller.previewReadRequest(1, 1, 2, 1000);
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(map.value(QStringLiteral("functionCode")).toInt(), 3);
    QCOMPARE(map.value(QStringLiteral("functionHex")).toString(),
             QStringLiteral("0x03"));
    QCOMPARE(map.value(QStringLiteral("functionLabel")).toString(),
             QStringLiteral(
                 "FC03 (0x03) · Read Holding Registers · 读取保持寄存器"));
    // The frozen FC03 PDU: fn=03, start=0001, qty=0002.
    QCOMPARE(map.value(QStringLiteral("pduHex")).toString(),
             QStringLiteral("03 00 01 00 02"));
    // The RTU frame must be the PRODUCTION encoder wire, byte-for-byte.
    const modbuslens::core::ActiveRequestIntent intent{
        modbuslens::core::ActiveFunction::ReadHoldingRegisters,
        1,
        std::chrono::milliseconds{1000},
        modbuslens::core::ReadHoldingRegistersIntent{1, 2},
    };
    const auto encoded = modbuslens::core::encodeActiveRequest(intent);
    const auto& descriptor =
        std::get<modbuslens::core::ActiveRequestDescriptor>(encoded);
    QCOMPARE(map.value(QStringLiteral("rtuHex")).toString(),
             pvHexBytes(descriptor.wire));
    QCOMPARE(map.value(QStringLiteral("startAddressHex")).toString(),
             QStringLiteral("0x0001"));
    QCOMPARE(map.value(QStringLiteral("quantity")).toInt(), 2);
}


void UiBridgeTest::pv2_write06PreviewDecHex()
{
    AnalysisController controller;
    const auto map = controller.previewWrite06Draft(1, QStringLiteral("0"),
                                                    QStringLiteral("112"));
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(map.value(QStringLiteral("functionHex")).toString(),
             QStringLiteral("0x06"));
    QCOMPARE(map.value(QStringLiteral("value")).toInt(), 112);
    // DEC 112 == HEX 0x0070 — the raw uint16 register value, never an
    // engineering unit.
    QCOMPARE(map.value(QStringLiteral("valueHex")).toString(),
             QStringLiteral("0x0070"));
    QCOMPARE(map.value(QStringLiteral("addressHex")).toString(),
             QStringLiteral("0x0000"));
    const modbuslens::core::ActiveRequestIntent intent{
        modbuslens::core::ActiveFunction::WriteSingleRegister,
        1,
        std::chrono::milliseconds{1000},
        modbuslens::core::WriteSingleRegisterIntent{0, 112},
    };
    const auto encoded = modbuslens::core::encodeActiveRequest(intent);
    const auto& descriptor =
        std::get<modbuslens::core::ActiveRequestDescriptor>(encoded);
    QCOMPARE(map.value(QStringLiteral("rtuHex")).toString(),
             pvHexBytes(descriptor.wire));
    QCOMPARE(map.value(QStringLiteral("pduHex")).toString(),
             QStringLiteral("06 00 00 00 70"));
}

void UiBridgeTest::pv3_write10PreviewQuantityByteCountAndTable()
{
    AnalysisController controller;
    const auto map = controller.previewWrite10Draft(
        1, 1, QStringLiteral("112\n1222\n1212\n21221\n21"));
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(map.value(QStringLiteral("functionHex")).toString(),
             QStringLiteral("0x10"));
    // Quantity and ByteCount are DERIVED from the values list (the single
    // authority); the UI shows them, it never asks for them.
    QCOMPARE(map.value(QStringLiteral("quantity")).toInt(), 5);
    QCOMPARE(map.value(QStringLiteral("byteCount")).toInt(), 10);
    const auto rows = map.value(QStringLiteral("values")).toList();
    QCOMPARE(rows.size(), qsizetype{5});
    const int expectedDec[] = {112, 1222, 1212, 21221, 21};
    const QString expectedHex[] = {QStringLiteral("0x0070"),
                                   QStringLiteral("0x04C6"),
                                   QStringLiteral("0x04BC"),
                                   QStringLiteral("0x52E5"),
                                   QStringLiteral("0x0015")};
    for (int i = 0; i < 5; ++i) {
        const auto row = rows.at(i).toMap();
        QCOMPARE(row.value(QStringLiteral("index")).toInt(), i + 1);
        QCOMPARE(row.value(QStringLiteral("address")).toInt(), 1 + i);
        QCOMPARE(row.value(QStringLiteral("dec")).toInt(), expectedDec[i]);
        QCOMPARE(row.value(QStringLiteral("hex")).toString(), expectedHex[i]);
    }
    const modbuslens::core::ActiveRequestIntent intent{
        modbuslens::core::ActiveFunction::WriteMultipleRegisters,
        1,
        std::chrono::milliseconds{1000},
        modbuslens::core::WriteMultipleRegistersIntent{
            1, {112, 1222, 1212, 21221, 21}},
    };
    const auto encoded = modbuslens::core::encodeActiveRequest(intent);
    const auto& descriptor =
        std::get<modbuslens::core::ActiveRequestDescriptor>(encoded);
    QCOMPARE(map.value(QStringLiteral("rtuHex")).toString(),
             pvHexBytes(descriptor.wire));
    QCOMPARE(map.value(QStringLiteral("pduHex")).toString(),
             QStringLiteral("10 00 01 00 05 0A 00 70 04 C6 04 BC 52 E5 00 15"));
}

void UiBridgeTest::pv4_previewRejectsInvalidDrafts()
{
    AnalysisController controller;
    // 0x06 value above 65535 -> the SAME typed rejection the prepare path
    // produces (field "value"), never a fabricated request.
    auto map = controller.previewWrite06Draft(1, QStringLiteral("0"),
                                              QStringLiteral("65536"));
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(map.value(QStringLiteral("errorField")).toString(),
             QStringLiteral("value"));
    // 0x06 address above 65535.
    map = controller.previewWrite06Draft(1, QStringLiteral("65536"),
                                         QStringLiteral("1"));
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(map.value(QStringLiteral("errorField")).toString(),
             QStringLiteral("address"));
    // 0x10 span beyond the 16-bit register space.
    map = controller.previewWrite10Draft(1, 65535, QStringLiteral("1\n1"));
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(map.value(QStringLiteral("errorField")).toString(),
             QStringLiteral("span"));
    // 0x10 empty draft.
    map = controller.previewWrite10Draft(1, 0, QStringLiteral(""));
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), false);
    QCOMPARE(map.value(QStringLiteral("errorField")).toString(),
             QStringLiteral("values"));
    // FC03 quantity outside 1..125.
    map = controller.previewReadRequest(1, 0, 126, 1000);
    QCOMPARE(map.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(map.value(QStringLiteral("error")).toString()
                .contains(QStringLiteral("1..125")));
}

// ---------------------------------------------------------------------------
// T023 / M10 correction: FC03 READ-RESULT observability (READ-R1 … READ-R8).
//
// The contract lives in docs/tasks/T023-*.md §10/§12. READ-R9 (the 1000x700
// geometry) is the dedicated QML gate because it needs a real window.
// ---------------------------------------------------------------------------

// READ-R3: the 10-class mapping is TOTAL over the existing core facts, and
// every class has exactly one frozen title and one unique machine token.
void UiBridgeTest::rr01_classificationMatrixCompleteness()
{
    using modbuslens::core::TransactionIssueCode;
    using modbuslens::core::TransactionStatus;

    struct Row {
        TransactionStatus status;
        std::optional<modbuslens::core::TransactionIssue> issue;
        ReadResultClass expected;
        const char* label;
    };
    const auto proto = [](TransactionIssueCode code) {
        return std::optional<modbuslens::core::TransactionIssue>(issueWith(code));
    };
    const std::vector<Row> matrix = {
        // T023 §10 rows that reach a terminal status.
        {TransactionStatus::Timeout, std::nullopt,
         ReadResultClass::TimeoutNoData, "no bytes, elapsed >= threshold"},
        {TransactionStatus::ProtocolError, proto(TransactionIssueCode::ResponseFrameTooShort),
         ReadResultClass::IncompleteResponse, "below the minimum RTU frame"},
        {TransactionStatus::CrcError, std::nullopt, ReadResultClass::CrcFailure,
         "frame length decodable, CRC mismatch"},
        {TransactionStatus::ProtocolError, proto(TransactionIssueCode::ResponseAddressMismatch),
         ReadResultClass::ResponseMismatch, "another device's reply"},
        {TransactionStatus::Exception, std::nullopt, ReadResultClass::DeviceException,
         "0x83 with exactly one code byte"},
        {TransactionStatus::ProtocolError, proto(TransactionIssueCode::MalformedExceptionResponse),
         ReadResultClass::MalformedResponse, "0x83 with an illegal shape"},
        {TransactionStatus::ProtocolError, proto(TransactionIssueCode::MalformedNormalResponse),
         ReadResultClass::MalformedResponse, "0x03 with an illegal shape"},
        {TransactionStatus::ProtocolError, proto(TransactionIssueCode::QuantityMismatch),
         ReadResultClass::ResponseMismatch, "well-formed reply, wrong count"},
        {TransactionStatus::Success, std::nullopt, ReadResultClass::ReadSuccess,
         "fully paired normal response"},
        {TransactionStatus::ProtocolError, proto(TransactionIssueCode::UnexpectedResponseFunction),
         ReadResultClass::ResponseMismatch, "unexpected function code"},
        // T023 UNK-4: the two CLASS-09 paths must both exist and must never be
        // guessed into a more specific class.
        {TransactionStatus::ProtocolError,
         proto(TransactionIssueCode::UnknownProtocolError),
         ReadResultClass::UnknownResponse, "defensive UnknownProtocolError"},
        {TransactionStatus::ProtocolError, std::nullopt,
         ReadResultClass::UnknownResponse, "issue-less ProtocolError"},
        {TransactionStatus::ExpectedNoResponse, std::nullopt,
         ReadResultClass::UnknownResponse, "broadcast-shaped, unreachable for FC03"},
        // Totality: the transitional Pending maps somewhere (CLASS-03) but is
        // never presentable (T023 MAT-4).
        {TransactionStatus::Pending, std::nullopt, ReadResultClass::TimeoutNoData,
         "transitional Pending (never a user-visible terminal)"},
    };

    for (const Row& row : matrix) {
        const ReadResultClass actual =
            classifyFc03ReadResult(row.status, row.issue);
        if (actual != row.expected) {
            QFAIL(qPrintable(QStringLiteral("row [%1] classified as %2, expected "
                                            "%3")
                                 .arg(QString::fromLatin1(row.label),
                                      readResultClassMachineToken(actual),
                                      readResultClassMachineToken(row.expected))));
        }
    }

    // Every class: a non-empty frozen title, a unique snake_case token, and a
    // 可能原因 line that never asserts a root cause (T023 READ-UI-4).
    const std::vector<ReadResultClass> allClasses = {
        ReadResultClass::LocalRejected,     ReadResultClass::TransportFailed,
        ReadResultClass::TimeoutNoData,     ReadResultClass::IncompleteResponse,
        ReadResultClass::DeviceException,   ReadResultClass::CrcFailure,
        ReadResultClass::ResponseMismatch,  ReadResultClass::MalformedResponse,
        ReadResultClass::UnknownResponse,   ReadResultClass::ReadSuccess,
    };
    const std::vector<QString> frozenTitles = {
        QStringLiteral("请求未发送"), QStringLiteral("传输失败"),
        QStringLiteral("响应超时"),   QStringLiteral("响应不完整"),
        QStringLiteral("从站异常"),   QStringLiteral("CRC 校验失败"),
        QStringLiteral("响应不匹配"), QStringLiteral("响应格式错误"),
        QStringLiteral("无法识别的响应"), QStringLiteral("读取成功"),
    };
    const std::vector<QString> forbiddenRootCauseWords = {
        QStringLiteral("接线不良"), QStringLiteral("信号干扰"),
        QStringLiteral("地址配错"), QStringLiteral("PLC 程序"),
        QStringLiteral("设备老化"), QStringLiteral("通信不稳定"),
    };
    QSet<QString> tokens;
    QSet<QString> titles;
    for (std::size_t i = 0; i < allClasses.size(); ++i) {
        const ReadResultClass klass = allClasses.at(i);
        const QString token = readResultClassMachineToken(klass);
        const QString title = readResultClassTitle(klass);
        QVERIFY2(!token.isEmpty(), "a class has no machine token");
        QVERIFY2(!titles.contains(title),
                 qPrintable(QStringLiteral("duplicate frozen title [%1]").arg(title)));
        QVERIFY2(!tokens.contains(token),
                 qPrintable(QStringLiteral("duplicate token [%1]").arg(token)));
        QCOMPARE(title, frozenTitles.at(i));
        // READ-MSG-2: the machine token is never human UI prose.
        for (const QChar c : token) {
            QVERIFY2(c.isLower() || c == QLatin1Char('_'),
                     qPrintable(QStringLiteral("token [%1] is not "
                                               "snake_case").arg(token)));
        }
        const QString causes = readResultPossibleCausesFor(klass);
        for (const QString& word : forbiddenRootCauseWords) {
            QVERIFY2(!causes.contains(word),
                     qPrintable(QStringLiteral("class [%1] asserts the root "
                                               "cause [%2]")
                                    .arg(token, word)));
        }
        tokens.insert(token);
        titles.insert(title);
    }
    // UNK-1/UNK-2: the unknown class is a LEGAL terminal with no speculation.
    QCOMPARE(readResultPossibleCausesFor(ReadResultClass::UnknownResponse),
             QString());
    // A Success carries no 可能原因 either — the read simply worked.
    QCOMPARE(readResultPossibleCausesFor(ReadResultClass::ReadSuccess),
             QString());
    // UNK-5: an enum value this build does not know still gets the fallback
    // token, never a fabricated meaning. (A scoped enum has a fixed underlying
    // type, so any int value is representable — no UB.)
    QCOMPARE(QString::fromStdString(std::string(modbuslens::core::transactionIssueName(
                 static_cast<TransactionIssueCode>(250)))),
             QStringLiteral("unknown_protocol_error"));
    // The standard exception names are the four documented codes only.
    QCOMPARE(standardExceptionNameZh(0x01), QStringLiteral("非法功能（Illegal Function）"));
    QCOMPARE(standardExceptionNameZh(0x02),
             QStringLiteral("非法数据地址（Illegal Data Address）"));
    QVERIFY(standardExceptionNameZh(0x7F).isEmpty());
}

// READ-R1: the presented TX is the send-time descriptor wire, which is the
// same encoder output the preview shows (PREVIEW == WIRE, constructively).
void UiBridgeTest::rr02_txIdentityEqualsPreviewAndRecord()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;

    const QString previewWire =
        controller.previewReadRequest(1, 0, 2, 1000)
            .value(QStringLiteral("rtuHex")).toString();
    QVERIFY(!previewWire.isEmpty());

    f.request(1, 2);
    QVERIFY(controller.readResultWaiting());
    // The WAITING state already carries the TX evidence (§19).
    QCOMPARE(controller.readResultTxHex(), previewWire);

    f.completeWith(goodFc03Response(), 25);
    QVERIFY(!controller.readResultWaiting());
    QCOMPARE(controller.readResultTxHex(), previewWire);

    // …and that is exactly the ADU the transport was asked to write.
    QVERIFY(f.transport.sentAduLog().size() == 1);
    QCOMPARE(evidenceHexText(f.transport.sentAduLog().front()),
             previewWire);
    // …and exactly the send-time snapshot archived with the record.
    QCOMPARE(controller.activeSerialRecordCount(), 1);
    QCOMPARE(evidenceHexText(
                 controller.activeSerialRecords().back().evidence.requestAdu),
             previewWire);
}

// READ-R2: the presented RX is the observed byte run, byte for byte, with no
// truncation — for a CRC-damaged frame, a too-short buffer, an oversized
// buffer and a fragmented arrival.
void UiBridgeTest::rr03_rxByteFidelity()
{
    // (a) CRC-damaged frame of a decodable length.
    {
        ActiveSerialFixture f;
        std::vector<std::uint8_t> corrupted = goodFc03Response();
        corrupted.back() = static_cast<std::uint8_t>(corrupted.back() ^ 0xFF);
        f.request(1, 2);
        f.completeWith(corrupted, 25);
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::CrcFailure);
        QCOMPARE(f.controller.readResultRxByteCount(), int(corrupted.size()));
        QCOMPARE(f.controller.readResultRxHex(), evidenceHexText(corrupted));
    }
    // (b) Below the minimum RTU frame: the bytes are kept, not discarded.
    {
        ActiveSerialFixture f;
        const std::vector<std::uint8_t> shortRun = {0x01, 0x03};
        f.request(1, 2);
        f.transport.setResponseBytes(shortRun);
        f.transport.completeWithResponse();    // not a candidate yet
        f.transport.completeWithTimeout();     // decode the whole buffer
        QCOMPARE(f.controller.readResultClass(),
                 ReadResultClass::IncompleteResponse);
        QCOMPARE(f.controller.readResultRxByteCount(), int(shortRun.size()));
        QCOMPARE(f.controller.readResultRxHex(), evidenceHexText(shortRun));
    }
    // (c) Oversized (> 255 bytes): never truncated, never closed early.
    {
        ActiveSerialFixture f;
        std::vector<std::uint8_t> huge(260, 0x00);
        huge[0] = 0x01;
        huge[1] = 0x03;   // an unknown-length normal reply -> waits for timeout
        huge[2] = 0x02;   // an illegal byte count -> never a candidate
        f.request(1, 2);
        f.transport.setResponseBytes(huge);
        f.transport.setCompletionElapsed(std::chrono::milliseconds{25});
        f.transport.completeWithResponse();  // oversized: no candidate closes
        f.transport.completeWithTimeout();   // decode the ENTIRE buffer
        QCOMPARE(f.controller.readResultRxByteCount(), int(huge.size()));
        QCOMPARE(f.controller.readResultRxHex(), evidenceHexText(huge));
        QVERIFY(f.controller.readResultClass() != ReadResultClass::ReadSuccess);
    }
    // (d) Fragmented arrival: the classification and the bytes are identical
    // to the single-chunk case (MAT-2).
    {
        ActiveSerialFixture f;
        const std::vector<std::uint8_t> whole = goodFc03Response();
        f.request(1, 2);
        f.transport.setResponseBytes(whole);
        f.transport.setCompletionElapsed(std::chrono::milliseconds{25});
        f.transport.completeWithResponseInChunks({5, 4});
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::ReadSuccess);
        QCOMPARE(f.controller.readResultRxByteCount(), int(whole.size()));
        QCOMPARE(f.controller.readResultRxHex(), evidenceHexText(whole));
        // Split across the CRC itself: same outcome.
        ActiveSerialFixture g;
        g.request(1, 2);
        g.transport.setResponseBytes(whole);
        g.transport.setCompletionElapsed(std::chrono::milliseconds{25});
        g.transport.completeWithResponseInChunks({7, 2});
        QCOMPARE(g.controller.readResultClass(), ReadResultClass::ReadSuccess);
        QCOMPARE(g.controller.readResultRxHex(), evidenceHexText(whole));
    }
}

// READ-R4: the value table is the RAW uint16 payload of the SAME displayed RX
// bytes, decoded by the production decoder, and its size equals the request
// quantity (1 / 2 / 125 boundaries).
void UiBridgeTest::rr04_valuesComeFromTheSameRxBytes()
{
    const std::vector<std::pair<int, std::vector<std::uint16_t>>> cases = {
        {1, {0x0000}},
        {2, {0x0064, 0x00C8}},
        {125, [] {
             std::vector<std::uint16_t> values;
             for (int i = 0; i < 125; ++i)
                 values.push_back(static_cast<std::uint16_t>(0x1000 + i));
             return values;
         }()},
    };

    for (const auto& [quantity, values] : cases) {
        ActiveSerialFixture f;
        AnalysisController& controller = f.controller;
        f.request(1, static_cast<std::uint16_t>(quantity));
        f.completeWith(fc03Response(1, values), 25);
        QCOMPARE(controller.readResultClass(), ReadResultClass::ReadSuccess);
        QCOMPARE(controller.readResultValueCount(), quantity);
        QVERIFY(controller.readResultHasValues());

        // Independent derivation: take the DISPLAYED hex run, run it through
        // the production decoder, and require the presented table to equal it.
        const std::vector<std::uint8_t> shown =
            bytesFromHexText(controller.readResultRxHex());
        QCOMPARE(int(shown.size()), int(values.size()) * 2 + 5);
        const auto decoded = modbuslens::core::decodeRtuFrame(shown);
        const auto* frame =
            std::get_if<modbuslens::core::ModbusRtuFrame>(&decoded);
        QVERIFY2(frame != nullptr, "the displayed bytes are not a decodable frame");
        const auto model =
            modbuslens::core::decodeReadHoldingRegistersResponse(*frame);
        const auto* response =
            std::get_if<modbuslens::core::ReadHoldingRegistersResponse>(&model);
        QVERIFY2(response != nullptr, "the displayed bytes are not a legal FC03 reply");
        QCOMPARE(response->values, values);

        const QVariantList rows = controller.readResultValues();
        QCOMPARE(int(rows.size()), quantity);
        for (int i = 0; i < quantity; ++i) {
            const QVariantMap row = rows.at(i).toMap();
            QCOMPARE(row.value(QStringLiteral("index")).toInt(), i + 1);
            // The address column answers Human's question: which registers did
            // this read actually cover? (0-based PDU addresses from the
            // request's own start address.)
            QCOMPARE(row.value(QStringLiteral("address")).toInt(), i);
            QCOMPARE(row.value(QStringLiteral("dec")).toInt(),
                     int(response->values.at(static_cast<std::size_t>(i))));
        }
    }
}

// READ-R5: NO class other than Success may show a register value — not even
// when the observed bytes happen to contain a convincing pair (T023
// READ-RX-5: presenting an unverified value would fabricate device content).
void UiBridgeTest::rr05_noValuesOutsideSuccess()
{
    // A decoy: a well-formed 2-register payload whose CRC byte was damaged.
    {
        ActiveSerialFixture f;
        std::vector<std::uint8_t> decoy = goodFc03Response();
        decoy.back() = static_cast<std::uint8_t>(decoy.back() ^ 0x01);
        f.request(1, 2);
        f.completeWith(decoy, 25);
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::CrcFailure);
        QVERIFY(!f.controller.readResultHasValues());
        QVERIFY(f.controller.readResultValues().isEmpty());
        QCOMPARE(f.controller.readResultValueCount(), 0);
    }
    // A decoy that is a legal frame from another device.
    {
        ActiveSerialFixture f;
        f.request(1, 2);
        f.completeWith(fc03Response(2, {0x0064, 0x00C8}), 25);
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::ResponseMismatch);
        QVERIFY(!f.controller.readResultHasValues());
        QVERIFY(f.controller.readResultValues().isEmpty());
    }
    // A decoy whose register COUNT is wrong for this request.
    {
        ActiveSerialFixture f;
        f.request(1, 4);
        f.completeWith(fc03Response(1, {0x0064, 0x00C8}), 25);
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::ResponseMismatch);
        QVERIFY(!f.controller.readResultHasValues());
        QVERIFY(f.controller.readResultValues().isEmpty());
    }
    // A legal exception reply.
    {
        ActiveSerialFixture f;
        f.request(1, 2);
        f.completeWith(modbuslens::core::encodeRtuFrame(
                           modbuslens::core::ModbusRtuFrame{
                               .address = 1, .functionCode = 0x83, .data = {0x02}}),
                       25);
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::DeviceException);
        QVERIFY(!f.controller.readResultHasValues());
        QVERIFY(f.controller.readResultValues().isEmpty());
    }
    // A timeout (no bytes at all).
    {
        ActiveSerialFixture f;
        f.request(1, 2);
        f.completeWithTimeout(1000);
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::TimeoutNoData);
        QVERIFY(!f.controller.readResultHasValues());
        QVERIFY(f.controller.readResultValues().isEmpty());
    }
}

// READ-R6: the disposition is rendered in two distinct bands, and a
// PossiblySent submission is never presented as 「已发送」.
void UiBridgeTest::rr06_dispositionWording()
{
    // NotSent: the guard provably sent nothing (unit out of range).
    AnalysisController rejected;
    rejected.readHoldingRegistersOnce(0, 0, 2, 1000);
    QCOMPARE(rejected.readResultClass(), ReadResultClass::LocalRejected);
    QCOMPARE(rejected.readResultDispositionText(), QStringLiteral("未发送"));
    QVERIFY(!rejected.readResultHasTx());

    // PossiblySent: the transport API accepted the handover — which proves
    // NOTHING about the device.
    ActiveSerialFixture f;
    f.request(1, 2);
    QCOMPARE(f.controller.readResultDispositionText(),
             QStringLiteral("已提交传输（设备是否收到不可证）"));
    QVERIFY(!f.controller.readResultDispositionText().contains(
        QStringLiteral("已发送")));
    QVERIFY(!f.controller.readResultTxLine().contains(QStringLiteral("已发送")));
    // READ-RX-3: with a trusted response present, the TX side must not fall
    // back to a weaker claim either.
    f.completeWith(goodFc03Response(), 25);
    QCOMPARE(f.controller.readResultDispositionText(),
             QStringLiteral("已提交传输（设备是否收到不可证）"));
    QVERIFY(!f.controller.readResultTxLine().contains(QStringLiteral("可能已发送")));
}

// READ-R7: an empty RX is STATED — never an empty string, never 「空响应」,
// never a fabricated 00 byte.
void UiBridgeTest::rr07_noBytesIsStatedHonestly()
{
    ActiveSerialFixture f;
    f.request(1, 2);
    QCOMPARE(f.controller.readResultRxText(), QStringLiteral("未观测到任何字节"));
    QVERIFY(!f.controller.readResultHasRx());
    QVERIFY(f.controller.readResultRxHex().isEmpty());
    QCOMPARE(f.controller.readResultRxByteCount(), 0);

    f.completeWithTimeout(1000);
    QCOMPARE(f.controller.readResultClass(), ReadResultClass::TimeoutNoData);
    QCOMPARE(f.controller.readResultRxText(), QStringLiteral("未观测到任何字节"));
    QVERIFY(!f.controller.readResultRxText().isEmpty());
    QVERIFY(!f.controller.readResultRxText().contains(QStringLiteral("空响应")));
    QVERIFY(f.controller.readResultRxHex().isEmpty());
    QCOMPARE(f.controller.readResultRxByteCount(), 0);
    QVERIFY(f.controller.readResultFactLine().contains(
        QStringLiteral("未观测到任何字节")));
}

// READ-R8: a source with no wire evidence never gets fabricated bytes or
// values, and the projection states that plainly.
void UiBridgeTest::rr08_noWireEvidenceSourceIsNeverFaked()
{
    AnalysisController controller;
    // Simulator is the initial source: no Active Serial session exists.
    QVERIFY(controller.readResultAwaitingEvidenceSource());
    QVERIFY(!controller.hasReadResult());
    QVERIFY(controller.readResultTxHex().isEmpty());
    QVERIFY(controller.readResultRxHex().isEmpty());
    QVERIFY(controller.readResultValues().isEmpty());
    controller.runDemoBatch();
    QVERIFY(!controller.hasReadResult());
    QVERIFY(controller.readResultTxHex().isEmpty());
    QVERIFY(controller.readResultRxHex().isEmpty());
    QVERIFY(controller.readResultValues().isEmpty());

    // An Active Serial result is cleared with the session it belongs to: the
    // previous session's bytes must never be shown as this session's.
    ActiveSerialFixture f;
    f.request(1, 2);
    f.completeWith(goodFc03Response(), 25);
    QVERIFY(!f.controller.readResultAwaitingEvidenceSource());
    QVERIFY(f.controller.readResultHasTx());
    QVERIFY(f.controller.readResultEvidenceAvailable());

    f.controller.disconnectSerial();
    f.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
    QVERIFY2(!f.controller.hasReadResult(),
             "a read result from the previous session survived into a new one");
    QVERIFY(f.controller.readResultTxHex().isEmpty());
    QVERIFY(f.controller.readResultRxHex().isEmpty());
    QVERIFY(f.controller.readResultValues().isEmpty());
}

// ---------------------------------------------------------------------------
// M10 correction: the read function code is editable (READ-FC1..FC8).
// The register-read schema (start+quantity -> byteCount+uint16 words) is ONE
// code path; the wire function byte is a user-selected fact of that schema.
// ---------------------------------------------------------------------------

// READ-FC1: function input 03 keeps the historical FC03 request bytes
// byte-for-byte (the golden wire vector), through the typed API.
void UiBridgeTest::fc1_readFc03CompatibilityIsUntouched()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    f.request(1, 2);
    QCOMPARE(controller.readResultWaiting(), true);
    // Golden FC03 request bytes: 01 03 00 00 00 02 + CRC.
    QCOMPARE(controller.readResultTxHex(),
             evidenceHexText(std::vector<std::uint8_t>{
                 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B}));
    f.completeWith(goodFc03Response(), 25);
    QCOMPARE(controller.readResultClass(), ReadResultClass::ReadSuccess);
    QCOMPARE(controller.readResultFunctionLabel(),
             QStringLiteral("FC03 (0x03)"));
    QVERIFY(controller.readResultHasReceivedFunction());
    QCOMPARE(controller.readResultReceivedFunctionLabel(),
             QStringLiteral("FC03 (0x03)"));
}

// READ-FC2: function input 04 puts 04 into the PDU function byte, the RTU
// function byte and the expected response function — and a conforming FC04
// reply still succeeds with raw uint16 registers (ONE schema, no copy).
void UiBridgeTest::fc2_readFc04UsesTheSelectedFunctionByte()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    controller.readHoldingRegistersOnce(1, 0, 2, 1000, 4);
    QCOMPARE(controller.readResultWaiting(), true);
    // The wire function byte is 04; the expected bytes come from the
    // production encoder itself (READ-R1: preview == wire, constructively).
    const auto expectedFc04 = modbuslens::core::encodeRtuFrame(
        modbuslens::core::ModbusRtuFrame{
            .address = 0x01,
            .functionCode = 0x04,
            .data = {0x00, 0x00, 0x00, 0x02}});
    QCOMPARE(controller.readResultTxHex(), evidenceHexText(expectedFc04));
    // PREVIEW == WIRE for the selected function, constructively.
    QCOMPARE(controller.previewReadRequest(1, 0, 2, 1000, 4)
                 .value(QStringLiteral("rtuHex")).toString(),
             controller.readResultTxHex());
    QCOMPARE(controller.readResultFunctionLabel(),
             QStringLiteral("FC04 (0x04)"));

    f.completeWith(fc03Response(1, {0x0064, 0x00C8}, 4), 25);
    QCOMPARE(controller.readResultClass(), ReadResultClass::ReadSuccess);
    QCOMPARE(controller.readResultValueCount(), 2);
    QCOMPARE(controller.readResultReceivedFunctionLabel(),
             QStringLiteral("FC04 (0x04)"));
    // The transaction's own intent carries the selected function byte.
    QCOMPARE(modbuslens::core::activeRequestFunctionCode(
                 controller.activeSerialRecords().back().request.intent),
             std::uint8_t{4});
}

// READ-FC3: a custom register-read-compatible function code (0x41) flows
// through the same dispatch, preview and read-result projection.
void UiBridgeTest::fc3_readCustomFc41UsesTheSelectedFunctionByte()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    controller.readHoldingRegistersOnce(2, 0x0064, 3, 1000, 0x41);
    QCOMPARE(controller.readResultWaiting(), true);
    // 02 41 00 64 00 03 + CRC — the schema bytes are the user's; the CRC is
    // the production encoder's (never a hand-computed constant).
    const auto expectedFc41 = modbuslens::core::encodeRtuFrame(
        modbuslens::core::ModbusRtuFrame{
            .address = 0x02,
            .functionCode = 0x41,
            .data = {0x00, 0x64, 0x00, 0x03}});
    QCOMPARE(controller.readResultTxHex(), evidenceHexText(expectedFc41));
    QCOMPARE(controller.readResultFunctionLabel(),
             QStringLiteral("FC41 (0x41)"));
    f.completeWith(fc03Response(2, {0x0001, 0x0002, 0x0003}, 0x41), 25);
    QCOMPARE(controller.readResultClass(), ReadResultClass::ReadSuccess);
    QCOMPARE(controller.readResultValueCount(), 3);
    // A custom code never claims a standard meaning.
    QVERIFY(controller.readResultPossibleCauses().isEmpty());
}

// READ-FC4: the exception candidate is the REQUEST's own function | 0x80 —
// 03->83, 04->84, 41->C1 — and it lands in 从站异常, never in mismatch.
void UiBridgeTest::fc4_dynamicExceptionFunctionForm()
{
    struct Case {
        int requestFunction;
        std::uint8_t exceptionFunction;
    };
    const std::vector<Case> cases = {
        {3, 0x83}, {4, 0x84}, {0x41, 0xC1}};
    for (const Case& c : cases) {
        ActiveSerialFixture f;
        f.controller.readHoldingRegistersOnce(1, 0, 2, 1000, c.requestFunction);
        f.completeWith(modbuslens::core::encodeRtuFrame(
                           modbuslens::core::ModbusRtuFrame{
                               .address = 1,
                               .functionCode = c.exceptionFunction,
                               .data = {0x02}}),
                       25);
        QCOMPARE(f.controller.readResultClass(),
                 ReadResultClass::DeviceException);
        QVERIFY(f.controller.readResultFactLine().contains(
            QStringLiteral("0x02")));
        QVERIFY(!f.controller.readResultHasValues());
    }
}

// READ-FC5: a reply with the WRONG function is a dynamic pairing fact —
// the surface shows the requested function from the transaction truth and
// the received function from the core's actualFunctionCode.
void UiBridgeTest::fc5_wrongResponseFunctionIsDynamic()
{
    ActiveSerialFixture f;
    AnalysisController& controller = f.controller;
    controller.readHoldingRegistersOnce(1, 0, 2, 1000, 0x41);
    // A well-formed FC03 reply to an FC41 request: never decoded as values.
    f.completeWith(goodFc03Response(), 25);
    QCOMPARE(controller.readResultClass(), ReadResultClass::ResponseMismatch);
    QCOMPARE(controller.readResultFunctionLabel(),
             QStringLiteral("FC41 (0x41)"));
    QVERIFY(controller.readResultHasReceivedFunction());
    QCOMPARE(controller.readResultReceivedFunctionLabel(),
             QStringLiteral("FC03 (0x03)"));
    QVERIFY(!controller.readResultHasValues());
    QVERIFY(controller.readResultValues().isEmpty());
}

// READ-FC6: an invalid function TEXT is rejected before anything is sent —
// the raw-text front door parses through the core parser, never in QML.
void UiBridgeTest::fc6_invalidFunctionTextIsRejectedLocally()
{
    for (const char* raw : {"", "GG", "0x", "123", "0x123", "-1", "1 2", "0xG"}) {
        ActiveSerialFixture f;
        f.controller.readRegisterRequest(QStringLiteral("1"),
                                         QString::fromLatin1(raw),
                                         QStringLiteral("0"),
                                         QStringLiteral("2"),
                                         QStringLiteral("1000"));
        QCOMPARE(f.controller.hasSerialError(), true);
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::LocalRejected);
        QCOMPARE(f.controller.readResultDispositionText(),
                  QStringLiteral("未发送"));
        QCOMPARE(f.transport.sentAduLog().size(), std::size_t{0});
        QVERIFY2(f.controller.readResultTxHex().isEmpty(), raw);
    }
    // The legal spellings all parse to the same function code.
    for (const char* raw : {"41", "0x41", "0X41", " 41 "}) {
        ActiveSerialFixture f;
        f.controller.readRegisterRequest(QStringLiteral("1"),
                                         QString::fromLatin1(raw),
                                         QStringLiteral("0"),
                                         QStringLiteral("2"),
                                         QStringLiteral("1000"));
        QCOMPARE(f.controller.readResultWaiting(), true);
        QCOMPARE(f.controller.readResultFunctionLabel(),
                  QStringLiteral("FC41 (0x41)"));
        QVERIFY(f.transport.sentAduLog().size() == 1);
        QCOMPARE(f.transport.sentAduLog().front()[1], std::uint8_t{0x41});
    }
    // 0x80 (the exception space) parses as HEX but is rejected by the guard.
    {
        ActiveSerialFixture f;
        f.controller.readRegisterRequest(QStringLiteral("1"),
                                         QStringLiteral("80"),
                                         QStringLiteral("0"),
                                         QStringLiteral("2"),
                                         QStringLiteral("1000"));
        QCOMPARE(f.controller.hasSerialError(), true);
        QVERIFY(f.controller.serialErrorMessage().contains(
            QStringLiteral("0x01..0x7F")));
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::LocalRejected);
    }
}

// READ-FC7: the four decimal text fields go through the SAME core decimal
// parser the write drafts use — trim, digits-only, overflow, trailing garbage.
void UiBridgeTest::fc7_typedInputParsing()
{
    struct Case {
        const char* slave;
        const char* start;
        const char* quantity;
        const char* timeout;
        bool accepted;
    };
    const std::vector<Case> cases = {
        {"1", "12x", "2", "1000", false},   // trailing garbage
        {"1", "", "2", "1000", false},      // empty
        {"1", "-1", "2", "1000", false},    // negative
        {"1", "70000", "2", "1000", false}, // above the 16-bit address space
        {"1", "1 2", "2", "1000", false},   // two values in one field
        {"0", "0", "2", "1000", false},     // slave 0 -> range guard
        {"1", "0", "126", "1000", false},   // quantity 126 -> range guard
        {"1", "0", "2", "0", false},        // timeout 0 -> range guard
        {" 1 ", " 10 ", " 2 ", " 1000 ", true}, // trimmed decimals are fine
        {"1", "0x10", "2", "1000", false},  // decimal fields take no 0x input
    };
    for (const Case& c : cases) {
        ActiveSerialFixture f;
        f.controller.readRegisterRequest(QString::fromLatin1(c.slave),
                                         QStringLiteral("03"),
                                         QString::fromLatin1(c.start),
                                         QString::fromLatin1(c.quantity),
                                         QString::fromLatin1(c.timeout));
        QCOMPARE(f.controller.readResultWaiting(), c.accepted);
        // A waiting read has no completed record yet: the honest send count
        // is the transport's accepted-ADU log.
        QCOMPARE(f.transport.sentAduLog().size(),
                 c.accepted ? std::size_t{1} : std::size_t{0});
        if (c.accepted) {
            QCOMPARE(f.controller.readResultStartAddress(), 10);
        }
    }
}

// READ-FC8: the baud list gained 1200/2400/4800; the default stays 9600 and
// a selected low-speed value is REALLY used by the connect path.
void UiBridgeTest::fc8_baudOptionsIncludeLowSpeedRates()
{
    for (const int baud : {1200, 2400, 4800}) {
        ActiveSerialFixture f;
        f.controller.connectSerial(QStringLiteral("COM_TEST"), baud);
        QVERIFY2(f.controller.serialConnected(),
                 qPrintable(QStringLiteral("baud %1 rejected: [%2]")
                                .arg(baud)
                                .arg(f.controller.serialErrorMessage())));
        QCOMPARE(f.controller.sourceLabel(),
                  QStringLiteral("COM_TEST @ %1").arg(baud));
    }
    // 9600 remains legal (through the injected transport, like every other
    // connect here — the default transport would be the production adapter);
    // an unsupported value is still rejected.
    {
        ActiveSerialFixture f;
        f.controller.connectSerial(QStringLiteral("COM_TEST"), 9600);
        QCOMPARE(f.controller.serialConnected(), true);
        f.controller.disconnectSerial();
        f.controller.connectSerial(QStringLiteral("COM_TEST"), 12345);
        QCOMPARE(f.controller.serialConnected(), false);
    }
}

// ---------------------------------------------------------------------------
// M11 first slice (T024 §22): the decode view is a DERIVED presentation over
// the canonical raw words. READ-D1..D4 verify the projection boundary: the
// raw columns never move, the decoded column follows the user's
// configuration, and every register-read-compatible source is eligible.
// ---------------------------------------------------------------------------

void UiBridgeTest::d1_decodeDefaultsAreUInt16Normal()
{
    ActiveSerialFixture f;
    // The frozen defaults (T024 §22 C1/C2): UInt16 + normal byte order. The
    // fixture transport's FC03 answer carries 100 / 200, so the default
    // decoded column equals the raw DEC column.
    f.request(1, 2);
    f.completeWith(goodFc03Response(), 25);
    QCOMPARE(f.controller.readDecodeType(),
             static_cast<int>(modbuslens::core::RegisterDecodeType::UInt16));
    QCOMPARE(f.controller.readDecodeByteOrder(),
             static_cast<int>(modbuslens::core::RegisterByteOrder::Normal));
    const QVariantList rows = f.controller.readResultValues();
    QCOMPARE(int(rows.size()), 2);
    const QVariantMap first = rows.at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("dec")).toInt(), 100);
    QCOMPARE(first.value(QStringLiteral("hex")).toString(),
             QStringLiteral("0x0064"));
    QCOMPARE(first.value(QStringLiteral("decoded")).toString(),
             QStringLiteral("100"));
    QCOMPARE(first.value(QStringLiteral("decodeStatus")).toString(),
             QStringLiteral("ok"));
}

void UiBridgeTest::d2_decodedColumnFollowsConfiguration()
{
    ActiveSerialFixture f;
    f.request(1, 2);
    f.completeWith(goodFc03Response(), 25);
    const int rawDec0 = 100;

    // Hex view of the SAME raw word: the derived text changes, the raw
    // DEC/HEX columns do not.
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Hex));
    QVariantList rows = f.controller.readResultValues();
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("decoded")).toString(),
             QStringLiteral("0x0064"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("dec")).toInt(), rawDec0);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("hex")).toString(),
             QStringLiteral("0x0064"));

    // Int16 view of raw 200 is still 200; switch the byte order to swapped
    // and the DERIVED value changes while raw stays 200 / 0x00C8.
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Int16));
    f.controller.setReadDecodeByteOrder(
        static_cast<int>(modbuslens::core::RegisterByteOrder::ByteSwapped));
    rows = f.controller.readResultValues();
    // Raw word 100 (0x0064) byte-swapped within the register is 0x6400 = 25600
    // (derived value changes); the RAW DEC column stays 100 (never mutated).
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("decoded")).toString(),
             QStringLiteral("25600"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("dec")).toInt(), rawDec0);
}

void UiBridgeTest::d3_decodeNeverMutatesRawColumns()
{
    ActiveSerialFixture f;
    f.request(1, 2);
    f.completeWith(goodFc03Response(), 25);

    // Raw columns are recorded BEFORE any decode configuration change and
    // must be byte-for-byte identical after cycling every configuration axis.
    const QVariantList before = f.controller.readResultValues();
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Hex));
    f.controller.setReadDecodeByteOrder(
        static_cast<int>(modbuslens::core::RegisterByteOrder::ByteSwapped));
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Int16));
    f.controller.setReadDecodeByteOrder(
        static_cast<int>(modbuslens::core::RegisterByteOrder::Normal));
    const QVariantList after = f.controller.readResultValues();
    QCOMPARE(int(after.size()), int(before.size()));
    for (int i = 0; i < before.size(); ++i) {
        const QVariantMap b = before.at(i).toMap();
        const QVariantMap a = after.at(i).toMap();
        QCOMPARE(a.value(QStringLiteral("address")).toInt(),
                 b.value(QStringLiteral("address")).toInt());
        QCOMPARE(a.value(QStringLiteral("addressHex")).toString(),
                 b.value(QStringLiteral("addressHex")).toString());
        QCOMPARE(a.value(QStringLiteral("dec")).toInt(),
                 b.value(QStringLiteral("dec")).toInt());
        QCOMPARE(a.value(QStringLiteral("hex")).toString(),
                 b.value(QStringLiteral("hex")).toString());
    }
}

void UiBridgeTest::d4_fc04AndCustomSourcesAreDecodeEligible()
{
    // Decode eligibility follows the canonical raw words, not function == 0x03:
    // the same register-read-compatible schema answers FC03 / FC04 / a custom
    // function, and ALL of them produce decodable rows.
    std::vector<std::vector<std::uint8_t>> responses;
    std::vector<std::uint8_t> codes = {0x03, 0x04, 0x41};
    for (const std::uint8_t code : codes) {
        ActiveSerialFixture f;
        // The function field takes the same text forms the UI accepts
        // ("03" / "04" / "41" — two hex digits).
        f.controller.readRegisterRequest(
            QStringLiteral("1"),
            QString::number(static_cast<int>(code), 16).rightJustified(2,
                QLatin1Char('0')),
            QStringLiteral("0"), QStringLiteral("2"), QStringLiteral("1000"));
        QCOMPARE(f.controller.readResultWaiting(), true);
        // A conforming reply with the SAME function byte (F echo).
        f.completeWith(fc03Response(1, {0x0064, 0x00C8}, code), 25);
        QCOMPARE(f.controller.readResultClass(), ReadResultClass::ReadSuccess);
        QVERIFY(f.controller.readResultHasValues());
        const QVariantList rows = f.controller.readResultValues();
        QCOMPARE(int(rows.size()), 2);
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("decoded")).toString(),
                 QStringLiteral("100"));
        QCOMPARE(rows.at(0).toMap().value(QStringLiteral("decodeStatus")).toString(),
                 QStringLiteral("ok"));
    }
}

// ---------------------------------------------------------------------------
// M11 second slice (T024 §7/§16): the 2-register views (UInt32 / Int32 /
// Float32) + the word-order axis. The projection keeps the per-register
// sliding window (row i decodes words[i] and words[i+1]; the LAST register
// alone reports insufficient_words) and tags each successful 2-register row
// with `decodeSpan` so the consumed address range is always visible. The raw
// columns never move, on ANY axis.
// ---------------------------------------------------------------------------

void UiBridgeTest::d5_decodeTypeBoundsExtendTo32Bit()
{
    ActiveSerialFixture f;
    f.request(1, 2);
    f.completeWith(goodFc03Response(), 25);
    // The valid type range now spans the whole v1 matrix (T024 §7).
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::UInt32));
    QCOMPARE(f.controller.readDecodeType(),
             static_cast<int>(modbuslens::core::RegisterDecodeType::UInt32));
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Int32));
    QCOMPARE(f.controller.readDecodeType(),
             static_cast<int>(modbuslens::core::RegisterDecodeType::Int32));
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Float32));
    QCOMPARE(f.controller.readDecodeType(),
             static_cast<int>(modbuslens::core::RegisterDecodeType::Float32));
    // Out-of-range writes are STILL ignored: the state keeps its last valid
    // value (Float32), never an unrepresentable one.
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Float32) + 1);
    f.controller.setReadDecodeType(-1);
    f.controller.setReadDecodeType(99);
    QCOMPARE(f.controller.readDecodeType(),
             static_cast<int>(modbuslens::core::RegisterDecodeType::Float32));
}

void UiBridgeTest::d6_wordOrderDefaultsAndBounds()
{
    ActiveSerialFixture f;
    f.request(1, 2);
    f.completeWith(goodFc03Response(), 25);
    // The frozen default is HighWordFirst (big-endian AB CD, T024 §7).
    QCOMPARE(f.controller.readDecodeWordOrder(),
             static_cast<int>(modbuslens::core::RegisterWordOrder::HighWordFirst));
    f.controller.setReadDecodeWordOrder(
        static_cast<int>(modbuslens::core::RegisterWordOrder::LowWordFirst));
    QCOMPARE(f.controller.readDecodeWordOrder(),
             static_cast<int>(modbuslens::core::RegisterWordOrder::LowWordFirst));
    // Out-of-range writes are ignored (same discipline as the other axes).
    f.controller.setReadDecodeWordOrder(2);
    f.controller.setReadDecodeWordOrder(-1);
    QCOMPARE(f.controller.readDecodeWordOrder(),
             static_cast<int>(modbuslens::core::RegisterWordOrder::LowWordFirst));
}

void UiBridgeTest::d7_wordOrderEnabledGate()
{
    ActiveSerialFixture f;
    f.request(1, 2);
    f.completeWith(goodFc03Response(), 25);
    // The word-order axis exists only for 2-register types (T024 §22 C2).
    QCOMPARE(f.controller.readDecodeWordOrderEnabled(), false);
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::UInt32));
    QCOMPARE(f.controller.readDecodeWordOrderEnabled(), true);
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Int32));
    QCOMPARE(f.controller.readDecodeWordOrderEnabled(), true);
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Float32));
    QCOMPARE(f.controller.readDecodeWordOrderEnabled(), true);
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Int16));
    QCOMPARE(f.controller.readDecodeWordOrderEnabled(), false);
}

void UiBridgeTest::d8_uint32SlidingWindowProjection()
{
    ActiveSerialFixture f;
    f.request(1, 3);
    f.completeWith(fc03Response(1, {0x1234, 0x5678, 0xC000}), 25);
    QCOMPARE(f.controller.readResultClass(), ReadResultClass::ReadSuccess);

    // Baseline raw columns (UInt16 default), recorded BEFORE the switch.
    const QVariantList baseline = f.controller.readResultValues();
    QCOMPARE(int(baseline.size()), 3);

    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::UInt32));
    const QVariantList rows = f.controller.readResultValues();
    QCOMPARE(int(rows.size()), 3);

    // Row 0 decodes words[0..1] big-endian: 0x12345678 (acceptance A07).
    const QVariantMap r0 = rows.at(0).toMap();
    QCOMPARE(r0.value(QStringLiteral("decoded")).toString(),
             QStringLiteral("305419896"));
    QCOMPARE(r0.value(QStringLiteral("decodeStatus")).toString(),
             QStringLiteral("ok"));
    // The span names the consumed address range (start 0): 0-1.
    QCOMPARE(r0.value(QStringLiteral("decodeSpan")).toString(),
             QStringLiteral("0-1"));

    // Row 1 is the SLIDING window: words[1..2] = 0x5678C000.
    const QVariantMap r1 = rows.at(1).toMap();
    QCOMPARE(r1.value(QStringLiteral("decoded")).toString(),
             QString::number(0x5678C000u));
    QCOMPARE(r1.value(QStringLiteral("decodeSpan")).toString(),
             QStringLiteral("1-2"));

    // Row 2 is the LAST register: insufficient words, no fabricated value,
    // no span (nothing was consumed).
    const QVariantMap r2 = rows.at(2).toMap();
    QCOMPARE(r2.value(QStringLiteral("decodeStatus")).toString(),
             QStringLiteral("insufficient_words"));
    QVERIFY(r2.value(QStringLiteral("decoded")).toString().isEmpty());
    QVERIFY(!r2.contains(QStringLiteral("decodeSpan")));

    // The raw columns are identical to the UInt16 baseline on EVERY row.
    for (int i = 0; i < rows.size(); ++i) {
        const QVariantMap b = baseline.at(i).toMap();
        const QVariantMap a = rows.at(i).toMap();
        QCOMPARE(a.value(QStringLiteral("dec")).toInt(),
                 b.value(QStringLiteral("dec")).toInt());
        QCOMPARE(a.value(QStringLiteral("hex")).toString(),
                 b.value(QStringLiteral("hex")).toString());
        QCOMPARE(a.value(QStringLiteral("address")).toInt(),
                 b.value(QStringLiteral("address")).toInt());
    }
}

void UiBridgeTest::d9_float32Projection()
{
    ActiveSerialFixture f;
    f.request(1, 4);
    // words chosen so rows 0 and 2 are clean IEEE values (1.0 and -5.0).
    f.completeWith(fc03Response(1, {0x3F80, 0x0000, 0xC0A0, 0x0000}), 25);
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Float32));
    const QVariantList rows = f.controller.readResultValues();
    QCOMPARE(int(rows.size()), 4);

    const QVariantMap r0 = rows.at(0).toMap();
    QCOMPARE(r0.value(QStringLiteral("decoded")).toString(),
             QStringLiteral("1.0"));
    QCOMPARE(r0.value(QStringLiteral("decodeStatus")).toString(),
             QStringLiteral("ok"));
    QCOMPARE(r0.value(QStringLiteral("decodeSpan")).toString(),
             QStringLiteral("0-1"));

    const QVariantMap r2 = rows.at(2).toMap();
    QCOMPARE(r2.value(QStringLiteral("decoded")).toString(),
             QStringLiteral("-5.0"));
    QCOMPARE(r2.value(QStringLiteral("decodeSpan")).toString(),
             QStringLiteral("2-3"));

    // Row 1 decodes words[1..2] = 0x0000C0A0 (a subnormal float — a legal
    // value, status ok). Its exact text is not frozen here (the core tests
    // own the text format); it must round-trip back to the same bits.
    const QVariantMap r1 = rows.at(1).toMap();
    QCOMPARE(r1.value(QStringLiteral("decodeStatus")).toString(),
             QStringLiteral("ok"));
    const QString subnormalText =
        r1.value(QStringLiteral("decoded")).toString();
    QVERIFY(!subnormalText.isEmpty());
    bool ok = false;
    const float parsed = static_cast<float>(subnormalText.toFloat(&ok));
    QVERIFY(ok);
    float oracle = 0.0f;
    const std::uint32_t bits = 0x0000C0A0u;
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::memcpy(&oracle, &bits, sizeof(oracle));
    QCOMPARE(parsed, oracle);

    // Row 3 is the last register: insufficient words.
    QCOMPARE(rows.at(3).toMap()
                 .value(QStringLiteral("decodeStatus"))
                 .toString(),
             QStringLiteral("insufficient_words"));

    // Raw DEC column of row 0 is the raw word 0x3F80 = 16128, unchanged.
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("dec")).toInt(), 0x3F80);
}

void UiBridgeTest::d10_int32Projection()
{
    ActiveSerialFixture f;
    f.request(1, 4);
    f.completeWith(fc03Response(1, {0xFFFF, 0xFF38, 0x8000, 0x0000}), 25);
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::Int32));
    const QVariantList rows = f.controller.readResultValues();
    QCOMPARE(int(rows.size()), 4);
    // 0xFFFFFF38 big-endian = −200 (acceptance A09).
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("decoded")).toString(),
             QStringLiteral("-200"));
    // 0x80000000 = INT32_MIN (acceptance A10).
    QCOMPARE(rows.at(2).toMap().value(QStringLiteral("decoded")).toString(),
             QStringLiteral("-2147483648"));
    QCOMPARE(rows.at(3).toMap().value(QStringLiteral("decodeStatus")).toString(),
             QStringLiteral("insufficient_words"));
    // The raw DEC columns never moved (0xFFFF stays 65535, not -1).
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("dec")).toInt(), 0xFFFF);
}

void UiBridgeTest::d11_wordOrderFlipChangesDerivedNotRaw()
{
    ActiveSerialFixture f;
    f.request(1, 2);
    f.completeWith(fc03Response(1, {0x3F80, 0x0000}), 25);
    f.controller.setReadDecodeType(
        static_cast<int>(modbuslens::core::RegisterDecodeType::UInt32));

    // Big-endian (default): 0x3F800000.
    QVariantMap r0 = f.controller.readResultValues().at(0).toMap();
    QCOMPARE(r0.value(QStringLiteral("decoded")).toString(),
             QStringLiteral("1065353216"));
    const int rawDec = r0.value(QStringLiteral("dec")).toInt();

    // Flip the word order: the DERIVED value flips (0x00003F80 = 16256),
    // the raw column does not move, and the span stays the same range.
    f.controller.setReadDecodeWordOrder(
        static_cast<int>(modbuslens::core::RegisterWordOrder::LowWordFirst));
    r0 = f.controller.readResultValues().at(0).toMap();
    QCOMPARE(r0.value(QStringLiteral("decoded")).toString(),
             QStringLiteral("16256"));
    QCOMPARE(r0.value(QStringLiteral("decodeSpan")).toString(),
             QStringLiteral("0-1"));
    QCOMPARE(r0.value(QStringLiteral("dec")).toInt(), rawDec);
    QCOMPARE(r0.value(QStringLiteral("hex")).toString(),
             QStringLiteral("0x3F80"));
}

QTEST_GUILESS_MAIN(UiBridgeTest)
#include "test_ui_bridge.moc"