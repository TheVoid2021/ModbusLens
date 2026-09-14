#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/protocol/Function06.h"
#include "core/protocol/Function16.h"
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
using modbuslens::core::Function16DecodeError;
using modbuslens::core::Function16DecodeErrorCode;
using modbuslens::core::WriteMultipleRegistersResponse;
using modbuslens::core::decodeWriteMultipleRegistersResponse;
using modbuslens::core::readWriteMultipleRegistersFields;
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
    // T015 semantic audit (P0): the generic exception matcher must require a
    // request function WITHOUT the exception bit; a request whose function
    // already carries 0x80 can never be "answered" by (fn | 0x80).
    void p16_requestFunctionWithExceptionBitIsNeverAnException();
    // ---- T015 Part C: Function 0x10 (Write Multiple Registers) ----
    void f16_u01_structural();
    void f16_u02_quantity1();
    void f16_u03_quantity123();
    void f16_u04_quantity0();
    void f16_u05_quantity124();
    void f16_u06_byteCountRelation();
    void f16_u07_byteCountMismatch();
    void f16_u08_truncated();
    void f16_u09_excess();
    void f16_u10_responseDecode();
    void f16_u11_malformedResponse();
    void c01_normalUnicastSuccess();
    void c02_wrongResponseAddress();
    void c03_wrongResponseFunction();
    void c04_badResponseCrc();
    void c05_malformedNormalResponse();
    void c06_startAddressMismatch();
    void c07_quantityWrittenMismatch();
    void c08_genericException090();
    void c09_invalidQuantityWithException();
    void c10_byteCountIssueWithException();
    void c11_invalidDoesNotPoison();
    void c12_broadcastNoResponse();
    void c13_broadcastResponse();
    void c14_invalidBroadcastNoResponse();
    void c15_fc08StaysUnsupported();
    void c16_mixedFunctionStatistics();
    void c17_deterministicRepeat();
    void multi_c01();
    void multi_c02();
    void bcast_c01();
    void bcast_c02();
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
    QVERIFY(batch->transactions[0].requestIssues.empty());
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
    QVERIFY(batch->transactions[0].requestIssues.empty());
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
    QCOMPARE(outcome.requestIssues.size(), std::size_t{1});
    const auto& quantityIssue = outcome.requestIssues[0];
    QCOMPARE(quantityIssue.code, TransactionRequestIssueCode::InvalidRequestQuantity);
    QVERIFY(quantityIssue.observedQuantity.has_value());
    QCOMPARE(*quantityIssue.observedQuantity, std::uint16_t{126});
    QVERIFY(quantityIssue.minAllowedQuantity.has_value());
    QCOMPARE(*quantityIssue.minAllowedQuantity, std::uint16_t{1});
    QVERIFY(quantityIssue.maxAllowedQuantity.has_value());
    QCOMPARE(*quantityIssue.maxAllowedQuantity, std::uint16_t{125});
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
    QVERIFY(batch->transactions[0].requestIssues.empty());
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
    QCOMPARE(outcome.requestIssues.size(), std::size_t{1});
    QCOMPARE(outcome.requestIssues[0].code,
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
    QCOMPARE(batch->transactions.size(), std::size_t{1});
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

void PassiveAnalysisTest::p16_requestFunctionWithExceptionBitIsNeverAnException()
{
    // A captured request whose function code already has the MSB set (0x88)
    // is NOT a normal Modbus request function. Without an MSB guard,
    // (0x88 | 0x80) == 0x88 makes the "exception reply" match itself and a
    // 1-byte payload would be misreported as a legal Exception 0x01.
    const ModbusRtuFrame exceptionShapedRequest{
        .address = 0x01, .functionCode = 0x88, .data = {0x00}};
    const ModbusRtuFrame oneByteReply{
        .address = 0x01, .functionCode = 0x88, .data = {0x01}};

    const auto batch =
        analyzeBatch(logOf({recordOf(exceptionShapedRequest, oneByteReply, 12)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{0});
    QCOMPARE(batch->unsupportedRecords.size(), std::size_t{1});
    QCOMPARE(batch->unsupportedRecords[0].functionCode, std::uint8_t{0x88});
    QCOMPARE(batch->statistics.exceptionCount, std::size_t{0});

    // Same invariant for a longer (non-1-byte) reply: it must not become a
    // malformed-exception protocol error either — the pair simply has no
    // normal semantics for a function ModbusLens does not model.
    const ModbusRtuFrame fourByteReply{
        .address = 0x01, .functionCode = 0x88, .data = {0x01, 0x02, 0x03, 0x04}};
    const auto batch2 =
        analyzeBatch(logOf({recordOf(exceptionShapedRequest, fourByteReply, 12)}));
    QVERIFY(batch2.has_value());
    QCOMPARE(batch2->transactions.size(), std::size_t{0});
    QCOMPARE(batch2->unsupportedRecords.size(), std::size_t{1});
    QCOMPARE(batch2->unsupportedRecords[0].functionCode, std::uint8_t{0x88});
    QCOMPARE(batch2->statistics.protocolErrorCount, std::size_t{0});

    // Control: the legitimate generic-exception path is untouched
    // (request 0x08 answered by 0x88/0x01 is still Exception 0x01).
    const ModbusRtuFrame fc08Exception{
        .address = 0x01, .functionCode = 0x88, .data = {0x01}};
    const auto control =
        analyzeBatch(logOf({recordOf(kFc08Request, fc08Exception, 14)}));
    QVERIFY(control.has_value());
    QCOMPARE(control->transactions.size(), std::size_t{1});
    QCOMPARE(control->transactions[0].analysis.status, TransactionStatus::Exception);
    QCOMPARE(*control->transactions[0].analysis.exceptionCode, std::uint8_t{0x01});
}

void PassiveAnalysisTest::f16_u01_structural()
{
    const ModbusRtuFrame request{.address = 0x01, .functionCode = 0x10,
                                 .data = {0x00, 0x10, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02}};
    const auto fields = modbuslens::core::readWriteMultipleRegistersFields(request);
    QVERIFY(fields.has_value());
    QCOMPARE(fields->startingAddress, std::uint16_t{0x0010});
    QCOMPARE(fields->quantity, std::uint16_t{2});
    QCOMPARE(fields->byteCount, std::uint8_t{4});
    QCOMPARE(fields->actualValueByteCount, std::uint16_t{4});
}

void PassiveAnalysisTest::f16_u02_quantity1()
{
    const ModbusRtuFrame request{.address = 0x01, .functionCode = 0x10,
                                 .data = {0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const auto batch = analyzeBatch(logOf({recordOf(
        request,
        ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x00, 0x00, 0x01}},
        10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::Success);
    QVERIFY(batch->transactions[0].requestIssues.empty());
}

void PassiveAnalysisTest::f16_u03_quantity123()
{
    const auto req = ModbusRtuFrame{.address = 0x01, .functionCode = 0x10,
                                    .data = {0x00, 0x00, 0x00, 0x7B, 0x00}};
    // Semantically invalid (empty payload vs declared 0+quantity 123), the
    // boundary check here is the request-side classification.
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    // quantity 123 is inside the domain: no quantity issue.
    bool quantityIssue = false;
    for (const auto& issue : batch->transactions[0].requestIssues) {
        if (issue.code == TransactionRequestIssueCode::InvalidRequestQuantity) {
            quantityIssue = true;
        }
    }
    QVERIFY(!quantityIssue);
}

void PassiveAnalysisTest::f16_u04_quantity0()
{
    // quantity=0 + exception: quantity issue only (anti-cascade demo).
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x00, 0x00, 0x00, 0x00}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::Exception);
    QCOMPARE(batch->transactions[0].requestIssues.size(), std::size_t{1});
    const auto& issue = batch->transactions[0].requestIssues[0];
    QCOMPARE(issue.code, TransactionRequestIssueCode::InvalidRequestQuantity);
    QCOMPARE(*issue.observedQuantity, std::uint16_t{0});
    QCOMPARE(*issue.minAllowedQuantity, std::uint16_t{1});
    QCOMPARE(*issue.maxAllowedQuantity, std::uint16_t{123});
}

void PassiveAnalysisTest::f16_u05_quantity124()
{
    // quantity=124 on a SHORT, CRC-valid semantic fixture that stays well
    // inside the RTU 256-byte frame limit (MULTI-C02 review ruling).
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x00, 0x00, 0x7C, 0x00}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].requestIssues.size(), std::size_t{1});
    const auto& issue = batch->transactions[0].requestIssues[0];
    QCOMPARE(issue.code, TransactionRequestIssueCode::InvalidRequestQuantity);
    QCOMPARE(*issue.observedQuantity, std::uint16_t{124});
    QCOMPARE(*issue.maxAllowedQuantity, std::uint16_t{123});
}

void PassiveAnalysisTest::f16_u06_byteCountRelation()
{
    // quantity=2 byteCount=4 exact payload = valid normal unicast.
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x02}},
        10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::Success);
    QVERIFY(batch->transactions[0].requestIssues.empty());
}

