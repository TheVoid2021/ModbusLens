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
    // SERIAL-I09 (M10-E4 real hot-unplug): the presence watch declares a
    // confirmed disappearance exactly once, then stops itself — a reinserted
    // adapter cannot revive the old session.
    void i09_presenceWatchDeclaresRemovalOnce();
    // SERIAL-I10: a single transient enumeration miss must not tear down a
    // healthy link (anti-false-positive confirmations).
    void i10_presenceWatchToleratesATransientMiss();
    // SERIAL-I11: the watch is inert until a session explicitly starts it, and
    // stop() is final.
    void i11_presenceWatchRequiresAnExplicitStart();
    // SERIAL-I12 (the real hot-unplug oracle): a confirmed disappearance travels
    // the PRODUCTION teardown — one bounded error, the port really closed, the
    // pending-request evidence untouched, and no second report from the
    // QSerialPort error door for the same physical loss.
    void i12_adapterPresenceLossTearsTheSessionDownOnce();
    // SERIAL-I13 (regression guard): while the local port is still ENUMERATED, a
    // silent remote slave produces no local-loss report at all.
    void i13_enumeratedPortIsNeverALoss();
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

// ---- M10-E4 real hot-unplug correction: LOCAL PORT PRESENCE OBSERVATION ----
//
// Real hardware showed that "given a fatal QSerialPort error the teardown is
// right" was not sufficient evidence: the shipped UI could keep presenting an
// open port, because QSerialPort::isOpen() answers about the HANDLE, not about
// whether the endpoint still exists. These tests drive the observation boundary
// itself — the injectable presence probe — so the production check and the
// production teardown are what actually run.

void SerialAdapterTest::i09_presenceWatchDeclaresRemovalOnce()
{
    LocalPortPresenceWatch watch;
    bool present = true;
    watch.setProbe([&present](const QString&) { return present; });
    watch.setInterval(10);
    watch.setConfirmations(2);
    QSignalSpy spy(&watch, &LocalPortPresenceWatch::portDisappeared);

    watch.start(QStringLiteral("COM3"));
    QVERIFY(watch.isWatching());
    QCOMPARE(watch.watchedPort(), QStringLiteral("COM3"));

    // Still enumerated: nothing to report, however long we watch.
    QTest::qWait(60);
    QCOMPARE(spy.count(), 0);
    QVERIFY(watch.isWatching());

    // The adapter is physically removed: the OS stops enumerating it.
    present = false;
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("COM3"));

    // Reported once, then the watch stops itself.
    QVERIFY(!watch.isWatching());
    QVERIFY(watch.watchedPort().isEmpty());

    // Reinserting the adapter must NOT revive anything: no second signal, and
    // nothing is watching.
    present = true;
    QTest::qWait(60);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!watch.isWatching());
}

void SerialAdapterTest::i10_presenceWatchToleratesATransientMiss()
{
    LocalPortPresenceWatch watch;
    int absentFor = 0; // number of consecutive absent answers to give
    watch.setProbe([&absentFor](const QString&) { return absentFor-- <= 0; });
    watch.setInterval(10);
    watch.setConfirmations(2);
    QSignalSpy spy(&watch, &LocalPortPresenceWatch::portDisappeared);

    watch.start(QStringLiteral("COM3"));
    // One single absent observation, then present again: a transient
    // enumeration hiccup must not tear down a healthy link.
    absentFor = 1;
    QTest::qWait(120);
    QCOMPARE(spy.count(), 0);
    QVERIFY(watch.isWatching());
    watch.stop();
}

void SerialAdapterTest::i11_presenceWatchRequiresAnExplicitStart()
{
    LocalPortPresenceWatch watch;
    watch.setProbe([](const QString&) { return false; }); // endpoint absent
    watch.setInterval(10);
    watch.setConfirmations(1);
    QSignalSpy spy(&watch, &LocalPortPresenceWatch::portDisappeared);

    // Never started: a watch is inert, even with a permanently absent probe.
    QTest::qWait(60);
    QCOMPARE(spy.count(), 0);
    QVERIFY(!watch.isWatching());

    // stop() is final: an explicitly stopped watch does not keep observing.
    watch.start(QStringLiteral("COM3"));
    watch.stop();
    QVERIFY(!watch.isWatching());
    QVERIFY(watch.watchedPort().isEmpty());
    QTest::qWait(60);
    QCOMPARE(spy.count(), 0);
}

