#include <QtTest>

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/protocol/Function06.h"
#include "core/serial/SerialTransactionSession.h"

using modbuslens::core::ActiveFunction;
using modbuslens::core::ActiveRequestDescriptor;
using modbuslens::core::ActiveRequestEncodeError;
using modbuslens::core::ActiveRequestEncodeErrorCode;
using modbuslens::core::ActiveRequestIntent;
using modbuslens::core::encodeActiveRequest;
using modbuslens::core::encodeWriteSingleRegisterRequest;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::WriteSingleRegisterIntent;

namespace {

// ---------------------------------------------------------------------------
// INDEPENDENT golden oracle.
//
// This CRC is a test-local bitwise implementation of CRC-16/MODBUS written
// straight from the definition (polynomial 0xA001 reflected, init 0xFFFF,
// LSB-first) — it deliberately does NOT call the production
// calculateModbusCrc, so the encoder and its expected bytes can never
// "prove each other" from one shared helper. The full-ADU literals in the
// vector table below were derived by hand (field bytes) plus this independent
// CRC, and the G3 row doubles as the field sequence of the published MODBUS
// application example (11 06 00 01 00 03), so an external document
// corroborates the layout.
// ---------------------------------------------------------------------------
std::uint16_t independentCrc(std::span<const std::uint8_t> bytes)
{
    std::uint16_t crc = 0xFFFF;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001) != 0) {
                crc = static_cast<std::uint16_t>((crc >> 1) ^ 0xA001);
            } else {
                crc = static_cast<std::uint16_t>(crc >> 1);
            }
        }
    }
    return crc;
}

// The golden vector table: unit / address / value + the EXACT expected ADU.
// The ADU literals are fixed in the source on purpose (a long-lived golden
// vector), not recomputed by the production encoder.
struct GoldenVector {
    const char* name;
    std::uint8_t unit;
    std::uint16_t address;
    std::uint16_t value;
    std::vector<std::uint8_t> expectedAdu;
    bool hasHardCodedLiteral;
};

std::vector<GoldenVector> goldenVectors()
{
    return {
        {"G1 canonical (1 / 0 / 0)", 1, 0, 0,
         {0x01, 0x06, 0x00, 0x00, 0x00, 0x00, 0x89, 0xCA}, true},
        {"G2 high boundary (247 / 65535 / 65535)", 247, 65535, 65535,
         {0xF7, 0x06, 0xFF, 0xFF, 0xFF, 0xFF, 0x9C, 0xC8}, true},
        {"G3 protocol example (0x11 / 1 / 3)", 0x11, 1, 3,
         {0x11, 0x06, 0x00, 0x01, 0x00, 0x03, 0x9A, 0x9B}, true},
        {"G4 address max (1 / 65535 / 0)", 1, 65535, 0,
         {0x01, 0x06, 0xFF, 0xFF, 0x00, 0x00, 0x89, 0xEE}, true},
        {"G5 value max (1 / 0 / 65535)", 1, 0, 65535,
         {0x01, 0x06, 0x00, 0x00, 0xFF, 0xFF, 0x88, 0x7A}, true},
        {"G6 smallest non-zero value (1 / 0 / 1)", 1, 0, 1,
         {0x01, 0x06, 0x00, 0x00, 0x00, 0x01, 0x48, 0x0A}, true},
    };
}

ActiveRequestIntent writeIntent(std::uint8_t unit, std::uint16_t address,
                                std::uint16_t value)
{
    return ActiveRequestIntent{
        .function = ActiveFunction::WriteSingleRegister,
        .unitId = unit,
        .timeout = std::chrono::milliseconds{1000},
        .payload = WriteSingleRegisterIntent{.registerAddress = address,
                                            .value = value},
    };
}

} // namespace

class WriteEncoderTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- golden vectors (E1–E6 / G1–G6) ----
    void g1_canonical();
    void g2_highBoundary();
    void g3_protocolExample();
    void g4_addressMax();
    void g5_valueMax();
    void g6_valueOne();
    void e11_crcOrderIsLowByteFirst();
    void gAll_independentCrcAgreesWithHardCodedLiterals();

    // ---- encoder domain (E7–E12) ----
    void e7_unitZeroRejected();
    void e8_unitAbove247Rejected();
    void e9_descriptorFunctionIs0x06();
    void e10_wireSizeIsEight();
    void e12_writeMultipleRegistersStillUnsupported();

    // ---- descriptor contract ----
    void d1_descriptorKeepsIntentFrameAndWire();
    void d2_descriptorIsImmutableCopy();

    // ---- staging: encoder exists, capability does NOT (S1 / S2 / S6) ----
    void s1_encoderSucceedsWhileSessionStillRefuses();
    void s6_sessionRefusesToBeginWriteSingleRegister();
};

