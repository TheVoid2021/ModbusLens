#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/analysis/PassiveTransactionAnalysis.h"
#include "core/analysis/TransactionAnalysis.h"
#include "core/protocol/Function06.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/serial/SerialTransactionSession.h"

using modbuslens::core::ActiveFunction;
using modbuslens::core::ActiveRequestDescriptor;
using modbuslens::core::ActiveRequestIntent;
using modbuslens::core::analyzeWriteSingleRegisterTransaction;
using modbuslens::core::encodeActiveRequest;
using modbuslens::core::encodeRtuFrame;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::NoResponse;
using modbuslens::core::ResponseObservation;
using modbuslens::core::SerialFeedResult;
using modbuslens::core::SerialTransactionError;
using modbuslens::core::SerialTransactionErrorCode;
using modbuslens::core::SerialTransactionSession;
using modbuslens::core::SerialTransactionState;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionIssueCode;
using modbuslens::core::TransactionStatus;
using modbuslens::core::WriteSingleRegisterIntent;

using ms = std::chrono::milliseconds;

namespace {

constexpr std::uint8_t kUnit = 0x11;
constexpr std::uint16_t kAddress = 0x0001;
constexpr std::uint16_t kValue = 0x0003;
constexpr ms kTimeout{1000};

ActiveRequestIntent writeIntent(std::uint8_t unit = kUnit,
                                std::uint16_t address = kAddress,
                                std::uint16_t value = kValue)
{
    return ActiveRequestIntent{
        .function = ActiveFunction::WriteSingleRegister,
        .unitId = unit,
        .timeout = kTimeout,
        .payload = WriteSingleRegisterIntent{.registerAddress = address,
                                             .value = value},
    };
}

ActiveRequestDescriptor writeDescriptor(std::uint8_t unit = kUnit,
                                        std::uint16_t address = kAddress,
                                        std::uint16_t value = kValue)
{
    const auto encoded = encodeActiveRequest(writeIntent(unit, address, value));
    return std::get<ActiveRequestDescriptor>(encoded);
}

// The exact echo a conforming device returns for the default request.
std::vector<std::uint8_t> echoWire(std::uint8_t unit = kUnit,
                                   std::uint16_t address = kAddress,
                                   std::uint16_t value = kValue)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit,
        .functionCode = 0x06,
        .data = {static_cast<std::uint8_t>(address >> 8),
                 static_cast<std::uint8_t>(address & 0xFF),
                 static_cast<std::uint8_t>(value >> 8),
                 static_cast<std::uint8_t>(value & 0xFF)},
    });
}

std::vector<std::uint8_t> exceptionWire(std::uint8_t unit, std::uint8_t code)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit, .functionCode = 0x86, .data = {code}});
}

std::vector<std::uint8_t> fc03ResponseWire(std::uint8_t unit)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit, .functionCode = 0x03, .data = {0x02, 0x00, 0x64}});
}

std::vector<std::uint8_t> fc10ResponseWire(std::uint8_t unit)
{
    // Function 0x10 normal response: start address + quantity written (4 data
    // bytes) -> 8 bytes total, the same fixed shape 0x06 has.
    return encodeRtuFrame(ModbusRtuFrame{.address = unit,
                                         .functionCode = 0x10,
                                         .data = {0x00, 0x01, 0x00, 0x01}});
}

std::vector<std::uint8_t> unknownShapeWire(std::uint8_t unit)
{
    // Function 0x04: NOT in the framing table (a deliberate limitation), but
    // still a valid RTU frame on the wire.
    return encodeRtuFrame(ModbusRtuFrame{
        .address = unit, .functionCode = 0x04, .data = {0x02, 0x00, 0x64}});
}

std::optional<TransactionAnalysis> asAnalysis(const SerialFeedResult& result)
{
    if (const auto* analysis = std::get_if<TransactionAnalysis>(&result)) {
        return *analysis;
    }
    return std::nullopt;
}

// Feed bytes in the given chunk sizes and return the analysis of the chunk
// that completed the candidate (nullopt when nothing completed yet).
std::optional<TransactionAnalysis> feedChunks(SerialTransactionSession& session,
                                              const std::vector<std::uint8_t>& wire,
                                              const std::vector<std::size_t>& chunks)
{
    std::size_t offset = 0;
    for (const std::size_t size : chunks) {
        const std::size_t end = std::min(offset + size, wire.size());
        const auto result = session.feedResponseBytes(
            std::span<const std::uint8_t>(wire.data() + offset, end - offset),
            ms{25});
        if (const auto analysis = asAnalysis(result)) {
            return analysis;
        }
        offset = end;
    }
    return std::nullopt;
}

} // namespace

