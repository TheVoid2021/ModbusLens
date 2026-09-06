#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "core/analysis/TransactionAnalysis.h"
#include "core/analysis/TransactionStatistics.h"
#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/simulator/SimulatedSlave.h"
#include "core/simulator/SimulationFault.h"

using modbuslens::core::DeliveredWire;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::NoResponse;
using modbuslens::core::ResponseObservation;
using modbuslens::core::RtuDecodeError;
using modbuslens::core::RtuDecodeErrorCode;
using modbuslens::core::SimulatedSlave;
using modbuslens::core::SimulationFaultConfig;
using modbuslens::core::SimulationFaultMode;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionStatus;
using modbuslens::core::analyzeFunction03Transaction;
using modbuslens::core::decodeRtuFrame;
using modbuslens::core::encodeRtuFrame;
using modbuslens::core::summarizeTransactions;

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

ModbusRtuFrame readRequest(std::uint16_t start, std::uint16_t quantity)
{
    return ModbusRtuFrame{
        .address = 0x01,
        .functionCode = 0x03,
        .data = {
            static_cast<std::uint8_t>(start >> 8),
            static_cast<std::uint8_t>(start & 0xFF),
            static_cast<std::uint8_t>(quantity >> 8),
            static_cast<std::uint8_t>(quantity & 0xFF),
        },
    };
}

class StatisticsIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    // STAT-I01 (P0): T005+T006+T007A+T007B — four real transactions
    // (Success / Exception / CrcError / Timeout) aggregated into one
    // diagnostic snapshot.
    void i01_realAnalysesIntoSnapshot();
};

void StatisticsIntegrationTest::i01_realAnalysesIntoSnapshot()
{
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);

    // A. Success — legal read through the simulated slave (elapsed 15ms).
    const auto successResult = slave.handleRequest(readRequest(0, 2));
    const auto* successFrame = std::get_if<ModbusRtuFrame>(&successResult);
    QVERIFY(successFrame != nullptr);
    const auto success = analyzeFunction03Transaction(
        readRequest(0, 2), ResponseObservation{*successFrame}, ms{15}, ms{1000});
    QCOMPARE(success.status, TransactionStatus::Success);

    // B. Exception — out-of-range address, slave answers 0x83/{0x02}.
    const auto exceptionResult = slave.handleRequest(readRequest(10, 1));
    const auto* exceptionFrame = std::get_if<ModbusRtuFrame>(&exceptionResult);
    QVERIFY(exceptionFrame != nullptr);
    const auto exception = analyzeFunction03Transaction(
        readRequest(10, 1), ResponseObservation{*exceptionFrame}, ms{20}, ms{1000});
    QCOMPARE(exception.status, TransactionStatus::Exception);

    // C. CrcError — correct response corrupted in the delivery layer.
    const auto responseWire = encodeRtuFrame(*successFrame);
    const auto delivery = applySimulationFault(
        responseWire, SimulationFaultConfig{.mode = SimulationFaultMode::CorruptCrc});
    const auto* corrupted = std::get_if<DeliveredWire>(&delivery);
    QVERIFY(corrupted != nullptr);
    const auto decodeResult = decodeRtuFrame(corrupted->bytes);
    const auto* decodeError = std::get_if<RtuDecodeError>(&decodeResult);
    QVERIFY(decodeError != nullptr);
    QCOMPARE(decodeError->code, RtuDecodeErrorCode::CrcMismatch);
    const auto crcError = analyzeFunction03Transaction(
        readRequest(0, 2), ResponseObservation{*decodeError}, ms{20}, ms{1000});
    QCOMPARE(crcError.status, TransactionStatus::CrcError);

    // D. Timeout — no response delivered, threshold reached.
    const auto timeout = analyzeFunction03Transaction(
        readRequest(0, 2), ResponseObservation{NoResponse{}}, ms{1000}, ms{1000});
    QCOMPARE(timeout.status, TransactionStatus::Timeout);

    // Aggregate the four REAL analyses into one snapshot.
    const std::vector<TransactionAnalysis> batch{
        success, exception, crcError, timeout};
    const auto snapshot = summarizeTransactions(batch);

    QCOMPARE(snapshot.observedCount, std::size_t{4});
    QCOMPARE(snapshot.pendingCount, std::size_t{0});
    QCOMPARE(snapshot.completedCount, std::size_t{4});
    QCOMPARE(snapshot.successCount, std::size_t{1});
    QCOMPARE(snapshot.exceptionCount, std::size_t{1});
    QCOMPARE(snapshot.crcErrorCount, std::size_t{1});
    QCOMPARE(snapshot.timeoutCount, std::size_t{1});
    QCOMPARE(snapshot.protocolErrorCount, std::size_t{0});
    QVERIFY(snapshot.successRate.has_value());
    QVERIFY(qFuzzyCompare(*snapshot.successRate, 0.25));
    QVERIFY(snapshot.averageSuccessLatencyMs.has_value());
    QVERIFY(qFuzzyCompare(*snapshot.averageSuccessLatencyMs, 15.0));
}

} // namespace

QTEST_MAIN(StatisticsIntegrationTest)
#include "test_statistics_integration.moc"