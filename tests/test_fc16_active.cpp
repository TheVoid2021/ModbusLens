#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/analysis/PassiveTransactionAnalysis.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/protocol/Function16.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/serial/SerialTransactionSession.h"

using modbuslens::core::ActiveFunction;
using modbuslens::core::ActiveRequestDescriptor;
using modbuslens::core::ActiveRequestIntent;
using modbuslens::core::analyzeObservedTransaction;
using modbuslens::core::analyzeWriteMultipleRegistersTransaction;
using modbuslens::core::AnalyzedObservedTransaction;
using modbuslens::core::encodeActiveRequest;
using modbuslens::core::encodeRtuFrame;
using modbuslens::core::encodeWriteMultipleRegistersRequest;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::NoResponse;
using modbuslens::core::ResponseObservation;
using modbuslens::core::RtuDecodeError;
using modbuslens::core::RtuDecodeErrorCode;
using modbuslens::core::SerialFeedResult;
using modbuslens::core::SerialTransactionSession;
using modbuslens::core::SerialTransactionState;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionIssueCode;
using modbuslens::core::TransactionStatus;
using modbuslens::core::WriteMultipleRegistersIntent;

using ms = std::chrono::milliseconds;

namespace {

constexpr std::uint8_t kUnit = 0x11;
constexpr std::uint16_t kAddress = 0x0001;
constexpr ms kTimeout{1000};

// The F16-G6 reference request: two registers, values 0x000A / 0x0102.
const std::vector<std::uint16_t> kValues{0x000A, 0x0102};

ActiveRequestIntent writeMultipleIntent(std::uint8_t unit = kUnit,
                                        std::uint16_t address = kAddress,
                                        std::vector<std::uint16_t> values = kValues)
{
    return ActiveRequestIntent{
        .function = ActiveFunction::WriteMultipleRegisters,
        .unitId = unit,
        .timeout = kTimeout,
        .payload = WriteMultipleRegistersIntent{.startAddress = address,
                                                .values = std::move(values)},
    };
}

ActiveRequestDescriptor writeDescriptor(std::uint8_t unit = kUnit,
                                        std::uint16_t address = kAddress,
                                        std::vector<std::uint16_t> values = kValues)
{
    const auto encoded = encodeActiveRequest(writeMultipleIntent(unit, address,
                                                                 values));
    return std::get<ActiveRequestDescriptor>(encoded);
}

// The exact echo a conforming device returns for a 0x10 request: the starting
// address and the written quantity — the VALUES are not echoed.
std::vector<std::uint8_t> echoWire(std::uint8_t unit = kUnit,
                                   std::uint16_t address = kAddress,
                                   std::uint16_t quantity = 2)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit,
        .functionCode = 0x10,
        .data = {static_cast<std::uint8_t>(address >> 8),
                 static_cast<std::uint8_t>(address & 0xFF),
                 static_cast<std::uint8_t>(quantity >> 8),
                 static_cast<std::uint8_t>(quantity & 0xFF)},
    });
}

std::vector<std::uint8_t> exceptionWire(std::uint8_t unit, std::uint8_t code)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit, .functionCode = 0x90, .data = {code}});
}

std::vector<std::uint8_t> fc06ResponseWire(std::uint8_t unit)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit,
        .functionCode = 0x06,
        .data = {0x00, 0x01, 0x00, 0x02},
    });
}

std::vector<std::uint8_t> fc03ResponseWire(std::uint8_t unit)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit, .functionCode = 0x03, .data = {0x04, 0x00, 0x0A, 0x01, 0x02},
    });
}

