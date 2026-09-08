#include <QtTest>

#include <QObject>
#include <QSerialPort>
#include <QSignalSpy>
#include <QString>

#include <chrono>

#include "ui/serial/SerialPortAdapter.h"

using modbuslens::core::TransactionAnalysis;

// Adapter wiring tests WITHOUT real hardware (T010 Part B: SERIAL-I01~I05):
// the heavy transaction logic is covered by the Pure session tests; here we
// prove the Qt layer opens/set-errors correctly, keeps failed-open errors
// BOUNDED (PE-4 regression), stays silent on intentional close, never
// crashes and never fabricates a Modbus TransactionStatus out of a transport
// failure.
class SerialAdapterTest : public QObject
{
    Q_OBJECT

private slots:
    // SERIAL-I01 (P1): opening a definitely-nonexistent port yields a
    // transport error — no crash, no Timeout/pending transaction.
    void i01_invalidPortOpen();
    // SERIAL-I02 (P0, PE-4 regression): the failed-open transportError is
    // BOUNDED — exactly one user-visible emission after the event loop runs
    // (the fixed errorOccurred storm used to crash the app).
    void i02_failedOpenErrorIsBounded();
    // SERIAL-I03 (P1): intentional close is silent — no transport error,
    // no crash (closing a never-opened port is the stable hardware-free seam;
    // a truly-opened port cannot be manufactured without hardware).
    void i03_intentionalCloseIsSilent();
    // SERIAL-I04 (wiring smoke): fresh adapter is idle and closed.
    void i04_freshAdapterIdle();
    // SERIAL-I05 (P1): startTransaction without an open port fails with a
    // single transport error and never creates a transaction.
    void i05_startWithoutOpenPort();
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

    const bool opened = adapter.openPort(
        QStringLiteral("MODBUSLENS_TEST_NONEXISTENT_PORT"), 9600);

    QVERIFY(!opened);
    QVERIFY(errorSignaled);
    QVERIFY(!message.isEmpty());
    QVERIFY(!adapter.hasActiveTransaction());
    QVERIFY(!adapter.isPortOpen());
    QVERIFY(!completed);
}

void SerialAdapterTest::i02_failedOpenErrorIsBounded()
{
    SerialTransactionAdapter adapter;
    QSignalSpy spy(&adapter, &SerialTransactionAdapter::transportError);

    const bool opened = adapter.openPort(
        QStringLiteral("MODBUSLENS_TEST_NONEXISTENT_PORT"), 9600);
    QVERIFY(!opened);

    // Let every queued errorOccurred emission (the old PE-4 storm) drain.
    QCoreApplication::processEvents();
    QTest::qWait(100);
    QCoreApplication::processEvents();

    QCOMPARE(spy.count(), 1);           // exactly one user-visible error
    QVERIFY(!adapter.isPortOpen());
    QVERIFY(!adapter.hasActiveTransaction());
}

void SerialAdapterTest::i03_intentionalCloseIsSilent()
{
    SerialTransactionAdapter adapter;
    QSignalSpy spy(&adapter, &SerialTransactionAdapter::transportError);

    adapter.closePort(); // never-opened port: must stay completely silent
    QCoreApplication::processEvents();
    QTest::qWait(100);
    QCoreApplication::processEvents();

    QCOMPARE(spy.count(), 0);
    QVERIFY(!adapter.isPortOpen());
    QVERIFY(!adapter.hasActiveTransaction());

    // Even after a failed open, a close must not add another user-visible
    // error beyond the single open failure.
    adapter.openPort(QStringLiteral("MODBUSLENS_TEST_NONEXISTENT_PORT"), 9600);
    adapter.closePort();
    QCoreApplication::processEvents();
    QTest::qWait(100);
    QCoreApplication::processEvents();
    QCOMPARE(spy.count(), 1);
}

void SerialAdapterTest::i04_freshAdapterIdle()
{
    SerialTransactionAdapter adapter;
    QVERIFY(!adapter.hasActiveTransaction());
    QVERIFY(!adapter.isPortOpen());
}

void SerialAdapterTest::i05_startWithoutOpenPort()
{
    SerialTransactionAdapter adapter;
    QSignalSpy spy(&adapter, &SerialTransactionAdapter::transportError);
    bool completed = false;
    connect(&adapter, &SerialTransactionAdapter::transactionCompleted, this,
            [&](TransactionAnalysis) { completed = true; });

    const bool started = adapter.startTransaction(
        /*slave*/ 0x01, /*start*/ 0x0000, /*quantity*/ 0x0002,
        std::chrono::milliseconds{100});

    QVERIFY(!started);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!adapter.hasActiveTransaction());
    QVERIFY(!adapter.isPortOpen());
    QVERIFY(!completed);
}

QTEST_GUILESS_MAIN(SerialAdapterTest)
#include "test_serial_adapter.moc"