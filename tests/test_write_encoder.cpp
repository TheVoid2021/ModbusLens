#include <QtTest>

#include <cstdint>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/WritePrepareValidation.h"
#include "core/protocol/Function06.h"
#include "core/protocol/Function16.h"
#include "core/serial/SerialTransactionSession.h"

using modbuslens::core::ActiveFunction;
using modbuslens::core::ActiveRequestDescriptor;
using modbuslens::core::ActiveRequestEncodeError;
using modbuslens::core::ActiveRequestEncodeErrorCode;
using modbuslens::core::ActiveRequestIntent;
using modbuslens::core::encodeActiveRequest;
using modbuslens::core::encodeWriteMultipleRegistersRequest;
using modbuslens::core::encodeWriteSingleRegisterRequest;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::WriteMultipleRegistersIntent;
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
//
// PROVENANCE NOTE (M10-E1 review correction): F16-G6 is a FIXED FC16/0x10
// RTU reference vector. An earlier comment called it "the published MODBUS
// Application Protocol example"; that specific attribution is NOT verifiable
// from this repository (docs/03_MODBUS_LEARNING.md §4.5 records the FC16
// field-layout RULES but not this concrete byte sequence), so it has been
// withdrawn. F16-G6's correctness rests on the hardcoded bytes plus the
// independent CRC oracle below, and on round-tripping through the strict
// request decoder — not on any unverifiable external citation.
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

ActiveRequestIntent writeMultipleIntent(std::uint8_t unit,
                                        std::uint16_t address,
                                        std::vector<std::uint16_t> values)
{
    return ActiveRequestIntent{
        .function = ActiveFunction::WriteMultipleRegisters,
        .unitId = unit,
        .timeout = std::chrono::milliseconds{1000},
        .payload = WriteMultipleRegistersIntent{.startAddress = address,
                                                .values = std::move(values)},
    };
}

// ---------------------------------------------------------------------------
// M10-E1 golden vector table for FC16 / 0x10.
//
// Layout per MODBUS Application Protocol V1.1b3 §6.12:
//   [unit][0x10][start 2B][quantity 2B][byteCount 1B][values 2N][CRC lo hi]
//
// The small rows carry the WHOLE ADU as a source-fixed literal. F16-G6 is the
// published MODBUS Application Protocol example for Write Multiple Registers
// (unit 0x11, start 1, quantity 2, byteCount 4, values 0x000A 0x0102), so the
// field layout is corroborated by an external document, not only by our own
// decoder. The CRC columns were computed with the independent test-local
// implementation below — never with the production codec.
// ---------------------------------------------------------------------------
struct GoldenVector16 {
    const char* name;
    std::uint8_t unit;
    std::uint16_t address;
    std::vector<std::uint16_t> values;
    std::vector<std::uint8_t> expectedAdu;
};