// ---------------------------------------------------------------------------
// M10-D2: FC06 ACTIVE response support — the protocol/session half of the 0x06
// write path. No Controller dispatch, no transport send, no product capability:
// `write06Supported` still does not exist.
// ---------------------------------------------------------------------------
class Fc06ActiveTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- RED evidence (captured before the implementation) ----
    void r1_beginWriteSingleRegisterWasRefused();
    void r2_matchingEchoDidNotFrameOnArrival();
    void r3_wf2Fc10ShapeDidNotFrame();

    // ---- normal response + fragmentation ----
    void nr1_normalEcho();
    void f1_singleChunk();
    void f2_splitOneSeven();
    void f3_splitTwoTwoFour();
    void f4_byteByByte();
    void f5_crcBytesSplit();

    // ---- exception + error classification ----
    void ex1_exceptionSingleChunk();
    void ex2_exceptionSplit();
    void ex3_exceptionByteByByte();
    void crc1_crcErrorKeepsRawBytes();
    void echo1_addressMismatch();
    void echo2_valueMismatch();
    void echo3_bothFieldsMismatch();
    void unit1_wrongUnitIsAddressMismatch();
    void wf1_fc03AnswerToFc06Request();
    void wf2_fc10AnswerToFc06Request();
    void reg1_fc03RequestAnsweredByFc10();
    void lim1_unknownShapeWaitsForTimeout();

    // ---- timeout / partial / overlong / malformed ----
    void t1_emptyResponseTimeout();
    void p1_partialOneByte();
    void p2_partialThreeBytes();
    void p3_partialSevenBytes();
    void o1_overlongNineBytes();
    void m1_malformedSixBytes();

    // ---- shared analyzer equivalence + support matrix ----
    void eq1_passiveAndSharedAgree();
    void sup1_supportMatrix();
};

// ---------------------------------------------------------------------------
// The three facts that were RED before this change (begin refused, echo never
// framed, 0x10 shape never framed) are archived in T022 §V; here they are
// asserted in their post-D2 form, which is what a regression must protect.
// ---------------------------------------------------------------------------
void Fc06ActiveTest::r1_beginWriteSingleRegisterWasRefused()
{
    // Post-D2: the session accepts a 0x06 descriptor and enters
    // AwaitingResponse (RED before: UnsupportedFunction, state Idle).
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);
    const auto pending = session.pendingRequest();
    QVERIFY(pending.has_value());
    QCOMPARE(pending->wire, writeDescriptor().wire);
}

