#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "core/analysis/TransactionAnalysis.h"
#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/simulator/SimulatedSlave.h"
#include "core/simulator/SimulationFault.h"

using modbuslens::core::DeliveredWire;
using modbuslens::core::DroppedResponse;
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

ModbusRtuFrame readTwoRegistersRequest()
{
    return ModbusRtuFrame{
        .address = 0x01, .functionCode = 0x03, .data = {0x00, 0x00, 0x00, 0x02}};
}

SimulatedSlave makeSlaveWithTwoRegisters()
{
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);
    return slave;
}

// TX-I01 (P0): T005 simulator output feeds T007 directly (semantic path —
// the full wire loop already lives in SIM-I01).
class TransactionIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void i01_simulatorSuccess();
    void i02_dropResponseBecomesPendingThenTimeout();
    void i03_crcFaultBecomesCrcError();
};

void TransactionIntegrationTest::i01_simulatorSuccess()
{
    SimulatedSlave slave = makeSlaveWithTwoRegisters();
    const auto slaveResult = slave.handleRequest(readTwoRegistersRequest());
    const auto* responseFrame = std::get_if<ModbusRtuFrame>(&slaveResult);
    QVERIFY(responseFrame != nullptr);

    const ResponseObservation observation{*responseFrame};
    const auto analysis =
        analyzeFunction03Transaction(readTwoRegistersRequest(), observation, ms{12}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::Success);
    QCOMPARE(analysis.elapsed, ms{12});
    QVERIFY(!analysis.exceptionCode.has_value());
}

void TransactionIntegrationTest::i02_dropResponseBecomesPendingThenTimeout()
{
    SimulatedSlave slave = makeSlaveWithTwoRegisters();
    const auto slaveResult = slave.handleRequest(readTwoRegistersRequest());
    const auto* responseFrame = std::get_if<ModbusRtuFrame>(&slaveResult);
    QVERIFY(responseFrame != nullptr);

    const auto responseWire = encodeRtuFrame(*responseFrame);
    const auto delivery = applySimulationFault(
        responseWire, SimulationFaultConfig{.mode = SimulationFaultMode::DropResponse});
    const auto* dropped = std::get_if<DroppedResponse>(&delivery);
    QVERIFY(dropped != nullptr);

    // Adapter at the integration boundary: T006's DroppedResponse becomes the
    // abstract NoResponse observation the analyzer understands.
    const ResponseObservation observation{NoResponse{}};

    const auto beforeThreshold =
        analyzeFunction03Transaction(readTwoRegistersRequest(), observation, ms{500}, ms{1000});
    QCOMPARE(beforeThreshold.status, TransactionStatus::Pending);

    const auto atThreshold =
        analyzeFunction03Transaction(readTwoRegistersRequest(), observation, ms{1000}, ms{1000});
    QCOMPARE(atThreshold.status, TransactionStatus::Timeout);
}

void TransactionIntegrationTest::i03_crcFaultBecomesCrcError()
{
    SimulatedSlave slave = makeSlaveWithTwoRegisters();
    const auto slaveResult = slave.handleRequest(readTwoRegistersRequest());
    const auto* responseFrame = std::get_if<ModbusRtuFrame>(&slaveResult);
    QVERIFY(responseFrame != nullptr);

    const auto responseWire = encodeRtuFrame(*responseFrame);
    const auto delivery = applySimulationFault(
        responseWire, SimulationFaultConfig{.mode = SimulationFaultMode::CorruptCrc});
    const auto* corrupted = std::get_if<DeliveredWire>(&delivery);
    QVERIFY(corrupted != nullptr);

    const auto decodeResult = decodeRtuFrame(corrupted->bytes);
    const auto* decodeError = std::get_if<RtuDecodeError>(&decodeResult);
    QVERIFY(decodeError != nullptr);
    QCOMPARE(decodeError->code, RtuDecodeErrorCode::CrcMismatch);

    const ResponseObservation observation{*decodeError};
    const auto analysis =
        analyzeFunction03Transaction(readTwoRegistersRequest(), observation, ms{20}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::CrcError);
}

} // namespace

QTEST_MAIN(TransactionIntegrationTest)
#include "test_transaction_integration.moc"