void PassiveAnalysisTest::f16_u07_byteCountMismatch()
{
    // quantity=2, byteCount=2, exact 2-byte payload (no length issue).
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x02, 0x02, 0x00, 0x01}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].requestIssues.size(), std::size_t{1});
    const auto& issue = batch->transactions[0].requestIssues[0];
    QCOMPARE(issue.code, TransactionRequestIssueCode::InvalidRequestByteCount);
    QCOMPARE(*issue.observedByteCount, std::uint8_t{2});
    QCOMPARE(*issue.expectedByteCount, std::uint8_t{4});
}

void PassiveAnalysisTest::f16_u08_truncated()
{
    // quantity=2, byteCount=4, only 2 value bytes.
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x02, 0x04, 0x00, 0x01}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].requestIssues.size(), std::size_t{1});
    const auto& issue = batch->transactions[0].requestIssues[0];
    QCOMPARE(issue.code, TransactionRequestIssueCode::InvalidRequestLength);
    QCOMPARE(*issue.observedLength, std::uint16_t{7});
    QCOMPARE(*issue.expectedLength, std::uint16_t{9});
}

void PassiveAnalysisTest::f16_u09_excess()
{
    // quantity=1, byteCount=2, 4 value bytes (excess payload).
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x01, 0x00, 0x02}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].requestIssues.size(), std::size_t{1});
    const auto& issue = batch->transactions[0].requestIssues[0];
    QCOMPARE(issue.code, TransactionRequestIssueCode::InvalidRequestLength);
    QCOMPARE(*issue.observedLength, std::uint16_t{9});
    QCOMPARE(*issue.expectedLength, std::uint16_t{7});
}

