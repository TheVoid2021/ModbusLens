#include <QtTest>

#include <array>
#include <cstdint>
#include <span>

#include "core/protocol/ModbusCrc.h"

using modbuslens::core::calculateModbusCrc;

namespace {

class ModbusCrcTest : public QObject
{
    Q_OBJECT

private slots:
    // CRC-T01 (P0): standard known answer test on the universal check string.
    void t01_standardKat();
    // CRC-T02 (P0): real Modbus request known answer. Value only: the wire
    // bytes 84 0A (low byte first, V1.02) are the T003 serialization
    // acceptance, deliberately out of T002 scope.
    void t02_modbusRequestKat();
    // CRC-T03 (P0): empty input boundary of the pure CRC function. Empty is
    // NOT a legal Modbus frame; frame legality is T003's concern.
    void t03_emptyInput();
    // CRC-T04 (P1): single byte boundary.
    void t04_singleByte();
    // CRC-T05 (P2): sensitivity, two requests differing in one byte.
    void t05_inputChangesResult();
    // CRC-T06 (P1): zero remainder invariant (supplementary, not a KAT).
    void t06_zeroRemainder();
};

void ModbusCrcTest::t01_standardKat()
{
    const std::array<std::uint8_t, 9> data{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    QCOMPARE(calculateModbusCrc(data), std::uint16_t{0x4B37});
}

void ModbusCrcTest::t02_modbusRequestKat()
{
    // Slave 1, function 0x03 (read holding registers), 1 register at address 0.
    const std::array<std::uint8_t, 6> message{0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    QCOMPARE(calculateModbusCrc(message), std::uint16_t{0x0A84});
}

void ModbusCrcTest::t03_emptyInput()
{
    const std::span<const std::uint8_t> empty{};
    QCOMPARE(calculateModbusCrc(empty), std::uint16_t{0xFFFF});
}

void ModbusCrcTest::t04_singleByte()
{
    const std::array<std::uint8_t, 1> zero{0x00};
    QCOMPARE(calculateModbusCrc(zero), std::uint16_t{0x40BF});
}

void ModbusCrcTest::t05_inputChangesResult()
{
    // Read request with quantity 0 vs quantity 1: exactly one byte differs.
    const std::array<std::uint8_t, 6> quantityZero{0x01, 0x03, 0x00, 0x00, 0x00, 0x00};
    const std::array<std::uint8_t, 6> quantityOne{0x01, 0x03, 0x00, 0x00, 0x00, 0x01};

    QCOMPARE(calculateModbusCrc(quantityZero), std::uint16_t{0xCA45});
    QCOMPARE(calculateModbusCrc(quantityOne), std::uint16_t{0x0A84});
}

void ModbusCrcTest::t06_zeroRemainder()
{
    // Vector B followed by its correct wire CRC bytes (84 0A).
    const std::array<std::uint8_t, 8> frameWithCrc{
        0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
    QCOMPARE(calculateModbusCrc(frameWithCrc), std::uint16_t{0x0000});
}

} // namespace

QTEST_MAIN(ModbusCrcTest)
#include "test_modbus_crc.moc"