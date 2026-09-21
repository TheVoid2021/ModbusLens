#include <QtTest>

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/active/PreparedWriteSnapshot.h"
#include "core/active/WriteDraftParsing.h"
#include "core/active/WritePrepareValidation.h"

using modbuslens::core::ActiveFunction;
using modbuslens::core::ActiveRequestIntent;
using modbuslens::core::PreparedWriteInvalidReason;
using modbuslens::core::PreparedWriteSnapshot;
using modbuslens::core::PreparedWriteState;
using modbuslens::core::PreparedWriteStore;
using modbuslens::core::WriteMultipleRegistersIntent;
using modbuslens::core::WriteSingleRegisterIntent;
using modbuslens::core::WriteValidationError;
using modbuslens::core::WriteValidationErrorCode;
using modbuslens::core::parseDecimalRegisterValue;
using modbuslens::core::parseRegisterValues;
using modbuslens::core::ParsedRegisterValues;
using modbuslens::core::SingleRegisterValue;
using modbuslens::core::ValuesParseError;
using modbuslens::core::ValuesParseErrorCode;

namespace {

// Copy semantics on purpose — see docs/issues/ISSUE-001.
template <typename T, typename Variant>
std::optional<T> as(const Variant& result)
{
    if (auto* value = std::get_if<T>(&result)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<WriteValidationErrorCode> errorCodeOf(
    const std::variant<ActiveRequestIntent, WriteValidationError>& result)
{
    if (const auto* error = std::get_if<WriteValidationError>(&result)) {
        return error->code;
    }
    return std::nullopt;
}

std::string manyValues(int count, int first = 1)
{
    std::string text;
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            text += '\n';
        }
        text += std::to_string(first + i);
    }
    return text;
}

PreparedWriteSnapshot makeSnapshot(std::uint64_t token,
                                   std::uint64_t sessionId = 1)
{
    auto intent = std::get<ActiveRequestIntent>(
        modbuslens::core::prepareWriteSingleRegisterIntent(
            11, 0x0064, 0x0064, 1000));
    return PreparedWriteSnapshot{
        .token = token,
        .intent = std::move(intent),
        .sessionId = sessionId,
        .connectionLabel = "COM_TEST @ 9600",
    };
}

} // namespace

class WritePrepareTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- parser (P01–P12) ----
    void p01_lfSeparated();
    void p02_crlfSeparated();
    void p03_leadingTrailingBlankLinesIgnored();
    void p04_middleEmptyLineRejected();
    void p05_middleWhitespaceOnlyRejected();
    void p06_maxValueAccepted();
    void p07_aboveMaxRejected();
    void p08_nonDecimalFormsRejected();
    void p09_123ValuesAccepted();
    void p10_124ValuesRejected();
    void p11_allBlankRejected();
    void p12_hugeIntegerDeterministicError();
    // M10-D1: single-value decimal field (0x06 address / value)
    void d1_emptyRejected();
    void d2_zeroAccepted();
    void d3_maxAccepted();
    void d4_aboveMaxRejected();
    void d5_negativeRejected();
    void d6_plusSignRejected();
    void d7_trailingGarbageRejected();
    void d8_hexFormRejected();
    void d9_decimalPointRejected();
    void d10_leadingZerosAccepted();
    void d11_outerWhitespaceTrimmed();
    void d12_hugeIntegerDeterministicRangeError();
    void d13_twoValuesLfRejected();
    void d14_twoValuesCrlfRejected();
    void d15_trailingNewlineTolerated();
    void d16_sameAuthorityAsMultiLineList();

    // ---- numeric validation (V01–V12) ----
    void v01_unitBounds06();
    void v02_unitOutOfRange06();
    void v03_addressBounds06();
    void v04_addressOutOfRangeBeforeNarrowing();
    void v05_valueBounds06();
    void v06_valueOutOfRangeBeforeNarrowing();
    void v07_timeoutUiBounds();
    void v08_timeoutOutOfUiBounds();
    void v09_toV12_spanBoundaries();

    // ---- prepared snapshot state machine (S01–S09) ----
    void s01_prepareStartsPreparedWithToken();
    void s02_projectionEqualsIntent();
    void s03_secondPrepareWhilePreparedRejected();
    void s04_confirmConsumes();
    void s05_confirmTwiceRejected();
    void s06_cancelInvalidates();
    void s07_invalidatedTokenCannotConfirm();
    void s08_newPrepareNewTokenOldStale();
    void s09_draftChangeCannotAlterSnapshot();
};

