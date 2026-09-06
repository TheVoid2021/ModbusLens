#include <QtTest>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/simulator/SimulatedSlave.h"
#include "core/simulator/SimulationFault.h"

using modbuslens::core::DeliveredWire;
using modbuslens::core::DroppedResponse;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::RtuDecodeError;
using modbuslens::core::RtuDecodeErrorCode;
using modbuslens::core::SimulatedSlave;
using modbuslens::core::SimulationFaultConfig;
using modbuslens::core::SimulationFaultMode;
using modbuslens::core::applySimulationFault;
using modbuslens::core::decodeRtuFrame;
using modbuslens::core::encodeRtuFrame;

namespace {

// Copy semantics on purpose — see docs/issues/ISSUE-001.
template <typename T, typename Variant>
std::optional<T> as(const Variant& result)
{
    if (auto* value = std::get_if<T>(&result)) {
        return *value;
    }
    return std::nullopt;
}

class FaultIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    // FAULT-I01 (P0): correct slave response -> CorruptCrc on the wire ->
    // decoder reports CrcMismatch. Proves the CRC error was created in the
    // delivery layer, not by the simulator.
    void i01_simulatorCorruptCrcDecoder();
    // FAULT-I02 (P1): correct slave response -> DropResponse -> nothing is
    // delivered. No Timeout judgement here (no waiter, no clock).
    void i02_simulatorDropResponse();
};

ModbusRtuFrame readTwoRegistersRequest()
{
    return ModbusRtuFrame{
        .address = 0x01, .functionCode = 0x03, .data = {0x00, 0x00, 0x00, 0x02}};
}

void FaultIntegrationTest::i01_simulatorCorruptCrcDecoder()
{
    // Correct slave path: registers 0=100, 1=200 answer a valid request.
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);

    const auto slaveResult = slave.handleRequest(readTwoRegistersRequest());
    const auto* responseFrame = std::get_if<ModbusRtuFrame>(&slaveResult);
    QVERIFY(responseFrame != nullptr);

    const auto responseWire = encodeRtuFrame(*responseFrame);
    const std::vector<std::uint8_t> expectedWire{
        0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7A};
    QCOMPARE(responseWire, expectedWire);

    // The delivery layer corrupts it deterministically.
    const auto delivery = applySimulationFault(
        responseWire, SimulationFaultConfig{.mode = SimulationFaultMode::CorruptCrc});
    const auto* corrupted = std::get_if<DeliveredWire>(&delivery);
    QVERIFY(corrupted != nullptr);

    const std::vector<std::uint8_t> expectedCorrupted{
        0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7B};
    QCOMPARE(corrupted->bytes, expectedCorrupted);

    // The decoder sees exactly what a real receiver would see.
    const auto decodeResult = decodeRtuFrame(corrupted->bytes);
    const auto decodeError = as<RtuDecodeError>(decodeResult);
    QVERIFY(decodeError.has_value());
    QCOMPARE(decodeError->code, RtuDecodeErrorCode::CrcMismatch);
}

void FaultIntegrationTest::i02_simulatorDropResponse()
{
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);

    const auto slaveResult = slave.handleRequest(readTwoRegistersRequest());
    const auto* responseFrame = std::get_if<ModbusRtuFrame>(&slaveResult);
    QVERIFY(responseFrame != nullptr);

    const auto responseWire = encodeRtuFrame(*responseFrame);

    const auto delivery = applySimulationFault(
        responseWire, SimulationFaultConfig{.mode = SimulationFaultMode::DropResponse});
    const auto* dropped = std::get_if<DroppedResponse>(&delivery);
    QVERIFY(dropped != nullptr);
    // Deliberately no Timeout judgement here: no waiter, no clock (T007).
}

} // namespace

QTEST_MAIN(FaultIntegrationTest)
#include "test_fault_integration.moc"