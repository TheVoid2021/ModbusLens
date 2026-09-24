#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "core/active/ActiveRequestIntent.h"
#include "core/active/ActiveTransactionEvidence.h"
#include "core/analysis/PassiveTransactionAnalysis.h"
#include "core/analysis/TransactionProvenance.h"
#include "core/protocol/Function06.h"
#include "core/protocol/Function16.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/serial/SerialTransactionSession.h"

using modbuslens::core::ActiveFunction;
using modbuslens::core::ActiveRequestDescriptor;
using modbuslens::core::ActiveRequestEncodeError;
using modbuslens::core::ActiveRequestEncodeErrorCode;
using modbuslens::core::ActiveRequestEncodeResult;
using modbuslens::core::ActiveRequestIntent;
using modbuslens::core::ActiveRequestValidationError;
using modbuslens::core::ActiveTransactionEvidence;
using modbuslens::core::ActiveTransactionResult;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::ReadHoldingRegistersIntent;
using modbuslens::core::SerialTransactionError;
using modbuslens::core::SerialTransactionErrorCode;
using modbuslens::core::SerialTransactionSession;
using modbuslens::core::SerialTransactionState;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionIssueCode;
using modbuslens::core::TransactionStatus;
using modbuslens::core::TransportDisposition;
using modbuslens::core::WriteMultipleRegistersIntent;
using modbuslens::core::WriteSingleRegisterIntent;
using modbuslens::core::activeFunctionCode;
using modbuslens::core::activeFunctionName;
using modbuslens::core::encodeActiveRequest;
using modbuslens::core::encodeWriteMultipleRegistersRequest;
using modbuslens::core::encodeRtuFrame;
using modbuslens::core::validateActiveRequestIntent;

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

ActiveRequestIntent readIntent(std::uint8_t unit = 0x01,
                               std::uint16_t start = 0x0000,
                               std::uint16_t quantity = 0x0002,
                               ms timeout = ms{1000})
{
    return ActiveRequestIntent{
        .function = ActiveFunction::ReadHoldingRegisters,
        .unitId = unit,
        .timeout = timeout,
        .payload = ReadHoldingRegistersIntent{.startAddress = start,
                                              .quantity = quantity},
    };
}

// FC03 request 01 03 00 00 00 02 + CRC (T009 golden bytes).
const auto kFc03Wire = wire({0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B});
const auto kGoodResponse9 =
    wire({0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7A});

} // namespace

class ActiveRequestTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- unified intent: validation + encoding (M10-A §6) ----
    void ac01_validationTable();
    void ac02_fc03DescriptorGoldenWire();
    void ac03_writeEncodeContract();

    // ---- generic session lifecycle (M10-A §9) ----
    void ac04_beginAcceptsDescriptorAndSnapshots();
    void ac05_inconsistentDescriptorRejected();
    void ac06_writeDescriptorRejectedBeforeSend();
    void ac07_pendingSnapshotSurvivesRefusedSecondBegin();
    void ac08_genericPathFc03Equivalence();
    void ac09_genericPathTimeout();

    // ---- evidence / disposition / provenance tokens ----
    void ac10_resultEvidenceAndDispositionTokens();
    void ac11_sourceAndFunctionTokens();

    // ---- frozen semantics regression (M10-A §27/§29) ----
    void ac12_fc06EchoMismatchRegression();
    void ac13_fc10EchoMismatchRegression();
    void ac14_outcomeIssueOrthogonalityRegression();
    void ac15_broadcastUnitNeverSentActively();
};

