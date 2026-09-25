#include <QtTest>

#include <cstdint>
#include <string>

#include "core/analysis/RegisterDecode.h"

using modbuslens::core::RegisterDecodedWord;
using modbuslens::core::decodeEffectiveWord;
using modbuslens::core::decodeRegisterWord;
using modbuslens::core::RegisterByteOrder;
using modbuslens::core::RegisterDecodeStatus;
using modbuslens::core::RegisterDecodeType;

namespace {

// Independent derivation helpers (the oracle is NOT the production decoder):
// byte swap written straight from the definition, and the two's-complement
// conversion done in int arithmetic with an explicit threshold.
std::uint16_t swapBytes(std::uint16_t word)
{
    return static_cast<std::uint16_t>(((word & 0xFF) << 8) | (word >> 8));
}

int int16Value(std::uint16_t word)
{
    int value = static_cast<int>(word);
    if (value >= 0x8000) {
        value -= 0x10000;
    }
    return value;
}

RegisterDecodedWord decode(std::uint16_t rawWord, RegisterDecodeType type,
                           bool swapped)
{
    return decodeRegisterWord(rawWord, type,
                              swapped ? RegisterByteOrder::ByteSwapped
                                      : RegisterByteOrder::Normal);
}

} // namespace

class RegisterDecodeTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- UInt16 (the frozen default view, T024 §22 C1) ----
    void u1_zero();
    void u2_max();
    void u3_normal0x1234();
    void u4_byteSwapped0x1234();

    // ---- Int16 (two's complement, no implementation-defined cast) ----
    void i1_zero();
    void i2_minusOne();
    void i3_negativeBoundary();
    void i4_byteSwappedNegative();

    // ---- Hex / Binary derived formatting views ----
    void h1_hexNormal();
    void h2_hexByteSwapped();
    void b1_binaryNormal();
    void b2_binaryByteSwapped();

    // ---- raw truth preservation ----
    void r1_decodeNeverMutatesTheRawWord();
    void r2_configChangeDoesNotMutateRaw();

    // ---- status contract ----
    void s1_unknownTypeIsUnsupported();
    void s2_statusTokens();

    // ---- effective-word helper ----
    void e1_effectiveWordBothOrders();
};

void RegisterDecodeTest::u1_zero()
{
    const auto result = decode(0x0000, RegisterDecodeType::UInt16, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("0"));
}

void RegisterDecodeTest::u2_max()
{
    const auto result = decode(0xFFFF, RegisterDecodeType::UInt16, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("65535"));
}

void RegisterDecodeTest::u3_normal0x1234()
{
    // The T024 §J1 contract vector: raw 0x1234, Normal → 4660.
    const auto result = decode(0x1234, RegisterDecodeType::UInt16, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("4660"));
}

void RegisterDecodeTest::u4_byteSwapped0x1234()
{
    // The T024 §J1 contract vector: raw 0x1234, Byte-swapped → 0x3412 → 13330.
    // The RAW word stays 0x1234 — asserted by r1 below through the helper.
    const auto result = decode(0x1234, RegisterDecodeType::UInt16, true);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("13330"));
}

void RegisterDecodeTest::i1_zero()
{
    const auto result = decode(0x0000, RegisterDecodeType::Int16, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("0"));
}

void RegisterDecodeTest::i2_minusOne()
{
    const auto result = decode(0xFFFF, RegisterDecodeType::Int16, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("-1"));
}

void RegisterDecodeTest::i3_negativeBoundary()
{
    const auto result = decode(0x8000, RegisterDecodeType::Int16, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QString::number(int16Value(0x8000)));
}

void RegisterDecodeTest::i4_byteSwappedNegative()
{
    // Raw 0x0080 byte-swapped is 0x8000 → −32768 (the A29 acceptance vector).
    const auto result = decode(0x0080, RegisterDecodeType::Int16, true);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QString::number(int16Value(swapBytes(0x0080))));
}

void RegisterDecodeTest::h1_hexNormal()
{
    const auto result = decode(0x1234, RegisterDecodeType::Hex, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("0x1234"));
}

void RegisterDecodeTest::h2_hexByteSwapped()
{
    // Derived Hex is a VIEW: the raw HEX column stays 0x1234 (r1) while the
    // derived view shows the effective word 0x3412 (T024 §J3).
    const auto result = decode(0x1234, RegisterDecodeType::Hex, true);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("0x3412"));
}

void RegisterDecodeTest::b1_binaryNormal()
{
    const auto result = decode(0x0001, RegisterDecodeType::Binary, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("0b0000000000000001"));
}

void RegisterDecodeTest::b2_binaryByteSwapped()
{
    const auto result = decode(0x1234, RegisterDecodeType::Binary, true);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("0b0011010000010010"));
}

