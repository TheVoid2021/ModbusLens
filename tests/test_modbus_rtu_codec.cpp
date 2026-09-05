#include <QtTest>

#include <cstdint>
#include <variant>
#include <vector>

#include "core/protocol/ModbusRtuCodec.h"

using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::RtuDecodeError;
using modbuslens::core::RtuDecodeErrorCode;
using modbuslens::core::RtuDecodeResult;
using modbuslens::core::decodeRtuFrame;
using modbuslens::core::encodeRtuFrame;

namespace {

const RtuDecodeError* asError(const RtuDecodeResult& result)
{
    return std::get_if<RtuDecodeError>(&result);
}

const ModbusRtuFrame* asFrame(const RtuDecodeResult& result)
{
    return std::get_if<ModbusRtuFrame>(&result);
}

class ModbusRtuCodecTest : public QObject
{
    Q_OBJECT

private slots:
    // RTU-A01 (P0): encode known request.
    void a01_encodeKnownRequest();
    // RTU-A02 (P0): decode known request.
    void a02_decodeKnownRequest();
    // RTU-A03 (P0): corrupted data byte with original CRC -> CrcMismatch,
    // no frame.
    void a03_decodeCrcMismatch();
    // RTU-A04 (P0): below the 4-byte container minimum -> FrameTooShort,
    // no out-of-bounds access.
    void a04_frameTooShort();
    // RTU-A05 (P1): empty data still forms a wire frame (container level;
    // function-specific legality is Part B's concern).
    void a05_encodeEmptyData();
    // RTU-A06 (P1): exception-shaped frame is handled transparently;
    // 0x02 meaning is NOT interpreted here.
    void a06_exceptionShapedRoundTrip();
    // RTU-A07 (P1): round trip symmetry (supplementary evidence only —
    // cannot replace the external KATs A01/A02).
    void a07_normalFrameRoundTrip();
};

void ModbusRtuCodecTest::a01_encodeKnownRequest()
{
    const ModbusRtuFrame frame{
        .address = 0x01,
        .functionCode = 0x03,
        .data = {0x00, 0x00, 0x00, 0x01},
    };

    const std::vector<std::uint8_t> expected{
        0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
    QCOMPARE(encodeRtuFrame(frame), expected);
}

void ModbusRtuCodecTest::a02_decodeKnownRequest()
{
    const std::vector<std::uint8_t> wire{
        0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};

    const auto* frame = asFrame(decodeRtuFrame(wire));
    QVERIFY(frame != nullptr);
    QCOMPARE(frame->address, std::uint8_t{0x01});
    QCOMPARE(frame->functionCode, std::uint8_t{0x03});

    const std::vector<std::uint8_t> expectedData{0x00, 0x00, 0x00, 0x01};
    QVERIFY(frame->data == expectedData);
}

void ModbusRtuCodecTest::a03_decodeCrcMismatch()
{
    // One data byte flipped from A02's wire, original CRC kept: the real
    // CRC of this payload is C4 0B, so 84 0A must be rejected.
    const std::vector<std::uint8_t> wire{
        0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0x84, 0x0A};

    const auto* error = asError(decodeRtuFrame(wire));
    QVERIFY(error != nullptr);
    QCOMPARE(error->code, RtuDecodeErrorCode::CrcMismatch);
}

void ModbusRtuCodecTest::a04_frameTooShort()
{
    const auto* emptyError = asError(decodeRtuFrame({}));
    QVERIFY(emptyError != nullptr);
    QCOMPARE(emptyError->code, RtuDecodeErrorCode::FrameTooShort);

    const std::vector<std::uint8_t> threeBytes{0x01, 0x03, 0x00};
    const auto* shortError = asError(decodeRtuFrame(threeBytes));
    QVERIFY(shortError != nullptr);
    QCOMPARE(shortError->code, RtuDecodeErrorCode::FrameTooShort);
}

void ModbusRtuCodecTest::a05_encodeEmptyData()
{
    const ModbusRtuFrame frame{
        .address = 0x01,
        .functionCode = 0x07,
        .data = {},
    };

    const std::vector<std::uint8_t> expected{0x01, 0x07, 0x41, 0xE2};
    QCOMPARE(encodeRtuFrame(frame), expected);
}

void ModbusRtuCodecTest::a06_exceptionShapedRoundTrip()
{
    const ModbusRtuFrame frame{
        .address = 0x01,
        .functionCode = 0x83,
        .data = {0x02},
    };

    const std::vector<std::uint8_t> expectedWire{0x01, 0x83, 0x02, 0xC0, 0xF1};
    const auto wire = encodeRtuFrame(frame);
    QCOMPARE(wire, expectedWire);

    const auto* decoded = asFrame(decodeRtuFrame(wire));
    QVERIFY(decoded != nullptr);
    QVERIFY(*decoded == frame);
}

void ModbusRtuCodecTest::a07_normalFrameRoundTrip()
{
    const ModbusRtuFrame frame{
        .address = 0x01,
        .functionCode = 0x03,
        .data = {0x02, 0x00, 0x64},
    };

    const std::vector<std::uint8_t> expectedWire{
        0x01, 0x03, 0x02, 0x00, 0x64, 0xB9, 0xAF};
    const auto wire = encodeRtuFrame(frame);
    QCOMPARE(wire, expectedWire);

    const auto* decoded = asFrame(decodeRtuFrame(wire));
    QVERIFY(decoded != nullptr);
    QVERIFY(*decoded == frame);
}

} // namespace

QTEST_MAIN(ModbusRtuCodecTest)
#include "test_modbus_rtu_codec.moc"