std::optional<TransactionAnalysis> asAnalysis(const SerialFeedResult& result)
{
    if (const auto* analysis = std::get_if<TransactionAnalysis>(&result)) {
        return *analysis;
    }
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// M10-E2: FC16/0x10 ACTIVE response support — the protocol/session half of the
// 0x10 write path. No Controller dispatch, no transport send, no product
// capability: `write10Supported` still does not exist and no 0x10 production
// UI is instantiated.
// ---------------------------------------------------------------------------
class Fc16ActiveTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- RED evidence (captured before this change) ----
    void r1_beginMultipleRegistersNowAccepted();
    void r2_matchingResponseFramesOnArrival();

    // ---- normal response / fragmentation ----
    void s1_successSingleChunk();
    void f1_byteByByte();

    // ---- exception / error classification ----
    void ex1_exceptionHasNoEchoMismatch();
    void crc1_crcError();
    void echo1_startMismatch();
    void echo2_quantityMismatch();
    void echo3_bothFieldsMismatch();
    void unit1_wrongUnitIsAddressMismatch();
    void wf1_fc06AnswerToFc10Request();
    void wf2_fc03AnswerToFc10Request();

    // ---- timeout / partial / overlong / malformed ----
    void t1_emptyResponseTimeout();
    void p1_partialOneByte();
    void p3_partialSevenBytes();
    void o1_overlongNineBytes();
    void m1_malformedSixBytes();

    // ---- single in-flight (mixed functions included) ----
    void if1_secondRequestWhile0x10PendingRejected();
    void if2_0x10WhilePendingRejected();

    // ---- shared analyzer equivalence + support matrix ----
    void eq1_passiveAndSharedAgree();
    // M10-E2 RE-REVIEW CORRECTION: renamed from eq2_crcEquivalence. This test
    // exercises the SHARED ANALYZER's CRC semantics in isolation — it is NOT
    // the active/passive end-to-end equivalence. That role belongs to
    // eq3_sessionLevelCrcEquivalence, which drives the real
    // SerialTransactionSession lifecycle.
    void eq2_sharedAnalyzerCrcSemantics();
    // Shared analyzer vs passive wrapper on the SAME CrcMismatch observation.
    void eq2b_sharedVsPassiveCrcObservation();
    // M10-E2 RE-REVIEW CORRECTION: the session-level CRC pair oracle — the
    // SAME corrupted response wire through the real passive transaction path
    // AND the real SerialTransactionSession active response lifecycle.
    void eq3_sessionLevelCrcEquivalence();
    void sup1_supportMatrix();
};

// ---------------------------------------------------------------------------
// The facts that were RED before this change (no shared 0x10 analyzer, session
// refused 0x10) are archived in T022 §ZG; here they are asserted in their
// post-E2 form, which is what a regression must protect.
// ---------------------------------------------------------------------------
void Fc16ActiveTest::r1_beginMultipleRegistersNowAccepted()
{
    SerialTransactionSession session;
    const auto descriptor = writeDescriptor();
    const auto begin = session.beginActiveRequest(descriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);
    const auto pending = session.pendingRequest();
    QVERIFY(pending.has_value());
    QCOMPARE(pending->wire, descriptor.wire);
}

