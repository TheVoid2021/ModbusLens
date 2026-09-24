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
using modbuslens::core::TransactionIssueCode;
using modbuslens::core::TransactionStatus;
using modbuslens::core::analyzeFunction03Transaction;
using modbuslens::core::transactionIssueName;

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

// M10 correction: the register-read schema's function byte is a parameter
// (0x03 default) — the analyzer must be function-code agnostic.
ModbusRtuFrame readRequest(std::uint8_t device, std::uint16_t start,
                           std::uint16_t quantity,
                           std::uint8_t readFunctionCode = 0x03)
{
    return ModbusRtuFrame{
        .address = device,
        .functionCode = readFunctionCode,
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
    // ---- T014: deterministic issue detail (ProtocolError reasons) ----
    // T014-A13 (P0): defensive request path -> UnknownProtocolError sentinel.
    void a13_defensiveUnknownProtocolError();
    // T014-A14 (P0): every non-ProtocolError status leaves issue absent.
    void a14_nonProtocolStatusesIssueAbsent();
    // T014-A15 (P0): status==ProtocolError <=> issue.has_value() sweep.
    void a15_protocolErrorIssuePresenceInvariant();
    // T014-A16 (P0): repeated analysis is fully deterministic (incl. issue).
    void a16_issueDeterminism();
    // T014-A17 (P0): per-code payload invariants (required/absent columns).
    void a17_perCodePayloadInvariants();
    // T014-A18 (P1): stable machine serialization tokens.
    void a18_issueNameTokens();

    // ---- M10 correction: the read function code is dynamic ----
    // The register-read schema (start+quantity -> byteCount+uint16 words) is
    // function-code agnostic: expected normal/exception codes derive from the
    // REQUEST frame (0x03->0x83, 0x04->0x84, 0x41->0xC1).
    void fc01_readSchemaIsFunctionCodeAgnostic();
    void fc02_dynamicExceptionFunction();
    void fc03_dynamicMismatchCarriesReceivedFunction();
};

void TransactionAnalysisTest::a01_success()
{
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(kNormalResponse), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::Success);
    QCOMPARE(analysis.elapsed, ms{25});
    QVERIFY(!analysis.exceptionCode.has_value());
    QVERIFY(!analysis.issue.has_value());
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
    QVERIFY(!analysis.issue.has_value());
}

void TransactionAnalysisTest::a03_crcError()
{
    const ResponseObservation observation{RtuDecodeError{RtuDecodeErrorCode::CrcMismatch}};
    const auto analysis = analyzeFunction03Transaction(kRequest, observation, ms{40}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::CrcError);
    QVERIFY(!analysis.exceptionCode.has_value());
    QVERIFY(!analysis.issue.has_value());
}

void TransactionAnalysisTest::a04_pending()
{
    const ResponseObservation observation{NoResponse{}};
    const auto analysis = analyzeFunction03Transaction(kRequest, observation, ms{800}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::Pending);
    QCOMPARE(analysis.elapsed, ms{800});
    QVERIFY(!analysis.exceptionCode.has_value());
    QVERIFY(!analysis.issue.has_value());
}

void TransactionAnalysisTest::a05_timeoutBoundary()
{
    const ResponseObservation observation{NoResponse{}};

    const auto atBoundary =
        analyzeFunction03Transaction(kRequest, observation, ms{1000}, ms{1000});
    QCOMPARE(atBoundary.status, TransactionStatus::Timeout);
    QCOMPARE(atBoundary.elapsed, ms{1000});
    QVERIFY(!atBoundary.issue.has_value());

    const auto beyond =
        analyzeFunction03Transaction(kRequest, observation, ms{1200}, ms{1000});
    QCOMPARE(beyond.status, TransactionStatus::Timeout);
    QCOMPARE(beyond.elapsed, ms{1200});
    QVERIFY(!beyond.issue.has_value());
}

