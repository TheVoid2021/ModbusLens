#include <QtTest>

#include <cstdint>
#include <cstring>
#include <string>

#include "core/analysis/RegisterDecode.h"

using modbuslens::core::RegisterDecodedPair;
using modbuslens::core::RegisterDecodedView;
using modbuslens::core::RegisterDecodedWord;
using modbuslens::core::RegisterWordOrder;
using modbuslens::core::decodeEffectiveWord;
using modbuslens::core::decodeRegisterPair;
using modbuslens::core::decodeRegisterView;
using modbuslens::core::decodeRegisterWord;
using modbuslens::core::registerDecodeTypeWordCount;
using modbuslens::core::registerWordOrderName;
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

// Independent 32-bit combination oracle (T024 §7 frozen pipeline, written
// from the definition): byte swap within each register, then word-order
// combination. The production decoder must agree with this, never with a
// second copy of itself.
std::uint32_t combineWords(std::uint16_t word0, std::uint16_t word1,
                           RegisterWordOrder wordOrder,
                           RegisterByteOrder byteOrder)
{
    const std::uint16_t e0 = byteOrder == RegisterByteOrder::ByteSwapped
                                 ? swapBytes(word0)
                                 : word0;
    const std::uint16_t e1 = byteOrder == RegisterByteOrder::ByteSwapped
                                 ? swapBytes(word1)
                                 : word1;
    const std::uint16_t high =
        wordOrder == RegisterWordOrder::HighWordFirst ? e0 : e1;
    const std::uint16_t low =
        wordOrder == RegisterWordOrder::HighWordFirst ? e1 : e0;
    return (static_cast<std::uint32_t>(high) << 16) | low;
}

// Independent 32-bit two's-complement oracle (int64 arithmetic, no
// implementation-defined conversion).
std::int64_t int32Value(std::uint32_t bits)
{
    std::int64_t value = bits;
    if (value >= 0x80000000LL) {
        value -= 0x100000000LL;
    }
    return value;
}