void ActiveRequestTest::ac01_validationTable()
{
    // Broadcast unit 0 and reserved 248+ are structurally representable but
    // never actively sendable.
    for (const std::uint8_t unit : {std::uint8_t{0}, std::uint8_t{248}}) {
        QCOMPARE(validateActiveRequestIntent(readIntent(unit)),
                 std::optional{ActiveRequestValidationError::UnitIdNotUnicast});
    }
    // FC03 quantity domain 1..125.
    QCOMPARE(validateActiveRequestIntent(readIntent(0x01, 0x0000, 0)),
             std::optional{ActiveRequestValidationError::QuantityOutOfRange});
    QCOMPARE(validateActiveRequestIntent(readIntent(0x01, 0x0000, 126)),
             std::optional{ActiveRequestValidationError::QuantityOutOfRange});
    // Timeout must be positive.
    QCOMPARE(validateActiveRequestIntent(readIntent(0x01, 0x0000, 2, ms{0})),
             std::optional{ActiveRequestValidationError::TimeoutNotPositive});
    // Payload alternative must match the declared function.
    const auto mismatched = ActiveRequestIntent{
        .function = ActiveFunction::WriteSingleRegister,
        .unitId = 1,
        .timeout = ms{1000},
        .payload = ReadHoldingRegistersIntent{.startAddress = 0, .quantity = 1},
    };
    QCOMPARE(validateActiveRequestIntent(mismatched),
             std::optional{ActiveRequestValidationError::PayloadFunctionMismatch});

    // 0x10 quantity is derived from values (1..123) — the single authority.
    const auto tooMany = ActiveRequestIntent{
        .function = ActiveFunction::WriteMultipleRegisters,
        .unitId = 1,
        .timeout = ms{1000},
        .payload = WriteMultipleRegistersIntent{
            .startAddress = 0, .values = std::vector<std::uint16_t>(124, 0x2A)},
    };
    QCOMPARE(validateActiveRequestIntent(tooMany),
             std::optional{ActiveRequestValidationError::QuantityOutOfRange});
    const auto empty = ActiveRequestIntent{
        .function = ActiveFunction::WriteMultipleRegisters,
        .unitId = 1,
        .timeout = ms{1000},
        .payload = WriteMultipleRegistersIntent{.startAddress = 0, .values = {}},
    };
    QCOMPARE(validateActiveRequestIntent(empty),
             std::optional{ActiveRequestValidationError::QuantityOutOfRange});
    // A legal 0x06 intent carries no quantity domain at all.
    QCOMPARE(validateActiveRequestIntent(
                 ActiveRequestIntent{.function = ActiveFunction::WriteSingleRegister,
                                     .unitId = 1,
                                     .timeout = ms{1000},
                                     .payload = WriteSingleRegisterIntent{
                                         .registerAddress = 0xFFFF, .value = 0xFFFF}}),
             std::nullopt);
}

void ActiveRequestTest::ac02_fc03DescriptorGoldenWire()
{
    const auto result = encodeActiveRequest(readIntent());
    const auto descriptor = as<ActiveRequestDescriptor>(result);
    QVERIFY(descriptor.has_value());
    QCOMPARE(descriptor->wire, kFc03Wire);
    QCOMPARE(descriptor->frame.address, std::uint8_t{0x01});
    QCOMPARE(descriptor->frame.functionCode, std::uint8_t{0x03});
    const std::vector<std::uint8_t> expectedData = {0x00, 0x00, 0x00, 0x02};
    QCOMPARE(descriptor->frame.data, expectedData);
    // The descriptor is the whole send-time snapshot: intent embedded.
    QCOMPARE(descriptor->intent, readIntent());
}

