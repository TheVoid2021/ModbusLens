#include <QtTest>

#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/protocol/ModbusRtuCodec.h"
#include "core/replay/ReplayAnalysis.h"
#include "core/replay/ReplayLog.h"

using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::ReplayAnalysisResult;
using modbuslens::core::ReplayBatchAnalysis;
using modbuslens::core::ReplayExecutionError;
using modbuslens::core::ReplayExecutionErrorCode;
using modbuslens::core::ReplayLog;
using modbuslens::core::ReplayParseError;
using modbuslens::core::ReplayTransactionRecord;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionStatus;
using modbuslens::core::analyzeReplayLog;
using modbuslens::core::encodeRtuFrame;
using modbuslens::core::parseReplayLog;

namespace {

using ms = std::chrono::milliseconds;

// Copy semantics on purpose — see docs/issues/ISSUE-001.
template <typename T, typename Variant>
std::optional<T> as(const Variant& result)
{
    if (auto* value = std::get_if<T>(&result)) {
        return *value;
    }
    return std::nullopt;
}

std::vector<std::uint8_t> wire(std::initializer_list<int> bytes)
{
    std::vector<std::uint8_t> result;
    result.reserve(bytes.size());
    for (const int b : bytes) {
        result.push_back(static_cast<std::uint8_t>(b));
    }
    return result;
}

// The committed golden fixture, copied to the build tree by CMake
// (configure_file COPYONLY); only this TEST target sees the path.
std::string readGoldenFixture()
{
    std::ifstream input{MODBUSLENS_DEMO_MLOG_PATH, std::ios::binary};
    if (!input.good()) {
        return {};
    }
    return std::string{std::istreambuf_iterator<char>{input}, {}};
}

ReplayLog singleRecordLog(std::vector<std::uint8_t> request,
                          std::optional<std::vector<std::uint8_t>> response,
                          long long elapsed)
{
    ReplayLog log;
    log.timeoutThreshold = ms{1000};
    ReplayTransactionRecord record;
    record.elapsed = ms{elapsed};
    record.requestWire = std::move(request);
    record.responseWire = std::move(response);
    log.transactions.push_back(std::move(record));
    return log;
}

// Valid read-2-registers request for device 1 (CRC verified in T009 design).
const auto kGoodRequest = wire({0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B});
// Same payload but CRC byte flipped -> decode must report CrcMismatch.
const auto kBadCrcRequest = wire({0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0A});
// quantity = 0 with a CORRECT CRC (0xCA45 -> wire 45 CA): frame + function
// both valid, 0x03 semantic validation fails.
const auto kZeroQuantityRequest = wire({0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x45, 0xCA});
// CRC-corrupted response (last CRC byte should be 7A, is 7B).
const auto kBadCrcResponse = wire({0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7B});

} // namespace

class ReplayAnalysisTest : public QObject
{
    Q_OBJECT

private slots:
    // REPLAY-I01 (P0): golden fixture -> 4 outcomes in fixed order + the
    // exact statistics T008's demo dashboard produced.
    void i01_goldenReplay();
    // REPLAY-I02 (P1): analyzing the same log twice yields identical batches.
    void i02_determinism();
    // REPLAY-I03 (P0): bad request CRC -> InvalidRequestWire + 0-based index.
    void i03_invalidRequestWire();
    // REPLAY-I03B (P0): quantity=0 with valid CRC -> InvalidRequestData.
    void i03b_invalidRequestData();
    // InvalidRequestFunction: frame valid but function code != 0x03.
    void i03c_invalidRequestFunction();
    // REPLAY-I04 (P0): bad response CRC -> batch still succeeds, CrcError.
    void i04_badResponseCrc();
    // REPLAY-I05 (P1): response with mismatched device address ->
    // ProtocolError, not a replay failure.
    void i05_protocolError();
};

void ReplayAnalysisTest::i01_goldenReplay()
{
    const auto parsed = parseReplayLog(readGoldenFixture());
    const auto log = as<ReplayLog>(parsed);
    QVERIFY(log.has_value());

    const auto analyzed = analyzeReplayLog(*log);
    const auto batch = as<ReplayBatchAnalysis>(analyzed);
    QVERIFY(batch.has_value());
    QVERIFY(!as<ReplayExecutionError>(analyzed).has_value());

    QCOMPARE(batch->transactions.size(), std::size_t{4});

    const auto statusOf = [&](std::size_t i) {
        return batch->transactions[i].analysis.status;
    };
    QCOMPARE(statusOf(0), TransactionStatus::Success);
    QCOMPARE(statusOf(1), TransactionStatus::Exception);
    QCOMPARE(statusOf(2), TransactionStatus::CrcError);
    QCOMPARE(statusOf(3), TransactionStatus::Timeout);

    for (const auto& outcome : batch->transactions) {
        QCOMPARE(outcome.deviceAddress, std::uint8_t{0x01});
        QCOMPARE(outcome.functionCode, std::uint8_t{0x03});
    }

    // Success row keeps its historical elapsed (25 ms) and the exception row
    // carries the numeric exception code 0x02.
    QCOMPARE(batch->transactions[0].analysis.elapsed, ms{25});
    QVERIFY(batch->transactions[1].analysis.exceptionCode.has_value());
    QCOMPARE(*batch->transactions[1].analysis.exceptionCode, std::uint8_t{0x02});

    const auto& stats = batch->statistics;
    QCOMPARE(stats.observedCount, std::size_t{4});
    QCOMPARE(stats.completedCount, std::size_t{4});
    QCOMPARE(stats.pendingCount, std::size_t{0});

    QCOMPARE(stats.successCount, std::size_t{1});
    QCOMPARE(stats.exceptionCount, std::size_t{1});
    QCOMPARE(stats.crcErrorCount, std::size_t{1});
    QCOMPARE(stats.timeoutCount, std::size_t{1});
    QCOMPARE(stats.protocolErrorCount, std::size_t{0});

    QVERIFY(stats.successRate.has_value());
    QCOMPARE(*stats.successRate, 0.25);
    QVERIFY(stats.averageSuccessLatencyMs.has_value());
    QCOMPARE(*stats.averageSuccessLatencyMs, 25.0);
}