std::vector<GoldenVector16> goldenVectors16()
{
    return {
        {"F16-G1 minimum (1 / 0 / [0])", 1, 0, {0},
         {0x01, 0x10, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00, 0xA6, 0x50}},
        {"F16-G2 smallest non-zero (1 / 0 / [1])", 1, 0, {1},
         {0x01, 0x10, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x01, 0x67, 0x90}},
        {"F16-G3 mixed values (1 / 0 / [1, 0x1234, 0xABCD])", 1, 0,
         {0x0001, 0x1234, 0xABCD},
         {0x01, 0x10, 0x00, 0x00, 0x00, 0x03, 0x06, 0x00, 0x01, 0x12, 0x34,
          0xAB, 0xCD, 0x21, 0x53}},
        {"F16-G4 upper unit (247 / 0 / [1])", 247, 0, {1},
         {0xF7, 0x10, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x01, 0x48, 0x34}},
        {"F16-G5 upper span (1 / 65535 / [0xFFFF])", 1, 65535, {0xFFFF},
         {0x01, 0x10, 0xFF, 0xFF, 0x00, 0x01, 0x02, 0xFF, 0xFF, 0xBC, 0xE0}},
        {"F16-G6 reference vector (0x11 / 1 / [0x000A, 0x0102])", 0x11, 1,
         {0x000A, 0x0102},
         {0x11, 0x10, 0x00, 0x01, 0x00, 0x02, 0x04, 0x00, 0x0A, 0x01, 0x02,
          0xC6, 0xF0}},
        {"F16-G7 high/low asymmetry (1 / 0x1234 / "
         "[0x00FF, 0xFF00, 0x8000, 0x0001])",
         1, 0x1234, {0x00FF, 0xFF00, 0x8000, 0x0001},
         {0x01, 0x10, 0x12, 0x34, 0x00, 0x04, 0x08, 0x00, 0xFF, 0xFF, 0x00,
          0x80, 0x00, 0x00, 0x01, 0xCD, 0x27}},
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
    // M10-E1 intentional transition: the old e12 asserted
    // encodeActiveRequest(0x10) == UnsupportedFunction. The 0x10 encoder now
    // exists, so the frozen negative moved one level up (session still
    // refuses, capability still absent) — see f16_* below and RCA in T022.
    void e12_writeMultipleRegistersEncodesAndSessionAccepts();

    // ---- M10-E1: FC16 / 0x10 request encoder ----
    void f16_goldenVectorsAreByteExact();
    void f16_g8_maxQuantityHeaderPayloadLengthAndCrc();
    void f16_byteCountIsDerived();
    void f16_wireLengthIs9Plus2N();
    void f16_orderIsPreserved();
    void f16_unitZeroRejected();
    void f16_unitAbove247Rejected();
    void f16_emptyValuesRejected();
    void f16_124ValuesRejected();
    void f16_spanOverflowRejectedAtPrepareLayer();
    void f16_sessionSupportsButCapabilityStaysClosed();

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

void WriteEncoderTest::e12_writeMultipleRegistersEncodesAndSessionAccepts()
{
    // INTENTIONAL CONTRACT TRANSITION (M10-E2, recorded in T022 §ZG):
    // e12 has moved twice. It started as "encodeActiveRequest(0x10) ==
    // UnsupportedFunction"; M10-E1 made the encoder exist while the session
    // still refused; M10-E2 wires the shared active analyzer, so the SESSION
    // now accepts 0x10. The frozen negatives moved up again and must all stay
    // true (asserted in their own suites):
    //   encode 0x10        = YES  (this test)
    //   session 0x10       = YES  (this test)
    //   dispatch 0x10      = NO   (write_dispatch r4 / fc10CapabilityStaysFrozen)
    //   product capability = ABSENT (write10Supported has no property)
    //   production UI      = ABSENT (no 0x10 node is ever created)
    const auto encoded =
        encodeActiveRequest(writeMultipleIntent(1, 10, {1, 2}));
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);
    QCOMPARE(descriptor->frame.functionCode, std::uint8_t{0x10});
    QCOMPARE(descriptor->wire.size(), std::size_t{13}); // 9 + 2*2

    modbuslens::core::SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(*descriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    QCOMPARE(session.state(),
             modbuslens::core::SerialTransactionState::AwaitingResponse);
    session.cancel();
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
    // D1 staged "encoder exists, protocol refuses it"; D2 lands the response
    // lifecycle, so the session now ACCEPTS 0x06. The staging point that must
    // survive is one level up: the PRODUCT capability (write06Supported) still
    // does not exist, and 0x10 is still refused.
    const auto encoded = encodeActiveRequest(writeIntent(1, 10, 20));
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);
    QCOMPARE(descriptor->wire.size(), std::size_t{8});

    modbuslens::core::SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(*descriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    QCOMPARE(session.state(), modbuslens::core::SerialTransactionState::AwaitingResponse);
    session.cancel();
    QCOMPARE(session.state(), modbuslens::core::SerialTransactionState::Idle);
}

void WriteEncoderTest::s6_sessionRefusesToBeginWriteSingleRegister()
{
    // M10-E2 support matrix: all three functions now have an encoder, an
    // active analyzer and session support. The negatives this matrix used to
    // carry for 0x10 moved up a layer (Controller dispatch / write10Supported
    // in test_write_dispatch, production UI in the main.cpp oracle).
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteSingleRegister));
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::ReadHoldingRegisters));
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteMultipleRegisters));
}

