#include <QtTest>

#include <cstdint>
#include <optional>
#include <utility>
#include <variant>

#include "core/simulator/SimulatedSlave.h"

using modbuslens::core::IgnoredRequest;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::SimulatedSlave;
using modbuslens::core::SimulatorResult;

namespace {

// Copy semantics on purpose: the variant may be a temporary, so handing out a
// pointer into it would dangle (ISSUE-001). Optional copies the value out.
template <typename T, typename Variant>
std::optional<T> as(const Variant& result)
{
    if (auto* value = std::get_if<T>(&result)) {
        return *value;
    }
    return std::nullopt;
}

ModbusRtuFrame readRequest(std::uint8_t device, std::uint16_t start, std::uint16_t quantity)
{
    return ModbusRtuFrame{
        .address = device,
        .functionCode = 0x03,
        .data = {
            static_cast<std::uint8_t>(start >> 8),
            static_cast<std::uint8_t>(start & 0xFF),
            static_cast<std::uint8_t>(quantity >> 8),
            static_cast<std::uint8_t>(quantity & 0xFF),
        },
    };
}

class SimulatedSlaveTest : public QObject
{
    Q_OBJECT

private slots:
    // SIM-T01 (P0): read one register.
    void t01_readOneRegister();
    // SIM-T02 (P0): read two registers.
    void t02_readTwoRegisters();
    // SIM-T03 (P0): non-zero start address (0x05DC = 1500 checks the
    // big-endian response writer).
    void t03_nonZeroStartAddress();
    // SIM-T04 (P0): start address beyond the register file -> 0x83/{0x02}.
    void t04_illegalAddress();
    // SIM-T05 (P0): range crosses the end of the file -> whole request fails.
    void t05_rangeCrossesEnd();
    // SIM-T06 (P1): frame addressed to another device -> IgnoredRequest.
    void t06_wrongDeviceAddress();
    // SIM-T07 (P1): malformed 0x03 data reuses the T004 decoder -> 0x83/{0x03};
    // unknown function -> fn|0x80/{0x01}.
    void t07a_malformedFunction03Data();
    void t07b_unknownFunction();
};

void SimulatedSlaveTest::t01_readOneRegister()
{
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);

    const auto frame = as<ModbusRtuFrame>(slave.handleRequest(readRequest(0x01, 0, 1)));
    QVERIFY(frame.has_value());

    const ModbusRtuFrame expected{
        .address = 0x01, .functionCode = 0x03, .data = {0x02, 0x00, 0x64}};
    QCOMPARE(*frame, expected);
}

void SimulatedSlaveTest::t02_readTwoRegisters()
{
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);

    const auto frame = as<ModbusRtuFrame>(slave.handleRequest(readRequest(0x01, 0, 2)));
    QVERIFY(frame.has_value());

    const ModbusRtuFrame expected{
        .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    QCOMPARE(*frame, expected);
}

void SimulatedSlaveTest::t03_nonZeroStartAddress()
{
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);
    slave.setHoldingRegister(2, 1500);

    const auto frame = as<ModbusRtuFrame>(slave.handleRequest(readRequest(0x01, 1, 2)));
    QVERIFY(frame.has_value());

    const ModbusRtuFrame expected{
        .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0xC8, 0x05, 0xDC}};
    QCOMPARE(*frame, expected);
}

void SimulatedSlaveTest::t04_illegalAddress()
{
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 1);
    slave.setHoldingRegister(1, 2);
    slave.setHoldingRegister(2, 3);

    const auto frame = as<ModbusRtuFrame>(slave.handleRequest(readRequest(0x01, 10, 1)));
    QVERIFY(frame.has_value());
    QCOMPARE(frame->address, std::uint8_t{0x01});
    QCOMPARE(frame->functionCode, std::uint8_t{0x83});

    const ModbusRtuFrame expected{
        .address = 0x01, .functionCode = 0x83, .data = {0x02}};
    QCOMPARE(*frame, expected);
}

void SimulatedSlaveTest::t05_rangeCrossesEnd()
{
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 1);
    slave.setHoldingRegister(1, 2);
    slave.setHoldingRegister(2, 3);

    // start=2 is legal but start+quantity=4 crosses the end: the whole
    // request must fail, no partial response.
    const auto frame = as<ModbusRtuFrame>(slave.handleRequest(readRequest(0x01, 2, 2)));
    QVERIFY(frame.has_value());

    const ModbusRtuFrame expected{
        .address = 0x01, .functionCode = 0x83, .data = {0x02}};
    QCOMPARE(*frame, expected);
}

void SimulatedSlaveTest::t06_wrongDeviceAddress()
{
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);

    const auto ignored = as<IgnoredRequest>(slave.handleRequest(readRequest(0x02, 0, 1)));
    QVERIFY(ignored.has_value());
}

void SimulatedSlaveTest::t07a_malformedFunction03Data()
{
    SimulatedSlave slave{0x01};

    // The simulator must reuse the T004 decoder, so a malformed 0x03 request
    // comes back as Illegal Data Value — never a silent ignore.
    const ModbusRtuFrame malformed{.address = 0x01, .functionCode = 0x03, .data = {}};
    const auto frame = as<ModbusRtuFrame>(slave.handleRequest(malformed));
    QVERIFY(frame.has_value());

    const ModbusRtuFrame expected{
        .address = 0x01, .functionCode = 0x83, .data = {0x03}};
    QCOMPARE(*frame, expected);
}

void SimulatedSlaveTest::t07b_unknownFunction()
{
    SimulatedSlave slave{0x01};

    // Function 0x06 is not implemented in v1 -> Illegal Function.
    const ModbusRtuFrame writeSingleRegister{
        .address = 0x01, .functionCode = 0x06, .data = {0x00, 0x01, 0x00, 0x02}};
    const auto frame = as<ModbusRtuFrame>(slave.handleRequest(writeSingleRegister));
    QVERIFY(frame.has_value());

    const ModbusRtuFrame expected{
        .address = 0x01, .functionCode = 0x86, .data = {0x01}};
    QCOMPARE(*frame, expected);
}

} // namespace

QTEST_MAIN(SimulatedSlaveTest)
#include "test_simulated_slave.moc"