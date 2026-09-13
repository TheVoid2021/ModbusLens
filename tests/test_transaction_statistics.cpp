#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/analysis/TransactionStatistics.h"

using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::ResponseObservation;
using modbuslens::core::RtuDecodeError;
using modbuslens::core::RtuDecodeErrorCode;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionStatisticsSnapshot;
using modbuslens::core::TransactionStatus;
using modbuslens::core::analyzeFunction03Transaction;
using modbuslens::core::summarizeTransactions;

namespace {

using ms = std::chrono::milliseconds;

TransactionAnalysis makeAnalysis(TransactionStatus status, ms elapsed)
{
    return TransactionAnalysis{
        .status = status,
        .elapsed = elapsed,
        .exceptionCode = std::nullopt,
        .issue = std::nullopt};
}

TransactionAnalysis makeException(std::uint8_t code, ms elapsed)
{
    return TransactionAnalysis{
        .status = TransactionStatus::Exception,
        .elapsed = elapsed,
        .exceptionCode = code,
        .issue = std::nullopt};
}

class TransactionStatisticsTest : public QObject
{
    Q_OBJECT

private slots:
    // STAT-B01 (P0): empty input — all zeroes, both optionals nullopt.
    void b01_emptyInput();
    // STAT-B02 (P0): all success — rate 1.0, average 20.0.
    void b02_allSuccess();
    // STAT-B03 (P0): mixed completed statuses; Timeout elapsed stays out of
    // the success latency average; 2/6 uses tolerance.
    void b03_mixedCompleted();
    // STAT-B04 (P0): Pending never enters the rate denominator (1.0, not 1/3).
    void b04_pendingExcludedFromRate();
    // STAT-B05 (P0): all pending — "no data yet", both optionals nullopt.
    void b05_allPending();
    // STAT-B06 (P0): completed but no success — rate exactly 0.0, not nullopt.
    void b06_completedButNoSuccess();
    // STAT-B07 (P0): success latency isolation — only Success elapsed averages.
    void b07_successLatencyIsolation();
    // STAT-B08 (P1): snapshot invariants A-D on a mixed batch.
    void b08_invariants();
    // STAT-B09 (P0, T014): issue payload is orthogonal — the snapshot keys
    // ONLY on the high-level status, never on issue presence/payload.
    void b09_issuePresenceDoesNotChangeSnapshot();
};

void TransactionStatisticsTest::b01_emptyInput()
{
    const auto snapshot = summarizeTransactions({});

    QCOMPARE(snapshot.observedCount, std::size_t{0});
    QCOMPARE(snapshot.pendingCount, std::size_t{0});
    QCOMPARE(snapshot.completedCount, std::size_t{0});
    QCOMPARE(snapshot.successCount, std::size_t{0});
    QCOMPARE(snapshot.exceptionCount, std::size_t{0});
    QCOMPARE(snapshot.crcErrorCount, std::size_t{0});
    QCOMPARE(snapshot.timeoutCount, std::size_t{0});
    QCOMPARE(snapshot.protocolErrorCount, std::size_t{0});
    QVERIFY(!snapshot.successRate.has_value());
    QVERIFY(!snapshot.averageSuccessLatencyMs.has_value());
}

void TransactionStatisticsTest::b02_allSuccess()
{
    const std::vector<TransactionAnalysis> batch{
        makeAnalysis(TransactionStatus::Success, ms{10}),
        makeAnalysis(TransactionStatus::Success, ms{20}),
        makeAnalysis(TransactionStatus::Success, ms{30}),
    };

    const auto snapshot = summarizeTransactions(batch);
    QCOMPARE(snapshot.observedCount, std::size_t{3});
    QCOMPARE(snapshot.pendingCount, std::size_t{0});
    QCOMPARE(snapshot.completedCount, std::size_t{3});
    QCOMPARE(snapshot.successCount, std::size_t{3});
    QVERIFY(snapshot.successRate.has_value());
    QVERIFY(qFuzzyCompare(*snapshot.successRate, 1.0));
    QVERIFY(snapshot.averageSuccessLatencyMs.has_value());
    QVERIFY(qFuzzyCompare(*snapshot.averageSuccessLatencyMs, 20.0));
}

void TransactionStatisticsTest::b03_mixedCompleted()
{
    const std::vector<TransactionAnalysis> batch{
        makeAnalysis(TransactionStatus::Success, ms{10}),
        makeAnalysis(TransactionStatus::Success, ms{30}),
        makeException(0x02, ms{20}),
        makeAnalysis(TransactionStatus::CrcError, ms{15}),
        makeAnalysis(TransactionStatus::Timeout, ms{1000}),
        makeAnalysis(TransactionStatus::ProtocolError, ms{12}),
    };

    const auto snapshot = summarizeTransactions(batch);
    QCOMPARE(snapshot.observedCount, std::size_t{6});
    QCOMPARE(snapshot.pendingCount, std::size_t{0});
    QCOMPARE(snapshot.completedCount, std::size_t{6});
    QCOMPARE(snapshot.successCount, std::size_t{2});
    QCOMPARE(snapshot.exceptionCount, std::size_t{1});
    QCOMPARE(snapshot.crcErrorCount, std::size_t{1});
    QCOMPARE(snapshot.timeoutCount, std::size_t{1});
    QCOMPARE(snapshot.protocolErrorCount, std::size_t{1});
    QVERIFY(snapshot.successRate.has_value());
    QVERIFY(qFuzzyCompare(*snapshot.successRate, 2.0 / 6.0));
    QVERIFY(snapshot.averageSuccessLatencyMs.has_value());
    QVERIFY(qFuzzyCompare(*snapshot.averageSuccessLatencyMs, 20.0));
}

void TransactionStatisticsTest::b04_pendingExcludedFromRate()
{
    const std::vector<TransactionAnalysis> batch{
        makeAnalysis(TransactionStatus::Success, ms{25}),
        makeAnalysis(TransactionStatus::Pending, ms{500}),
        makeAnalysis(TransactionStatus::Pending, ms{800}),
    };

    const auto snapshot = summarizeTransactions(batch);
    QCOMPARE(snapshot.observedCount, std::size_t{3});
    QCOMPARE(snapshot.pendingCount, std::size_t{2});
    QCOMPARE(snapshot.completedCount, std::size_t{1});
    QCOMPARE(snapshot.successCount, std::size_t{1});
    QVERIFY(snapshot.successRate.has_value());
    QVERIFY(qFuzzyCompare(*snapshot.successRate, 1.0));
    QVERIFY(snapshot.averageSuccessLatencyMs.has_value());
    QVERIFY(qFuzzyCompare(*snapshot.averageSuccessLatencyMs, 25.0));
}

void TransactionStatisticsTest::b05_allPending()
{
    const std::vector<TransactionAnalysis> batch{
        makeAnalysis(TransactionStatus::Pending, ms{100}),
        makeAnalysis(TransactionStatus::Pending, ms{200}),
    };

    const auto snapshot = summarizeTransactions(batch);
    QCOMPARE(snapshot.observedCount, std::size_t{2});
    QCOMPARE(snapshot.pendingCount, std::size_t{2});
    QCOMPARE(snapshot.completedCount, std::size_t{0});
    QVERIFY(!snapshot.successRate.has_value());
    QVERIFY(!snapshot.averageSuccessLatencyMs.has_value());
}

void TransactionStatisticsTest::b06_completedButNoSuccess()
{
    const std::vector<TransactionAnalysis> batch{
        makeException(0x02, ms{20}),
        makeAnalysis(TransactionStatus::CrcError, ms{15}),
        makeAnalysis(TransactionStatus::Timeout, ms{1000}),
        makeAnalysis(TransactionStatus::ProtocolError, ms{12}),
    };

    const auto snapshot = summarizeTransactions(batch);
    QCOMPARE(snapshot.observedCount, std::size_t{4});
    QCOMPARE(snapshot.pendingCount, std::size_t{0});
    QCOMPARE(snapshot.completedCount, std::size_t{4});
    QCOMPARE(snapshot.successCount, std::size_t{0});
    // 0/4 is exactly 0.0 — exact comparison on purpose: qFuzzyCompare is
    // unreliable against zero, and the value here is mathematically exact.
    QVERIFY(snapshot.successRate.has_value());
    QVERIFY(*snapshot.successRate == 0.0);
    QVERIFY(!snapshot.averageSuccessLatencyMs.has_value());
}

void TransactionStatisticsTest::b07_successLatencyIsolation()
{
    const std::vector<TransactionAnalysis> batch{
        makeAnalysis(TransactionStatus::Success, ms{10}),
        makeAnalysis(TransactionStatus::Success, ms{30}),
        makeException(0x02, ms{200}),
        makeAnalysis(TransactionStatus::CrcError, ms{400}),
        makeAnalysis(TransactionStatus::Timeout, ms{1000}),
        makeAnalysis(TransactionStatus::ProtocolError, ms{500}),
    };

    const auto snapshot = summarizeTransactions(batch);
    QVERIFY(snapshot.averageSuccessLatencyMs.has_value());
    QVERIFY(qFuzzyCompare(*snapshot.averageSuccessLatencyMs, 20.0));
}

void TransactionStatisticsTest::b08_invariants()
{
    const std::vector<TransactionAnalysis> batch{
        makeAnalysis(TransactionStatus::Success, ms{10}),
        makeAnalysis(TransactionStatus::Success, ms{30}),
        makeException(0x02, ms{20}),
        makeAnalysis(TransactionStatus::CrcError, ms{15}),
        makeAnalysis(TransactionStatus::Timeout, ms{1000}),
        makeAnalysis(TransactionStatus::ProtocolError, ms{12}),
        makeAnalysis(TransactionStatus::Pending, ms{700}),
    };

    const auto snapshot = summarizeTransactions(batch);

    // Invariant A: observed == pending + completed.
    QCOMPARE(snapshot.observedCount,
        snapshot.pendingCount + snapshot.completedCount);
    // Invariant B: completed == sum of the five final statuses.
    QCOMPARE(snapshot.completedCount,
        snapshot.successCount + snapshot.exceptionCount
            + snapshot.crcErrorCount + snapshot.timeoutCount
            + snapshot.protocolErrorCount);
    // Invariant C: rate defined iff completed > 0.
    QCOMPARE(snapshot.successRate.has_value(), snapshot.completedCount > 0);
    // Invariant D: latency defined iff success > 0.
    QCOMPARE(snapshot.averageSuccessLatencyMs.has_value(),
        snapshot.successCount > 0);
}

void TransactionStatisticsTest::b09_issuePresenceDoesNotChangeSnapshot()
{
    // Same batch analyzed once WITH issues (analyzer-produced, three
    // different reasons) and once WITHOUT (hand-built equivalents). The
    // hand-built row is documented as a defensive-statistics input, NOT a
    // production-analyzer-valid object: it exists to prove the summarizer
    // keys on high-level status only (T014 R-STAT).
    const ModbusRtuFrame request{
        .address = 0x01, .functionCode = 0x03, .data = {0x00, 0x00, 0x00, 0x02}};
    const ModbusRtuFrame wrongAddress{
        .address = 0x02, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    const ModbusRtuFrame wrongFunction{
        .address = 0x01, .functionCode = 0x04, .data = {0x02, 0x00, 0x64}};
    const ModbusRtuFrame quantityMismatch{
        .address = 0x01, .functionCode = 0x03, .data = {0x06, 0x00, 0x64, 0x00, 0xC8, 0x05, 0xDC}};

    std::vector<TransactionAnalysis> withIssues;
    withIssues.push_back(analyzeFunction03Transaction(
        request, ResponseObservation{wrongAddress}, ms{25}, ms{1000}));
    withIssues.push_back(analyzeFunction03Transaction(
        request, ResponseObservation{wrongFunction}, ms{25}, ms{1000}));
    withIssues.push_back(analyzeFunction03Transaction(
        request, ResponseObservation{RtuDecodeError{RtuDecodeErrorCode::FrameTooShort}},
        ms{40}, ms{1000}));
    withIssues.push_back(analyzeFunction03Transaction(
        request, ResponseObservation{quantityMismatch}, ms{25}, ms{1000}));

    std::vector<TransactionAnalysis> withoutIssues;
    for (const auto& analysis : withIssues) {
        withoutIssues.push_back(TransactionAnalysis{
            .status = analysis.status,
            .elapsed = analysis.elapsed,
            .exceptionCode = analysis.exceptionCode,
            .issue = std::nullopt,
        });
    }

    const auto withSnapshot = summarizeTransactions(withIssues);
    const auto withoutSnapshot = summarizeTransactions(withoutIssues);

    QCOMPARE(withSnapshot.protocolErrorCount, std::size_t{4});
    QCOMPARE(withSnapshot.observedCount, withoutSnapshot.observedCount);
    QCOMPARE(withSnapshot.pendingCount, withoutSnapshot.pendingCount);
    QCOMPARE(withSnapshot.completedCount, withoutSnapshot.completedCount);
    QCOMPARE(withSnapshot.successCount, withoutSnapshot.successCount);
    QCOMPARE(withSnapshot.exceptionCount, withoutSnapshot.exceptionCount);
    QCOMPARE(withSnapshot.crcErrorCount, withoutSnapshot.crcErrorCount);
    QCOMPARE(withSnapshot.timeoutCount, withoutSnapshot.timeoutCount);
    QCOMPARE(withSnapshot.protocolErrorCount, withoutSnapshot.protocolErrorCount);
    QCOMPARE(withSnapshot.successRate.has_value(),
             withoutSnapshot.successRate.has_value());
    QCOMPARE(withSnapshot.averageSuccessLatencyMs.has_value(),
             withoutSnapshot.averageSuccessLatencyMs.has_value());
}

} // namespace

QTEST_MAIN(TransactionStatisticsTest)
#include "test_transaction_statistics.moc"