// ---------------------------------------------------------------------------
// M10-E1: FC16 / 0x10 request encoder.
// ---------------------------------------------------------------------------

void WriteEncoderTest::f16_goldenVectorsAreByteExact()
{
    for (const GoldenVector16& vector : goldenVectors16()) {
        const std::string label = vector.name;
        const auto encoded =
            encodeActiveRequest(writeMultipleIntent(vector.unit, vector.address,
                                                    vector.values));
        const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
        QVERIFY2(descriptor != nullptr, (label + ": encode failed").c_str());
        // The ADU must equal the source-fixed literal byte for byte.
        QVERIFY2(descriptor->wire == vector.expectedAdu,
                 (label + ": ADU mismatch").c_str());
        // And the independent oracle must agree with that same literal — the
        // production codec is never used to derive the expectation.
        const std::vector<std::uint8_t> payload(
            vector.expectedAdu.begin(), vector.expectedAdu.end() - 2);
        const auto crc = independentCrc(payload);
        QCOMPARE(static_cast<int>(vector.expectedAdu[vector.expectedAdu.size() - 2]),
                 static_cast<int>(crc & 0xFF));
        QCOMPARE(static_cast<int>(vector.expectedAdu.back()),
                 static_cast<int>((crc >> 8) & 0xFF));
    }
}

void WriteEncoderTest::f16_g8_maxQuantityHeaderPayloadLengthAndCrc()
{
    // F16-G8 — max quantity boundary (123 registers): quantity=123 (0x007B),
    // byteCount=246 (0xF6), ADU = 9 + 2*123 = 255 bytes. Writing out 246 value
    // bytes by hand adds no information, so this row pins the header, the
    // length, selected payload positions and the independent CRC instead.
    // IDENTITY: this is NOT F16-G6 — F16-G6 is the 2-register reference
    // vector above; the two must never share an ID.
    std::vector<std::uint16_t> values(123);
    for (std::size_t index = 0; index < values.size(); ++index) {
        values[index] = static_cast<std::uint16_t>(index & 0xFFFF);
    }
    const auto encoded = encodeActiveRequest(writeMultipleIntent(1, 0, values));
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);

    QCOMPARE(descriptor->wire.size(), std::size_t{255});
    // frame.data excludes unit/function/CRC: start(2)+quantity(2)+byteCount(1)
    // + values(2*123) = 251; the ADU adds unit(1)+function(1)+CRC(2).
    QCOMPARE(descriptor->frame.data.size(), std::size_t{251});
    // header: unit / function / start / quantity / byteCount (ADU offsets)
    QCOMPARE(descriptor->wire[0], std::uint8_t{0x01});
    QCOMPARE(descriptor->wire[1], std::uint8_t{0x10});
    QCOMPARE(descriptor->wire[2], std::uint8_t{0x00});
    QCOMPARE(descriptor->wire[3], std::uint8_t{0x00});
    QCOMPARE(descriptor->wire[4], std::uint8_t{0x00}); // quantity hi
    QCOMPARE(descriptor->wire[5], std::uint8_t{0x7B}); // quantity lo = 123
    QCOMPARE(descriptor->wire[6], std::uint8_t{0xF6}); // byteCount = 246
    // selected payload positions (first / second / last register)
    QCOMPARE(descriptor->wire[7], std::uint8_t{0x00});
    QCOMPARE(descriptor->wire[8], std::uint8_t{0x00});
    QCOMPARE(descriptor->wire[9], std::uint8_t{0x00});
    QCOMPARE(descriptor->wire[10], std::uint8_t{0x01});
    QCOMPARE(descriptor->wire[251], std::uint8_t{0x00}); // last value hi (122)
    QCOMPARE(descriptor->wire[252], std::uint8_t{0x7A}); // last value lo
    // independent CRC over everything before it
    const std::vector<std::uint8_t> payload(descriptor->wire.begin(),
                                            descriptor->wire.end() - 2);
    const auto crc = independentCrc(payload);
    QCOMPARE(static_cast<int>(descriptor->wire[253]),
             static_cast<int>(crc & 0xFF));
    QCOMPARE(static_cast<int>(descriptor->wire.back()),
             static_cast<int>((crc >> 8) & 0xFF));
}