void PassiveAnalysisTest::f16_u10_responseDecode()
{
    const ModbusRtuFrame response{.address = 0x01, .functionCode = 0x10,
                                  .data = {0x00, 0x10, 0x00, 0x02}};
    const auto decoded = modbuslens::core::decodeWriteMultipleRegistersResponse(response);
    const auto model = as<modbuslens::core::WriteMultipleRegistersResponse>(decoded);
    QVERIFY(model.has_value());
    QCOMPARE(model->startingAddress, std::uint16_t{0x0010});
    QCOMPARE(model->quantityWritten, std::uint16_t{2});
}

void PassiveAnalysisTest::f16_u11_malformedResponse()
{
    const ModbusRtuFrame response{.address = 0x01, .functionCode = 0x10,
                                  .data = {0x00, 0x10}};
    const auto decoded = modbuslens::core::decodeWriteMultipleRegistersResponse(response);
    const auto error = as<modbuslens::core::Function16DecodeError>(decoded);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, modbuslens::core::Function16DecodeErrorCode::InvalidResponseLength);
}

void PassiveAnalysisTest::c01_normalUnicastSuccess()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x02}},
        31)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].functionCode, std::uint8_t{0x10});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::Success);
    QCOMPARE(batch->transactions[0].analysis.elapsed, ms{31});
    QVERIFY(batch->transactions[0].requestIssues.empty());
}

void PassiveAnalysisTest::c02_wrongResponseAddress()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x02, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x01}},
        10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& a = batch->transactions[0].analysis;
    QCOMPARE(a.status, TransactionStatus::ProtocolError);
    QCOMPARE(a.issue->code, modbuslens::core::TransactionIssueCode::ResponseAddressMismatch);
    QCOMPARE(*a.issue->expectedAddress, std::uint8_t{0x01});
    QCOMPARE(*a.issue->actualAddress, std::uint8_t{0x02});
}

