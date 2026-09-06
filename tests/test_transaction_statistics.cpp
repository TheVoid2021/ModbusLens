#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/analysis/TransactionStatistics.h"

using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionStatisticsSnapshot;
using modbuslens::core::TransactionStatus;
using modbuslens::core::summarizeTransactions;

namespace {

using ms = std::chrono::milliseconds;

TransactionAnalysis makeAnalysis(TransactionStatus status, ms elapsed)
{
    return TransactionAnalysis{
        .status = status,
        .elapsed = elapsed,
        .exceptionCode = std::nullopt};
}

TransactionAnalysis makeException(std::uint8_t code, ms elapsed)
{
    return TransactionAnalysis{
        .status = TransactionStatus::Exception,
        .elapsed = elapsed,
        .exceptionCode = code};
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

} // namespace

QTEST_MAIN(TransactionStatisticsTest)
#include "test_transaction_statistics.moc"