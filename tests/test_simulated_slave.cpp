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
using modbuslens::core::SimulatorWriteOutcome;

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

    // ---- M10-A: writable register-bank foundation (SA matrix) ----
    // SA1 (P0): default mode mutates NOTHING — writable is strictly opt-in.
    void sa1_defaultModeAllowsNoMutation();
    // SA2 (P0): opt-in writable + legal 0x06 -> exactly one register updated.
    void sa2_writeSingleRegister();
    // SA3 (P0): opt-in writable + legal 0x10 -> contiguous registers updated.
    void sa3_writeMultipleRegisters();
    // SA4 (P0): malformed / exception-shaped / foreign writes never mutate.
    void sa4_invalidWritesNeverMutate();
    // SA5 (P0): before -> request -> after is deterministically assertable.
    void sa5_beforeAfterDeterminism();
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

// ---- M10-A writable foundation ----

void SimulatedSlaveTest::sa1_defaultModeAllowsNoMutation()
{
    // Default construction keeps the v1 read-only endpoint: a write request
    // mutates NOTHING even though it decodes cleanly.
    SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0x000A, 0x0064);
    QCOMPARE(slave.writeMode(), SimulatedSlave::WriteMode::ReadOnly);

    const ModbusRtuFrame writeSingleRegister{
        .address = 0x01, .functionCode = 0x06, .data = {0x00, 0x0A, 0x00, 0xFF}};
    QCOMPARE(slave.applyWriteRequest(writeSingleRegister),
             SimulatorWriteOutcome::ReadOnlyMode);

    QVERIFY(slave.holdingRegister(0x000A).has_value());
    QCOMPARE(*slave.holdingRegister(0x000A), std::uint16_t{0x0064});
}

void SimulatedSlaveTest::sa2_writeSingleRegister()
{
    SimulatedSlave slave{0x01, SimulatedSlave::WriteMode::Writable};
    slave.setHoldingRegister(0x000A, 0x0064);

    const ModbusRtuFrame writeSingleRegister{
        .address = 0x01, .functionCode = 0x06, .data = {0x00, 0x0A, 0x00, 0xFF}};
    QCOMPARE(slave.applyWriteRequest(writeSingleRegister),
             SimulatorWriteOutcome::Applied);

    // Exactly ONE register changed; the neighbours are untouched (0x0009 was
    // zero-filled by the initializer, 0x000B is still outside the bank —
    // a single write never grows the file past the written address).
    QCOMPARE(*slave.holdingRegister(0x0009), std::uint16_t{0x0000});
    QCOMPARE(*slave.holdingRegister(0x000A), std::uint16_t{0x00FF});
    QVERIFY(!slave.holdingRegister(0x000B).has_value());
    QCOMPARE(slave.registerCount(), std::size_t{0x000B});
}

void SimulatedSlaveTest::sa3_writeMultipleRegisters()
{
    SimulatedSlave slave{0x01, SimulatedSlave::WriteMode::Writable};
    slave.setHoldingRegister(0x0010, 0x0001);
    slave.setHoldingRegister(0x0011, 0x0002);
    slave.setHoldingRegister(0x0012, 0x0003);

    // Two values at 0x0010: byteCount = 2*quantity = 4.
    const ModbusRtuFrame writeMultiple{
        .address = 0x01,
        .functionCode = 0x10,
        .data = {0x00, 0x10, 0x00, 0x02, 0x04, 0xAA, 0xBB, 0xCC, 0xDD}};
    QCOMPARE(slave.applyWriteRequest(writeMultiple),
             SimulatorWriteOutcome::Applied);

    QCOMPARE(*slave.holdingRegister(0x0010), std::uint16_t{0xAABB});
    QCOMPARE(*slave.holdingRegister(0x0011), std::uint16_t{0xCCDD});
    QCOMPARE(*slave.holdingRegister(0x0012), std::uint16_t{0x0003}); // untouched
}