// ---- parser ----

void WritePrepareTest::p01_lfSeparated()
{
    const auto result = parseRegisterValues("1\n2\n3");
    const auto parsed = as<modbuslens::core::ParsedRegisterValues>(result);
    QVERIFY(parsed.has_value());
    const std::vector<std::uint16_t> expected = {1, 2, 3};
    QCOMPARE(parsed->values, expected);
}

void WritePrepareTest::p02_crlfSeparated()
{
    const auto result = parseRegisterValues("1\r\n2\r\n3");
    const auto parsed = as<modbuslens::core::ParsedRegisterValues>(result);
    QVERIFY(parsed.has_value());
    const std::vector<std::uint16_t> expected = {1, 2, 3};
    QCOMPARE(parsed->values, expected);
}

void WritePrepareTest::p03_leadingTrailingBlankLinesIgnored()
{
    const std::string_view inputs[] = {"\n1\n2\n", "\r\n1\r\n2\r\n",
                                       "   \n 1 \n 2 \n   "};
    for (const std::string_view text : inputs) {
        const auto result = parseRegisterValues(text);
        const auto parsed = as<modbuslens::core::ParsedRegisterValues>(result);
        QVERIFY(parsed.has_value());
        const std::vector<std::uint16_t> expected = {1, 2};
        QCOMPARE(parsed->values, expected);
    }
}

void WritePrepareTest::p04_middleEmptyLineRejected()
{
    const auto result = parseRegisterValues("1\n\n2");
    const auto error = as<modbuslens::core::ValuesParseError>(result);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, modbuslens::core::ValuesParseErrorCode::BlankLineInside);
    QCOMPARE(error->lineIndex, std::size_t{1});
}

void WritePrepareTest::p05_middleWhitespaceOnlyRejected()
{
    const auto result = parseRegisterValues("1\n   \n2");
    const auto error = as<modbuslens::core::ValuesParseError>(result);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, modbuslens::core::ValuesParseErrorCode::BlankLineInside);
    QCOMPARE(error->lineIndex, std::size_t{1});
}

void WritePrepareTest::p06_maxValueAccepted()
{
    const auto parsed =
        as<modbuslens::core::ParsedRegisterValues>(parseRegisterValues("65535"));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->values, std::vector<std::uint16_t>{65535});
}

void WritePrepareTest::p07_aboveMaxRejected()
{
    const auto error =
        as<modbuslens::core::ValuesParseError>(parseRegisterValues("65536"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, modbuslens::core::ValuesParseErrorCode::ValueOutOfRange);
    QCOMPARE(error->lineIndex, std::size_t{0});
}

void WritePrepareTest::p08_nonDecimalFormsRejected()
{
    for (const std::string_view text : {std::string_view("+12"), std::string_view("-1"),
                                        std::string_view("0x10"), std::string_view("1.5"),
                                        std::string_view("1,2"), std::string_view("abc"),
                                        std::string_view("1 2"), std::string_view("0b101")}) {
        const auto error =
            as<modbuslens::core::ValuesParseError>(parseRegisterValues(text));
        QVERIFY2(error.has_value(), std::string(text).c_str());
        QCOMPARE(error->code,
                 modbuslens::core::ValuesParseErrorCode::InvalidCharacter);
    }
}

void WritePrepareTest::p09_123ValuesAccepted()
{
    const auto parsed =
        as<modbuslens::core::ParsedRegisterValues>(parseRegisterValues(manyValues(123)));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->values.size(), std::size_t{123});
}

void WritePrepareTest::p10_124ValuesRejected()
{
    const auto error =
        as<modbuslens::core::ValuesParseError>(parseRegisterValues(manyValues(124)));
    QVERIFY(error.has_value());
    QCOMPARE(error->code,
             modbuslens::core::ValuesParseErrorCode::TooManyValues);
}

