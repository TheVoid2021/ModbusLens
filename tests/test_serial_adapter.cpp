#include <QtTest>

#include <QObject>
#include <QString>

#include <chrono>

#include "ui/serial/SerialPortAdapter.h"

using modbuslens::core::TransactionAnalysis;

// Adapter wiring tests WITHOUT real hardware (T010 design SERIAL-I01 + I02):
// the heavy transaction logic is covered by the Pure session tests; here we
// only prove the Qt layer opens/set-errors correctly, never crashes and
// never fabricates a Modbus TransactionStatus out of a transport failure.
class SerialAdapterTest : public QObject
{
    Q_OBJECT

private slots:
    // SERIAL-I01 (P1): opening a definitely-nonexistent port yields a
    // transport error — no crash, no Timeout/pending transaction.
    void i01_invalidPortOpen();
    // SERIAL-I02 (wiring smoke): fresh adapter is idle and closed.
    void i02_freshAdapterIdle();
};

void SerialAdapterTest::i01_invalidPortOpen()
{
    SerialTransactionAdapter adapter;
    QString message;
    bool errorSignaled = false;
    bool completed = false;
    connect(&adapter, &SerialTransactionAdapter::transportError, this,
            [&](const QString& m) {
                message = m;
                errorSignaled = true;
            });
    connect(&adapter, &SerialTransactionAdapter::transactionCompleted, this,
            [&](TransactionAnalysis) { completed = true; });

    const bool started = adapter.startTransaction(
        QStringLiteral("MODBUSLENS_TEST_NONEXISTENT_PORT"), 9600,
        /*slave*/ 0x01, /*start*/ 0x0000, /*quantity*/ 0x0002,
        std::chrono::milliseconds{100});

    QVERIFY(!started);
    QVERIFY(errorSignaled);
    QVERIFY(!message.isEmpty());
    QVERIFY(!adapter.hasActiveTransaction());
    QVERIFY(!adapter.isPortOpen());
    QVERIFY(!completed);
}

void SerialAdapterTest::i02_freshAdapterIdle()
{
    SerialTransactionAdapter adapter;
    QVERIFY(!adapter.hasActiveTransaction());
    QVERIFY(!adapter.isPortOpen());
    adapter.closePort(); // idempotent, no crash
    QVERIFY(!adapter.hasActiveTransaction());
}

QTEST_GUILESS_MAIN(SerialAdapterTest)
#include "test_serial_adapter.moc"