void Fc06ActiveTest::r2_matchingEchoDidNotFrameOnArrival()
{
    // Post-D2: the 8-byte echo completes the transaction the moment it arrives
    // (RED before: no 0x06 framing rule existed, so nothing could frame).
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(echoWire(), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

void Fc06ActiveTest::r3_wf2Fc10ShapeDidNotFrame()
{
    // Post-D2: the recognition-only 0x10 shape rule frames the reply, so it is
    // judged immediately instead of degrading to a timeout (RED before: the
    // bytes could not frame at all).
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis =
        asAnalysis(session.feedResponseBytes(fc10ResponseWire(kUnit), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::UnexpectedResponseFunction);
}

// ---------------------------------------------------------------------------
// Normal response + fragmentation
// ---------------------------------------------------------------------------
void Fc06ActiveTest::nr1_normalEcho()
{
    SerialTransactionSession session;
    const auto descriptor = writeDescriptor();
    const auto begin = session.beginActiveRequest(descriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(
        session.feedResponseBytes(echoWire(), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
    QVERIFY(!analysis->issue.has_value());
    QVERIFY(!analysis->exceptionCode.has_value());
    // The completed transaction resets the session to Idle by contract; the
    // send-time evidence lives in the descriptor that travelled with it (and,
    // at runtime, in the ActiveTransactionResult the transport publishes).
    QCOMPARE(descriptor.wire.size(), std::size_t{8});
    QCOMPARE(descriptor.wire[1], std::uint8_t{0x06});
    QCOMPARE(session.state(), SerialTransactionState::Idle);
    QVERIFY(!session.pendingRequest().has_value());
}

void Fc06ActiveTest::f1_singleChunk()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = feedChunks(session, echoWire(), {8});
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

void Fc06ActiveTest::f2_splitOneSeven()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = feedChunks(session, echoWire(), {1, 7});
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

void Fc06ActiveTest::f3_splitTwoTwoFour()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = feedChunks(session, echoWire(), {2, 2, 4});
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

void Fc06ActiveTest::f4_byteByByte()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = feedChunks(session, echoWire(), {1, 1, 1, 1, 1, 1, 1, 1});
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

void Fc06ActiveTest::f5_crcBytesSplit()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = feedChunks(session, echoWire(), {6, 1, 1});
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

// ---------------------------------------------------------------------------
// Exception + error classification
// ---------------------------------------------------------------------------
void Fc06ActiveTest::ex1_exceptionSingleChunk()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis =
        asAnalysis(session.feedResponseBytes(exceptionWire(kUnit, 0x02), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Exception);
    QVERIFY(analysis->exceptionCode.has_value());
    QCOMPARE(*analysis->exceptionCode, std::uint8_t{0x02});
}

void Fc06ActiveTest::ex2_exceptionSplit()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = feedChunks(session, exceptionWire(kUnit, 0x03), {1, 4});
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Exception);
    QCOMPARE(*analysis->exceptionCode, std::uint8_t{0x03});
}

void Fc06ActiveTest::ex3_exceptionByteByByte()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis =
        feedChunks(session, exceptionWire(kUnit, 0x04), {1, 1, 1, 1, 1});
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Exception);
    QCOMPARE(*analysis->exceptionCode, std::uint8_t{0x04});
}

void Fc06ActiveTest::crc1_crcErrorKeepsRawBytes()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    std::vector<std::uint8_t> wire = echoWire();
    wire[3] ^= 0xFF; // corrupt a data byte: the CRC no longer matches
    const auto analysis = asAnalysis(session.feedResponseBytes(wire, ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::CrcError);
    QVERIFY(!analysis->issue.has_value());
}

void Fc06ActiveTest::echo1_addressMismatch()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(
        echoWire(kUnit, 0x0042, kValue), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code,
             TransactionIssueCode::WriteSingleRegisterEchoMismatch);
    QCOMPARE(analysis->issue->expectedRegisterAddress, std::optional<std::uint16_t>{kAddress});
    QCOMPARE(analysis->issue->actualRegisterAddress, std::optional<std::uint16_t>{0x0042});
    QCOMPARE(analysis->issue->expectedRegisterValue, std::optional<std::uint16_t>{kValue});
    QCOMPARE(analysis->issue->actualRegisterValue, std::optional<std::uint16_t>{kValue});
}

void Fc06ActiveTest::echo2_valueMismatch()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(
        echoWire(kUnit, kAddress, 0x0099), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code,
             TransactionIssueCode::WriteSingleRegisterEchoMismatch);
    QCOMPARE(analysis->issue->expectedRegisterValue, std::optional<std::uint16_t>{kValue});
    QCOMPARE(analysis->issue->actualRegisterValue, std::optional<std::uint16_t>{0x0099});
}

