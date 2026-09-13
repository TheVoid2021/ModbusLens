#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/protocol/Function06.h"
#include "core/replay/ReplayAnalysis.h"

using modbuslens::core::Function06DecodeError;
using modbuslens::core::Function06DecodeErrorCode;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::ReplayBatchAnalysis;
using modbuslens::core::ReplayExecutionError;
using modbuslens::core::ReplayLog;
using modbuslens::core::ReplayTransactionRecord;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionRequestIssue;
using modbuslens::core::TransactionRequestIssueCode;
using modbuslens::core::TransactionStatus;
using modbuslens::core::UnsupportedObservedTransaction;
using modbuslens::core::UnsupportedSemanticsReason;
using modbuslens::core::WriteSingleRegisterRequest;
using modbuslens::core::WriteSingleRegisterResponse;
using modbuslens::core::analyzeReplayLog;
using modbuslens::core::decodeWriteSingleRegisterRequest;
using modbuslens::core::decodeWriteSingleRegisterResponse;
using modbuslens::core::encodeRtuFrame;

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

ReplayTransactionRecord recordOf(const ModbusRtuFrame& request,
                                 std::optional<ModbusRtuFrame> response,
                                 long long elapsedMs)
{
    ReplayTransactionRecord record;
    record.elapsed = ms{elapsedMs};
    record.requestWire = encodeRtuFrame(request);
    if (response.has_value()) {
        record.responseWire = encodeRtuFrame(*response);
    } else {
        record.responseWire = std::nullopt;
    }
    return record;
}

ReplayTransactionRecord recordFromWire(std::vector<std::uint8_t> requestWire,
                                       std::optional<std::vector<std::uint8_t>> responseWire,
                                       long long elapsedMs)
{
    ReplayTransactionRecord record;
    record.elapsed = ms{elapsedMs};
    record.requestWire = std::move(requestWire);
    record.responseWire = std::move(responseWire);
    return record;
}

ReplayLog logOf(std::vector<ReplayTransactionRecord> records)
{
    ReplayLog log;
    log.timeoutThreshold = ms{1000};
    log.transactions = std::move(records);
    return log;
}

std::optional<ReplayBatchAnalysis> analyzeBatch(const ReplayLog& log)
{
    return as<ReplayBatchAnalysis>(analyzeReplayLog(log));
}

// Shared fixtures.
const ModbusRtuFrame kFc03Read2{
    .address = 0x01, .functionCode = 0x03, .data = {0x00, 0x00, 0x00, 0x02}};
const ModbusRtuFrame kFc03Normal2{
    .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
const ModbusRtuFrame kFc06Write{
    .address = 0x01, .functionCode = 0x06, .data = {0x00, 0x0A, 0x00, 0x64}};
const ModbusRtuFrame kFc08Request{
    .address = 0x01, .functionCode = 0x08, .data = {0x00, 0x00, 0x00, 0x00}};
const ModbusRtuFrame kFc03Quantity126{
    .address = 0x01, .functionCode = 0x03, .data = {0x00, 0x00, 0x00, 0x7E}};

} // namespace

class PassiveAnalysisTest : public QObject
{
    Q_OBJECT

private slots:
    // F06 unit decoders (official spec backfilled in 03_MODBUS_LEARNING §4.5).
    void f06_requestDecode();
    void f06_requestWrongLength();
    void f06_responseDecode();
    void f06_wrongFunction();

    // T015 Gate F: FC03 golden behavior is the unchanged regression anchor.
    void p01_fc03GoldenUnchanged();

    // Passive multi-function expansion.
    void p02_fc06NormalUnicastSuccess();
    void p03_fc06EchoMismatch();
    void p04_genericExceptionFc08();
    void p05_invalidFc03QuantityWithException();
    void p06_invalidRecordDoesNotPoisonBatch();
    void p07_unsupportedNormalFunction();
    void p08_unicastNoResponseStaysTimeout();
    void p09_fc06BroadcastExpectedNoResponse();
    void p10_broadcastUnexpectedResponse();
    void p11_addressZeroFc03IsNotBroadcast();
    void p12_fc06BadResponseCrc();
    void p13_fc06WrongResponseAddress();
    void p14_mixedBatchStatistics();
    void p15_deterministicRepeat();
};

