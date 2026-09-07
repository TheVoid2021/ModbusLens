#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "core/replay/ReplayLog.h"

using modbuslens::core::ReplayLog;
using modbuslens::core::ReplayParseError;
using modbuslens::core::ReplayParseErrorCode;
using modbuslens::core::ReplayParseResult;
using modbuslens::core::ReplayTransactionRecord;
using modbuslens::core::parseReplayLog;

namespace {

using ms = std::chrono::milliseconds;

// Copy semantics on purpose — see docs/issues/ISSUE-001.
template <typename T, typename Variant>
std::optional<T> as(const Variant& result)
{
    if (auto* value = std::get_if<T>(&result)) {
        return *value;
    }
    return std::nullopt;
}

std::vector<std::uint8_t> wire(std::initializer_list<int> bytes)
{
    std::vector<std::uint8_t> result;
    result.reserve(bytes.size());
    for (const int b : bytes) {
        result.push_back(static_cast<std::uint8_t>(b));
    }
    return result;
}

constexpr std::string_view kHeader = "MODBUSLENS_MLOG|1|timeout_ms=1000";

const auto kSuccessRequest = wire({0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B});
const auto kSuccessResponse = wire({0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7A});

} // namespace

class ReplayLogTest : public QObject
{
    Q_OBJECT

private slots:
    // REPLAY-A01 (P0): valid header parses into an empty ReplayLog.
    void a01_validHeader();
    // REPLAY-A02 (P0): one valid TXN yields elapsed/request/response.
    void a02_oneValidTransaction();
    // REPLAY-A03 (P0): NO_RESPONSE -> nullopt responseWire.
    void a03_noResponse();
    // REPLAY-A04 (P0): unknown version -> UnsupportedVersion.
    void a04_unsupportedVersion();
    // REPLAY-A05 (P0): bad hex token -> InvalidHex + correct lineNumber.
    void a05_invalidHex();
    // REPLAY-A06 (P0): record shape violations -> InvalidRecord.
    void a06_invalidRecordShape();
    // REPLAY-A07 (P1): comments / blank lines ignored, CRLF accepted.
    void a07_commentsBlankLinesCrlf();
    // REPLAY-A08 (P1): non-integer / negative elapsed -> InvalidElapsed.
    void a08_invalidElapsed();
    // MissingHeader semantics: no meaningful line -> lineNumber 0;
    // first meaningful line is a TXN -> that physical line.
    void a09_missingHeader();
    // Header field validation: timeout <= 0 / garbage -> InvalidHeader.
    void a10_invalidHeader();
    // Empty request field -> MissingRequest.
    void a11_missingRequest();
    // Empty response field -> InvalidResponseField; NO_RESPONSE in request
    // field is not valid hex -> InvalidHex.
    void a12_responseFieldRules();
};

void ReplayLogTest::a01_validHeader()
{
    const auto result = parseReplayLog(kHeader);
    const auto log = as<ReplayLog>(result);
    QVERIFY(log.has_value());
    QCOMPARE(log->timeoutThreshold, ms{1000});
    QCOMPARE(log->transactions.size(), std::size_t{0});
}

void ReplayLogTest::a02_oneValidTransaction()
{
    const auto result = parseReplayLog(
        "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
        "TXN|25|01 03 00 00 00 02 C4 0B|01 03 04 00 64 00 C8 BA 7A\n");
    const auto log = as<ReplayLog>(result);
    QVERIFY(log.has_value());
    QCOMPARE(log->transactions.size(), std::size_t{1});

    const auto& record = log->transactions[0];
    QCOMPARE(record.elapsed, ms{25});
    QCOMPARE(record.requestWire, kSuccessRequest);
    QVERIFY(record.responseWire.has_value());
    QCOMPARE(*record.responseWire, kSuccessResponse);
}

void ReplayLogTest::a03_noResponse()
{
    const auto result = parseReplayLog(
        "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
        "TXN|1000|01 03 00 00 00 02 C4 0B|NO_RESPONSE\n");
    const auto log = as<ReplayLog>(result);
    QVERIFY(log.has_value());
    QCOMPARE(log->transactions.size(), std::size_t{1});
    QCOMPARE(log->transactions[0].elapsed, ms{1000});
    QVERIFY(!log->transactions[0].responseWire.has_value());
}

void ReplayLogTest::a04_unsupportedVersion()
{
    const auto result = parseReplayLog("MODBUSLENS_MLOG|2|timeout_ms=1000\n");
    const auto error = as<ReplayParseError>(result);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ReplayParseErrorCode::UnsupportedVersion);
    QCOMPARE(error->lineNumber, std::size_t{1});
    QVERIFY(!as<ReplayLog>(result).has_value());
}

void ReplayLogTest::a05_invalidHex()
{
    const auto result = parseReplayLog(
        "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
        "TXN|25|GG|01 03 04 00 64 00 C8 BA 7A\n"
        "# trailing comment line\n");
    const auto error = as<ReplayParseError>(result);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ReplayParseErrorCode::InvalidHex);
    QCOMPARE(error->lineNumber, std::size_t{2});
}