// Independent IEEE-754 bit reinterpretation oracle.
float bitsToFloat(std::uint32_t bits)
{
    float value = 0.0f;
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

RegisterDecodedPair decodePair(std::uint16_t word0, std::uint16_t word1,
                               RegisterDecodeType type,
                               RegisterWordOrder wordOrder, bool swapped)
{
    return decodeRegisterPair(word0, word1, type,
                              swapped ? RegisterByteOrder::ByteSwapped
                                      : RegisterByteOrder::Normal,
                              wordOrder);
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

    // ---- second slice: 32-bit pair decode (T024 §7 / §16 A07-A31) ----
    void w1_typeWordCounts();
    void w2_wordOrderTokens();
    void p1_pairEntryRejectsNonPairTypes();
    void u32_1_bigEndianDefault_A07();
    void u32_2_littleEndianWordOrder_A08();
    void u32_3_max();
    void u32_4_byteSwappedBigEndianWord();
    void u32_5_byteSwappedLittleEndianWord();
    void i32_1_negative_A09();
    void i32_2_min_A10();
    void i32_3_maxPositive();
    void i32_4_minusOne();
    void f32_1_one_A11();
    void f32_2_zero_A12();
    void f32_3_minusTwo_A13();
    void f32_4_nanIsOk_A14();
    void f32_5_posInfIsOk_A15();
    void f32_6_negInfIsOk();
    void f32_7_wordOrderReversal_A16();
    void f32_8_byteSwappedBigEndianWord_A30();
    void f32_9_byteSwappedLittleEndianWord_A31();
    void f32_10_tenthRoundTrip();
    void f32_11_piRoundTrip();

    // ---- second slice: decodeRegisterView (the single entry, T024 §12) ----
    void v1_viewSingleWordPassthrough();
    void v2_viewPairWindow();
    void v3_viewLastRegisterInsufficient_A17();
    void v4_viewStartOutOfRange();
    void v5_viewNegativeStartInvalidConfig();
    void v6_viewUnknownTypeInvalidConfig();
    void v7_viewEmptyWordsOutOfRange();
    void v8_viewWordOrderAxis();
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

// ===========================================================================
// M11 second slice: 32-bit pair decode + word order (T024 §7, §16 A07-A31).
// The frozen pipeline is byte swap within each register → word-order
// combination → target-type reinterpretation; every test below pins ONE cell
// of that matrix against the INDEPENDENT oracle helpers, never against a
// second copy of the production code.
// ===========================================================================

void RegisterDecodeTest::w1_typeWordCounts()
{
    QCOMPARE(registerDecodeTypeWordCount(RegisterDecodeType::Hex), 1);
    QCOMPARE(registerDecodeTypeWordCount(RegisterDecodeType::Binary), 1);
    QCOMPARE(registerDecodeTypeWordCount(RegisterDecodeType::UInt16), 1);
    QCOMPARE(registerDecodeTypeWordCount(RegisterDecodeType::Int16), 1);
    QCOMPARE(registerDecodeTypeWordCount(RegisterDecodeType::UInt32), 2);
    QCOMPARE(registerDecodeTypeWordCount(RegisterDecodeType::Int32), 2);
    QCOMPARE(registerDecodeTypeWordCount(RegisterDecodeType::Float32), 2);
    // Outside the v1 matrix: consumes nothing (defensive).
    QCOMPARE(registerDecodeTypeWordCount(static_cast<RegisterDecodeType>(99)),
             0);
}

void RegisterDecodeTest::w2_wordOrderTokens()
{
    QCOMPARE(QString::fromStdString(
                 std::string(registerWordOrderName(
                     RegisterWordOrder::HighWordFirst))),
             QStringLiteral("high_word_first"));
    QCOMPARE(QString::fromStdString(
                 std::string(registerWordOrderName(
                     RegisterWordOrder::LowWordFirst))),
             QStringLiteral("low_word_first"));
}

void RegisterDecodeTest::p1_pairEntryRejectsNonPairTypes()
{
    // The pair entry is ONLY the 2-register entry: a 16-bit view routed
    // through it must yield UnsupportedType with no fabricated text, never a
    // silent 16-bit reinterpretation of the combined bits.
    for (const auto type : {RegisterDecodeType::Hex,
                            RegisterDecodeType::Binary,
                            RegisterDecodeType::UInt16,
                            RegisterDecodeType::Int16}) {
        const auto result =
            decodePair(0x1234, 0x5678, type, RegisterWordOrder::HighWordFirst,
                       false);
        QCOMPARE(result.status, RegisterDecodeStatus::UnsupportedType);
        QVERIFY(result.text.empty());
    }
}

void RegisterDecodeTest::u32_1_bigEndianDefault_A07()
{
    // A07: [0x1234, 0x5678] big-endian (HighWordFirst, default) = 0x12345678.
    const auto result = decodePair(0x1234, 0x5678, RegisterDecodeType::UInt32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QString::number(combineWords(0x1234, 0x5678,
                                          RegisterWordOrder::HighWordFirst,
                                          RegisterByteOrder::Normal)));
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("305419896"));
}

void RegisterDecodeTest::u32_2_littleEndianWordOrder_A08()
{
    // A08: same words, LowWordFirst (CD AB) = 0x56781234. This is the
    // ordering pin: swapping the implementation's high/low choice would make
    // this produce A07's value and fail.
    const auto result = decodePair(0x1234, 0x5678, RegisterDecodeType::UInt32,
                                   RegisterWordOrder::LowWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("1450709556"));
}

void RegisterDecodeTest::u32_3_max()
{
    const auto result = decodePair(0xFFFF, 0xFFFF, RegisterDecodeType::UInt32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("4294967295"));
}

void RegisterDecodeTest::u32_4_byteSwappedBigEndianWord()
{
    // Byte order (within register) and word order (across registers) are
    // independent axes: swapping each register's bytes first, then combining
    // big-endian, must equal the Normal+big-endian view of the swapped words.
    const auto result = decodePair(0x3412, 0x7856, RegisterDecodeType::UInt32,
                                   RegisterWordOrder::HighWordFirst, true);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QString::number(combineWords(0x3412, 0x7856,
                                          RegisterWordOrder::HighWordFirst,
                                          RegisterByteOrder::ByteSwapped)));
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("305419896"));
}

void RegisterDecodeTest::u32_5_byteSwappedLittleEndianWord()
{
    const auto result = decodePair(0x3412, 0x7856, RegisterDecodeType::UInt32,
                                   RegisterWordOrder::LowWordFirst, true);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QString::number(combineWords(0x3412, 0x7856,
                                          RegisterWordOrder::LowWordFirst,
                                          RegisterByteOrder::ByteSwapped)));
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("1450709556"));
}