void ActiveRequestTest::ac03_writeEncodeContract()
{
    // M10-D1 changes the 0x06 half of this contract on purpose: the encoder
    // now exists (its golden bytes and independent CRC oracle live in the
    // write_encoder suite). 0x10 stays unsupported, and the session still
    // refuses to BEGIN an active 0x06 transaction, so encoder != capability.
    const auto single = encodeActiveRequest(ActiveRequestIntent{
        .function = ActiveFunction::WriteSingleRegister,
        .unitId = 1,
        .timeout = ms{1000},
        .payload = WriteSingleRegisterIntent{.registerAddress = 0x000A,
                                             .value = 0x0064}});
    const auto* singleDescriptor = std::get_if<ActiveRequestDescriptor>(&single);
    QVERIFY(singleDescriptor != nullptr);
    QCOMPARE(singleDescriptor->frame.functionCode, std::uint8_t{0x06});
    QCOMPARE(singleDescriptor->wire.size(), std::size_t{8});
    const std::vector<std::uint8_t> expectedData = {0x00, 0x0A, 0x00, 0x64};
    QCOMPARE(singleDescriptor->frame.data, expectedData);

    // M10-E1 INTENTIONAL TRANSITION: the 0x10 request encoder now exists, so
    // this contract changed from "returns UnsupportedFunction" to "returns a
    // descriptor". An encoder is still NOT active support — the session gate
    // (activeFunctionSupported) refuses 0x10 until M10-E2, so nothing here
    // can be sent. See T022 §ZE for the recorded transition.
    const ActiveRequestEncodeResult multiple = encodeActiveRequest(
        ActiveRequestIntent{
            .function = ActiveFunction::WriteMultipleRegisters,
            .unitId = 1,
            .timeout = ms{1000},
            .payload = WriteMultipleRegistersIntent{
                .startAddress = 0x0010,
                .values = std::vector<std::uint16_t>{0x0001, 0x0002}}});
    const auto* multipleDescriptor =
        std::get_if<ActiveRequestDescriptor>(&multiple);
    QVERIFY(multipleDescriptor != nullptr);
    QCOMPARE(multipleDescriptor->frame.functionCode, std::uint8_t{0x10});
    // start(2) + quantity(2) + byteCount(1) + values(2*2)
    QCOMPARE(multipleDescriptor->frame.data.size(), std::size_t{9});
    QCOMPARE(multipleDescriptor->wire.size(), std::size_t{13});
}

void ActiveRequestTest::ac04_beginAcceptsDescriptorAndSnapshots()
{
    SerialTransactionSession session;
    const auto encoded = encodeActiveRequest(readIntent());
    const auto descriptor = std::get<ActiveRequestDescriptor>(encoded);

    const auto start = session.beginActiveRequest(descriptor);
    const auto accepted = as<ActiveRequestDescriptor>(start);
    QVERIFY(accepted.has_value());
    QCOMPARE(*accepted, descriptor);
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);
    // The session retains the send-time snapshot verbatim.
    QVERIFY(session.pendingRequest().has_value());
    QCOMPARE(*session.pendingRequest(), descriptor);
}

void ActiveRequestTest::ac05_inconsistentDescriptorRejected()
{
    SerialTransactionSession session;

    // Wire bytes that do NOT encode the declared frame: refuse before any
    // send — the transport may only write provably-intent-derived bytes.
    auto tampered = std::get<ActiveRequestDescriptor>(encodeActiveRequest(readIntent()));
    tampered.wire[2] ^= 0x01;
    const auto start = session.beginActiveRequest(tampered);
    const auto error = as<SerialTransactionError>(start);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, SerialTransactionErrorCode::InvalidRequestDescriptor);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
    QVERIFY(!session.pendingRequest().has_value());

    // Frame/address disagreement with the intent is the same refusal.
    auto wrongFrame = std::get<ActiveRequestDescriptor>(encodeActiveRequest(readIntent()));
    wrongFrame.frame.address = 0x02;
    const auto start2 = session.beginActiveRequest(wrongFrame);
    QCOMPARE(as<SerialTransactionError>(start2)->code,
             SerialTransactionErrorCode::InvalidRequestDescriptor);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void ActiveRequestTest::ac06_writeDescriptorRejectedBeforeSend()
{
    SerialTransactionSession session;

    // M10-D2 changed this contract on purpose: the 0x06 descriptor is now
    // ACCEPTED (the framing rule, the shared echo analyzer and the session
    // tests exist). M10-E2 extended the same change to 0x10: both write
    // functions are now accepted by the session, and the "refused before any
    // send" negative has no session-level carrier left — the remaining
    // negatives live above the session (Controller capability guard,
    // write10Supported absence, production UI absence).
    const ActiveRequestDescriptor writeDescriptor{
        .intent = ActiveRequestIntent{
            .function = ActiveFunction::WriteSingleRegister,
            .unitId = 0x01,
            .timeout = ms{1000},
            .payload = WriteSingleRegisterIntent{.registerAddress = 0x000A,
                                                 .value = 0x0064}},
        .frame = ModbusRtuFrame{
            .address = 0x01, .functionCode = 0x06,
            .data = {0x00, 0x0A, 0x00, 0x64}},
        .wire = std::vector<std::uint8_t>{},
    };
    const auto accepted = session.beginActiveRequest(
        ActiveRequestDescriptor{.intent = writeDescriptor.intent,
                                .frame = writeDescriptor.frame,
                                .wire = encodeRtuFrame(writeDescriptor.frame)});
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&accepted) != nullptr);
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);
    session.cancel();
    QCOMPARE(session.state(), SerialTransactionState::Idle);

    // 0x10: M10-E2 wired the shared active analyzer, so the session now
    // ACCEPTS a 0x10 descriptor too. The "refused before any send" negative
    // has no session-level carrier left (every ActiveFunction is supported);
    // the remaining negatives live above the session — the Controller
    // capability guard and the production oracle. The descriptor is built the
    // canonical way (production encoder + codec), because the session refuses
    // a descriptor whose wire does not decode back to its frame.
    const auto multipleFrame =
        encodeWriteMultipleRegistersRequest(0x01, 0x000A, {0x0064});
    const ActiveRequestDescriptor multipleDescriptor{
        .intent = ActiveRequestIntent{
            .function = ActiveFunction::WriteMultipleRegisters,
            .unitId = 0x01,
            .timeout = ms{1000},
            .payload = WriteMultipleRegistersIntent{
                .startAddress = 0x000A, .values = {0x0064}}},
        .frame = multipleFrame,
        .wire = encodeRtuFrame(multipleFrame),
    };
    const auto started = session.beginActiveRequest(multipleDescriptor);
    QVERIFY(std::get_if<ActiveRequestDescriptor>(&started) != nullptr);
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);
    session.cancel();
    QCOMPARE(session.state(), SerialTransactionState::Idle);
    QVERIFY(!session.pendingRequest().has_value()); // nothing snapshotted
}