void ReplayLogTest::a06_invalidRecordShape()
{
    // Too few fields.
    {
        const auto result = parseReplayLog(
            "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
            "TXN|25|01 03\n");
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::InvalidRecord);
        QCOMPARE(error->lineNumber, std::size_t{2});
    }
    // Too many fields.
    {
        const auto result = parseReplayLog(
            "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
            "TXN|25|01 03 00 00 00 02 C4 0B|NO_RESPONSE|extra\n");
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::InvalidRecord);
    }
    // Not a TXN record at all.
    {
        const auto result = parseReplayLog(
            "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
            "DATA|25|01 03|NO_RESPONSE\n");
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::InvalidRecord);
    }
}

void ReplayLogTest::a07_commentsBlankLinesCrlf()
{
    const std::string crlf =
        "# Known four-outcome demo log\r\n"
        "\r\n"
        "MODBUSLENS_MLOG|1|timeout_ms=1000\r\n"
        "\r\n"
        "TXN|25|01 03 00 00 00 02 C4 0B|01 03 04 00 64 00 C8 BA 7A\r\n"
        "# trailing comment\r\n";

    const std::string lf =
        "# Known four-outcome demo log\n"
        "\n"
        "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
        "\n"
        "TXN|25|01 03 00 00 00 02 C4 0B|01 03 04 00 64 00 C8 BA 7A\n"
        "# trailing comment\n";

    const auto crlfResult = parseReplayLog(crlf);
    const auto lfResult = parseReplayLog(lf);

    const auto crlfLog = as<ReplayLog>(crlfResult);
    const auto lfLog = as<ReplayLog>(lfResult);
    QVERIFY(crlfLog.has_value());
    QVERIFY(lfLog.has_value());
    QCOMPARE(crlfLog->transactions.size(), std::size_t{1});
    QCOMPARE(crlfLog->transactions[0].requestWire, kSuccessRequest);
    // CRLF input must yield exactly the same log as LF input.
    QVERIFY(*crlfLog == *lfLog);
}

void ReplayLogTest::a08_invalidElapsed()
{
    for (const std::string& text : {
             std::string{"MODBUSLENS_MLOG|1|timeout_ms=1000\nTXN|abc|01 03|NO_RESPONSE\n"},
             std::string{"MODBUSLENS_MLOG|1|timeout_ms=1000\nTXN|-1|01 03|NO_RESPONSE\n"},
         }) {
        const auto result = parseReplayLog(text);
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::InvalidElapsed);
        QCOMPARE(error->lineNumber, std::size_t{2});
    }
}

void ReplayLogTest::a09_missingHeader()
{
    // No meaningful line at all -> lineNumber 0 (no offending line).
    {
        const auto result = parseReplayLog("\n\n  \t\n");
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::MissingHeader);
        QCOMPARE(error->lineNumber, std::size_t{0});
    }
    // Only comments -> still nothing meaningful -> lineNumber 0.
    {
        const auto result = parseReplayLog("# header\n# more\n");
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::MissingHeader);
        QCOMPARE(error->lineNumber, std::size_t{0});
    }
    // First meaningful line is a TXN on physical line 3 -> that line.
    {
        const auto result = parseReplayLog(
            "# comment on line 1\n"
            "\n"
            "TXN|25|01 03 00 00 00 02 C4 0B|NO_RESPONSE\n");
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::MissingHeader);
        QCOMPARE(error->lineNumber, std::size_t{3});
    }
}

void ReplayLogTest::a10_invalidHeader()
{
    for (const std::string& text : {
             std::string{"MODBUSLENS_MLOG|1|timeout_ms=0\n"},
             std::string{"MODBUSLENS_MLOG|1|timeout_ms=-5\n"},
             std::string{"MODBUSLENS_MLOG|1|timeout_ms=abc\n"},
             std::string{"MODBUSLENS_MLOG|1|timeout_ms=1000abc\n"},
             std::string{"MODBUSLENS_MLOG|1|timeout=1000\n"},
             std::string{"MODBUSLENS_MLOG|1|timeout_ms=99999999999999999999\n"},
         }) {
        const auto result = parseReplayLog(text);
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::InvalidHeader);
        QCOMPARE(error->lineNumber, std::size_t{1});
    }
}

void ReplayLogTest::a11_missingRequest()
{
    const auto result = parseReplayLog(
        "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
        "TXN|25||01 03 04 00 64 00 C8 BA 7A\n");
    const auto error = as<ReplayParseError>(result);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ReplayParseErrorCode::MissingRequest);
    QCOMPARE(error->lineNumber, std::size_t{2});
}

void ReplayLogTest::a12_responseFieldRules()
{
    // Empty response field -> InvalidResponseField.
    {
        const auto result = parseReplayLog(
            "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
            "TXN|25|01 03 00 00 00 02 C4 0B|\n");
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::InvalidResponseField);
    }
    // NO_RESPONSE is only legal in the response field; in the request field
    // it is simply not valid hex.
    {
        const auto result = parseReplayLog(
            "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
            "TXN|25|NO_RESPONSE|01 03 04 00 64 00 C8 BA 7A\n");
        const auto error = as<ReplayParseError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, ReplayParseErrorCode::InvalidHex);
    }
}

QTEST_MAIN(ReplayLogTest)
#include "test_replay_log.moc"