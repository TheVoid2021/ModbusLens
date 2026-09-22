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
#include <optional>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/ActiveTransactionEvidence.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/active/ActiveRequestIntent.h"
#include "fake_chat_completions_server.h"
#include "fake_serial_transport.h"
#include "ui/AnalysisController.h"
#include "ui/TransactionListModel.h"

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

QTEST_GUILESS_MAIN(UiBridgeTest)
#include "test_ui_bridge.moc"