void WritePrepareTest::p11_allBlankRejected()
{
    const std::string_view inputs[] = {"", "\n", "   \n  \n"};
    for (const std::string_view text : inputs) {
        const auto error =
            as<modbuslens::core::ValuesParseError>(parseRegisterValues(text));
        QVERIFY(error.has_value());
        QCOMPARE(error->code, modbuslens::core::ValuesParseErrorCode::NoValues);
    }
}

void WritePrepareTest::p12_hugeIntegerDeterministicError()
{
    const auto error = as<modbuslens::core::ValuesParseError>(
        parseRegisterValues("999999999999999999999"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, modbuslens::core::ValuesParseErrorCode::ValueOutOfRange);
}

// ---- validation ----

void WritePrepareTest::v01_unitBounds06()
{
    for (const std::int64_t unit : {std::int64_t{1}, std::int64_t{247}}) {
        const auto intent = as<ActiveRequestIntent>(
            modbuslens::core::prepareWriteSingleRegisterIntent(unit, 0, 0, 1000));
        QVERIFY(intent.has_value());
        QCOMPARE(intent->unitId, static_cast<std::uint8_t>(unit));
    }
}

void WritePrepareTest::v02_unitOutOfRange06()
{
    for (const std::int64_t unit : {std::int64_t{0}, std::int64_t{248},
                                    std::int64_t{-1}, std::int64_t{65536}}) {
        const auto code = errorCodeOf(
            modbuslens::core::prepareWriteSingleRegisterIntent(unit, 0, 0, 1000));
        QVERIFY(code.has_value());
        QCOMPARE(*code, WriteValidationErrorCode::UnitIdOutOfRange);
    }
}

void WritePrepareTest::v03_addressBounds06()
{
    const auto intent = as<ActiveRequestIntent>(
        modbuslens::core::prepareWriteSingleRegisterIntent(1, 65535, 0, 1000));
    QVERIFY(intent.has_value());
    QCOMPARE(std::get<WriteSingleRegisterIntent>(intent->payload).registerAddress,
             std::uint16_t{65535});
}

void WritePrepareTest::v04_addressOutOfRangeBeforeNarrowing()
{
    for (const std::int64_t address : {std::int64_t{-1}, std::int64_t{65536}}) {
        const auto code = errorCodeOf(
            modbuslens::core::prepareWriteSingleRegisterIntent(1, address, 0, 1000));
        QVERIFY(code.has_value());
        QCOMPARE(*code, WriteValidationErrorCode::AddressOutOfRange);
    }
}

void WritePrepareTest::v05_valueBounds06()
{
    const auto intent = as<ActiveRequestIntent>(
        modbuslens::core::prepareWriteSingleRegisterIntent(1, 0, 65535, 1000));
    QVERIFY(intent.has_value());
    QCOMPARE(std::get<WriteSingleRegisterIntent>(intent->payload).value,
             std::uint16_t{65535});
}

void WritePrepareTest::v06_valueOutOfRangeBeforeNarrowing()
{
    for (const std::int64_t value : {std::int64_t{-1}, std::int64_t{65536}}) {
        const auto code = errorCodeOf(
            modbuslens::core::prepareWriteSingleRegisterIntent(1, 0, value, 1000));
        QVERIFY(code.has_value());
        QCOMPARE(*code, WriteValidationErrorCode::ValueOutOfRange);
    }
}

void WritePrepareTest::v07_timeoutUiBounds()
{
    for (const std::int64_t timeout : {std::int64_t{100}, std::int64_t{10000}}) {
        const auto intent = as<ActiveRequestIntent>(
            modbuslens::core::prepareWriteSingleRegisterIntent(1, 0, 0, timeout));
        QVERIFY(intent.has_value());
        QCOMPARE(intent->timeout.count(), timeout);
    }
}

void WritePrepareTest::v08_timeoutOutOfUiBounds()
{
    for (const std::int64_t timeout : {std::int64_t{99}, std::int64_t{10001},
                                       std::int64_t{0}, std::int64_t{-5}}) {
        const auto code = errorCodeOf(
            modbuslens::core::prepareWriteSingleRegisterIntent(1, 0, 0, timeout));
        QVERIFY(code.has_value());
        QCOMPARE(*code, WriteValidationErrorCode::TimeoutOutOfRange);
    }
}

void WritePrepareTest::v09_toV12_spanBoundaries()
{
    // V09–V12: the widened address-span rule (start + quantity - 1 <= 0xFFFF).
    struct Case { int start; const char *values; bool valid; };
    const Case cases[] = {
        {65535, "1", true},          // V09
        {65535, "1\n2", false},      // V10
        {65534, "1\n2", true},       // V11
        {65534, "1\n2\n3", false},   // V12
    };
    for (const Case& c : cases) {
        const auto result = modbuslens::core::prepareWriteMultipleRegistersIntent(
            1, c.start, c.values, 1000);
        const auto intent = as<ActiveRequestIntent>(result);
        QVERIFY2(intent.has_value() == c.valid, c.values);
        if (c.valid) {
            QCOMPARE(std::get<WriteMultipleRegistersIntent>(intent->payload)
                         .startAddress,
                     static_cast<std::uint16_t>(c.start));
        } else {
            QVERIFY(errorCodeOf(result).has_value());
            QCOMPARE(*errorCodeOf(result),
                     WriteValidationErrorCode::AddressSpanOutOfRange);
        }
    }
}

// ---- prepared snapshot state machine ----

void WritePrepareTest::s01_prepareStartsPreparedWithToken()
{
    PreparedWriteStore store;
    QCOMPARE(store.state(), PreparedWriteState::None);

    const auto result = store.prepare(makeSnapshot(41));
    QVERIFY(std::get_if<modbuslens::core::PreparedWrite>(&result) != nullptr);
    QCOMPARE(store.state(), PreparedWriteState::Prepared);
    QVERIFY(store.token().has_value());
    QCOMPARE(*store.token(), std::uint64_t{41});
}

void WritePrepareTest::s02_projectionEqualsIntent()
{
    PreparedWriteStore store;
    store.prepare(makeSnapshot(7));
    const auto snapshot = store.snapshot();
    QVERIFY(snapshot.has_value());

    // Projection fields must equal the validated intent, field by field.
    QCOMPARE(snapshot->intent.function, ActiveFunction::WriteSingleRegister);
    QCOMPARE(snapshot->intent.unitId, std::uint8_t{11});
    QCOMPARE(std::get<WriteSingleRegisterIntent>(snapshot->intent.payload)
                 .registerAddress,
             std::uint16_t{0x0064});
    QCOMPARE(std::get<WriteSingleRegisterIntent>(snapshot->intent.payload).value,
             std::uint16_t{0x0064});
    QCOMPARE(snapshot->intent.timeout.count(), std::int64_t{1000});
    QCOMPARE(snapshot->sessionId, std::uint64_t{1});
    QCOMPARE(snapshot->connectionLabel, std::string{"COM_TEST @ 9600"});
    // Derived quantity is a function of the intent (never a second truth).
    QCOMPARE(modbuslens::core::preparedQuantity(snapshot->intent), 1);
}

void WritePrepareTest::s03_secondPrepareWhilePreparedRejected()
{
    PreparedWriteStore store;
    store.prepare(makeSnapshot(7));
    const auto second = store.prepare(makeSnapshot(8));
    QVERIFY(std::get_if<modbuslens::core::PrepareAlreadyPrepared>(&second) != nullptr);
    QCOMPARE(store.state(), PreparedWriteState::Prepared);
    QVERIFY(store.token().has_value());
    QCOMPARE(*store.token(), std::uint64_t{7}); // original generation kept
}

void WritePrepareTest::s04_confirmConsumes()
{
    PreparedWriteStore store;
    store.prepare(makeSnapshot(7));
    const auto result = store.confirm(7);
    QVERIFY(std::get_if<modbuslens::core::ConfirmAccepted>(&result) != nullptr);
    QCOMPARE(store.state(), PreparedWriteState::Consumed);
    QVERIFY(!store.snapshot().has_value());
}

void WritePrepareTest::s05_confirmTwiceRejected()
{
    PreparedWriteStore store;
    store.prepare(makeSnapshot(7));
    store.confirm(7);
    const auto second = store.confirm(7);
    const auto* rejected = std::get_if<modbuslens::core::ConfirmRejected>(&second);
    QVERIFY(rejected != nullptr);
    QCOMPARE(rejected->reason, modbuslens::core::ConfirmRejectReason::NotPrepared);
    QCOMPARE(store.state(), PreparedWriteState::Consumed);
}

void WritePrepareTest::s06_cancelInvalidates()
{
    PreparedWriteStore store;
    store.prepare(makeSnapshot(7));
    store.cancel(7);
    QCOMPARE(store.state(), PreparedWriteState::Invalidated);
    QCOMPARE(store.invalidReason(),
             std::optional{PreparedWriteInvalidReason::UserCancelled});
}

void WritePrepareTest::s07_invalidatedTokenCannotConfirm()
{
    PreparedWriteStore store;
    store.prepare(makeSnapshot(7));
    store.cancel(7);
    const auto result = store.confirm(7);
    QVERIFY(std::get_if<modbuslens::core::ConfirmRejected>(&result) != nullptr);
    QCOMPARE(store.state(), PreparedWriteState::Invalidated);
}

void WritePrepareTest::s08_newPrepareNewTokenOldStale()
{
    PreparedWriteStore store;
    store.prepare(makeSnapshot(7));
    store.cancel(7);
    const auto next = store.prepare(makeSnapshot(8));
    QVERIFY(std::get_if<modbuslens::core::PreparedWrite>(&next) != nullptr);
    QCOMPARE(store.state(), PreparedWriteState::Prepared);

    // The old token stays stale forever; only the new generation confirms.
    const auto stale = store.confirm(7);
    QVERIFY(std::get_if<modbuslens::core::ConfirmRejected>(&stale) != nullptr);
    const auto fresh = store.confirm(8);
    QVERIFY(std::get_if<modbuslens::core::ConfirmAccepted>(&fresh) != nullptr);
    QCOMPARE(store.state(), PreparedWriteState::Consumed);
}

void WritePrepareTest::s09_draftChangeCannotAlterSnapshot()
{
    PreparedWriteStore store;
    const auto snapshot = makeSnapshot(7);
    store.prepare(snapshot);

    // Simulate the user editing the draft afterwards: a *new* input string is
    // parsed and validated into a different intent, but the store keeps the
    // snapshot it was given (the runtime never re-reads UI input on confirm).
    auto other = as<ActiveRequestIntent>(
        modbuslens::core::prepareWriteSingleRegisterIntent(22, 0x0100, 0x00FF, 2000));
    QVERIFY(other.has_value());
    const auto rePrepare = store.prepare(PreparedWriteSnapshot{
        .token = 9, .intent = *other, .sessionId = 1,
        .connectionLabel = "other"});
    QVERIFY(std::get_if<modbuslens::core::PrepareAlreadyPrepared>(&rePrepare)
            != nullptr);

    const auto kept = store.snapshot();
    QVERIFY(kept.has_value());
    QCOMPARE(kept->token, std::uint64_t{7});
    QCOMPARE(kept->intent.unitId, std::uint8_t{11});
    QCOMPARE(kept->connectionLabel, std::string{"COM_TEST @ 9600"});
}

// ---------------------------------------------------------------------------
// M10-D1: parseDecimalRegisterValue — the single decimal authority for the
// 0x06 address / value fields. The QML field is presentation only and hands
// over the RAW text, so these cases are what the user can really type.
// ---------------------------------------------------------------------------

void WritePrepareTest::d1_emptyRejected()
{
    // Control characters are built from their codes so this file never depends
    // on how an escape sequence survives an editor/shell layer.
    const std::string lf(1, static_cast<char>(10));
    const std::string crlf = std::string(1, static_cast<char>(13)) + lf;
    const std::string inputs[] = {std::string{}, "   ", lf, crlf};
    for (const std::string& raw : inputs) {
        const auto error = as<ValuesParseError>(parseDecimalRegisterValue(raw));
        QVERIFY2(error.has_value(), raw.c_str());
        QCOMPARE(error->code, ValuesParseErrorCode::NoValues);
    }
}

void WritePrepareTest::d2_zeroAccepted()
{
    const auto parsed = as<SingleRegisterValue>(parseDecimalRegisterValue("0"));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->value, std::uint16_t{0});
}