void ReplayAnalysisTest::i02_determinism()
{
    const auto parsed = parseReplayLog(readGoldenFixture());
    const auto log = as<ReplayLog>(parsed);
    QVERIFY(log.has_value());

    const auto first = as<ReplayBatchAnalysis>(analyzeReplayLog(*log));
    const auto second = as<ReplayBatchAnalysis>(analyzeReplayLog(*log));
    QVERIFY(first.has_value());
    QVERIFY(second.has_value());
    QVERIFY(*first == *second);
}

void ReplayAnalysisTest::i03_invalidRequestWire()
{
    // Bad request is the FIRST record -> transactionIndex 0 (0-based).
    {
        const auto result = analyzeReplayLog(
            singleRecordLog(kBadCrcRequest, std::nullopt, 25));
        const auto error = as<ReplayExecutionError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayExecutionErrorCode::InvalidRequestWire);
        QCOMPARE(error->transactionIndex, std::size_t{0});
    }
    // A good first record must NOT mask the bad second one -> index 1.
    {
        ReplayLog log = singleRecordLog(
            kGoodRequest, wire({0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7A}), 25);
        ReplayTransactionRecord bad;
        bad.elapsed = ms{25};
        bad.requestWire = kBadCrcRequest;
        bad.responseWire = std::nullopt;
        log.transactions.push_back(std::move(bad));

        const auto result = analyzeReplayLog(log);
        const auto error = as<ReplayExecutionError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayExecutionErrorCode::InvalidRequestWire);
        QCOMPARE(error->transactionIndex, std::size_t{1});
    }
}

void ReplayAnalysisTest::i03b_invalidRequestData()
{
    // 01 03 00 00 00 00 45 CA: CRC valid, function 0x03, quantity 0.
    const auto result = analyzeReplayLog(
        singleRecordLog(kZeroQuantityRequest, std::nullopt, 25));
    const auto error = as<ReplayExecutionError>(result);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ReplayExecutionErrorCode::InvalidRequestData);
    QCOMPARE(error->transactionIndex, std::size_t{0});
}

void ReplayAnalysisTest::i03c_invalidRequestFunction()
{
    // Frame-level valid write-single-register request (0x06) built with the
    // codec so its CRC is correct — only the function code is unsupported.
    const ModbusRtuFrame writeReg{
        .address = 0x01,
        .functionCode = 0x06,
        .data = {0x00, 0x01, 0x00, 0x2A},
    };
    const auto requestWire = encodeRtuFrame(writeReg);

    const auto result = analyzeReplayLog(
        singleRecordLog(requestWire, std::nullopt, 25));
    const auto error = as<ReplayExecutionError>(result);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ReplayExecutionErrorCode::InvalidRequestFunction);
    QCOMPARE(error->transactionIndex, std::size_t{0});
}

void ReplayAnalysisTest::i04_badResponseCrc()
{
    const auto result = analyzeReplayLog(
        singleRecordLog(kGoodRequest, kBadCrcResponse, 17));
    const auto batch = as<ReplayBatchAnalysis>(result);
    // The replay itself must NOT fail: the bad CRC is the diagnosed fact.
    QVERIFY(batch.has_value());
    QVERIFY(!as<ReplayExecutionError>(result).has_value());

    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::CrcError);
    QCOMPARE(batch->statistics.crcErrorCount, std::size_t{1});
    QCOMPARE(batch->statistics.successCount, std::size_t{0});
}

void ReplayAnalysisTest::i05_protocolError()
{
    // Response with a DIFFERENT device address, CRC correct (codec-built).
    const ModbusRtuFrame wrongDevice{
        .address = 0x02,
        .functionCode = 0x03,
        .data = {0x04, 0x00, 0x64, 0x00, 0xC8},
    };
    const auto badBySemantics = encodeRtuFrame(wrongDevice);

    const auto result = analyzeReplayLog(
        singleRecordLog(kGoodRequest, badBySemantics, 30));
    const auto batch = as<ReplayBatchAnalysis>(result);
    QVERIFY(batch.has_value());
    QVERIFY(!as<ReplayExecutionError>(result).has_value());

    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::ProtocolError);
    QCOMPARE(batch->statistics.protocolErrorCount, std::size_t{1});
}

QTEST_MAIN(ReplayAnalysisTest)
#include "test_replay_analysis.moc"