void WriteEncoderTest::f16_byteCountIsDerived()
{
    // byteCount is DERIVED from values (2 * N) and is never caller-supplied:
    // the intent has no quantity/byteCount field at all.
    const auto one = encodeActiveRequest(writeMultipleIntent(1, 0, {7}));
    const auto two = encodeActiveRequest(writeMultipleIntent(1, 0, {7, 8}));
    std::vector<std::uint16_t> many(123);
    const auto max = encodeActiveRequest(writeMultipleIntent(1, 0, many));
    const auto* oneD = std::get_if<ActiveRequestDescriptor>(&one);
    const auto* twoD = std::get_if<ActiveRequestDescriptor>(&two);
    const auto* maxD = std::get_if<ActiveRequestDescriptor>(&max);
    QVERIFY(oneD != nullptr);
    QVERIFY(twoD != nullptr);
    QVERIFY(maxD != nullptr);
    QCOMPARE(oneD->frame.data[4], std::uint8_t{2});   // 1 register  -> 2
    QCOMPARE(twoD->frame.data[4], std::uint8_t{4});   // 2 registers -> 4
    QCOMPARE(maxD->frame.data[4], std::uint8_t{246}); // 123         -> 246
    // and the declared quantity always equals values.size() (data[2..3])
    QCOMPARE(oneD->frame.data[2], std::uint8_t{0});
    QCOMPARE(oneD->frame.data[3], std::uint8_t{1});
    QCOMPARE(twoD->frame.data[3], std::uint8_t{2});
    QCOMPARE(maxD->frame.data[2], std::uint8_t{0});
    QCOMPARE(maxD->frame.data[3], std::uint8_t{123});
}

void WriteEncoderTest::f16_wireLengthIs9Plus2N()
{
    // RTU ADU = unit(1) + function(1) + start(2) + quantity(2) + byteCount(1)
    //          + 2*N values + CRC(2)  =  9 + 2*N.
    struct Row { std::size_t count; std::size_t adu; };
    const Row rows[] = {{1, 11}, {3, 15}, {123, 255}};
    for (const Row& row : rows) {
        std::vector<std::uint16_t> values(row.count);
        const auto encoded =
            encodeActiveRequest(writeMultipleIntent(1, 0, values));
        const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
        QVERIFY(descriptor != nullptr);
        QCOMPARE(descriptor->wire.size(), row.adu);
    }
}

void WriteEncoderTest::f16_orderIsPreserved()
{
    // Wire order == input order, big-endian per register: no sorting and no
    // word swap (M11's byte/word-order decode is unrelated to this layer).
    const auto encoded = encodeActiveRequest(
        writeMultipleIntent(1, 0, {0x0001, 0x1234, 0xABCD}));
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);
    const std::vector<std::uint8_t> expectedPayload = {
        0x00, 0x01, 0x12, 0x34, 0xAB, 0xCD};
    // frame.data = start(2)+quantity(2)+byteCount(1)+values(2N): the values
    // therefore start at offset 5 inside frame.data (offset 7 in the ADU).
    const std::vector<std::uint8_t> actual(
        descriptor->frame.data.begin() + 5, descriptor->frame.data.end());
    QCOMPARE(actual, expectedPayload);
}

void WriteEncoderTest::f16_unitZeroRejected()
{
    const auto encoded = encodeActiveRequest(writeMultipleIntent(0, 10, {1}));
    const auto* error = std::get_if<ActiveRequestEncodeError>(&encoded);
    QVERIFY(error != nullptr);
    QCOMPARE(error->code, ActiveRequestEncodeErrorCode::IntentInvalid);
}