void PassiveAnalysisTest::f06_requestDecode()
{
    const auto result = decodeWriteSingleRegisterRequest(kFc06Write);
    const auto request = as<WriteSingleRegisterRequest>(result);
    QVERIFY(request.has_value());
    QCOMPARE(request->registerAddress, std::uint16_t{0x000A});
    QCOMPARE(request->registerValue, std::uint16_t{0x0064});
}

void PassiveAnalysisTest::f06_requestWrongLength()
{
    const ModbusRtuFrame shortWrite{
        .address = 0x01, .functionCode = 0x06, .data = {0x00, 0x0A}};
    const auto error = as<Function06DecodeError>(
        decodeWriteSingleRegisterRequest(shortWrite));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, Function06DecodeErrorCode::InvalidRequestLength);
}

void PassiveAnalysisTest::f06_responseDecode()
{
    const auto result = decodeWriteSingleRegisterResponse(kFc06Write);
    const auto response = as<WriteSingleRegisterResponse>(result);
    QVERIFY(response.has_value());
    QCOMPARE(response->registerAddress, std::uint16_t{0x000A});
    QCOMPARE(response->registerValue, std::uint16_t{0x0064});
}

void PassiveAnalysisTest::f06_wrongFunction()
{
    const auto error = as<Function06DecodeError>(
        decodeWriteSingleRegisterRequest(kFc03Read2));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, Function06DecodeErrorCode::WrongFunctionCode);
}

void PassiveAnalysisTest::p01_fc03GoldenUnchanged()
{
    const ModbusRtuFrame exceptionResponse{
        .address = 0x01, .functionCode = 0x83, .data = {0x02}};
    auto corrupted = encodeRtuFrame(kFc03Normal2);
    corrupted.back() ^= 0x01; // deliberate CRC damage

    const auto batch = analyzeBatch(logOf({
        recordOf(kFc03Read2, kFc03Normal2, 25),
        recordOf(kFc03Read2, exceptionResponse, 18),
        recordFromWire(encodeRtuFrame(kFc03Read2), corrupted, 17),
        recordFromWire(encodeRtuFrame(kFc03Read2), std::nullopt, 1000),
    }));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{4});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::Success);
    QCOMPARE(batch->transactions[1].analysis.status, TransactionStatus::Exception);
    QVERIFY(batch->transactions[1].analysis.exceptionCode.has_value());
    QCOMPARE(*batch->transactions[1].analysis.exceptionCode, std::uint8_t{0x02});
    QCOMPARE(batch->transactions[2].analysis.status, TransactionStatus::CrcError);
    QCOMPARE(batch->transactions[3].analysis.status, TransactionStatus::Timeout);

    QCOMPARE(batch->statistics.observedCount, std::size_t{4});
    QCOMPARE(batch->statistics.pendingCount, std::size_t{0});
    QCOMPARE(batch->statistics.completedCount, std::size_t{4});
    QCOMPARE(batch->statistics.successCount, std::size_t{1});
    QCOMPARE(batch->statistics.exceptionCount, std::size_t{1});
    QCOMPARE(batch->statistics.crcErrorCount, std::size_t{1});
    QCOMPARE(batch->statistics.timeoutCount, std::size_t{1});
    QCOMPARE(batch->statistics.protocolErrorCount, std::size_t{0});
    QCOMPARE(batch->statistics.expectedNoResponseCount, std::size_t{0});
    QVERIFY(batch->statistics.successRate.has_value());
    QCOMPARE(*batch->statistics.successRate, 0.25);
    QVERIFY(batch->statistics.averageSuccessLatencyMs.has_value());
    QCOMPARE(*batch->statistics.averageSuccessLatencyMs, 25.0);
    QVERIFY(batch->unsupportedRecords.empty());
}

void PassiveAnalysisTest::p02_fc06NormalUnicastSuccess()
{
    const auto batch = analyzeBatch(logOf({recordOf(kFc06Write, kFc06Write, 19)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].functionCode, std::uint8_t{0x06});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::Success);
    QVERIFY(!batch->transactions[0].requestIssue.has_value());
    QCOMPARE(batch->statistics.successCount, std::size_t{1});
}

