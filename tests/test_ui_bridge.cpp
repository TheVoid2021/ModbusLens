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

#include "core/analysis/TransactionAnalysis.h"
#include "fake_chat_completions_server.h"
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
    AnalysisController controller;
    controller.publishSerialResult(
        QStringLiteral("COM_TEST @ 9600"), 1,
        makeAnalysis(modbuslens::core::TransactionStatus::Success, 25));

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
    AnalysisController controller;
    controller.publishSerialResult(
        QStringLiteral("COM_TEST @ 9600"), 1,
        makeAnalysis(modbuslens::core::TransactionStatus::Timeout, 1000));

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
    AnalysisController controller;
    controller.publishSerialResult(
        QStringLiteral("COM_TEST @ 9600"), 1,
        makeAnalysis(modbuslens::core::TransactionStatus::Success, 25));
    QCOMPARE(controller.transactionModel()->rowCount(), 1);

    controller.publishSerialResult(
        QStringLiteral("COM_TEST @ 9600"), 1,
        makeAnalysis(modbuslens::core::TransactionStatus::Timeout, 1000));

    // Replace, never append: still exactly one row, latest-only statistics.
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    QCOMPARE(controller.observedCount(), 1);
    QCOMPARE(controller.successCount(), 0);
    QCOMPARE(controller.timeoutCount(), 1);
}

void UiBridgeTest::s08_clearSerialResults()
{
    AnalysisController controller;
    controller.publishSerialResult(
        QStringLiteral("COM_TEST @ 9600"), 1,
        makeAnalysis(modbuslens::core::TransactionStatus::Success, 25));
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

    // A late serial completion with NO pending metadata (source has moved
    // on) must be ignored entirely.
    controller.handleSerialTransactionCompleted(
        makeAnalysis(modbuslens::core::TransactionStatus::Success, 25));

    QCOMPARE(controller.transactionModel()->rowCount(), rowsBefore);
    QCOMPARE(controller.observedCount(), observedBefore);
    QCOMPARE(controller.modeLabel(), QStringLiteral("模拟器模式"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("确定性演示"));
}

// ---- T011 Part A test implementations ----

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
    AnalysisController controller;
    controller.publishSerialResult(
        QStringLiteral("COM_TEST @ 9600"), 1,
        makeAnalysis(modbuslens::core::TransactionStatus::Timeout, 1000));
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

    const ModbusRtuFrame request{
        .address = 0x01, .functionCode = 0x03, .data = {0x00, 0x00, 0x00, 0x02}};
    const ModbusRtuFrame foreign{
        .address = 0x02, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    const auto mismatch = analyzeFunction03Transaction(
        request, ResponseObservation{foreign}, ms{25}, ms{1000});

    AnalysisController controller;
    controller.publishSerialResult(QStringLiteral("COM1 @ 9600"), 1, mismatch);

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

    // Success: issueText role is empty — the additive row contract.
    const auto success = analyzeFunction03Transaction(
        request,
        ResponseObservation{ModbusRtuFrame{
            .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}}},
        ms{25}, ms{1000});
    controller.publishSerialResult(QStringLiteral("COM1 @ 9600"), 1, success);
    QCOMPARE(controller.transactionModel()->rowCount(), 1);
    const QModelIndex successIndex = controller.transactionModel()->index(0, 0);
    QCOMPARE(controller.transactionModel()
                 ->data(successIndex, TransactionListModel::IssueTextRole)
                 .toString(),
             QString());
}

} // namespace

QTEST_GUILESS_MAIN(UiBridgeTest)
#include "test_ui_bridge.moc"