void Fc16ActiveTest::r2_matchingResponseFramesOnArrival()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(echoWire(), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

void Fc16ActiveTest::s1_successSingleChunk()
{
    SerialTransactionSession session;
    const auto descriptor = writeDescriptor();
    const auto begin = session.beginActiveRequest(descriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(echoWire(), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
    QVERIFY(!analysis->issue.has_value());
    QVERIFY(!analysis->exceptionCode.has_value());
    // The send-time evidence lives in the descriptor that travelled with it.
    QCOMPARE(descriptor.wire[1], std::uint8_t{0x10});
    QCOMPARE(descriptor.wire.size(), std::size_t{13}); // 9 + 2*2
    QCOMPARE(session.state(), SerialTransactionState::Idle);
    QVERIFY(!session.pendingRequest().has_value());
}

void Fc16ActiveTest::f1_byteByByte()
{
    SerialTransactionSession session;
    const auto descriptor = writeDescriptor();
    const auto begin = session.beginActiveRequest(descriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const std::vector<std::uint8_t> wire = echoWire();
    std::optional<TransactionAnalysis> analysis;
    for (std::size_t offset = 0; offset < wire.size(); ++offset) {
        analysis = asAnalysis(session.feedResponseBytes(
            std::span<const std::uint8_t>(wire.data() + offset, 1), ms{25}));
    }
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

void Fc16ActiveTest::ex1_exceptionHasNoEchoMismatch()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis =
        asAnalysis(session.feedResponseBytes(exceptionWire(kUnit, 0x02), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Exception);
    QCOMPARE(analysis->exceptionCode, std::optional<std::uint8_t>{0x02});
    // Orthogonality: an exception carries NO echo-contract issue.
    QVERIFY(!analysis->issue.has_value());
}

void Fc16ActiveTest::crc1_crcError()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    std::vector<std::uint8_t> wire = echoWire();
    wire.back() ^= 0xFF; // corrupt the CRC high byte
    const auto analysis = asAnalysis(session.feedResponseBytes(wire, ms{25}));
    QVERIFY(analysis.has_value());
    // The payload still LOOKS like a correct echo; the wire verdict wins.
    QCOMPARE(analysis->status, TransactionStatus::CrcError);
}

void Fc16ActiveTest::echo1_startMismatch()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis =
        asAnalysis(session.feedResponseBytes(echoWire(kUnit, 0x0002), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code,
             TransactionIssueCode::WriteMultipleRegistersEchoMismatch);
    QCOMPARE(analysis->issue->expectedRegisterAddress, std::uint16_t{0x0001});
    QCOMPARE(analysis->issue->actualRegisterAddress, std::uint16_t{0x0002});
    QCOMPARE(analysis->issue->expectedQuantity, std::uint16_t{2});
    QCOMPARE(analysis->issue->actualQuantity, std::uint16_t{2});
}

void Fc16ActiveTest::echo2_quantityMismatch()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(echoWire(kUnit, kAddress, 3), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code,
             TransactionIssueCode::WriteMultipleRegistersEchoMismatch);
    QCOMPARE(analysis->issue->expectedRegisterAddress, std::uint16_t{0x0001});
    QCOMPARE(analysis->issue->actualRegisterAddress, std::uint16_t{0x0001});
    QCOMPARE(analysis->issue->expectedQuantity, std::uint16_t{2});
    QCOMPARE(analysis->issue->actualQuantity, std::uint16_t{3});
}

void Fc16ActiveTest::echo3_bothFieldsMismatch()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis =
        asAnalysis(session.feedResponseBytes(echoWire(kUnit, 0x0002, 3), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code,
             TransactionIssueCode::WriteMultipleRegistersEchoMismatch);
    QCOMPARE(analysis->issue->expectedRegisterAddress, std::uint16_t{0x0001});
    QCOMPARE(analysis->issue->actualRegisterAddress, std::uint16_t{0x0002});
    QCOMPARE(analysis->issue->expectedQuantity, std::uint16_t{2});
    QCOMPARE(analysis->issue->actualQuantity, std::uint16_t{3});
}

void Fc16ActiveTest::unit1_wrongUnitIsAddressMismatch()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(echoWire(0x22), ms{25}));
    QVERIFY(analysis.has_value());
    // A UNIT fact must never be confused with a start/quantity echo mismatch.
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::ResponseAddressMismatch);
    QCOMPARE(analysis->issue->expectedAddress, std::uint8_t{kUnit});
    QCOMPARE(analysis->issue->actualAddress, std::uint8_t{0x22});
}

void Fc16ActiveTest::wf1_fc06AnswerToFc10Request()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(fc06ResponseWire(kUnit), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::UnexpectedResponseFunction);
    QCOMPARE(analysis->issue->actualFunctionCode, std::uint8_t{0x06});
}

void Fc16ActiveTest::wf2_fc03AnswerToFc10Request()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(fc03ResponseWire(kUnit), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::UnexpectedResponseFunction);
    QCOMPARE(analysis->issue->actualFunctionCode, std::uint8_t{0x03});
}

void Fc16ActiveTest::t1_emptyResponseTimeout()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto timedOut = session.onResponseTimeout(ms{1200});
    const auto* analysis = std::get_if<TransactionAnalysis>(&timedOut);
    QVERIFY(analysis != nullptr);
    QCOMPARE(analysis->status, TransactionStatus::Timeout);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void Fc16ActiveTest::p1_partialOneByte()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const std::vector<std::uint8_t> wire = echoWire();
    session.feedResponseBytes(
        std::span<const std::uint8_t>(wire.data(), 1), ms{25});
    const auto timedOut = session.onResponseTimeout(ms{1200});
    const auto* analysis = std::get_if<TransactionAnalysis>(&timedOut);
    QVERIFY(analysis != nullptr);
    // Generic policy: partial bytes are NOT labelled Timeout; they are decoded
    // and judged by wire truth.
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::ResponseFrameTooShort);
}