void ActiveRequestTest::ac07_pendingSnapshotSurvivesRefusedSecondBegin()
{
    SerialTransactionSession session;
    const auto first = std::get<ActiveRequestDescriptor>(
        encodeActiveRequest(readIntent(0x11, 0x0064, 0x0002)));
    QVERIFY(as<ActiveRequestDescriptor>(session.beginActiveRequest(first)).has_value());

    // A second, DIFFERENT request while pending must be refused whole, and
    // the send-time snapshot must stay exactly the first request — this is
    // the pending-intent authority the response will be matched against.
    const auto second = std::get<ActiveRequestDescriptor>(
        encodeActiveRequest(readIntent(0x22, 0x0100, 0x0001)));
    const auto refused = session.beginActiveRequest(second);
    QCOMPARE(as<SerialTransactionError>(refused)->code,
             SerialTransactionErrorCode::Busy);
    QVERIFY(session.pendingRequest().has_value());
    QCOMPARE(*session.pendingRequest(), first);
    QCOMPARE(session.pendingRequest()->intent.unitId, std::uint8_t{0x11});
}

void ActiveRequestTest::ac08_genericPathFc03Equivalence()
{
    SerialTransactionSession session;
    const auto descriptor =
        std::get<ActiveRequestDescriptor>(encodeActiveRequest(readIntent()));
    QVERIFY(as<ActiveRequestDescriptor>(session.beginActiveRequest(descriptor))
                .has_value());

    const auto result = session.feedResponseBytes(kGoodResponse9, ms{25});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
    // Completed: the snapshot is consumed with the transaction.
    QVERIFY(!session.pendingRequest().has_value());
}