void Fc06ActiveTest::echo3_bothFieldsMismatch()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis = asAnalysis(session.feedResponseBytes(
        echoWire(kUnit, 0x0100, 0x0200), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    // ONE issue carrying the full expected/actual quad — never two issues.
    QCOMPARE(analysis->issue->code,
             TransactionIssueCode::WriteSingleRegisterEchoMismatch);
    QCOMPARE(analysis->issue->expectedRegisterAddress, std::optional<std::uint16_t>{kAddress});
    QCOMPARE(analysis->issue->actualRegisterAddress, std::optional<std::uint16_t>{0x0100});
    QCOMPARE(analysis->issue->expectedRegisterValue, std::optional<std::uint16_t>{kValue});
    QCOMPARE(analysis->issue->actualRegisterValue, std::optional<std::uint16_t>{0x0200});
}

void Fc06ActiveTest::unit1_wrongUnitIsAddressMismatch()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    // A perfect 0x06 echo shape, but from another device.
    const auto analysis = asAnalysis(session.feedResponseBytes(
        echoWire(0x22, kAddress, kValue), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::ResponseAddressMismatch);
    QCOMPARE(analysis->issue->expectedAddress, std::optional<std::uint8_t>{kUnit});
    QCOMPARE(analysis->issue->actualAddress, std::optional<std::uint8_t>{0x22});
    // The unit mismatch is NEVER expressed as a register-echo mismatch.
    QVERIFY(analysis->issue->code != TransactionIssueCode::WriteSingleRegisterEchoMismatch);
}

void Fc06ActiveTest::wf1_fc03AnswerToFc06Request()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis =
        asAnalysis(session.feedResponseBytes(fc03ResponseWire(kUnit), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::UnexpectedResponseFunction);
    QCOMPARE(analysis->issue->actualFunctionCode, std::optional<std::uint8_t>{0x03});
}

void Fc06ActiveTest::wf2_fc10AnswerToFc06Request()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis =
        asAnalysis(session.feedResponseBytes(fc10ResponseWire(kUnit), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::UnexpectedResponseFunction);
    QCOMPARE(analysis->issue->actualFunctionCode, std::optional<std::uint8_t>{0x10});
}

void Fc06ActiveTest::reg1_fc03RequestAnsweredByFc10()
{
    // The 0x10 shape recognition is a SHARED framing rule, so the FC03 path
    // must be regression-tested: this behaviour (framing on arrival instead of
    // waiting for the timeout) is a deliberate D2 change and is archived.
    const auto encoded = encodeActiveRequest(ActiveRequestIntent{
        .function = ActiveFunction::ReadHoldingRegisters,
        .unitId = kUnit,
        .timeout = kTimeout,
        .payload = modbuslens::core::ReadHoldingRegistersIntent{
            .startAddress = 0, .quantity = 1},
    });
    const auto& descriptor = std::get<ActiveRequestDescriptor>(encoded);
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(descriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto analysis =
        asAnalysis(session.feedResponseBytes(fc10ResponseWire(kUnit), ms{25}));
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::UnexpectedResponseFunction);
    QCOMPARE(analysis->issue->actualFunctionCode, std::optional<std::uint8_t>{0x10});
}

void Fc06ActiveTest::lim1_unknownShapeWaitsForTimeout()
{
    // KNOWN LIMITATION (M10 D Phase 1 §S3): the framing table only contains
    // shapes this product can meet or send. An unrecognized function (0x04
    // here) has no reliable length rule, so the session deliberately does NOT
    // guess: the bytes accumulate until the timeout, and only then are they
    // decoded as a whole.
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const auto onArrival =
        session.feedResponseBytes(unknownShapeWire(kUnit), ms{25});
    QVERIFY(asAnalysis(onArrival) == std::nullopt); // no invented framing
    const auto timedOut = session.onResponseTimeout(ms{1200});
    const auto* analysis = std::get_if<TransactionAnalysis>(&timedOut);
    QVERIFY(analysis != nullptr);
    // The whole buffer is a valid RTU frame, so the timeout path can still
    // classify it — but notably NOT as a timeout.
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::UnexpectedResponseFunction);
}

// ---------------------------------------------------------------------------
// Timeout / partial / overlong / malformed
// ---------------------------------------------------------------------------
void Fc06ActiveTest::t1_emptyResponseTimeout()
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

void Fc06ActiveTest::p1_partialOneByte()
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

void Fc06ActiveTest::p2_partialThreeBytes()
{
    SerialTransactionSession session;
    const auto begin = session.beginActiveRequest(writeDescriptor());
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&begin) != nullptr);
    const std::vector<std::uint8_t> wire = echoWire();
    session.feedResponseBytes(
        std::span<const std::uint8_t>(wire.data(), 3), ms{25});
    const auto timedOut = session.onResponseTimeout(ms{1200});
    const auto* analysis = std::get_if<TransactionAnalysis>(&timedOut);
    QVERIFY(analysis != nullptr);
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code, TransactionIssueCode::ResponseFrameTooShort);
}

void Fc06ActiveTest::p3_partialSevenBytes()
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

void Fc06ActiveTest::o1_overlongNineBytes()
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
    // RECORDED REAL CONTRACT (not shaped to a guess): nine bytes are never
    // truncated into a Success. Because the codec treats the LAST two bytes as
    // the CRC, the trailing byte decides the wire verdict — with a 0x00 trailer
    // the nine-byte buffer happens to pass CRC and then fails the 0x06 data
    // length check, which is MalformedNormalResponse / ProtocolError. The
    // contract this oracle protects is "no silent truncation", not a specific
    // status for arbitrary garbage.
    QVERIFY(analysis->status != TransactionStatus::Success);
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QVERIFY(analysis->issue.has_value());
    QCOMPARE(analysis->issue->code,
             TransactionIssueCode::MalformedNormalResponse);
}