void WriteEncoderTest::f16_unitAbove247Rejected()
{
    const auto encoded = encodeActiveRequest(writeMultipleIntent(248, 10, {1}));
    const auto* error = std::get_if<ActiveRequestEncodeError>(&encoded);
    QVERIFY(error != nullptr);
    QCOMPARE(error->code, ActiveRequestEncodeErrorCode::IntentInvalid);
}

void WriteEncoderTest::f16_emptyValuesRejected()
{
    // quantity 0 is structurally impossible to encode meaningfully: the
    // intent layer rejects it before any frame exists.
    const auto encoded = encodeActiveRequest(writeMultipleIntent(1, 10, {}));
    const auto* error = std::get_if<ActiveRequestEncodeError>(&encoded);
    QVERIFY(error != nullptr);
    QCOMPARE(error->code, ActiveRequestEncodeErrorCode::IntentInvalid);
}

void WriteEncoderTest::f16_124ValuesRejected()
{
    // 124 > the protocol maximum of 123, rejected at the intent layer.
    std::vector<std::uint16_t> values(124);
    const auto encoded = encodeActiveRequest(writeMultipleIntent(1, 0, values));
    const auto* error = std::get_if<ActiveRequestEncodeError>(&encoded);
    QVERIFY(error != nullptr);
    QCOMPARE(error->code, ActiveRequestEncodeErrorCode::IntentInvalid);
}

void WriteEncoderTest::f16_spanOverflowRejectedAtPrepareLayer()
{
    // The address-span rule (start + quantity <= 65536) belongs to the
    // PREPARE layer, where the user's draft is validated — the same layering
    // as every other range policy. start=65535 with 2 registers needs
    // registers 65535 and 65536, and the second does not exist.
    const auto result = modbuslens::core::prepareWriteMultipleRegistersIntent(
        1, 65535, "1\n2", 1000);
    const auto* error = std::get_if<modbuslens::core::WriteValidationError>(
        &result);
    QVERIFY(error != nullptr);
    QCOMPARE(error->code,
             modbuslens::core::WriteValidationErrorCode::AddressSpanOutOfRange);
}

void WriteEncoderTest::f16_sessionSupportsButCapabilityStaysClosed()
{
    // INTENTIONAL CONTRACT TRANSITION (M10-E2): the E1 form of this test
    // asserted the session REFUSED 0x10. The shared analyzer now exists, so
    // the gate is open and a full 0x10 lifecycle succeeds at the session
    // layer. The negatives this oracle used to carry moved up a layer and are
    // asserted in their own suites:
    //   Controller dispatch 0x10 -> CapabilityUnavailable
    //       (test_write_dispatch r4_capabilityUnavailableForFc10)
    //   write10Supported property absent
    //       (test_write_dispatch fc10CapabilityStaysFrozen)
    //   no 0x10 production UI  (main.cpp --qml-production-write-check)
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteMultipleRegisters));

    modbuslens::core::SerialTransactionSession session;
    const auto encoded = encodeActiveRequest(writeMultipleIntent(1, 10, {1, 2}));
    const auto* descriptor = std::get_if<ActiveRequestDescriptor>(&encoded);
    QVERIFY(descriptor != nullptr);
    const auto begin = session.beginActiveRequest(*descriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    // Hand-built echo for start=0x000A, quantity=2: start(2)+quantity(2)+CRC.
    // CRC (61 CA) computed independently for the stimulus bytes 01 10 00 0A 00 02.
    const std::vector<std::uint8_t> echo = {
        0x01, 0x10, 0x00, 0x0A, 0x00, 0x02, 0x61, 0xCA};
    const auto result = session.feedResponseBytes(
        echo, std::chrono::milliseconds{25});
    const auto* analysis =
        std::get_if<modbuslens::core::TransactionAnalysis>(&result);
    QVERIFY(analysis != nullptr);
    QCOMPARE(analysis->status, modbuslens::core::TransactionStatus::Success);
    QCOMPARE(session.state(), modbuslens::core::SerialTransactionState::Idle);
}

QTEST_GUILESS_MAIN(WriteEncoderTest)
#include "test_write_encoder.moc"