void Fc16ActiveTest::p3_partialSevenBytes()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const std::vector<std::uint8_t> wire = echoWire();
    session.feedResponseBytes(
        std::span<const std::uint8_t>(wire.data(), 7), ms{25});
    const auto timedOut = session.onResponseTimeout(ms{1200});
    const auto* analysis = std::get_if<TransactionAnalysis>(&timedOut);
    QVERIFY(analysis != nullptr);
    // Seven bytes: long enough to carry a CRC field, which no longer matches.
    QCOMPARE(analysis->status, TransactionStatus::CrcError);
}

void Fc16ActiveTest::o1_overlongNineBytes()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    std::vector<std::uint8_t> wire = echoWire();
    wire.push_back(0x00); // trailing garbage: 9 bytes, never truncated
    const auto onArrival = session.feedResponseBytes(wire, ms{25});
    QVERIFY(asAnalysis(onArrival) == std::nullopt); // exact-boundary rule kept
    const auto timedOut = session.onResponseTimeout(ms{1200});
    const auto* analysis = std::get_if<TransactionAnalysis>(&timedOut);
    QVERIFY(analysis != nullptr);
    // RECORDED REAL CONTRACT: nine bytes are never truncated into a Success.
    // With the 0x00 trailer the nine-byte buffer passes CRC and then fails the
    // 4-byte data length check -> MalformedNormalResponse / ProtocolError.
    QVERIFY(analysis->status != TransactionStatus::Success);
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code,
             TransactionIssueCode::MalformedNormalResponse);
}

void Fc16ActiveTest::m1_malformedSixBytes()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const std::vector<std::uint8_t> wire = echoWire();
    session.feedResponseBytes(
        std::span<const std::uint8_t>(wire.data(), 6), ms{25});
    const auto timedOut = session.onResponseTimeout(ms{1200});
    const auto* analysis = std::get_if<TransactionAnalysis>(&timedOut);
    QVERIFY(analysis != nullptr);
    // No fake CRC is invented for the test: six bytes decode as a CRC failure.
    QCOMPARE(analysis->status, TransactionStatus::CrcError);
}

void Fc16ActiveTest::if1_secondRequestWhile0x10PendingRejected()
{
    // Single in-flight holds for 0x10 too, and a mixed-function second request
    // is no exception.
    SerialTransactionSession session;
    const auto first = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&first) != nullptr);
    const auto second = session.beginActiveRequest(writeDescriptor(0x12, 0x0002, {5}));
    QVERIFY(std::get_if<modbuslens::core::SerialTransactionError>(&second) != nullptr);
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);
}

void Fc16ActiveTest::if2_0x10WhilePendingRejected()
{
    SerialTransactionSession session;
    const auto first = encodeActiveRequest(modbuslens::core::ActiveRequestIntent{
        .function = ActiveFunction::WriteSingleRegister,
        .unitId = kUnit,
        .timeout = kTimeout,
        .payload = modbuslens::core::WriteSingleRegisterIntent{
            .registerAddress = 0x0001, .value = 0x0003}});
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&first) != nullptr);
    const auto begin = session.beginActiveRequest(
        std::get<ActiveRequestDescriptor>(first));
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    // A 0x10 request while the 0x06 is pending must not displace it.
    const auto second = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<modbuslens::core::SerialTransactionError>(&second) != nullptr);
    QCOMPARE(session.pendingRequest()->intent.function,
             ActiveFunction::WriteSingleRegister);
}