void Fc06ActiveTest::m1_malformedSixBytes()
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

// ---------------------------------------------------------------------------
// Shared analyzer equivalence + support matrix
// ---------------------------------------------------------------------------
void Fc06ActiveTest::eq1_passiveAndSharedAgree()
{
    // The SAME request/response pair through the passive analyzer and through
    // the shared FC06 analyzer must agree on status, issues and payload: the
    // shared function is the passive path's authority, not a parallel copy.
    const ModbusRtuFrame request = modbuslens::core::encodeWriteSingleRegisterRequest(
        kUnit, kAddress, kValue);

    struct Case {
        const char* name;
        ModbusRtuFrame response;
    };
    const std::vector<Case> cases = {
        {"success", modbuslens::core::encodeWriteSingleRegisterRequest(kUnit, kAddress, kValue)},
        {"address mismatch", modbuslens::core::encodeWriteSingleRegisterRequest(kUnit, 0x0042, kValue)},
        {"value mismatch", modbuslens::core::encodeWriteSingleRegisterRequest(kUnit, kAddress, 0x0099)},
        {"wrong unit", modbuslens::core::encodeWriteSingleRegisterRequest(0x22, kAddress, kValue)},
        {"wrong function", ModbusRtuFrame{.address = kUnit, .functionCode = 0x03, .data = {0x02, 0x00, 0x64}}},
        {"exception", ModbusRtuFrame{.address = kUnit, .functionCode = 0x86, .data = {0x02}}},
    };

    for (const Case& item : cases) {
        const ResponseObservation observation = item.response;
        const auto shared = analyzeWriteSingleRegisterTransaction(
            request, observation, ms{25}, kTimeout);
        const auto passive = modbuslens::core::analyzeObservedTransaction(
            request, observation, ms{25}, kTimeout);
        const auto* analyzed =
            std::get_if<modbuslens::core::AnalyzedObservedTransaction>(&passive);
        QVERIFY2(analyzed != nullptr, item.name);
        QCOMPARE(shared.status, analyzed->analysis.status);
        QCOMPARE(shared.exceptionCode, analyzed->analysis.exceptionCode);
        QCOMPARE(shared.issue.has_value(), analyzed->analysis.issue.has_value());
        if (shared.issue.has_value() && analyzed->analysis.issue.has_value()) {
            QCOMPARE(shared.issue->code, analyzed->analysis.issue->code);
            QCOMPARE(shared.issue->expectedAddress, analyzed->analysis.issue->expectedAddress);
            QCOMPARE(shared.issue->actualAddress, analyzed->analysis.issue->actualAddress);
            QCOMPARE(shared.issue->expectedRegisterAddress,
                     analyzed->analysis.issue->expectedRegisterAddress);
            QCOMPARE(shared.issue->actualRegisterAddress,
                     analyzed->analysis.issue->actualRegisterAddress);
            QCOMPARE(shared.issue->expectedRegisterValue,
                     analyzed->analysis.issue->expectedRegisterValue);
            QCOMPARE(shared.issue->actualRegisterValue,
                     analyzed->analysis.issue->actualRegisterValue);
            QCOMPARE(shared.issue->actualFunctionCode,
                     analyzed->analysis.issue->actualFunctionCode);
        }
    }
}

void Fc06ActiveTest::sup1_supportMatrix()
{
    // 0x03: encoder + session support.
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::ReadHoldingRegisters));
    // 0x06: encoder + session support (D2), but NO product capability.
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteSingleRegister));
    // 0x10: M10-E2 wired the shared active analyzer, so the session gate is
    // open. The negatives this matrix used to carry moved up a layer and are
    // asserted in their own suites (Controller dispatch / write10Supported in
    // test_write_dispatch, production UI in the main.cpp oracle).
    QVERIFY(modbuslens::core::activeFunctionSupported(
        ActiveFunction::WriteMultipleRegisters));
}

QTEST_GUILESS_MAIN(Fc06ActiveTest)
#include "test_fc06_active.moc"