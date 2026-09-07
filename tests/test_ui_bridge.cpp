#include <QtTest>

#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryFile>
#include <QUrl>
#include <QVariant>

#include <cstdint>
#include <optional>

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
    QCOMPARE(model.data(index, TransactionListModel::StatusTextRole), QStringLiteral("Success"));
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
    QCOMPARE(model.data(index, TransactionListModel::StatusTextRole), QStringLiteral("Exception"));
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

    struct Row { const char* status; qint64 elapsed; };
    const Row expected[] = {
        {"Success", 25}, {"Exception", 18}, {"CRC Error", 17}, {"Timeout", 1000}
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
// function both valid, 0x03 semantics fail -> InvalidRequestData.
const QString kZeroQuantityLog = QStringLiteral(
    "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
    "TXN|1|01 03 00 00 00 00 45 CA|NO_RESPONSE\n");

} // namespace

void UiBridgeTest::r01_goldenReplay()
{
    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));

    QCOMPARE(controller.modeLabel(), QStringLiteral("Replay Mode"));
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

    struct Row { const char* status; qint64 elapsed; bool hasException; int exception; };
    const Row expected[] = {
        {"Success", 25, false, 0},
        {"Exception", 18, true, 2},
        {"CRC Error", 17, false, 0},
        {"Timeout", 1000, false, 0},
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
    QVERIFY(controller.replayErrorMessage().contains(QStringLiteral("line")));
    QVERIFY(controller.replayErrorMessage().contains(QStringLiteral("invalid hex")));

    // The WHOLE previous successful state is preserved (rule A).
    QCOMPARE(controller.observedCount(), observedBefore);
    QCOMPARE(controller.transactionModel()->rowCount(), rowsBefore);
    QCOMPARE(controller.modeLabel(), QStringLiteral("Replay Mode"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("demo_v1.mlog"));
    QVERIFY(controller.hasSuccessRate());
}

void UiBridgeTest::r04_executionError()
{
    AnalysisController controller;
    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));

    std::optional<QTemporaryFile> holder;
    const QString badPath = writeTempMlog(kZeroQuantityLog, holder);
    QVERIFY(!badPath.isEmpty());
    controller.loadReplayFile(QUrl::fromLocalFile(badPath));

    QVERIFY(controller.hasReplayError());
    // 0-based core index 0 -> human-readable "transaction 1".
    QVERIFY(controller.replayErrorMessage().contains(QStringLiteral("transaction 1")));
    QVERIFY(controller.replayErrorMessage().contains(QStringLiteral("invalid request data")));

    // Old batch and source stay (rule A).
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("Replay Mode"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("demo_v1.mlog"));
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
    QVERIFY(controller.replayErrorMessage().contains(QStringLiteral("load failed")));

    // Demo batch + Simulator source preserved (rule A).
    QCOMPARE(controller.observedCount(), 4);
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("Simulator Mode"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("Deterministic Demo"));
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
    QCOMPARE(controller.modeLabel(), QStringLiteral("Replay Mode"));
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
    QCOMPARE(controller.modeLabel(), QStringLiteral("Replay Mode"));

    controller.runDemoBatch();
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("Simulator Mode"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("Deterministic Demo"));
    QVERIFY(!controller.hasReplayError());

    controller.loadReplayFile(QUrl::fromLocalFile(QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    QCOMPARE(controller.transactionModel()->rowCount(), 4);
    QCOMPARE(controller.modeLabel(), QStringLiteral("Replay Mode"));
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
    QCOMPARE(controller.modeLabel(), QStringLiteral("Replay Mode"));
    QCOMPARE(controller.sourceLabel(), QStringLiteral("demo_v1.mlog"));
}

} // namespace

QTEST_GUILESS_MAIN(UiBridgeTest)
#include "test_ui_bridge.moc"