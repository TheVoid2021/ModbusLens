#include <QtTest>

#include <cstdint>
#include <vector>

#include "core/protocol/ModbusRtuFrame.h"

using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::isExceptionResponse;

namespace {

class ModbusRtuFrameTest : public QObject
{
    Q_OBJECT

private slots:
    // FRAME-T01: represent the request 01 03 00 00 00 01 (wire CRC 84 0A is
    // deliberately NOT encoded or parsed here — wire codec belongs to T004).
    void t01_readHoldingRegistersRequest();
    // FRAME-T02: represent a 0x03 response as byte count + register data.
    // Register value interpretation (0x0064 = 100) belongs to T004.
    void t02_readHoldingRegistersResponse();
    // FRAME-T03: exception flag is a generic frame-level property
    // (0x03 -> 0x83). Exception-code interpretation belongs to T004+.
    void t03_exceptionResponseFlag();
    // FRAME-T04: plain value semantics, no copy tricks.
    void t04_equalityAndValueSemantics();
};

void ModbusRtuFrameTest::t01_readHoldingRegistersRequest()
{
    const ModbusRtuFrame frame{
        .address = 0x01,
        .functionCode = 0x03,
        .data = {0x00, 0x00, 0x00, 0x01},
    };

    QCOMPARE(frame.address, std::uint8_t{0x01});
    QCOMPARE(frame.functionCode, std::uint8_t{0x03});

    const std::vector<std::uint8_t> expectedData{0x00, 0x00, 0x00, 0x01};
    QVERIFY(frame.data == expectedData);
}

void ModbusRtuFrameTest::t02_readHoldingRegistersResponse()
{
    // Slave 1, function 0x03, byte count = 2, register bytes 00 64.
    const ModbusRtuFrame frame{
        .address = 0x01,
        .functionCode = 0x03,
        .data = {0x02, 0x00, 0x64},
    };

    QCOMPARE(frame.address, std::uint8_t{0x01});
    QCOMPARE(frame.functionCode, std::uint8_t{0x03});

    const std::vector<std::uint8_t> expectedData{0x02, 0x00, 0x64};
    QVERIFY(frame.data == expectedData);
}

void ModbusRtuFrameTest::t03_exceptionResponseFlag()
{
    // Exception reply to function 0x03 -> 0x83, exception code byte in data.
    const ModbusRtuFrame exceptionFrame{
        .address = 0x01,
        .functionCode = 0x83,
        .data = {0x02},
    };
    QVERIFY(isExceptionResponse(exceptionFrame));

    const ModbusRtuFrame normalFrame{
        .address = 0x01,
        .functionCode = 0x03,
        .data = {0x02, 0x00, 0x64},
    };
    QVERIFY(!isExceptionResponse(normalFrame));
}

void ModbusRtuFrameTest::t04_equalityAndValueSemantics()
{
    const ModbusRtuFrame frameA{
        .address = 0x01,
        .functionCode = 0x03,
        .data = {0x00, 0x01},
    };
    const ModbusRtuFrame frameB{
        .address = 0x01,
        .functionCode = 0x03,
        .data = {0x00, 0x01},
    };
    QVERIFY(frameA == frameB);

    auto mutated = frameB;
    mutated.data[0] = 0x02;
    QVERIFY(frameA != mutated);
}

} // namespace

QTEST_MAIN(ModbusRtuFrameTest)
#include "test_modbus_rtu_frame.moc"