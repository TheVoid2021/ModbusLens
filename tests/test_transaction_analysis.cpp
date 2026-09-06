#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>

#include "core/analysis/TransactionAnalysis.h"

using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::NoResponse;
using modbuslens::core::ResponseObservation;
using modbuslens::core::RtuDecodeError;
using modbuslens::core::RtuDecodeErrorCode;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionStatus;
using modbuslens::core::analyzeFunction03Transaction;

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

ResponseObservation frameObservation(ModbusRtuFrame frame)
{
    return ResponseObservation{std::move(frame)};
}

// TX-A01 baseline pair: read 2 registers from device 1.
const ModbusRtuFrame kRequest = readRequest(0x01, 0, 2);
const ModbusRtuFrame kNormalResponse{
    .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};

class TransactionAnalysisTest : public QObject
{
    Q_OBJECT

private slots:
    // TX-A01 (P0): Success.
    void a01_success();
    // TX-A02 (P0): matching exception, numeric code only, elapsed kept.
    void a02_exception();
    // TX-A03 (P0): CrcMismatch -> CrcError.
    void a03_crcError();
    // TX-A04 (P0): NoResponse below threshold -> Pending.
    void a04_pending();
    // TX-A05 (P0): NoResponse at/above threshold -> Timeout (boundary).
    void a05_timeoutBoundary();
    // TX-A06 (P0): response from another device -> ProtocolError.
    void a06_wrongDeviceAddress();
    // TX-A07 (P0): wrong function (0x04 and 0x84 both rejected).
    void a07_wrongFunction();
    // TX-A08 (P0): malformed normal response reuses the T004B decoder.
    void a08_malformedNormalResponse();
    // TX-A09 (P0): quantity mismatch (3 values vs 2 requested).
    void a09_quantityMismatch();
    // TX-A10 (P1): malformed exception.
    void a10_malformedException();
    // TX-A11 (P1): FrameTooShort -> ProtocolError.
    void a11_frameTooShort();
    // TX-A12 (P1): elapsed preserved on Success (37ms, no real waiting).
    void a12_elapsedPreserved();
};

void TransactionAnalysisTest::a01_success()
{
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(kNormalResponse), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::Success);
    QCOMPARE(analysis.elapsed, ms{25});
    QVERIFY(!analysis.exceptionCode.has_value());
}

void TransactionAnalysisTest::a02_exception()
{
    const ModbusRtuFrame exceptionResponse{
        .address = 0x01, .functionCode = 0x83, .data = {0x02}};
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(exceptionResponse), ms{30}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::Exception);
    QCOMPARE(analysis.elapsed, ms{30});
    QVERIFY(analysis.exceptionCode.has_value());
    QCOMPARE(analysis.exceptionCode.value(), std::uint8_t{0x02});
}

void TransactionAnalysisTest::a03_crcError()
{
    const ResponseObservation observation{RtuDecodeError{RtuDecodeErrorCode::CrcMismatch}};
    const auto analysis = analyzeFunction03Transaction(kRequest, observation, ms{40}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::CrcError);
    QVERIFY(!analysis.exceptionCode.has_value());
}

void TransactionAnalysisTest::a04_pending()
{
    const ResponseObservation observation{NoResponse{}};
    const auto analysis = analyzeFunction03Transaction(kRequest, observation, ms{800}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::Pending);
    QCOMPARE(analysis.elapsed, ms{800});
    QVERIFY(!analysis.exceptionCode.has_value());
}

void TransactionAnalysisTest::a05_timeoutBoundary()
{
    const ResponseObservation observation{NoResponse{}};

    const auto atBoundary =
        analyzeFunction03Transaction(kRequest, observation, ms{1000}, ms{1000});
    QCOMPARE(atBoundary.status, TransactionStatus::Timeout);
    QCOMPARE(atBoundary.elapsed, ms{1000});

    const auto beyond =
        analyzeFunction03Transaction(kRequest, observation, ms{1200}, ms{1000});
    QCOMPARE(beyond.status, TransactionStatus::Timeout);
    QCOMPARE(beyond.elapsed, ms{1200});
}

void TransactionAnalysisTest::a06_wrongDeviceAddress()
{
    const ModbusRtuFrame otherDevice{
        .address = 0x02, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(otherDevice), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
}

void TransactionAnalysisTest::a07_wrongFunction()
{
    const ModbusRtuFrame wrongFunction{
        .address = 0x01, .functionCode = 0x04, .data = {0x02, 0x00, 0x64}};
    const auto wrong = analyzeFunction03Transaction(
        kRequest, frameObservation(wrongFunction), ms{25}, ms{1000});
    QCOMPARE(wrong.status, TransactionStatus::ProtocolError);

    // 0x84 carries the exception bit, but it is the exception of function
    // 0x04 — not a match for a 0x03 request.
    const ModbusRtuFrame wrongException{
        .address = 0x01, .functionCode = 0x84, .data = {0x02}};
    const auto wrongExceptionAnalysis = analyzeFunction03Transaction(
        kRequest, frameObservation(wrongException), ms{25}, ms{1000});
    QCOMPARE(wrongExceptionAnalysis.status, TransactionStatus::ProtocolError);
}

void TransactionAnalysisTest::a08_malformedNormalResponse()
{
    // byteCount claims 4 register bytes, only 2 follow: response decoder
    // (T004B) must fail and the analyzer maps that to ProtocolError.
    const ModbusRtuFrame malformed{
        .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64}};
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(malformed), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
}

void TransactionAnalysisTest::a09_quantityMismatch()
{
    // Single-frame legal (3 registers) but the request asked for 2.
    const ModbusRtuFrame threeValues{
        .address = 0x01, .functionCode = 0x03, .data = {0x06, 0x00, 0x64, 0x00, 0xC8, 0x05, 0xDC}};
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(threeValues), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
}

void TransactionAnalysisTest::a10_malformedException()
{
    const ModbusRtuFrame malformed{.address = 0x01, .functionCode = 0x83, .data = {}};
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(malformed), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(!analysis.exceptionCode.has_value());
}

void TransactionAnalysisTest::a11_frameTooShort()
{
    const ResponseObservation observation{RtuDecodeError{RtuDecodeErrorCode::FrameTooShort}};
    const auto analysis = analyzeFunction03Transaction(kRequest, observation, ms{40}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
}

void TransactionAnalysisTest::a12_elapsedPreserved()
{
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(kNormalResponse), ms{37}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::Success);
    QCOMPARE(analysis.elapsed, ms{37});
    // a02/a03 already show Exception/CrcError preserving elapsed too — the
    // invariant holds on every return path (single makeAnalysis helper).
}

} // namespace

QTEST_MAIN(TransactionAnalysisTest)
#include "test_transaction_analysis.moc"