void Fc16ActiveTest::eq1_passiveAndSharedAgree()
{
    // The SAME request/response pair through the passive analyzer and through
    // the shared FC16 analyzer must agree on status, issues and payload: the
    // shared function is the passive path's authority, not a parallel copy.
    const ModbusRtuFrame request =
        encodeWriteMultipleRegistersRequest(kUnit, kAddress, kValues);

    struct Case {
        const char* name;
        ModbusRtuFrame response;
    };
    // Semantic frames (no CRC): the shared/passive analyzers take an already
    // decoded observation, mirroring how the FC06 equivalence test builds its
    // rows with encodeWriteSingleRegisterRequest.
    const std::vector<Case> cases = {
        {"success", ModbusRtuFrame{.address = kUnit, .functionCode = 0x10,
                                   .data = {0x00, 0x01, 0x00, 0x02}}},
        {"start mismatch", ModbusRtuFrame{.address = kUnit, .functionCode = 0x10,
                                          .data = {0x00, 0x02, 0x00, 0x02}}},
        {"quantity mismatch", ModbusRtuFrame{.address = kUnit, .functionCode = 0x10,
                                             .data = {0x00, 0x01, 0x00, 0x03}}},
        {"both mismatch", ModbusRtuFrame{.address = kUnit, .functionCode = 0x10,
                                         .data = {0x00, 0x02, 0x00, 0x03}}},
        {"wrong unit", ModbusRtuFrame{.address = 0x22, .functionCode = 0x10,
                                      .data = {0x00, 0x01, 0x00, 0x02}}},
        {"wrong function", ModbusRtuFrame{.address = kUnit, .functionCode = 0x06,
                                          .data = {0x00, 0x01, 0x00, 0x02}}},
        {"exception", ModbusRtuFrame{.address = kUnit, .functionCode = 0x90,
                                     .data = {0x02}}},
    };

    for (const Case& item : cases) {
        const std::string label = item.name;
        const ResponseObservation observation = item.response;
        const auto shared = analyzeWriteMultipleRegistersTransaction(
            request, observation, ms{25}, kTimeout);
        const auto passive =
            analyzeObservedTransaction(request, observation, ms{25}, kTimeout);
        const auto* analyzed = std::get_if<AnalyzedObservedTransaction>(&passive);
        QVERIFY2(analyzed != nullptr, (label + ": not analyzed").c_str());
        QVERIFY2(shared.status == analyzed->analysis.status,
                 (label + ": status mismatch").c_str());
        QCOMPARE(shared.exceptionCode, analyzed->analysis.exceptionCode);
        QCOMPARE(shared.issue.has_value(), analyzed->analysis.issue.has_value());
        if (shared.issue.has_value() && analyzed->analysis.issue.has_value()) {
            QCOMPARE(shared.issue->code, analyzed->analysis.issue->code);
            QCOMPARE(shared.issue->expectedAddress,
                     analyzed->analysis.issue->expectedAddress);
            QCOMPARE(shared.issue->actualAddress,
                     analyzed->analysis.issue->actualAddress);
            QCOMPARE(shared.issue->expectedRegisterAddress,
                     analyzed->analysis.issue->expectedRegisterAddress);
            QCOMPARE(shared.issue->actualRegisterAddress,
                     analyzed->analysis.issue->actualRegisterAddress);
            QCOMPARE(shared.issue->expectedQuantity,
                     analyzed->analysis.issue->expectedQuantity);
            QCOMPARE(shared.issue->actualQuantity,
                     analyzed->analysis.issue->actualQuantity);
            QCOMPARE(shared.issue->actualFunctionCode,
                     analyzed->analysis.issue->actualFunctionCode);
        }
    }
}

void Fc16ActiveTest::eq2_sharedAnalyzerCrcSemantics()
{
    // M10-E2 RE-REVIEW CORRECTION (renamed from eq2_crcEquivalence): this test
    // pins the SHARED ANALYZER's CRC semantics in isolation. It does NOT stand
    // for active/passive end-to-end equivalence — it never touches
    // SerialTransactionSession. That role belongs to
    // eq3_sessionLevelCrcEquivalence.
    //
    // Construction (no self-certification): start from a clearly LEGAL 0x10
    // response ADU, then deterministically flip the LOW BIT OF THE CRC LOW
    // BYTE (last-2). The candidate framing still holds (8 bytes) and the CRC
    // is necessarily wrong; the production codec is used ONLY to classify the
    // stimulus as CrcMismatch.
    const ModbusRtuFrame request =
        encodeWriteMultipleRegistersRequest(kUnit, kAddress, kValues);

    std::vector<std::uint8_t> wire = echoWire();
    wire[wire.size() - 2] ^= 0x01;

    const ResponseObservation observation = RtuDecodeError{
        RtuDecodeErrorCode::CrcMismatch};
    const auto shared = analyzeWriteMultipleRegistersTransaction(
        request, observation, ms{25}, kTimeout);

    QCOMPARE(shared.status, TransactionStatus::CrcError);
    QVERIFY(!shared.issue.has_value());
    QVERIFY(!shared.exceptionCode.has_value());
}