void WriteEncoderTest::g1_canonical()
{
    const auto frame = encodeWriteSingleRegisterRequest(1, 0, 0);
    QCOMPARE(frame.address, std::uint8_t{1});
    QCOMPARE(frame.functionCode, std::uint8_t{0x06});
    const std::vector<std::uint8_t> expectedData = {0x00, 0x00, 0x00, 0x00};
    QCOMPARE(frame.data, expectedData);
}

void WriteEncoderTest::g2_highBoundary()
{
    const auto frame = encodeWriteSingleRegisterRequest(247, 65535, 65535);
    QCOMPARE(frame.address, std::uint8_t{247});
    QCOMPARE(frame.functionCode, std::uint8_t{0x06});
    const std::vector<std::uint8_t> expectedData = {0xFF, 0xFF, 0xFF, 0xFF};
    QCOMPARE(frame.data, expectedData);
}

void WriteEncoderTest::g3_protocolExample()
{
    const auto frame = encodeWriteSingleRegisterRequest(0x11, 0x0001, 0x0003);
    QCOMPARE(frame.address, std::uint8_t{0x11});
    const std::vector<std::uint8_t> expectedData = {0x00, 0x01, 0x00, 0x03};
    QCOMPARE(frame.data, expectedData);
}

void WriteEncoderTest::g4_addressMax()
{
    const auto frame = encodeWriteSingleRegisterRequest(1, 65535, 0);
    const std::vector<std::uint8_t> expectedData = {0xFF, 0xFF, 0x00, 0x00};
    QCOMPARE(frame.data, expectedData);
}

void WriteEncoderTest::g5_valueMax()
{
    const auto frame = encodeWriteSingleRegisterRequest(1, 0, 65535);
    const std::vector<std::uint8_t> expectedData = {0x00, 0x00, 0xFF, 0xFF};
    QCOMPARE(frame.data, expectedData);
}

void WriteEncoderTest::g6_valueOne()
{
    const auto frame = encodeWriteSingleRegisterRequest(1, 0, 1);
    const std::vector<std::uint8_t> expectedData = {0x00, 0x00, 0x00, 0x01};
    QCOMPARE(frame.data, expectedData);
}

void WriteEncoderTest::e11_crcOrderIsLowByteFirst()
{
    // V1.02 places the CRC low byte first. Asserted against the independent
    // implementation rather than by re-reading the production CRC helper.
    for (const GoldenVector& vector : goldenVectors()) {
        const auto encoded =
            encodeActiveRequest(writeIntent(vector.unit, vector.address,
                                            vector.value));
        const auto* descriptor =
            std::get_if<ActiveRequestDescriptor>(&encoded);
        QVERIFY2(descriptor != nullptr, vector.name);
        const auto& wire = descriptor->wire;
        QCOMPARE(wire.size(), std::size_t{8});
        const std::uint16_t crc =
            independentCrc(std::span<const std::uint8_t>(wire.data(), 6));
        QCOMPARE(wire[6], static_cast<std::uint8_t>(crc & 0xFF));
        QCOMPARE(wire[7], static_cast<std::uint8_t>((crc >> 8) & 0xFF));
    }
}

void WriteEncoderTest::gAll_independentCrcAgreesWithHardCodedLiterals()
{
    // The fixed literals in the table were derived by hand + the independent
    // CRC; this checks the two independent sources agree with each other AND
    // with the production descriptor. A mismatch means the encoder or the
    // derivation is wrong — never "adjust the expected bytes".
    for (const GoldenVector& vector : goldenVectors()) {
        QVERIFY2(vector.hasHardCodedLiteral, vector.name);
        const std::uint16_t crc = independentCrc(
            std::span<const std::uint8_t>(vector.expectedAdu.data(), 6));
        QCOMPARE(vector.expectedAdu[6], static_cast<std::uint8_t>(crc & 0xFF));
        QCOMPARE(vector.expectedAdu[7], static_cast<std::uint8_t>((crc >> 8) & 0xFF));

        const auto encoded =
            encodeActiveRequest(writeIntent(vector.unit, vector.address,
                                            vector.value));
        const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
        QVERIFY2(descriptor != nullptr, vector.name);
        QCOMPARE(descriptor->wire, vector.expectedAdu);
    }
}

void WriteEncoderTest::e7_unitZeroRejected()
{
    const auto encoded = encodeActiveRequest(writeIntent(0, 10, 1));
    const auto* error = std::get_if<ActiveRequestEncodeError>(&encoded);
    QVERIFY(error != nullptr);
    // Broadcast is refused by the intent validation BEFORE any encoding, so no
    // descriptor (and therefore no wire) can exist for unit 0.
    QCOMPARE(error->code, ActiveRequestEncodeErrorCode::IntentInvalid);
}