void PassiveAnalysisTest::c03_wrongResponseFunction()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}},
        10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& a = batch->transactions[0].analysis;
    QCOMPARE(a.status, TransactionStatus::ProtocolError);
    QCOMPARE(a.issue->code, modbuslens::core::TransactionIssueCode::UnexpectedResponseFunction);
    QCOMPARE(*a.issue->actualFunctionCode, std::uint8_t{0x03});
}

void PassiveAnalysisTest::c04_badResponseCrc()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    auto corrupted = encodeRtuFrame(
        ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x01}});
    corrupted.back() ^= 0x01;
    const auto batch = analyzeBatch(
        logOf({recordFromWire(encodeRtuFrame(req), corrupted, 10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::CrcError);
}

void PassiveAnalysisTest::c05_malformedNormalResponse()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10}},
        10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& a = batch->transactions[0].analysis;
    QCOMPARE(a.status, TransactionStatus::ProtocolError);
    QCOMPARE(a.issue->code, modbuslens::core::TransactionIssueCode::MalformedNormalResponse);
}

void PassiveAnalysisTest::c06_startAddressMismatch()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x11, 0x00, 0x02}},
        10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& a = batch->transactions[0].analysis;
    QCOMPARE(a.status, TransactionStatus::ProtocolError);
    QCOMPARE(a.issue->code,
             modbuslens::core::TransactionIssueCode::WriteMultipleRegistersEchoMismatch);
    QCOMPARE(*a.issue->expectedRegisterAddress, std::uint16_t{0x0010});
    QCOMPARE(*a.issue->actualRegisterAddress, std::uint16_t{0x0011});
    QCOMPARE(*a.issue->expectedQuantity, std::uint16_t{2});
    QCOMPARE(*a.issue->actualQuantity, std::uint16_t{2});
}

void PassiveAnalysisTest::c07_quantityWrittenMismatch()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x03}},
        10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& a = batch->transactions[0].analysis;
    QCOMPARE(a.issue->code,
             modbuslens::core::TransactionIssueCode::WriteMultipleRegistersEchoMismatch);
    QCOMPARE(*a.issue->expectedQuantity, std::uint16_t{2});
    QCOMPARE(*a.issue->actualQuantity, std::uint16_t{3});
}

void PassiveAnalysisTest::c08_genericException090()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x02}}, 10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& a = batch->transactions[0].analysis;
    QCOMPARE(a.status, TransactionStatus::Exception);
    QCOMPARE(*a.exceptionCode, std::uint8_t{0x02});
    QVERIFY(batch->transactions[0].requestIssues.empty());
}

void PassiveAnalysisTest::c09_invalidQuantityWithException()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x7C, 0x00}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 16)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& outcome = batch->transactions[0];
    QCOMPARE(outcome.analysis.status, TransactionStatus::Exception);
    QCOMPARE(*outcome.analysis.exceptionCode, std::uint8_t{0x03});
    QCOMPARE(outcome.requestIssues.size(), std::size_t{1});
    QCOMPARE(outcome.requestIssues[0].code,
             TransactionRequestIssueCode::InvalidRequestQuantity);
    QCOMPARE(*outcome.requestIssues[0].observedQuantity, std::uint16_t{124});
}

void PassiveAnalysisTest::c10_byteCountIssueWithException()
{
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x02, 0x02, 0x00, 0x01}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 16)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& outcome = batch->transactions[0];
    QCOMPARE(outcome.analysis.status, TransactionStatus::Exception);
    QCOMPARE(outcome.requestIssues.size(), std::size_t{1});
    QCOMPARE(outcome.requestIssues[0].code,
             TransactionRequestIssueCode::InvalidRequestByteCount);
}

void PassiveAnalysisTest::c11_invalidDoesNotPoison()
{
    const ModbusRtuFrame badReq{.address = 0x01, .functionCode = 0x10,
                                .data = {0x00, 0x10, 0x00, 0x7C, 0x00}};
    const auto batch = analyzeBatch(logOf({
        recordOf(badReq, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 16),
        recordOf(kFc03Read2, kFc03Normal2, 25),
    }));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{2});
    QCOMPARE(batch->transactions[1].analysis.status, TransactionStatus::Success);
}

