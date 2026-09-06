#include <QtTest>

#include <QAbstractItemModel>
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

} // namespace

QTEST_GUILESS_MAIN(UiBridgeTest)
#include "test_ui_bridge.moc"