void SerialAdapterTest::i12_adapterPresenceLossTearsTheSessionDownOnce()
{
    // The adapter OWNS the watch, and it is the production code under test: the
    // injected probe replaces only the OS observation, and everything the
    // observation triggers (loss classification, evidence capture, port close,
    // bounded error lane, exactly-once reporting) is the shipped implementation.
    SerialTransactionAdapter adapter;
    bool present = true;
    adapter.localPortPresence().setProbe(
        [&present](const QString&) { return present; });
    adapter.localPortPresence().setInterval(10);
    adapter.localPortPresence().setConfirmations(2);

    QSignalSpy errorSpy(&adapter, &SerialTransactionAdapter::transportError);
    QSignalSpy terminalSpy(&adapter, &SerialTransactionAdapter::transactionTerminated);
    QSignalSpy completedSpy(&adapter, &SerialTransactionAdapter::transactionCompleted);

    // A REAL successful open cannot be manufactured without hardware, so the
    // test enters the state an open establishes through the watch's own
    // production entry point — the same call openPort() makes on success.
    adapter.localPortPresence().start(QStringLiteral("COM3"));
    QVERIFY(adapter.localPortPresence().isWatching());

    // Local loss: the OS stops enumerating COM3. Nothing else changes.
    present = false;
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 2000);

    const QString message = errorSpy.at(0).at(0).toString();
    QVERIFY2(message.contains(QStringLiteral("已从系统移除")),
             qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("COM3")), qPrintable(message));
    QVERIFY2(message.contains(QStringLiteral("串口设备不可用")), qPrintable(message));
    // A local loss must say nothing about a remote Modbus slave.
    QVERIFY(!message.contains(QStringLiteral("从站")));
    QVERIFY(!message.contains(QStringLiteral("设备已连接")));

    QVERIFY(!adapter.isPortOpen());               // the port is really closed
    QVERIFY(!adapter.localPortPresence().isWatching()); // and no longer watched
    QVERIFY(!adapter.hasActiveTransaction());
    QCOMPARE(terminalSpy.count(), 0);   // nothing was submitted -> no terminal
    QCOMPARE(completedSpy.count(), 0);  // and above all no Modbus outcome

    // Exactly once across BOTH doors: the QSerialPort error lane arriving later
    // for the same physical loss must not add a second report.
    adapter.deliverPortErrorForTest(QSerialPort::ResourceError);
    QCoreApplication::processEvents();
    QTest::qWait(60);
    QCOMPARE(errorSpy.count(), 1);

    // Reinsert does not auto-reconnect: the adapter stays closed and silent.
    present = true;
    QTest::qWait(60);
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(!adapter.isPortOpen());
    QVERIFY(!adapter.localPortPresence().isWatching());
}

void SerialAdapterTest::i13_enumeratedPortIsNeverALoss()
{
    // The regression that matters most: "the slave did not answer" must never be
    // read as "the USB adapter was removed".
    SerialTransactionAdapter adapter;
    adapter.localPortPresence().setProbe([](const QString&) { return true; });
    adapter.localPortPresence().setInterval(10);
    adapter.localPortPresence().setConfirmations(2);
    QSignalSpy errorSpy(&adapter, &SerialTransactionAdapter::transportError);

    adapter.localPortPresence().start(QStringLiteral("COM3"));
    QTest::qWait(120);

    QCOMPARE(errorSpy.count(), 0);
    QVERIFY(adapter.localPortPresence().isWatching());
    adapter.localPortPresence().stop();
}

QTEST_GUILESS_MAIN(SerialAdapterTest)
#include "test_serial_adapter.moc"