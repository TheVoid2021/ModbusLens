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
