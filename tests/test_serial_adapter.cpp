#include <QtTest>

#include <QObject>
#include <QSerialPort>
#include <QSignalSpy>
#include <QString>

#include <chrono>
#include <cstdint>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "ui/serial/SerialPortAdapter.h"

using modbuslens::core::ActiveFunction;
using modbuslens::core::ActiveRequestDescriptor;
using modbuslens::core::ActiveRequestIntent;
using modbuslens::core::ActiveTransactionResult;
using modbuslens::core::ReadHoldingRegistersIntent;
using modbuslens::core::TransportDisposition;
using modbuslens::core::encodeActiveRequest;

namespace {

modbuslens::core::ActiveRequestDescriptor fc03Descriptor(
    std::uint8_t unit = 0x01, std::uint16_t start = 0x0000,
    std::uint16_t quantity = 0x0002)
{
    return std::get<ActiveRequestDescriptor>(encodeActiveRequest(
        ActiveRequestIntent{
            .function = ActiveFunction::ReadHoldingRegisters,
            .unitId = unit,
            .timeout = std::chrono::milliseconds{100},
            .payload = ReadHoldingRegistersIntent{.startAddress = start,
                                                  .quantity = quantity}}));
}

} // namespace

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
    // SERIAL-I06 (M10-E4): the fatal-local-port-failure classification table.
    // A response Timeout and a not-open state are NOT removals; the real I/O /
    // device / permission errors are.
    void i06_portErrorClassification();
    // SERIAL-I07 (M10-E4, hot-unplug): a fatal LOCAL port failure is reported
    // on the bounded error lane even with NOTHING in flight. Before the
    // correction an idle removal closed the port silently and the owner kept
    // presenting a connection that no longer existed.
    void i07_idleFatalPortErrorIsReported();
    // SERIAL-I08 (M10-E4): a TimeoutError / NotOpenError must never be turned
    // into a removal — no error, no fabricated disconnect.
    void i08_silenceIsNotARemoval();
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
            [&](ActiveTransactionResult) { completed = true; });

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

    // M10-E4: the failed open already reported this failure, so a later fatal
    // port event cannot add a SECOND user-visible error for it. There is no
    // open port left, hence nothing that could be "removed".
    adapter.deliverPortErrorForTest(QSerialPort::ResourceError);
    QCoreApplication::processEvents();
    QCOMPARE(spy.count(), 1);
    QVERIFY(!adapter.isPortOpen());
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
            [&](ActiveTransactionResult) { completed = true; });

    const auto started = adapter.startActiveRequest(fc03Descriptor());

    QVERIFY(!started.accepted);
    // The pre-send refusal is a transport FACT: provably nothing was sent.
    QCOMPARE(started.disposition, TransportDisposition::NotSent);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!adapter.hasActiveTransaction());
    QVERIFY(!adapter.isPortOpen());
    QVERIFY(!completed);
}

void SerialAdapterTest::i06_portErrorClassification()
{
    using Adapter = SerialTransactionAdapter;

    // Fatal local port failures: the LOCAL port itself is gone/unusable.
    for (const auto error : {QSerialPort::ResourceError,
                             QSerialPort::DeviceNotFoundError,
                             QSerialPort::PermissionError,
                             QSerialPort::ReadError,
                             QSerialPort::WriteError,
                             QSerialPort::OpenError,
                             QSerialPort::UnsupportedOperationError,
                             QSerialPort::UnknownError}) {
        QVERIFY2(Adapter::isFatalLocalPortFailure(error),
                 qPrintable(QStringLiteral("error %1 must be fatal")
                                .arg(static_cast<int>(error))));
    }

    // NOT removals, and each for a different reason:
    //   NoError      -> not an error at all;
    //   TimeoutError -> this adapter never calls waitFor*(), and a response
    //                   timeout is decided by the session threshold callback,
    //                   so silence must never be presented as an unplug;
    //   NotOpenError -> a state statement about a closed handle.
    for (const auto error : {QSerialPort::NoError,
                             QSerialPort::TimeoutError,
                             QSerialPort::NotOpenError}) {
        QVERIFY2(!Adapter::isFatalLocalPortFailure(error),
                 qPrintable(QStringLiteral("error %1 must NOT be fatal")
                                .arg(static_cast<int>(error))));
    }
}

void SerialAdapterTest::i07_idleFatalPortErrorIsReported()
{
    // HOT-UNPLUG with nothing in flight. QSerialPort::errorOccurred cannot be
    // produced without a real device disappearing, so the exact event the Qt
    // slot receives is delivered through the same handler the signal is bound
    // to — identical classification, teardown, evidence and error lane.
    SerialTransactionAdapter adapter;
    QSignalSpy errorSpy(&adapter, &SerialTransactionAdapter::transportError);
    QSignalSpy terminalSpy(&adapter, &SerialTransactionAdapter::transactionTerminated);
    QSignalSpy completedSpy(&adapter, &SerialTransactionAdapter::transactionCompleted);

    adapter.deliverPortErrorForTest(QSerialPort::ResourceError);

    QCOMPARE(errorSpy.count(), 1);        // the removal IS user-visible
    QVERIFY(!errorSpy.at(0).at(0).toString().isEmpty());
    QCOMPARE(terminalSpy.count(), 0);     // nothing was submitted -> no terminal
    QCOMPARE(completedSpy.count(), 0);    // and above all no Modbus outcome
    QVERIFY(!adapter.isPortOpen());
    QVERIFY(!adapter.hasActiveTransaction());

    // Bounded: the same removal is never reported twice, no matter how many
    // further emissions a failed/closed port produces (T010 PE-4 guard).
    adapter.deliverPortErrorForTest(QSerialPort::ResourceError);
    QCoreApplication::processEvents();
    QCOMPARE(errorSpy.count(), 1);
}

void SerialAdapterTest::i08_silenceIsNotARemoval()
{
    // A remote slave that says nothing is NOT a disconnected adapter: neither
    // a TimeoutError nor a not-open state may manufacture a removal.
    SerialTransactionAdapter adapter;
    QSignalSpy errorSpy(&adapter, &SerialTransactionAdapter::transportError);
    QSignalSpy terminalSpy(&adapter, &SerialTransactionAdapter::transactionTerminated);

    adapter.deliverPortErrorForTest(QSerialPort::TimeoutError);
    adapter.deliverPortErrorForTest(QSerialPort::NotOpenError);
    adapter.deliverPortErrorForTest(QSerialPort::NoError);
    QCoreApplication::processEvents();

    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(terminalSpy.count(), 0);
}

QTEST_GUILESS_MAIN(SerialAdapterTest)
#include "test_serial_adapter.moc"