void ActiveRequestTest::ac09_genericPathTimeout()
{
    SerialTransactionSession session;
    const auto descriptor =
        std::get<ActiveRequestDescriptor>(encodeActiveRequest(readIntent()));
    QVERIFY(as<ActiveRequestDescriptor>(session.beginActiveRequest(descriptor))
                .has_value());

    const auto result = session.onResponseTimeout(ms{1000});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Timeout);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void ActiveRequestTest::ac10_resultEvidenceAndDispositionTokens()
{
    const auto descriptor =
        std::get<ActiveRequestDescriptor>(encodeActiveRequest(readIntent()));
    const auto response =
        wire({0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7B}); // bad CRC
    const ActiveTransactionResult result{
        .request = descriptor,
        .responseAdu = response,
        .disposition = TransportDisposition::PossiblySent,
        .analysis = TransactionAnalysis{.status = TransactionStatus::CrcError,
                                        .elapsed = ms{25},
                                        .exceptionCode = std::nullopt,
                                        .issue = std::nullopt,
                                        .values = {}},
    };

    // Evidence is a pure projection of the result: exact request ADU (CRC
    // included), exact observed response bytes, the transport disposition.
    const ActiveTransactionEvidence evidence = result.evidence();
    QCOMPARE(evidence.requestAdu, kFc03Wire);
    QCOMPARE(evidence.responseAdu, response);
    QCOMPARE(evidence.disposition, TransportDisposition::PossiblySent);

    QCOMPARE(modbuslens::core::transportDispositionName(
                 TransportDisposition::NotSent),
             std::string_view{"not_sent"});
    QCOMPARE(modbuslens::core::transportDispositionName(
                 TransportDisposition::PossiblySent),
             std::string_view{"possibly_sent"});

    const modbuslens::core::ActiveTransactionRecord record{
        .sessionId = 7,
        .request = descriptor,
        .evidence = evidence,
        .analysis = result.analysis,
    };
    QCOMPARE(record.unitId(), std::uint8_t{0x01});
    QCOMPARE(record.functionCode(), std::uint8_t{0x03});
}

void ActiveRequestTest::ac11_sourceAndFunctionTokens()
{
    QCOMPARE(activeFunctionCode(ActiveFunction::ReadHoldingRegisters),
             std::uint8_t{0x03});
    QCOMPARE(activeFunctionCode(ActiveFunction::WriteSingleRegister),
             std::uint8_t{0x06});
    // Naming discipline: 0x10 IS decimal 16 (Write Multiple Registers).
    QCOMPARE(activeFunctionCode(ActiveFunction::WriteMultipleRegisters),
             std::uint8_t{0x10});
    QCOMPARE(activeFunctionName(ActiveFunction::WriteMultipleRegisters),
             std::string_view{"write_multiple_registers"});

    using modbuslens::core::TransactionSourceKind;
    QCOMPARE(modbuslens::core::transactionSourceKindName(
                 TransactionSourceKind::Simulator),
             std::string_view{"simulator"});
    QCOMPARE(modbuslens::core::transactionSourceKindName(
                 TransactionSourceKind::Replay),
             std::string_view{"replay"});
    QCOMPARE(modbuslens::core::transactionSourceKindName(
                 TransactionSourceKind::ActiveSerial),
             std::string_view{"active_serial"});
}