void WriteEncoderTest::e8_unitAbove247Rejected()
{
    const auto encoded = encodeActiveRequest(writeIntent(248, 10, 1));
    const auto* error = std::get_if<ActiveRequestEncodeError>(&encoded);
    QVERIFY(error != nullptr);
    QCOMPARE(error->code, ActiveRequestEncodeErrorCode::IntentInvalid);
}

void WriteEncoderTest::e9_descriptorFunctionIs0x06()
{
    const auto encoded = encodeActiveRequest(writeIntent(7, 0x1234, 0x5678));
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);
    QCOMPARE(descriptor->intent.function, ActiveFunction::WriteSingleRegister);
    QCOMPARE(descriptor->frame.functionCode, std::uint8_t{0x06});
    QCOMPARE(descriptor->wire[1], std::uint8_t{0x06});
}

void WriteEncoderTest::e10_wireSizeIsEight()
{
    // unit(1) + function(1) + address(2) + value(2) + CRC(2)
    const auto encoded = encodeActiveRequest(writeIntent(1, 42, 43));
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);
    QCOMPARE(descriptor->wire.size(), std::size_t{8});
    QCOMPARE(descriptor->frame.data.size(), std::size_t{4});
}

void WriteEncoderTest::e12_writeMultipleRegistersStillUnsupported()
{
    // 0x10 stays ABSENT: D1 adds the 0x06 encoder only.
    const auto intent = ActiveRequestIntent{
        .function = ActiveFunction::WriteMultipleRegisters,
        .unitId = 1,
        .timeout = std::chrono::milliseconds{1000},
        .payload = modbuslens::core::WriteMultipleRegistersIntent{
            .startAddress = 0, .values = {1, 2}},
    };
    const auto encoded = encodeActiveRequest(intent);
    const auto* error = std::get_if<ActiveRequestEncodeError>(&encoded);
    QVERIFY(error != nullptr);
    QCOMPARE(error->code, ActiveRequestEncodeErrorCode::UnsupportedFunction);
}

void WriteEncoderTest::d1_descriptorKeepsIntentFrameAndWire()
{
    const auto intent = writeIntent(3, 0x00FF, 0x0100);
    const auto encoded = encodeActiveRequest(intent);
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);
    QCOMPARE(descriptor->intent, intent);
    QCOMPARE(descriptor->frame.address, std::uint8_t{3});
    QCOMPARE(descriptor->frame.functionCode, std::uint8_t{0x06});
    const std::vector<std::uint8_t> expectedData = {0x00, 0xFF, 0x01, 0x00};
    QCOMPARE(descriptor->frame.data, expectedData);
    QCOMPARE(descriptor->wire.size(), std::size_t{8});
}

void WriteEncoderTest::d2_descriptorIsImmutableCopy()
{
    ActiveRequestIntent intent = writeIntent(1, 100, 200);
    const auto encoded = encodeActiveRequest(intent);
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);

    // Editing the source intent afterwards must not reach the descriptor: it
    // owns its own copy, never a pointer/reference into a draft.
    intent.unitId = 9;
    std::get<WriteSingleRegisterIntent>(intent.payload).value = 999;
    QCOMPARE(descriptor->intent.unitId, std::uint8_t{1});
    QCOMPARE(std::get<WriteSingleRegisterIntent>(descriptor->intent.payload).value,
             std::uint16_t{200});
    QCOMPARE(descriptor->frame.address, std::uint8_t{1});
}

void WriteEncoderTest::s1_encoderSucceedsWhileSessionStillRefuses()
{
    // STAGING PROOF (M10-D Phase 1 §T): the encoder existing is NOT protocol
    // support. A 0x06 request encodes fine while the session still refuses to
    // begin an active 0x06 transaction — so nothing can send these bytes until
    // M10-D2 lands the response lifecycle.
    const auto encoded = encodeActiveRequest(writeIntent(1, 10, 20));
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);
    QCOMPARE(descriptor->wire.size(), std::size_t{8});

    modbuslens::core::SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(*descriptor);
    const auto* error =
        std::get_if<modbuslens::core::SerialTransactionError>(&begin);
    QVERIFY(error != nullptr);
    QCOMPARE(error->code,
             modbuslens::core::SerialTransactionErrorCode::UnsupportedFunction);
    QCOMPARE(session.state(), modbuslens::core::SerialTransactionState::Idle);
    QVERIFY(!session.pendingRequest().has_value());
}

void WriteEncoderTest::s6_sessionRefusesToBeginWriteSingleRegister()
{
    // The internal capability seam is still closed for 0x06 (D1 staging).
    QVERIFY(!modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteSingleRegister));
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::ReadHoldingRegisters));
    QVERIFY(!modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteMultipleRegisters));
}

QTEST_GUILESS_MAIN(WriteEncoderTest)
#include "test_write_encoder.moc"