void PassiveAnalysisTest::p03_fc06EchoMismatch()
{
    // Same shape, same length, but the echoed VALUE differs from the request.
    const ModbusRtuFrame wrongEcho{
        .address = 0x01, .functionCode = 0x06, .data = {0x00, 0x0A, 0x00, 0x65}};
    const auto batch = analyzeBatch(logOf({recordOf(kFc06Write, wrongEcho, 19)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& analysis = batch->transactions[0].analysis;
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code,
             modbuslens::core::TransactionIssueCode::WriteSingleRegisterEchoMismatch);
    QVERIFY(analysis.issue->expectedRegisterAddress.has_value());
    QCOMPARE(*analysis.issue->expectedRegisterAddress, std::uint16_t{0x000A});
    QCOMPARE(*analysis.issue->actualRegisterAddress, std::uint16_t{0x000A});
    QVERIFY(analysis.issue->expectedRegisterValue.has_value());
    QCOMPARE(*analysis.issue->expectedRegisterValue, std::uint16_t{0x0064});
    QCOMPARE(*analysis.issue->actualRegisterValue, std::uint16_t{0x0065});
}

void PassiveAnalysisTest::p04_genericExceptionFc08()
{
    const ModbusRtuFrame fc08Exception{
        .address = 0x01, .functionCode = 0x88, .data = {0x01}};
    const auto batch =
        analyzeBatch(logOf({recordOf(kFc08Request, fc08Exception, 14)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& analysis = batch->transactions[0].analysis;
    QCOMPARE(analysis.status, TransactionStatus::Exception);
    QVERIFY(analysis.exceptionCode.has_value());
    QCOMPARE(*analysis.exceptionCode, std::uint8_t{0x01});
    QCOMPARE(batch->transactions[0].functionCode, std::uint8_t{0x08});
    QVERIFY(!batch->transactions[0].requestIssue.has_value());
}

void PassiveAnalysisTest::p05_invalidFc03QuantityWithException()
{
    const ModbusRtuFrame exceptionResponse{
        .address = 0x01, .functionCode = 0x83, .data = {0x03}};
    const auto batch = analyzeBatch(
        logOf({recordOf(kFc03Quantity126, exceptionResponse, 16)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& outcome = batch->transactions[0];
    QCOMPARE(outcome.analysis.status, TransactionStatus::Exception);
    QVERIFY(outcome.analysis.exceptionCode.has_value());
    QCOMPARE(*outcome.analysis.exceptionCode, std::uint8_t{0x03});
    QVERIFY(outcome.requestIssue.has_value());
    QCOMPARE(outcome.requestIssue->code,
             TransactionRequestIssueCode::InvalidRequestQuantity);
    QVERIFY(outcome.requestIssue->observedQuantity.has_value());
    QCOMPARE(*outcome.requestIssue->observedQuantity, std::uint16_t{126});
    QVERIFY(outcome.requestIssue->maxAllowedQuantity.has_value());
    QCOMPARE(*outcome.requestIssue->maxAllowedQuantity, std::uint16_t{125});
    // The request anomaly must NOT pollute the protocol-error counter.
    QCOMPARE(batch->statistics.exceptionCount, std::size_t{1});
    QCOMPARE(batch->statistics.protocolErrorCount, std::size_t{0});
}

void PassiveAnalysisTest::p06_invalidRecordDoesNotPoisonBatch()
{
    const ModbusRtuFrame exceptionResponse{
        .address = 0x01, .functionCode = 0x83, .data = {0x03}};
    const auto batch = analyzeBatch(logOf({
        recordOf(kFc03Quantity126, exceptionResponse, 16),
        recordOf(kFc03Read2, kFc03Normal2, 25),
    }));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{2});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::Exception);
    QCOMPARE(batch->transactions[1].analysis.status, TransactionStatus::Success);
    QCOMPARE(batch->statistics.observedCount, std::size_t{2});
    QCOMPARE(batch->statistics.successCount, std::size_t{1});
    QCOMPARE(batch->statistics.exceptionCount, std::size_t{1});
}

void PassiveAnalysisTest::p07_unsupportedNormalFunction()
{
    // FC08 request + a normal-shaped 0x08 reply: ModbusLens has no FC08
    // normal semantics, so this is an explicit per-record "unsupported"
    // observation — NOT an invalid request and NOT a ProtocolError.
    const ModbusRtuFrame fc08Normal{
        .address = 0x01, .functionCode = 0x08, .data = {0x00, 0x00}};
    const auto batch =
        analyzeBatch(logOf({recordOf(kFc08Request, fc08Normal, 21)}));
    QVERIFY(batch.has_value());
    QVERIFY(batch->transactions.empty());
    QCOMPARE(batch->unsupportedRecords.size(), std::size_t{1});
    const auto& unsupported = batch->unsupportedRecords[0];
    QCOMPARE(unsupported.deviceAddress, std::uint8_t{0x01});
    QCOMPARE(unsupported.functionCode, std::uint8_t{0x08});
    QCOMPARE(unsupported.reason,
             UnsupportedSemanticsReason::UnsupportedNormalFunctionSemantics);
    // Unsupported records never enter the statistics pool.
    QCOMPARE(batch->statistics.observedCount, std::size_t{0});
}

void PassiveAnalysisTest::p08_unicastNoResponseStaysTimeout()
{
    const auto batch = analyzeBatch(
        logOf({recordFromWire(encodeRtuFrame(kFc03Read2), std::nullopt, 1000)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::Timeout);
    QCOMPARE(batch->statistics.timeoutCount, std::size_t{1});
}

void PassiveAnalysisTest::p09_fc06BroadcastExpectedNoResponse()
{
    const ModbusRtuFrame broadcastWrite{
        .address = 0x00, .functionCode = 0x06, .data = {0x00, 0x01, 0x00, 0x01}};
    const auto batch = analyzeBatch(
        logOf({recordFromWire(encodeRtuFrame(broadcastWrite), std::nullopt, 0)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status,
             TransactionStatus::ExpectedNoResponse);
    QVERIFY(!batch->transactions[0].requestIssue.has_value());
    QCOMPARE(batch->statistics.expectedNoResponseCount, std::size_t{1});
    QCOMPARE(batch->statistics.completedCount, std::size_t{1});
    QCOMPARE(batch->statistics.pendingCount, std::size_t{0});
}

void PassiveAnalysisTest::p10_broadcastUnexpectedResponse()
{
    const ModbusRtuFrame broadcastWrite{
        .address = 0x00, .functionCode = 0x06, .data = {0x00, 0x01, 0x00, 0x01}};
    // Even a perfect-looking echo is a protocol violation for a broadcast.
    const ModbusRtuFrame echo{
        .address = 0x00, .functionCode = 0x06, .data = {0x00, 0x01, 0x00, 0x01}};
    const auto batch =
        analyzeBatch(logOf({recordOf(broadcastWrite, echo, 12)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& analysis = batch->transactions[0].analysis;
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code,
             modbuslens::core::TransactionIssueCode::UnexpectedResponseForBroadcast);
}

void PassiveAnalysisTest::p11_addressZeroFc03IsNotBroadcast()
{
    // FC03 is a read: address 0 is NOT a valid broadcast — the request is
    // recorded as a request fact, and the transaction keeps normal unicast
    // no-response semantics (Pending here: elapsed < threshold).
    const ModbusRtuFrame addressZeroRead{
        .address = 0x00, .functionCode = 0x03, .data = {0x00, 0x00, 0x00, 0x01}};
    const auto batch = analyzeBatch(
        logOf({recordFromWire(encodeRtuFrame(addressZeroRead), std::nullopt, 5)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& outcome = batch->transactions[0];
    QCOMPARE(outcome.analysis.status, TransactionStatus::Pending);
    QVERIFY(outcome.analysis.status != TransactionStatus::ExpectedNoResponse);
    QVERIFY(outcome.requestIssue.has_value());
    QCOMPARE(outcome.requestIssue->code,
             TransactionRequestIssueCode::InvalidBroadcastFunction);
}

void PassiveAnalysisTest::p12_fc06BadResponseCrc()
{
    auto corrupted = encodeRtuFrame(kFc06Write);
    corrupted.back() ^= 0x01;
    const auto batch = analyzeBatch(
        logOf({recordFromWire(encodeRtuFrame(kFc06Write), corrupted, 19)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::CrcError);
}

void PassiveAnalysisTest::p13_fc06WrongResponseAddress()
{
    const ModbusRtuFrame foreignEcho{
        .address = 0x02, .functionCode = 0x06, .data = {0x00, 0x0A, 0x00, 0x64}};
    const auto batch = analyzeBatch(logOf({recordOf(kFc06Write, foreignEcho, 19)}));
    QVERIFY(batch.has_value());
    const auto& analysis = batch->transactions[0].analysis;
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code,
             modbuslens::core::TransactionIssueCode::ResponseAddressMismatch);
    QCOMPARE(*analysis.issue->expectedAddress, std::uint8_t{0x01});
    QCOMPARE(*analysis.issue->actualAddress, std::uint8_t{0x02});
}

void PassiveAnalysisTest::p14_mixedBatchStatistics()
{
    const ModbusRtuFrame fc08Exception{
        .address = 0x01, .functionCode = 0x88, .data = {0x01}};
    const ModbusRtuFrame broadcastWrite{
        .address = 0x00, .functionCode = 0x06, .data = {0x00, 0x01, 0x00, 0x01}};

    const auto batch = analyzeBatch(logOf({
        recordOf(kFc03Read2, kFc03Normal2, 20),
        recordOf(kFc06Write, kFc06Write, 30),
        recordOf(kFc08Request, fc08Exception, 10),
        recordFromWire(encodeRtuFrame(broadcastWrite), std::nullopt, 0),
    }));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{4});
    QCOMPARE(batch->statistics.observedCount, std::size_t{4});
    QCOMPARE(batch->statistics.pendingCount, std::size_t{0});
    QCOMPARE(batch->statistics.completedCount, std::size_t{4});
    QCOMPARE(batch->statistics.successCount, std::size_t{2});
    QCOMPARE(batch->statistics.exceptionCount, std::size_t{1});
    QCOMPARE(batch->statistics.crcErrorCount, std::size_t{0});
    QCOMPARE(batch->statistics.timeoutCount, std::size_t{0});
    QCOMPARE(batch->statistics.protocolErrorCount, std::size_t{0});
    QCOMPARE(batch->statistics.expectedNoResponseCount, std::size_t{1});
    // Approved ADR-003 formula: rate over rate-eligible completed only.
    // rateEligibleCompleted = 4 - 1 = 3; success = 2 -> 2/3.
    QVERIFY(batch->statistics.successRate.has_value());
    QCOMPARE(*batch->statistics.successRate, 2.0 / 3.0);
    QVERIFY(batch->statistics.averageSuccessLatencyMs.has_value());
    QCOMPARE(*batch->statistics.averageSuccessLatencyMs, 25.0);
}

void PassiveAnalysisTest::p15_deterministicRepeat()
{
    const ModbusRtuFrame fc08Exception{
        .address = 0x01, .functionCode = 0x88, .data = {0x01}};
    const ModbusRtuFrame broadcastWrite{
        .address = 0x00, .functionCode = 0x06, .data = {0x00, 0x01, 0x00, 0x01}};
    const ReplayLog log = logOf({
        recordOf(kFc03Read2, kFc03Normal2, 20),
        recordOf(kFc06Write, kFc06Write, 30),
        recordOf(kFc08Request, fc08Exception, 10),
        recordFromWire(encodeRtuFrame(broadcastWrite), std::nullopt, 0),
    });
    const auto first = analyzeBatch(log);
    const auto second = analyzeBatch(log);
    QVERIFY(first.has_value());
    QVERIFY(second.has_value());
    QVERIFY(*first == *second);
}

QTEST_GUILESS_MAIN(PassiveAnalysisTest)
#include "test_passive_analysis.moc"