void RegisterDecodeTest::i32_1_negative_A09()
{
    // A09: [0xFFFF, 0xFF38] big-endian = 0xFFFFFF38 = −200 (two's complement).
    const auto result = decodePair(0xFFFF, 0xFF38, RegisterDecodeType::Int32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QString::number(int32Value(combineWords(
                 0xFFFF, 0xFF38, RegisterWordOrder::HighWordFirst,
                 RegisterByteOrder::Normal))));
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("-200"));
}

void RegisterDecodeTest::i32_2_min_A10()
{
    // A10: [0x8000, 0x0000] = INT32_MIN.
    const auto result = decodePair(0x8000, 0x0000, RegisterDecodeType::Int32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("-2147483648"));
}

void RegisterDecodeTest::i32_3_maxPositive()
{
    const auto result = decodePair(0x7FFF, 0xFFFF, RegisterDecodeType::Int32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("2147483647"));
}

void RegisterDecodeTest::i32_4_minusOne()
{
    const auto result = decodePair(0xFFFF, 0xFFFF, RegisterDecodeType::Int32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("-1"));
}

void RegisterDecodeTest::f32_1_one_A11()
{
    // A11: [0x3F80, 0x0000] big-endian = IEEE-754 binary32 1.0.
    const auto result = decodePair(0x3F80, 0x0000, RegisterDecodeType::Float32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("1.0"));
}

void RegisterDecodeTest::f32_2_zero_A12()
{
    const auto result = decodePair(0x0000, 0x0000, RegisterDecodeType::Float32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("0.0"));
}

void RegisterDecodeTest::f32_3_minusTwo_A13()
{
    const auto result = decodePair(0xC000, 0x0000, RegisterDecodeType::Float32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("-2.0"));
}

void RegisterDecodeTest::f32_4_nanIsOk_A14()
{
    // A14 + T024 §22 C3: NaN is a LEGAL IEEE-754 result — status stays Ok
    // (never a decode failure, never a wire-failure mapping), and the text is
    // the frozen Chinese UI copy.
    const auto result = decodePair(0x7FC0, 0x0000, RegisterDecodeType::Float32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("非数字（NaN）"));
}

void RegisterDecodeTest::f32_5_posInfIsOk_A15()
{
    const auto result = decodePair(0x7F80, 0x0000, RegisterDecodeType::Float32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text),
             QStringLiteral("正无穷大（+Inf）"));
}

void RegisterDecodeTest::f32_6_negInfIsOk()
{
    // The frozen minus in −Inf is U+2212 (T024 §22 C3 verbatim bytes); the
    // expected string is built from the codepoint so this test cannot pass
    // through an ASCII-hyphen lookalike.
    const auto result = decodePair(0xFF80, 0x0000, RegisterDecodeType::Float32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    const QString expected =
        QStringLiteral("负无穷大（%1Inf）").arg(QChar(0x2212));
    QCOMPARE(QString::fromStdString(result.text), expected);
    // Byte-level guard: U+2212 is exactly the UTF-8 sequence e2 88 92.
    QCOMPARE(QString::fromStdString(result.text).toUtf8().contains(
                 QByteArray("\xe2\x88\x92")),
             true);
}

void RegisterDecodeTest::f32_7_wordOrderReversal_A16()
{
    // A16: the same 1.0 read out of words given in the opposite register
    // order — LowWordFirst must recombine them into the identical value.
    const auto result = decodePair(0x0000, 0x3F80, RegisterDecodeType::Float32,
                                   RegisterWordOrder::LowWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("1.0"));
}

void RegisterDecodeTest::f32_8_byteSwappedBigEndianWord_A30()
{
    // A30: byte-swap within each register + big-endian words (industrial
    // "BADC" family) → 1.0 again.
    const auto result = decodePair(0x803F, 0x0000, RegisterDecodeType::Float32,
                                   RegisterWordOrder::HighWordFirst, true);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("1.0"));
}

void RegisterDecodeTest::f32_9_byteSwappedLittleEndianWord_A31()
{
    // A31: byte-swap + little-endian words (the fully reversed DCBA) → 1.0.
    const auto result = decodePair(0x0000, 0x803F, RegisterDecodeType::Float32,
                                   RegisterWordOrder::LowWordFirst, true);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(result.text), QStringLiteral("1.0"));
}