void RegisterDecodeTest::r1_decodeNeverMutatesTheRawWord()
{
    // Raw truth preservation, checked through the independent helper: the
    // effective word only changes when the byte-order view asks for it, and
    // the raw word itself is always still readable unchanged.
    for (const std::uint16_t raw : {std::uint16_t{0x0000}, std::uint16_t{0x1234},
                                    std::uint16_t{0x8000}, std::uint16_t{0xFFFF}}) {
        const auto normal = decode(raw, RegisterDecodeType::UInt16, false);
        const auto swapped = decode(raw, RegisterDecodeType::UInt16, true);
        QCOMPARE(QString::fromStdString(normal.text).toUShort(),
                 static_cast<ushort>(raw));
        QCOMPARE(QString::fromStdString(swapped.text).toUShort(),
                 static_cast<ushort>(swapBytes(raw)));
    }
}

void RegisterDecodeTest::r2_configChangeDoesNotMutateRaw()
{
    // Switching decode type/byte order re-derives a DIFFERENT view of the SAME
    // raw word: all four views of one raw word must agree with the independent
    // derivations, and the raw word must remain what it was.
    const std::uint16_t raw = 0x8001;
    QCOMPARE(decodeEffectiveWord(raw, RegisterByteOrder::Normal), raw);
    QCOMPARE(decodeEffectiveWord(raw, RegisterByteOrder::ByteSwapped),
             swapBytes(raw));
    const auto hex = decode(raw, RegisterDecodeType::Hex, false);
    const auto bin = decode(raw, RegisterDecodeType::Binary, false);
    const auto u16 = decode(raw, RegisterDecodeType::UInt16, false);
    const auto i16 = decode(raw, RegisterDecodeType::Int16, false);
    QCOMPARE(QString::fromStdString(hex.text), QStringLiteral("0x8001"));
    QCOMPARE(QString::fromStdString(bin.text), QStringLiteral("0b1000000000000001"));
    QCOMPARE(QString::fromStdString(u16.text), QStringLiteral("32769"));
    QCOMPARE(QString::fromStdString(i16.text), QStringLiteral("-32767"));
    QCOMPARE(static_cast<int>(raw), 0x8001);
}

void RegisterDecodeTest::s1_unknownTypeIsUnsupported()
{
    // An out-of-matrix type (a caller bug or a future type) yields the
    // honest UnsupportedType with no fabricated text — never a guessed value.
    const auto broken = decodeRegisterWord(
        0x1234, static_cast<RegisterDecodeType>(99), RegisterByteOrder::Normal);
    QCOMPARE(broken.status, RegisterDecodeStatus::UnsupportedType);
    QVERIFY(broken.text.empty());
}

void RegisterDecodeTest::s2_statusTokens()
{
    QCOMPARE(QString::fromStdString(
                 std::string(modbuslens::core::registerDecodeStatusName(
                     RegisterDecodeStatus::Ok))),
             QStringLiteral("ok"));
    QCOMPARE(QString::fromStdString(
                 std::string(modbuslens::core::registerDecodeStatusName(
                     RegisterDecodeStatus::InsufficientWords))),
             QStringLiteral("insufficient_words"));
    QCOMPARE(QString::fromStdString(
                 std::string(modbuslens::core::registerDecodeStatusName(
                     RegisterDecodeStatus::OutOfRangeSelection))),
             QStringLiteral("out_of_range_selection"));
    QCOMPARE(QString::fromStdString(
                 std::string(modbuslens::core::registerDecodeStatusName(
                     RegisterDecodeStatus::InvalidConfiguration))),
             QStringLiteral("invalid_configuration"));
    QCOMPARE(QString::fromStdString(
                 std::string(modbuslens::core::registerDecodeStatusName(
                     RegisterDecodeStatus::UnsupportedType))),
             QStringLiteral("unsupported_type"));
}

void RegisterDecodeTest::e1_effectiveWordBothOrders()
{
    QCOMPARE(decodeEffectiveWord(0x1234, RegisterByteOrder::Normal),
             std::uint16_t{0x1234});
    QCOMPARE(decodeEffectiveWord(0x1234, RegisterByteOrder::ByteSwapped),
             std::uint16_t{0x3412});
    // The swap is an involution: swapping twice restores the raw word.
    QCOMPARE(decodeEffectiveWord(
                 decodeEffectiveWord(0x1234, RegisterByteOrder::ByteSwapped),
                 RegisterByteOrder::ByteSwapped),
             std::uint16_t{0x1234});
}

QTEST_GUILESS_MAIN(RegisterDecodeTest)
#include "test_register_decode.moc"