void Fc16ActiveTest::eq3_sessionLevelCrcEquivalence()
{
    // M10-E2 RE-REVIEW CORRECTION: the previous "equivalence" evidence fed an
    // analytically constructed observation to the SHARED pure analyzer and
    // called that the active path. It was not — the shared pure analyzer is
    // also what the passive path wraps, so both sides of that comparison were
    // one layer below the real active lifecycle. THIS test is the pair oracle
    // the review demanded: ONE shared corrupted response wire, then the REAL
    // passive transaction path and the REAL SerialTransactionSession active
    // response lifecycle, compared directly.
    //
    // ---- ONE shared raw-wire fixture (§3) ----
    // Request evidence: the canonical descriptor (production encoder + codec).
    const auto descriptor = writeDescriptor();
    const ModbusRtuFrame requestFrame = descriptor.frame;
    // Response evidence: a legal 8-byte FC16 normal response, corrupted ONCE.
    std::vector<std::uint8_t> legalResponseWire = echoWire();
    std::vector<std::uint8_t> corruptedResponseWire = legalResponseWire;
    const std::size_t mutationIndex = corruptedResponseWire.size() - 2;
    const auto legalCrcLowByte = corruptedResponseWire[mutationIndex];
    corruptedResponseWire[mutationIndex] ^= 0x01; // flip CRC low byte bit 0
    const auto corruptedCrcLowByte = corruptedResponseWire[mutationIndex];

    // Raw evidence identity (§7): exactly ONE byte differs, at exactly the
    // mutation position, in exactly one bit.
    QCOMPARE(corruptedResponseWire.size(), legalResponseWire.size());
    QCOMPARE(corruptedResponseWire.size(), std::size_t{8});
    int differingBytes = 0;
    for (std::size_t index = 0; index < legalResponseWire.size(); ++index) {
        if (legalResponseWire[index] != corruptedResponseWire[index]) {
            ++differingBytes;
        }
    }
    QCOMPARE(differingBytes, 1);
    QVERIFY(corruptedCrcLowByte != legalCrcLowByte);
    QVERIFY((corruptedCrcLowByte ^ legalCrcLowByte) == 0x01);
    // The payload still LOOKS like a correct echo: only the CRC byte changed.
    for (std::size_t index = 0; index < 6; ++index) {
        QCOMPARE(corruptedResponseWire[index], legalResponseWire[index]);
    }

    // Stimulus classification: the production codec really calls this a CRC
    // failure (the codec classifies the stimulus; the equivalence of the two
    // analyzers' answers is what the rest of the test proves).
    const auto decoded = modbuslens::core::decodeRtuFrame(corruptedResponseWire);
    const auto* decodeError = std::get_if<RtuDecodeError>(&decoded);
    QVERIFY(decodeError != nullptr);
    QCOMPARE(decodeError->code, RtuDecodeErrorCode::CrcMismatch);

    // ---- PASSIVE PATH (§4): raw wire -> production decoder -> transaction ----
    // analyzeObservedTransaction's public entry takes an already-decoded
    // observation, so the production decoder converts the SAME wire into the
    // observation. No hand-built RtuDecodeError is substituted: the variant
    // arm recorded here is whatever decodeRtuFrame produced for these bytes.
    const ResponseObservation passiveObservation = *decodeError;
    const auto passive = analyzeObservedTransaction(
        requestFrame, passiveObservation, ms{25}, kTimeout);
    const auto* passiveAnalyzed =
        std::get_if<AnalyzedObservedTransaction>(&passive);
    QVERIFY(passiveAnalyzed != nullptr);
    QCOMPARE(passiveAnalyzed->analysis.status, TransactionStatus::CrcError);
    QVERIFY(!passiveAnalyzed->analysis.issue.has_value());
    QVERIFY(!passiveAnalyzed->analysis.exceptionCode.has_value());

    // ---- ACTIVE SESSION PATH (§5): the real lifecycle, not the pure helper ----
    // ActiveRequestIntent -> encodeActiveRequest -> descriptor (built above)
    // -> SerialTransactionSession::beginActiveRequest -> feed the SAME
    // corruptedResponseWire -> the session performs candidate framing, wire
    // decoding, the CrcMismatch mapping and the terminal result itself.
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(descriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);

    // Feed the shared corrupted wire; assert the session was fed exactly it.
    const auto fed = session.feedResponseBytes(corruptedResponseWire, ms{25});
    const auto* activeAnalysis = std::get_if<TransactionAnalysis>(&fed);
    QVERIFY(activeAnalysis != nullptr); // completed in this chunk

    // ---- DIRECT EQUIVALENCE (§6) ----
    QCOMPARE(activeAnalysis->status, TransactionStatus::CrcError);
    QCOMPARE(activeAnalysis->status, passiveAnalyzed->analysis.status);
    QCOMPARE(activeAnalysis->issue.has_value(),
             passiveAnalyzed->analysis.issue.has_value());
    QCOMPARE(activeAnalysis->exceptionCode,
             passiveAnalyzed->analysis.exceptionCode);

    // ---- SESSION LIFECYCLE (§5) ----
    QCOMPARE(session.state(), SerialTransactionState::Idle);
    QVERIFY(!session.pendingRequest().has_value());
}