void TransactionAnalysisTest::a06_wrongDeviceAddress()
{
    const ModbusRtuFrame otherDevice{
        .address = 0x02, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(otherDevice), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QCOMPARE(analysis.elapsed, ms{25});
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code, TransactionIssueCode::ResponseAddressMismatch);
    QVERIFY(analysis.issue->expectedAddress.has_value());
    QCOMPARE(*analysis.issue->expectedAddress, std::uint8_t{0x01});
    QVERIFY(analysis.issue->actualAddress.has_value());
    QCOMPARE(*analysis.issue->actualAddress, std::uint8_t{0x02});
    QVERIFY(!analysis.issue->actualFunctionCode.has_value());
    QVERIFY(!analysis.issue->expectedQuantity.has_value());
    QVERIFY(!analysis.issue->actualQuantity.has_value());
}

void TransactionAnalysisTest::a07_wrongFunction()
{
    const ModbusRtuFrame wrongFunction{
        .address = 0x01, .functionCode = 0x04, .data = {0x02, 0x00, 0x64}};
    const auto wrong = analyzeFunction03Transaction(
        kRequest, frameObservation(wrongFunction), ms{25}, ms{1000});
    QCOMPARE(wrong.status, TransactionStatus::ProtocolError);
    QVERIFY(wrong.issue.has_value());
    QCOMPARE(wrong.issue->code, TransactionIssueCode::UnexpectedResponseFunction);
    QVERIFY(wrong.issue->actualFunctionCode.has_value());
    QCOMPARE(*wrong.issue->actualFunctionCode, std::uint8_t{0x04});
    QVERIFY(!wrong.issue->expectedAddress.has_value());
    QVERIFY(!wrong.issue->actualAddress.has_value());
    QVERIFY(!wrong.issue->expectedQuantity.has_value());
    QVERIFY(!wrong.issue->actualQuantity.has_value());

    // 0x84 carries the exception bit, but it is the exception of function
    // 0x04 — not a match for a 0x03 request.
    const ModbusRtuFrame wrongException{
        .address = 0x01, .functionCode = 0x84, .data = {0x02}};
    const auto wrongExceptionAnalysis = analyzeFunction03Transaction(
        kRequest, frameObservation(wrongException), ms{25}, ms{1000});
    QCOMPARE(wrongExceptionAnalysis.status, TransactionStatus::ProtocolError);
    QVERIFY(wrongExceptionAnalysis.issue.has_value());
    QCOMPARE(wrongExceptionAnalysis.issue->code,
             TransactionIssueCode::UnexpectedResponseFunction);
    QCOMPARE(*wrongExceptionAnalysis.issue->actualFunctionCode, std::uint8_t{0x84});
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
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code, TransactionIssueCode::MalformedNormalResponse);
    QVERIFY(!analysis.issue->expectedAddress.has_value());
    QVERIFY(!analysis.issue->actualAddress.has_value());
    QVERIFY(!analysis.issue->actualFunctionCode.has_value());
    QVERIFY(!analysis.issue->expectedQuantity.has_value());
    QVERIFY(!analysis.issue->actualQuantity.has_value());
}

void TransactionAnalysisTest::a09_quantityMismatch()
{
    // Single-frame legal (3 registers) but the request asked for 2.
    const ModbusRtuFrame threeValues{
        .address = 0x01, .functionCode = 0x03, .data = {0x06, 0x00, 0x64, 0x00, 0xC8, 0x05, 0xDC}};
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(threeValues), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code, TransactionIssueCode::QuantityMismatch);
    QVERIFY(analysis.issue->expectedQuantity.has_value());
    QCOMPARE(*analysis.issue->expectedQuantity, std::uint16_t{2});
    QVERIFY(analysis.issue->actualQuantity.has_value());
    QCOMPARE(*analysis.issue->actualQuantity, std::uint16_t{3});
    QVERIFY(!analysis.issue->expectedAddress.has_value());
    QVERIFY(!analysis.issue->actualAddress.has_value());
    QVERIFY(!analysis.issue->actualFunctionCode.has_value());
}