void RegisterDecodeTest::f32_10_tenthRoundTrip()
{
    // A non-trivial finite value: 0.1's binary32 bit pattern renders in
    // to_chars shortest form and parses back to the SAME bit pattern.
    const auto result = decodePair(0x3DCC, 0xCCCD, RegisterDecodeType::Float32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    bool ok = false;
    const float parsed =
        static_cast<float>(QString::fromStdString(result.text)
                               .toFloat(&ok));
    QVERIFY(ok);
    // The parsed text must land back on exactly the oracle's bit pattern.
    QCOMPARE(parsed, bitsToFloat(0x3DCCCCCD));
    std::uint32_t parsedBits = 0;
    std::memcpy(&parsedBits, &parsed, sizeof(parsedBits));
    QCOMPARE(parsedBits, 0x3DCCCCCDu);
}

void RegisterDecodeTest::f32_11_piRoundTrip()
{
    // Same round-trip discipline for 0x40490FDB (π's binary32 pattern): the
    // exact expected TEXT is not frozen (shortest form), the exact BITS are.
    const auto result = decodePair(0x4049, 0x0FDB, RegisterDecodeType::Float32,
                                   RegisterWordOrder::HighWordFirst, false);
    QCOMPARE(result.status, RegisterDecodeStatus::Ok);
    QVERIFY(!result.text.empty());
    bool ok = false;
    const float parsed =
        static_cast<float>(QString::fromStdString(result.text)
                               .toFloat(&ok));
    QVERIFY(ok);
    QCOMPARE(parsed, bitsToFloat(0x40490FDB));
    std::uint32_t parsedBits = 0;
    std::memcpy(&parsedBits, &parsed, sizeof(parsedBits));
    QCOMPARE(parsedBits, 0x40490FDBu);
}

// ===========================================================================
// M11 second slice: decodeRegisterView — the single entry (T024 §12) with
// the §9 failure model mapped as: start < 0 or type outside the matrix →
// InvalidConfiguration; start >= size → OutOfRangeSelection; a start inside
// the data without enough remaining words → InsufficientWords.
// ===========================================================================

void RegisterDecodeTest::v1_viewSingleWordPassthrough()
{
    // A 1-register type through the view equals the direct word decode.
    const std::vector<std::uint16_t> words = {0x1234, 0x0080};
    const auto view = decodeRegisterView(words, 1, RegisterDecodeType::UInt16,
                                         RegisterByteOrder::Normal,
                                         RegisterWordOrder::HighWordFirst);
    const auto direct = decodeRegisterWord(0x0080, RegisterDecodeType::UInt16,
                                           RegisterByteOrder::Normal);
    QCOMPARE(view.status, direct.status);
    QCOMPARE(view.text, direct.text);
    QCOMPARE(view.start, 1);
    QCOMPARE(view.wordCount, 1);
}

void RegisterDecodeTest::v2_viewPairWindow()
{
    // The 2-register sliding window: start 0 uses words[0..1], start 1 uses
    // words[1..2] — every word is a legal start (T024 §10 per-register
    // alignment), and the values follow the same pair decoder.
    const std::vector<std::uint16_t> words = {0x1234, 0x5678, 0xC000, 0x0000};
    const auto v0 = decodeRegisterView(words, 0, RegisterDecodeType::UInt32,
                                       RegisterByteOrder::Normal,
                                       RegisterWordOrder::HighWordFirst);
    QCOMPARE(v0.status, RegisterDecodeStatus::Ok);
    QCOMPARE(v0.start, 0);
    QCOMPARE(v0.wordCount, 2);
    QCOMPARE(QString::fromStdString(v0.text), QStringLiteral("305419896"));
    const auto v2 = decodeRegisterView(words, 2, RegisterDecodeType::UInt32,
                                       RegisterByteOrder::Normal,
                                       RegisterWordOrder::HighWordFirst);
    QCOMPARE(v2.status, RegisterDecodeStatus::Ok);
    QCOMPARE(v2.start, 2);
    QCOMPARE(QString::fromStdString(v2.text), QStringLiteral("3221225472"));
    const auto v1 = decodeRegisterView(words, 1, RegisterDecodeType::UInt32,
                                       RegisterByteOrder::Normal,
                                       RegisterWordOrder::HighWordFirst);
    QCOMPARE(v1.status, RegisterDecodeStatus::Ok);
    QCOMPARE(QString::fromStdString(v1.text),
             QString::number(combineWords(0x5678, 0xC000,
                                          RegisterWordOrder::HighWordFirst,
                                          RegisterByteOrder::Normal)));
}

void RegisterDecodeTest::v3_viewLastRegisterInsufficient_A17()
{
    // A17: the LAST register alone cannot feed a 2-register view.
    const std::vector<std::uint16_t> words = {0x1234};
    const auto view = decodeRegisterView(words, 0, RegisterDecodeType::UInt32,
                                         RegisterByteOrder::Normal,
                                         RegisterWordOrder::HighWordFirst);
    QCOMPARE(view.status, RegisterDecodeStatus::InsufficientWords);
    QVERIFY(view.text.empty());
    QCOMPARE(view.start, 0);
    QCOMPARE(view.wordCount, 2);
}

void RegisterDecodeTest::v4_viewStartOutOfRange()
{
    const std::vector<std::uint16_t> words = {0x1234, 0x5678};
    const auto view = decodeRegisterView(words, 2, RegisterDecodeType::UInt32,
                                         RegisterByteOrder::Normal,
                                         RegisterWordOrder::HighWordFirst);
    QCOMPARE(view.status, RegisterDecodeStatus::OutOfRangeSelection);
    QVERIFY(view.text.empty());
}

void RegisterDecodeTest::v5_viewNegativeStartInvalidConfig()
{
    const std::vector<std::uint16_t> words = {0x1234, 0x5678};
    const auto view = decodeRegisterView(words, -1, RegisterDecodeType::UInt32,
                                         RegisterByteOrder::Normal,
                                         RegisterWordOrder::HighWordFirst);
    QCOMPARE(view.status, RegisterDecodeStatus::InvalidConfiguration);
    QVERIFY(view.text.empty());
    QCOMPARE(view.wordCount, 0);
}

void RegisterDecodeTest::v6_viewUnknownTypeInvalidConfig()
{
    // Through the VIEW, a type outside the matrix is an invalid REQUEST
    // configuration (§9: 所需字数 ∉ {1,2}); the pair entry separately keeps
    // its own UnsupportedType for non-pair types (p1).
    const std::vector<std::uint16_t> words = {0x1234, 0x5678};
    const auto view = decodeRegisterView(
        words, 0, static_cast<RegisterDecodeType>(99), RegisterByteOrder::Normal,
        RegisterWordOrder::HighWordFirst);
    QCOMPARE(view.status, RegisterDecodeStatus::InvalidConfiguration);
    QVERIFY(view.text.empty());
    QCOMPARE(view.wordCount, 0);
}

void RegisterDecodeTest::v7_viewEmptyWordsOutOfRange()
{
    const std::vector<std::uint16_t> words;
    const auto view = decodeRegisterView(words, 0, RegisterDecodeType::UInt16,
                                         RegisterByteOrder::Normal,
                                         RegisterWordOrder::HighWordFirst);
    QCOMPARE(view.status, RegisterDecodeStatus::OutOfRangeSelection);
    QVERIFY(view.text.empty());
}

void RegisterDecodeTest::v8_viewWordOrderAxis()
{
    // The view forwards the word-order axis: the same words flip their
    // 32-bit combination when the order changes, exactly as the pair
    // decoder defines it.
    const std::vector<std::uint16_t> words = {0x3F80, 0x0000};
    const auto high = decodeRegisterView(words, 0, RegisterDecodeType::UInt32,
                                         RegisterByteOrder::Normal,
                                         RegisterWordOrder::HighWordFirst);
    const auto low = decodeRegisterView(words, 0, RegisterDecodeType::UInt32,
                                        RegisterByteOrder::Normal,
                                        RegisterWordOrder::LowWordFirst);
    QCOMPARE(QString::fromStdString(high.text),
             QStringLiteral("1065353216"));
    QCOMPARE(QString::fromStdString(low.text), QStringLiteral("16256"));
}

QTEST_GUILESS_MAIN(RegisterDecodeTest)
#include "test_register_decode.moc"