void PassiveAnalysisTest::c12_broadcastNoResponse()
{
    const ModbusRtuFrame broadcastReq{.address = 0x00, .functionCode = 0x10,
                                      .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const auto batch = analyzeBatch(
        logOf({recordFromWire(encodeRtuFrame(broadcastReq), std::nullopt, 0)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    QCOMPARE(batch->transactions[0].analysis.status, TransactionStatus::ExpectedNoResponse);
    QVERIFY(batch->transactions[0].requestIssues.empty());
}

void PassiveAnalysisTest::c13_broadcastResponse()
{
    const ModbusRtuFrame broadcastReq{.address = 0x00, .functionCode = 0x10,
                                      .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const auto batch = analyzeBatch(logOf({recordOf(
        broadcastReq, ModbusRtuFrame{.address = 0x00, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x01}},
        10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& a = batch->transactions[0].analysis;
    QCOMPARE(a.status, TransactionStatus::ProtocolError);
    QCOMPARE(a.issue->code, modbuslens::core::TransactionIssueCode::UnexpectedResponseForBroadcast);
}

void PassiveAnalysisTest::c14_invalidBroadcastNoResponse()
{
    const ModbusRtuFrame badBroadcastReq{.address = 0x00, .functionCode = 0x10,
                                         .data = {0x00, 0x10, 0x00, 0x7C, 0x00}};
    const auto batch = analyzeBatch(
        logOf({recordFromWire(encodeRtuFrame(badBroadcastReq), std::nullopt, 0)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& outcome = batch->transactions[0];
    QCOMPARE(outcome.analysis.status, TransactionStatus::ExpectedNoResponse);
    QCOMPARE(outcome.requestIssues.size(), std::size_t{1});
    QCOMPARE(outcome.requestIssues[0].code,
             TransactionRequestIssueCode::InvalidRequestQuantity);
}

void PassiveAnalysisTest::c15_fc08StaysUnsupported()
{
    const ModbusRtuFrame fc08Normal{
        .address = 0x01, .functionCode = 0x08, .data = {0x00, 0x00}};
    const auto batch = analyzeBatch(
        logOf({recordOf(kFc08Request, fc08Normal, 21)}));
    QVERIFY(batch.has_value());
    QVERIFY(batch->transactions.empty());
    QCOMPARE(batch->unsupportedRecords.size(), std::size_t{1});
}

void PassiveAnalysisTest::c16_mixedFunctionStatistics()
{
    const ModbusRtuFrame f16Req{.address = 0x01, .functionCode = 0x10,
                                .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const ModbusRtuFrame f16Broadcast{.address = 0x00, .functionCode = 0x10,
                                      .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const auto batch = analyzeBatch(logOf({
        recordOf(kFc03Read2, kFc03Normal2, 20),
        recordOf(kFc06Write, kFc06Write, 30),
        recordOf(f16Req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x01}}, 40),
        recordOf(f16Req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x02}}, 10),
        recordOf(f16Req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x02}}, 10),
        recordOf(f16Broadcast, std::nullopt, 0),
    }));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{6});
    QCOMPARE(batch->statistics.observedCount, std::size_t{6});
    QCOMPARE(batch->statistics.completedCount, std::size_t{6});
    QCOMPARE(batch->statistics.pendingCount, std::size_t{0});
    QCOMPARE(batch->statistics.successCount, std::size_t{3});
    QCOMPARE(batch->statistics.exceptionCount, std::size_t{1});
    QCOMPARE(batch->statistics.protocolErrorCount, std::size_t{1});
    QCOMPARE(batch->statistics.expectedNoResponseCount, std::size_t{1});
    // rateEligible = 6 - 1 = 5; success 3 -> 0.6.
    QVERIFY(batch->statistics.successRate.has_value());
    QCOMPARE(*batch->statistics.successRate, 0.6);
    // avg latency: (20+30+40)/3 = 30.
    QVERIFY(batch->statistics.averageSuccessLatencyMs.has_value());
    QCOMPARE(*batch->statistics.averageSuccessLatencyMs, 30.0);
}

void PassiveAnalysisTest::c17_deterministicRepeat()
{
    const ModbusRtuFrame f16Req{.address = 0x01, .functionCode = 0x10,
                                .data = {0x00, 0x10, 0x00, 0x01, 0x02, 0x00, 0x2A}};
    const ReplayLog log = logOf({
        recordOf(f16Req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x01}}, 40),
        recordOf(f16Req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x02}}, 10),
    });
    const auto first = analyzeBatch(log);
    const auto second = analyzeBatch(log);
    QVERIFY(first.has_value() && second.has_value());
    QVERIFY(*first == *second);
}

void PassiveAnalysisTest::multi_c01()
{
    // quantity=2, byteCount=2, actual payload=4 bytes: TWO issues, stable order.
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x02, 0x02, 0x00, 0x01, 0x00, 0x02}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 16)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& outcome = batch->transactions[0];
    QCOMPARE(outcome.analysis.status, TransactionStatus::Exception);
    QCOMPARE(outcome.requestIssues.size(), std::size_t{2});
    QCOMPARE(outcome.requestIssues[0].code,
             TransactionRequestIssueCode::InvalidRequestByteCount);
    QCOMPARE(*outcome.requestIssues[0].observedByteCount, std::uint8_t{2});
    QCOMPARE(*outcome.requestIssues[0].expectedByteCount, std::uint8_t{4});
    QCOMPARE(outcome.requestIssues[1].code,
             TransactionRequestIssueCode::InvalidRequestLength);
    QCOMPARE(*outcome.requestIssues[1].observedLength, std::uint16_t{9});
    QCOMPARE(*outcome.requestIssues[1].expectedLength, std::uint16_t{7});
}