void TransactionAnalysisTest::a10_malformedException()
{
    const ModbusRtuFrame malformed{.address = 0x01, .functionCode = 0x83, .data = {}};
    const auto analysis = analyzeFunction03Transaction(
        kRequest, frameObservation(malformed), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(!analysis.exceptionCode.has_value());
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code, TransactionIssueCode::MalformedExceptionResponse);
    QVERIFY(!analysis.issue->expectedAddress.has_value());
    QVERIFY(!analysis.issue->actualAddress.has_value());
    QVERIFY(!analysis.issue->actualFunctionCode.has_value());
    QVERIFY(!analysis.issue->expectedQuantity.has_value());
    QVERIFY(!analysis.issue->actualQuantity.has_value());
}

void TransactionAnalysisTest::a11_frameTooShort()
{
    const ResponseObservation observation{RtuDecodeError{RtuDecodeErrorCode::FrameTooShort}};
    const auto analysis = analyzeFunction03Transaction(kRequest, observation, ms{40}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QCOMPARE(analysis.elapsed, ms{40});
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code, TransactionIssueCode::ResponseFrameTooShort);
    QVERIFY(!analysis.issue->expectedAddress.has_value());
    QVERIFY(!analysis.issue->actualAddress.has_value());
    QVERIFY(!analysis.issue->actualFunctionCode.has_value());
    QVERIFY(!analysis.issue->expectedQuantity.has_value());
    QVERIFY(!analysis.issue->actualQuantity.has_value());
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

void TransactionAnalysisTest::a13_defensiveUnknownProtocolError()
{
    // The analyzer's request contract ("already-validated FC03 request") is
    // violated on purpose: an empty data field fails the defensive request
    // re-decoding inside the fc==0x03 branch (T014 branch 7). The analyzer
    // maps it deterministically to ProtocolError + UnknownProtocolError
    // sentinel — never a crash, never a fabricated reason.
    const ModbusRtuFrame invalidRequest{
        .address = 0x01, .functionCode = 0x03, .data = {}};
    const auto analysis = analyzeFunction03Transaction(
        invalidRequest, frameObservation(kNormalResponse), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code, TransactionIssueCode::UnknownProtocolError);
    QVERIFY(!analysis.issue->expectedAddress.has_value());
    QVERIFY(!analysis.issue->actualAddress.has_value());
    QVERIFY(!analysis.issue->actualFunctionCode.has_value());
    QVERIFY(!analysis.issue->expectedQuantity.has_value());
    QVERIFY(!analysis.issue->actualQuantity.has_value());
}

void TransactionAnalysisTest::a14_nonProtocolStatusesIssueAbsent()
{
    // Success / Exception / CrcError / Timeout / Pending must never carry an
    // issue: the orthogonal detail axis belongs to ProtocolError only.
    const ModbusRtuFrame exceptionResponse{
        .address = 0x01, .functionCode = 0x83, .data = {0x02}};

    const auto success = analyzeFunction03Transaction(
        kRequest, frameObservation(kNormalResponse), ms{25}, ms{1000});
    QVERIFY(!success.issue.has_value());

    const auto exception = analyzeFunction03Transaction(
        kRequest, frameObservation(exceptionResponse), ms{25}, ms{1000});
    QVERIFY(!exception.issue.has_value());

    const auto crcError = analyzeFunction03Transaction(
        kRequest, ResponseObservation{RtuDecodeError{RtuDecodeErrorCode::CrcMismatch}},
        ms{25}, ms{1000});
    QVERIFY(!crcError.issue.has_value());

    const auto timeout = analyzeFunction03Transaction(
        kRequest, ResponseObservation{NoResponse{}}, ms{1000}, ms{1000});
    QVERIFY(!timeout.issue.has_value());

    const auto pending = analyzeFunction03Transaction(
        kRequest, ResponseObservation{NoResponse{}}, ms{800}, ms{1000});
    QVERIFY(!pending.issue.has_value());
}

void TransactionAnalysisTest::a15_protocolErrorIssuePresenceInvariant()
{
    // Sweep: exactly the ProtocolError statuses carry an issue — the
    // production analyzer invariant from both directions on real inputs.
    const ModbusRtuFrame wrongAddress{
        .address = 0x02, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    const ModbusRtuFrame wrongFunction{
        .address = 0x01, .functionCode = 0x04, .data = {0x02, 0x00, 0x64}};
    const ModbusRtuFrame malformedNormalFrame{
        .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64}};
    const ModbusRtuFrame quantityMismatch{
        .address = 0x01, .functionCode = 0x03, .data = {0x06, 0x00, 0x64, 0x00, 0xC8, 0x05, 0xDC}};
    const ModbusRtuFrame malformedException{
        .address = 0x01, .functionCode = 0x83, .data = {}};
    const ModbusRtuFrame exceptionResponse{
        .address = 0x01, .functionCode = 0x83, .data = {0x02}};

    const auto shortWire = analyzeFunction03Transaction(
        kRequest, ResponseObservation{RtuDecodeError{RtuDecodeErrorCode::FrameTooShort}},
        ms{25}, ms{1000});

    QCOMPARE(shortWire.status, TransactionStatus::ProtocolError);
    QVERIFY(shortWire.issue.has_value());
    QCOMPARE(shortWire.issue->code, TransactionIssueCode::ResponseFrameTooShort);

    const auto addressMismatch = analyzeFunction03Transaction(
        kRequest, frameObservation(wrongAddress), ms{25}, ms{1000});
    QCOMPARE(addressMismatch.status, TransactionStatus::ProtocolError);
    QVERIFY(addressMismatch.issue.has_value());

    const auto functionMismatch = analyzeFunction03Transaction(
        kRequest, frameObservation(wrongFunction), ms{25}, ms{1000});
    QCOMPARE(functionMismatch.status, TransactionStatus::ProtocolError);
    QVERIFY(functionMismatch.issue.has_value());

    const auto malformedNormalAnalysis = analyzeFunction03Transaction(
        kRequest, frameObservation(malformedNormalFrame), ms{25}, ms{1000});
    QCOMPARE(malformedNormalAnalysis.status, TransactionStatus::ProtocolError);
    QVERIFY(malformedNormalAnalysis.issue.has_value());

    const auto quantityMismatchAnalysis = analyzeFunction03Transaction(
        kRequest, frameObservation(quantityMismatch), ms{25}, ms{1000});
    QCOMPARE(quantityMismatchAnalysis.status, TransactionStatus::ProtocolError);
    QVERIFY(quantityMismatchAnalysis.issue.has_value());

    const auto malformedExceptionAnalysis = analyzeFunction03Transaction(
        kRequest, frameObservation(malformedException), ms{25}, ms{1000});
    QCOMPARE(malformedExceptionAnalysis.status, TransactionStatus::ProtocolError);
    QVERIFY(malformedExceptionAnalysis.issue.has_value());
}

void TransactionAnalysisTest::a16_issueDeterminism()
{
    // Same input twice -> exactly equal analyses, including the issue and
    // its payload (pure function, T014 I6).
    const ModbusRtuFrame otherDevice{
        .address = 0x02, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    const auto first = analyzeFunction03Transaction(
        kRequest, frameObservation(otherDevice), ms{25}, ms{1000});
    const auto second = analyzeFunction03Transaction(
        kRequest, frameObservation(otherDevice), ms{25}, ms{1000});
    QVERIFY(first == second);

    const auto third = analyzeFunction03Transaction(
        kRequest, ResponseObservation{RtuDecodeError{RtuDecodeErrorCode::FrameTooShort}},
        ms{40}, ms{1000});
    const auto fourth = analyzeFunction03Transaction(
        kRequest, ResponseObservation{RtuDecodeError{RtuDecodeErrorCode::FrameTooShort}},
        ms{40}, ms{1000});
    QVERIFY(third == fourth);
}

void TransactionAnalysisTest::a17_perCodePayloadInvariants()
{
    // One analyzer-produced sample per reason code (the same production
    // inputs as the dedicated tests); assert the exact per-code payload
    // table: required fields present, every other column absent.
    auto hasOnly = [](const TransactionAnalysis& a,
                      TransactionIssueCode code) {
        return a.issue.has_value() && a.issue->code == code;
    };

    {
        const auto a = analyzeFunction03Transaction(
            kRequest, ResponseObservation{RtuDecodeError{RtuDecodeErrorCode::FrameTooShort}},
            ms{25}, ms{1000});
        QVERIFY(hasOnly(a, TransactionIssueCode::ResponseFrameTooShort));
        QVERIFY(!a.issue->expectedAddress && !a.issue->actualAddress
                && !a.issue->actualFunctionCode
                && !a.issue->expectedQuantity && !a.issue->actualQuantity);
    }
    {
        const ModbusRtuFrame other{
            .address = 0x02, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
        const auto a = analyzeFunction03Transaction(
            kRequest, frameObservation(other), ms{25}, ms{1000});
        QVERIFY(hasOnly(a, TransactionIssueCode::ResponseAddressMismatch));
        QVERIFY(a.issue->expectedAddress && a.issue->actualAddress);
        QVERIFY(!a.issue->actualFunctionCode
                && !a.issue->expectedQuantity && !a.issue->actualQuantity);
    }
    {
        const ModbusRtuFrame malformed{
            .address = 0x01, .functionCode = 0x83, .data = {}};
        const auto a = analyzeFunction03Transaction(
            kRequest, frameObservation(malformed), ms{25}, ms{1000});
        QVERIFY(hasOnly(a, TransactionIssueCode::MalformedExceptionResponse));
        QVERIFY(!a.issue->expectedAddress && !a.issue->actualAddress
                && !a.issue->actualFunctionCode
                && !a.issue->expectedQuantity && !a.issue->actualQuantity);
    }
    {
        const ModbusRtuFrame malformed{
            .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64}};
        const auto a = analyzeFunction03Transaction(
            kRequest, frameObservation(malformed), ms{25}, ms{1000});
        QVERIFY(hasOnly(a, TransactionIssueCode::MalformedNormalResponse));
        QVERIFY(!a.issue->expectedAddress && !a.issue->actualAddress
                && !a.issue->actualFunctionCode
                && !a.issue->expectedQuantity && !a.issue->actualQuantity);
    }
    {
        const ModbusRtuFrame threeValues{
            .address = 0x01, .functionCode = 0x03, .data = {0x06, 0x00, 0x64, 0x00, 0xC8, 0x05, 0xDC}};
        const auto a = analyzeFunction03Transaction(
            kRequest, frameObservation(threeValues), ms{25}, ms{1000});
        QVERIFY(hasOnly(a, TransactionIssueCode::QuantityMismatch));
        QVERIFY(a.issue->expectedQuantity && a.issue->actualQuantity);
        QVERIFY(!a.issue->expectedAddress && !a.issue->actualAddress
                && !a.issue->actualFunctionCode);
    }
    {
        const ModbusRtuFrame wrongFunction{
            .address = 0x01, .functionCode = 0x04, .data = {0x02, 0x00, 0x64}};
        const auto a = analyzeFunction03Transaction(
            kRequest, frameObservation(wrongFunction), ms{25}, ms{1000});
        QVERIFY(hasOnly(a, TransactionIssueCode::UnexpectedResponseFunction));
        QVERIFY(a.issue->actualFunctionCode);
        QVERIFY(!a.issue->expectedAddress && !a.issue->actualAddress
                && !a.issue->expectedQuantity && !a.issue->actualQuantity);
    }
    {
        const ModbusRtuFrame invalidRequest{
            .address = 0x01, .functionCode = 0x03, .data = {}};
        const auto a = analyzeFunction03Transaction(
            invalidRequest, frameObservation(kNormalResponse), ms{25}, ms{1000});
        QVERIFY(hasOnly(a, TransactionIssueCode::UnknownProtocolError));
        QVERIFY(!a.issue->expectedAddress && !a.issue->actualAddress
                && !a.issue->actualFunctionCode
                && !a.issue->expectedQuantity && !a.issue->actualQuantity);
    }
}

void TransactionAnalysisTest::a18_issueNameTokens()
{
    // Stable machine tokens used by adapter/tool serialization.
    QVERIFY(transactionIssueName(
        TransactionIssueCode::ResponseFrameTooShort) == "response_frame_too_short");
    QVERIFY(transactionIssueName(
        TransactionIssueCode::ResponseAddressMismatch) == "response_address_mismatch");
    QVERIFY(transactionIssueName(
        TransactionIssueCode::MalformedExceptionResponse) == "malformed_exception_response");
    QVERIFY(transactionIssueName(
        TransactionIssueCode::MalformedNormalResponse) == "malformed_normal_response");
    QVERIFY(transactionIssueName(
        TransactionIssueCode::QuantityMismatch) == "quantity_mismatch");
    QVERIFY(transactionIssueName(
        TransactionIssueCode::UnexpectedResponseFunction) == "unexpected_response_function");
    QVERIFY(transactionIssueName(
        TransactionIssueCode::UnknownProtocolError) == "unknown_protocol_error");
}

} // namespace

// ---------------------------------------------------------------------------
// M10 correction: the read function code is DYNAMIC. A register read with
// function 0x04 / a custom 0x41 must succeed through the SAME analyzer, and
// its exception form must be F|0x80 — never a second per-code analyzer.
// ---------------------------------------------------------------------------

void TransactionAnalysisTest::fc01_readSchemaIsFunctionCodeAgnostic()
{
    for (const std::uint8_t fn : {std::uint8_t{0x04}, std::uint8_t{0x41}}) {
        const auto request = readRequest(0x01, 0x0000, 2, fn);
        const ModbusRtuFrame normalResponse{
            .address = 0x01,
            .functionCode = fn,
            .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
        const auto analysis = analyzeFunction03Transaction(
            request, frameObservation(normalResponse), ms{25}, ms{1000});
        QCOMPARE(analysis.status, TransactionStatus::Success);
        QCOMPARE(analysis.elapsed, ms{25});
        QCOMPARE(analysis.values, (std::vector<std::uint16_t>{0x0064, 0x00C8}));
        QVERIFY(!analysis.issue.has_value());
    }
}

void TransactionAnalysisTest::fc02_dynamicExceptionFunction()
{
    struct Case {
        std::uint8_t requestFunction;
        std::uint8_t exceptionFunction;
    };
    const std::vector<Case> cases = {
        {0x03, 0x83}, {0x04, 0x84}, {0x41, 0xC1}, {0x7F, 0xFF}};
    for (const Case& c : cases) {
        const auto request = readRequest(0x01, 0x0000, 2, c.requestFunction);
        const ModbusRtuFrame exceptionResponse{
            .address = 0x01,
            .functionCode = c.exceptionFunction,
            .data = {0x02}};
        const auto analysis = analyzeFunction03Transaction(
            request, frameObservation(exceptionResponse), ms{30}, ms{1000});
        QCOMPARE(analysis.status, TransactionStatus::Exception);
        QVERIFY(analysis.exceptionCode.has_value());
        QCOMPARE(analysis.exceptionCode.value(), std::uint8_t{0x02});
        QVERIFY(!analysis.issue.has_value());
    }
}

void TransactionAnalysisTest::fc03_dynamicMismatchCarriesReceivedFunction()
{
    // A register read with function 0x41 answered by a well-formed 0x03
    // reply: the mismatch is a pairing fact and carries the RECEIVED byte.
    // The expected side is the request's own function (0x41) — asserted by
    // the controller-level READ-FC5 test, which owns the request identity.
    const auto request = readRequest(0x01, 0x0000, 2, 0x41);
    const ModbusRtuFrame fc03Reply{
        .address = 0x01,
        .functionCode = 0x03,
        .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    const auto analysis = analyzeFunction03Transaction(
        request, frameObservation(fc03Reply), ms{25}, ms{1000});
    QCOMPARE(analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(analysis.issue.has_value());
    QCOMPARE(analysis.issue->code, TransactionIssueCode::UnexpectedResponseFunction);
    QVERIFY(analysis.issue->actualFunctionCode.has_value());
    QCOMPARE(*analysis.issue->actualFunctionCode, std::uint8_t{0x03});
    // The VALUES column stays empty: a mismatched reply is never decoded as
    // if it belonged to this request (T023 READ-RX-5).
    QVERIFY(analysis.values.empty());
}

QTEST_MAIN(TransactionAnalysisTest)
#include "test_transaction_analysis.moc"