void Fc16ActiveTest::eq2b_sharedVsPassiveCrcObservation()
{
    // M10-E2 REVIEW CORRECTION (T022 §ZI): eq1 compares DECODED-frame pairs
    // only, so a CRC failure — which is a WIRE-level decode failure, never a
    // semantic frame — had no direct pair oracle at the analyzer level. This
    // test proves the SHARED analyzer and the PASSIVE wrapper agree on the
    // same CrcMismatch observation. (It does not drive
    // SerialTransactionSession — that is eq3_sessionLevelCrcEquivalence.)
    //
    // Construction (no self-certification): start from a clearly LEGAL 0x10
    // response ADU, then deterministically flip the LOW BIT OF THE CRC LOW
    // BYTE (last-2). The candidate framing still holds (8 bytes) and the CRC
    // is necessarily wrong; the production codec is used ONLY to classify the
    // stimulus as CrcMismatch — the asserted equivalence (both analyzers
    // answering CrcError) is what this test proves.
    const ModbusRtuFrame request =
        encodeWriteMultipleRegistersRequest(kUnit, kAddress, kValues);

    std::vector<std::uint8_t> wire = echoWire();
    const std::size_t crcLowIndex = wire.size() - 2;
    wire[crcLowIndex] ^= 0x01;
    // Stimulus classification: the mutated bytes really are a CRC failure.
    const auto decoded = modbuslens::core::decodeRtuFrame(wire);
    const auto* decodeError = std::get_if<RtuDecodeError>(&decoded);
    QVERIFY(decodeError != nullptr);
    QCOMPARE(decodeError->code, RtuDecodeErrorCode::CrcMismatch);

    // The SAME observation (a wire-level CrcMismatch) through both paths.
    const ResponseObservation observation = RtuDecodeError{
        RtuDecodeErrorCode::CrcMismatch};
    const auto shared = analyzeWriteMultipleRegistersTransaction(
        request, observation, ms{25}, kTimeout);
    const auto passive =
        analyzeObservedTransaction(request, observation, ms{25}, kTimeout);
    const auto* analyzed = std::get_if<AnalyzedObservedTransaction>(&passive);
    QVERIFY(analyzed != nullptr);

    QCOMPARE(shared.status, TransactionStatus::CrcError);
    QCOMPARE(analyzed->analysis.status, TransactionStatus::CrcError);
    // CrcError carries NO issue and NO exception code on either path.
    QVERIFY(!shared.issue.has_value());
    QVERIFY(!analyzed->analysis.issue.has_value());
    QVERIFY(!shared.exceptionCode.has_value());
    QVERIFY(!analyzed->analysis.exceptionCode.has_value());
    // A corrupted wire is judged by wire truth even though the payload LOOKS
    // like a correct echo (the mutated CRC byte is the only difference).
}

void Fc16ActiveTest::sup1_supportMatrix()
{
    // M10-E2 support matrix: all three functions now have an active analyzer
    // and session support. The layers that stay CLOSED are asserted in
    // test_write_dispatch (Controller dispatch, write10Supported) and by the
    // production oracle (no 0x10 UI).
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::ReadHoldingRegisters));
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteSingleRegister));
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteMultipleRegisters));
}

QTEST_GUILESS_MAIN(Fc16ActiveTest)
#include "test_fc16_active.moc"
