#include <QtTest>

#include <cstdint>
#include <variant>
#include <vector>

#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/simulator/SimulatedSlave.h"

using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::ReadHoldingRegistersResponse;
using modbuslens::core::SimulatedSlave;
using modbuslens::core::decodeReadHoldingRegistersResponse;
using modbuslens::core::decodeRtuFrame;
using modbuslens::core::encodeRtuFrame;

namespace {

// SIM-I01 (P0): full protocol round trip — the first time T002 (CRC),
// T003 (frame), T004A (wire codec), T004B (0x03 semantics) and T005
// (simulated slave) run as one closed loop. Still no serial/threads/timers.
class SimulatorIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void i01_fullProtocolRoundTrip();
};

void SimulatorIntegrationTest::i01_fullProtocolRoundTrip()
{
    // 1. Master builds a semantic request: slave 1, read 2 registers @ 0.
    const ModbusRtuFrame request{
        .address = 0x01, .functionCode = 0x03, .data = {0x00, 0x00, 0x00, 0x02}};

    // 2. Encode to wire (CRC appended, low byte first).
    const auto requestWire = encodeRtuFrame(request);
    const std::vector<std::uint8_t> expectedRequestWire{
        0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
    QCOMPARE(requestWire, expectedRequestWire);

    // 3. The transport hands the wire back to the codec: must decode cleanly.
    //    (Bind the variant to a local first — see ISSUE-001.)
    const auto requestDecodeResult = decodeRtuFrame(requestWire);
    const auto* decodedRequest = std::get_if<ModbusRtuFrame>(&requestDecodeResult);
    QVERIFY(decodedRequest != nullptr);

    // 4. The simulated slave (registers 0=100, 1=200) answers.
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);
    const auto slaveResult = slave.handleRequest(*decodedRequest);
    const auto* responseFrame = std::get_if<ModbusRtuFrame>(&slaveResult);
    QVERIFY(responseFrame != nullptr);

    // 5/6. Encode the response to wire (CRC appended) and hand it back.
    const auto responseWire = encodeRtuFrame(*responseFrame);
    const std::vector<std::uint8_t> expectedResponseWire{
        0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7A};
    QCOMPARE(responseWire, expectedResponseWire);

    // 7. Decode the response wire.
    const auto responseDecodeResult = decodeRtuFrame(responseWire);
    const auto* decodedResponse =
        std::get_if<ModbusRtuFrame>(&responseDecodeResult);
    QVERIFY(decodedResponse != nullptr);

    // 8/9. Interpret 0x03 semantics and assert the register values.
    const auto function03Result =
        decodeReadHoldingRegistersResponse(*decodedResponse);
    const auto* response =
        std::get_if<ReadHoldingRegistersResponse>(&function03Result);
    QVERIFY(response != nullptr);
    const ReadHoldingRegistersResponse expected{.values = {100, 200}};
    QCOMPARE(*response, expected);
}

} // namespace

QTEST_MAIN(SimulatorIntegrationTest)
#include "test_simulator_integration.moc"