void SimulatedSlaveTest::sa4_invalidWritesNeverMutate()
{
    SimulatedSlave slave{0x01, SimulatedSlave::WriteMode::Writable};
    slave.setHoldingRegister(0x000A, 0x0064);

    // 1. 0x06 with a wrong data length.
    QCOMPARE(slave.applyWriteRequest(ModbusRtuFrame{
                 .address = 0x01, .functionCode = 0x06, .data = {0x00, 0x0A}}),
             SimulatorWriteOutcome::MalformedRequest);
    // 2. 0x10 declaring byteCount=4 but carrying only 2 value bytes.
    QCOMPARE(slave.applyWriteRequest(ModbusRtuFrame{
                 .address = 0x01,
                 .functionCode = 0x10,
                 .data = {0x00, 0x0A, 0x00, 0x02, 0x04, 0xAA, 0xBB}}),
             SimulatorWriteOutcome::MalformedRequest);
    // 3. 0x10 with quantity 0 (illegal domain).
    QCOMPARE(slave.applyWriteRequest(ModbusRtuFrame{
                 .address = 0x01,
                 .functionCode = 0x10,
                 .data = {0x00, 0x0A, 0x00, 0x00, 0x00}}),
             SimulatorWriteOutcome::MalformedRequest);
    // 4. EXCEPTION-shaped frame (fn|0x80) is not a request at all.
    QCOMPARE(slave.applyWriteRequest(ModbusRtuFrame{
                 .address = 0x01, .functionCode = 0x86, .data = {0x00, 0x0A, 0x00, 0x64}}),
             SimulatorWriteOutcome::UnsupportedFunction);
    // 5. A read (0x03) is not a write.
    QCOMPARE(slave.applyWriteRequest(readRequest(0x01, 0x000A, 1)),
             SimulatorWriteOutcome::UnsupportedFunction);
    // 6. Someone else's request (and broadcast 0) is never acted upon.
    QCOMPARE(slave.applyWriteRequest(ModbusRtuFrame{
                 .address = 0x02, .functionCode = 0x06, .data = {0x00, 0x0A, 0x00, 0xFF}}),
             SimulatorWriteOutcome::NotMyAddress);

    // Nothing mutated anywhere.
    QCOMPARE(*slave.holdingRegister(0x000A), std::uint16_t{0x0064});
    QCOMPARE(slave.registerCount(), std::size_t{0x000B});
}

void SimulatedSlaveTest::sa5_beforeAfterDeterminism()
{
    SimulatedSlave slave{0x01, SimulatedSlave::WriteMode::Writable};
    slave.setHoldingRegister(0, 10);
    slave.setHoldingRegister(1, 20);

    // BEFORE: queryable, deterministic.
    QCOMPARE(*slave.holdingRegister(0), std::uint16_t{10});
    QCOMPARE(*slave.holdingRegister(1), std::uint16_t{20});
    QVERIFY(!slave.holdingRegister(2).has_value()); // outside the bank

    const ModbusRtuFrame writeMultiple{
        .address = 0x01,
        .functionCode = 0x10,
        .data = {0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x2A, 0x00, 0x2B}};
    QCOMPARE(slave.applyWriteRequest(writeMultiple),
             SimulatorWriteOutcome::Applied);

    // AFTER: a deterministic function of the request alone (no clock, no
    // randomness, no threads anywhere in the path).
    QCOMPARE(*slave.holdingRegister(0), std::uint16_t{0x002A});
    QCOMPARE(*slave.holdingRegister(1), std::uint16_t{0x002B});

    // Reading the bank back through the READ path reflects the mutation.
    const auto frame = as<ModbusRtuFrame>(slave.handleRequest(readRequest(0x01, 0, 2)));
    QVERIFY(frame.has_value());
    const ModbusRtuFrame expected{
        .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x2A, 0x00, 0x2B}};
    QCOMPARE(*frame, expected);
}

} // namespace

QTEST_MAIN(SimulatedSlaveTest)
#include "test_simulated_slave.moc"