void WritePrepareTest::d3_maxAccepted()
{
    const auto parsed = as<SingleRegisterValue>(parseDecimalRegisterValue("65535"));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->value, std::uint16_t{65535});
}

void WritePrepareTest::d4_aboveMaxRejected()
{
    const auto error = as<ValuesParseError>(parseDecimalRegisterValue("65536"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ValuesParseErrorCode::ValueOutOfRange);
}

void WritePrepareTest::d5_negativeRejected()
{
    const auto error = as<ValuesParseError>(parseDecimalRegisterValue("-1"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ValuesParseErrorCode::InvalidCharacter);
}

void WritePrepareTest::d6_plusSignRejected()
{
    const auto error = as<ValuesParseError>(parseDecimalRegisterValue("+1"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ValuesParseErrorCode::InvalidCharacter);
}

void WritePrepareTest::d7_trailingGarbageRejected()
{
    const auto error = as<ValuesParseError>(parseDecimalRegisterValue("12x"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ValuesParseErrorCode::InvalidCharacter);
}

void WritePrepareTest::d8_hexFormRejected()
{
    const auto error = as<ValuesParseError>(parseDecimalRegisterValue("0x10"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ValuesParseErrorCode::InvalidCharacter);
}

void WritePrepareTest::d9_decimalPointRejected()
{
    const auto error = as<ValuesParseError>(parseDecimalRegisterValue("1.5"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ValuesParseErrorCode::InvalidCharacter);
}

void WritePrepareTest::d10_leadingZerosAccepted()
{
    const auto parsed = as<SingleRegisterValue>(parseDecimalRegisterValue("00010"));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->value, std::uint16_t{10});
}

void WritePrepareTest::d11_outerWhitespaceTrimmed()
{
    const std::string lf(1, static_cast<char>(10));
    const std::string cr(1, static_cast<char>(13));
    const std::string raw = "  " + std::string(1, static_cast<char>(9))
                            + " 1234 " + cr + lf;
    const auto parsed = as<SingleRegisterValue>(parseDecimalRegisterValue(raw));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->value, std::uint16_t{1234});
    QVERIFY2(!raw.empty(), "the raw draft text itself is never rewritten");
}

void WritePrepareTest::d12_hugeIntegerDeterministicRangeError()
{
    const auto error = as<ValuesParseError>(
        parseDecimalRegisterValue("999999999999999999999999"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ValuesParseErrorCode::ValueOutOfRange);
}

void WritePrepareTest::d13_twoValuesLfRejected()
{
    const std::string lf(1, static_cast<char>(10));
    const auto error =
        as<ValuesParseError>(parseDecimalRegisterValue("1" + lf + "2"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ValuesParseErrorCode::MultipleValuesInSingleField);
}

void WritePrepareTest::d14_twoValuesCrlfRejected()
{
    const std::string cr(1, static_cast<char>(13));
    const std::string lf(1, static_cast<char>(10));
    const auto error = as<ValuesParseError>(
        parseDecimalRegisterValue("1" + cr + lf + "2"));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ValuesParseErrorCode::MultipleValuesInSingleField);
}

void WritePrepareTest::d15_trailingNewlineTolerated()
{
    // A pasted trailing newline is presentation noise, not a second value.
    const std::string lf(1, static_cast<char>(10));
    const auto parsed =
        as<SingleRegisterValue>(parseDecimalRegisterValue("1234" + lf));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->value, std::uint16_t{1234});
}

void WritePrepareTest::d16_sameAuthorityAsMultiLineList()
{
    // The single-value field must not grow a second acceptance table: for
    // every single-line input the two parsers agree on acceptance, and the
    // typed value is the one the multi-line parser produces for that line.
    const std::string_view inputs[] = {"0", "65535", "00010", " 1234 ", "65536",
                                       "-1", "12x", "0x10", "1.5", "+1", "   "};
    for (const std::string_view raw : inputs) {
        const auto single = parseDecimalRegisterValue(raw);
        const auto list = parseRegisterValues(raw);
        const bool singleOk = std::holds_alternative<SingleRegisterValue>(single);
        const bool listOk = std::holds_alternative<ParsedRegisterValues>(list);
        QCOMPARE(singleOk, listOk);
        if (singleOk) {
            QCOMPARE(std::get<SingleRegisterValue>(single).value,
                     std::get<ParsedRegisterValues>(list).values.front());
        } else {
            QCOMPARE(std::get<ValuesParseError>(single).code,
                     std::get<ValuesParseError>(list).code);
        }
    }
}

QTEST_GUILESS_MAIN(WritePrepareTest)
#include "test_write_prepare.moc"