void ActiveRequestTest::ac12_fc06EchoMismatchRegression()
{
    // Frozen passive semantics (M10-A must not change them): a structurally
    // legal FC06 reply whose echo differs -> ProtocolError +
    // WriteSingleRegisterEchoMismatch (outcome and issue as separate facts).
    const ModbusRtuFrame request{
        .address = 0x01, .functionCode = 0x06, .data = {0x00, 0x0A, 0x00, 0x64}};
    const ModbusRtuFrame wrongEcho{
        .address = 0x01, .functionCode = 0x06, .data = {0x00, 0x0A, 0x00, 0x65}};
    const auto result = modbuslens::core::analyzeObservedTransaction(
        request, modbuslens::core::ResponseObservation{wrongEcho}, ms{19}, ms{1000});
    const auto analyzed =
        std::get_if<modbuslens::core::AnalyzedObservedTransaction>(&result);
    QVERIFY(analyzed != nullptr);
    QCOMPARE(analyzed->analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(analyzed->analysis.issue.has_value());
    QCOMPARE(analyzed->analysis.issue->code,
             TransactionIssueCode::WriteSingleRegisterEchoMismatch);
    QCOMPARE(*analyzed->analysis.issue->expectedRegisterValue, std::uint16_t{0x0064});
    QCOMPARE(*analyzed->analysis.issue->actualRegisterValue, std::uint16_t{0x0065});
}

void ActiveRequestTest::ac13_fc10EchoMismatchRegression()
{
    // 0x10 (decimal 16) reply shaped right but fields differ -> ProtocolError
    // + WriteMultipleRegistersEchoMismatch with expected/actual facts.
    const ModbusRtuFrame request{
        .address = 0x01,
        .functionCode = 0x10,
        .data = {0x00, 0x10, 0x00, 0x02, 0x04, 0x00, 0x01, 0x00, 0x02}};
    const ModbusRtuFrame reply{
        .address = 0x01, .functionCode = 0x10, .data = {0x00, 0x11, 0x00, 0x02}};
    const auto result = modbuslens::core::analyzeObservedTransaction(
        request, modbuslens::core::ResponseObservation{reply}, ms{10}, ms{1000});
    const auto analyzed =
        std::get_if<modbuslens::core::AnalyzedObservedTransaction>(&result);
    QVERIFY(analyzed != nullptr);
    QCOMPARE(analyzed->analysis.status, TransactionStatus::ProtocolError);
    QVERIFY(analyzed->analysis.issue.has_value());
    QCOMPARE(analyzed->analysis.issue->code,
             TransactionIssueCode::WriteMultipleRegistersEchoMismatch);
    QCOMPARE(*analyzed->analysis.issue->expectedRegisterAddress, std::uint16_t{0x0010});
    QCOMPARE(*analyzed->analysis.issue->actualRegisterAddress, std::uint16_t{0x0011});
}

void ActiveRequestTest::ac14_outcomeIssueOrthogonalityRegression()
{
    // Orthogonality stays frozen: a MATCHING reply to a request that carries
    // its own semantic defects is still Success — request issues never
    // rewrite the outcome (forbidding `if issue -> ProtocolError`).
    // Fixture mirrors the T015 orthogonality case: 0x10 with byteCount=2 but
    // a 4-byte payload (two request-side issues) answered by the start/
    // quantity-matching reply.
    const ModbusRtuFrame request{
        .address = 0x01,
        .functionCode = 0x10,
        .data = {0x00, 0x10, 0x00, 0x02, 0x02, 0x00, 0x01, 0x00, 0x02}};
    const ModbusRtuFrame reply{
        .address = 0x01, .functionCode = 0x10, .data = {0x00, 0x10, 0x00, 0x02}};
    const auto result = modbuslens::core::analyzeObservedTransaction(
        request, modbuslens::core::ResponseObservation{reply}, ms{12}, ms{1000});
    const auto analyzed =
        std::get_if<modbuslens::core::AnalyzedObservedTransaction>(&result);
    QVERIFY(analyzed != nullptr);
    QCOMPARE(analyzed->analysis.status, TransactionStatus::Success);
    QVERIFY(!analyzed->requestIssues.empty());
}

void ActiveRequestTest::ac15_broadcastUnitNeverSentActively()
{
    // Active broadcast is REJECTED in v1 at validation — it must not reach
    // any transport (the passive ENR semantics stay untouched, covered by
    // the T015 suite).
    SerialTransactionSession session;
    const auto broadcast = ActiveRequestIntent{
        .function = ActiveFunction::ReadHoldingRegisters,
        .unitId = 0x00,
        .timeout = ms{1000},
        .payload = ReadHoldingRegistersIntent{.startAddress = 0, .quantity = 2},
    };
    const auto encoded = encodeActiveRequest(broadcast);
    const auto error = as<ActiveRequestEncodeError>(encoded);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, ActiveRequestEncodeErrorCode::IntentInvalid);

    const ActiveRequestDescriptor descriptor{
        .intent = broadcast,
        .frame = ModbusRtuFrame{.address = 0x00, .functionCode = 0x03, .data = {}},
        .wire = kFc03Wire,
    };
    const auto start = session.beginActiveRequest(descriptor);
    QCOMPARE(as<SerialTransactionError>(start)->code,
             SerialTransactionErrorCode::InvalidAddress);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

QTEST_GUILESS_MAIN(ActiveRequestTest)
#include "test_active_request.moc"