void PassiveAnalysisTest::multi_c02()
{
    // quantity=0, byteCount=0, payload=0: quantity issue ONLY.
    const ModbusRtuFrame req{.address = 0x01, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x00, 0x00}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x01, .functionCode = 0x90, .data = {0x03}}, 16)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& outcome = batch->transactions[0];
    QCOMPARE(outcome.requestIssues.size(), std::size_t{1});
    QCOMPARE(outcome.requestIssues[0].code,
             TransactionRequestIssueCode::InvalidRequestQuantity);
    QCOMPARE(*outcome.requestIssues[0].observedQuantity, std::uint16_t{0});
    QCOMPARE(*outcome.requestIssues[0].minAllowedQuantity, std::uint16_t{1});
    QCOMPARE(*outcome.requestIssues[0].maxAllowedQuantity, std::uint16_t{123});
}

void PassiveAnalysisTest::bcast_c01()
{
    // Function16 semantic-invalid broadcast + NO_RESPONSE:
    // ExpectedNoResponse (orthogonal) + requestIssues preserved.
    const ModbusRtuFrame req{.address = 0x00, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x7C, 0x00}};
    const auto batch = analyzeBatch(
        logOf({recordFromWire(encodeRtuFrame(req), std::nullopt, 0)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& outcome = batch->transactions[0];
    QCOMPARE(outcome.analysis.status, TransactionStatus::ExpectedNoResponse);
    QCOMPARE(outcome.requestIssues.size(), std::size_t{1});
    QCOMPARE(outcome.requestIssues[0].code,
             TransactionRequestIssueCode::InvalidRequestQuantity);
}

void PassiveAnalysisTest::bcast_c02()
{
    // Function16 semantic-invalid broadcast + response bytes:
    // UnexpectedResponseForBroadcast + requestIssues preserved.
    const ModbusRtuFrame req{.address = 0x00, .functionCode = 0x10,
                             .data = {0x00, 0x10, 0x00, 0x7C, 0x00}};
    const auto batch = analyzeBatch(logOf({recordOf(
        req, ModbusRtuFrame{.address = 0x00, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x7C}},
        10)}));
    QVERIFY(batch.has_value());
    QCOMPARE(batch->transactions.size(), std::size_t{1});
    const auto& outcome = batch->transactions[0];
    QCOMPARE(outcome.analysis.status, TransactionStatus::ProtocolError);
    QCOMPARE(outcome.analysis.issue->code,
             modbuslens::core::TransactionIssueCode::UnexpectedResponseForBroadcast);
    QCOMPARE(outcome.requestIssues.size(), std::size_t{1});
    QCOMPARE(outcome.requestIssues[0].code,
             TransactionRequestIssueCode::InvalidRequestQuantity);
}

QTEST_GUILESS_MAIN(PassiveAnalysisTest)
#include "test_passive_analysis.moc"
