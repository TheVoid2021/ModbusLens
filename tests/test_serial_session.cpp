#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/serial/SerialTransactionSession.h"

using modbuslens::core::AwaitingMoreData;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::SerialRequestStart;
using modbuslens::core::SerialTransactionError;
using modbuslens::core::SerialTransactionErrorCode;
using modbuslens::core::SerialTransactionSession;
using modbuslens::core::SerialTransactionState;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionStatus;
using modbuslens::core::encodeRtuFrame;

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

// FC03 request 01 03 00 00 00 02 with its verified CRC (T009 golden).
const auto kRequestWire = wire({0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B});
const auto kGoodResponse9 = wire({0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7A});
const auto kBadCrcResponse9 = wire({0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7B});
const auto kException5 = wire({0x01, 0x83, 0x02, 0xC0, 0xF1});

std::vector<std::uint8_t> encodeFrame(std::uint8_t address, std::uint8_t function,
                                     std::vector<std::uint8_t> data)
{
    return encodeRtuFrame(ModbusRtuFrame{
        .address = address, .functionCode = function, .data = std::move(data)});
}

} // namespace

class SerialSessionTest : public QObject
{
    Q_OBJECT

private slots:
    // --- FC03 request encoder (T010 addition, Pure Core) ---
    void enc01_validRequest();
    void enc02_invalidQuantity();
    void enc03_wireMatchesCodec();

    // --- SERIAL-A matrix ---
    void a01_startFc03();
    void a02_normalOneChunk();
    void a03_splitResponse();
    void a04_exception();
    void a05_crcError();
    void a06_noResponseTimeout();
    void a07_partialResponseTimeout();
    void a08_busy();
    void a09_resetAfterCompletion();
    void a10_cancel();
    void a11_wrongAddress();
    void a12_invalidQuantity();
    void a13_invalidAddress();
    void a14_oversizedBuffer();
    void a15_wrongExceptionFunction();
    void a16_wrongByteCount();
};

// ---- encoder ----

void SerialSessionTest::enc01_validRequest()
{
    const auto result = modbuslens::core::encodeReadHoldingRegistersRequest(0x01, 0x0064, 0x0001);
    const auto frame = as<ModbusRtuFrame>(result);
    QVERIFY(frame.has_value());
    QCOMPARE(frame->address, std::uint8_t{0x01});
    QCOMPARE(frame->functionCode, std::uint8_t{0x03});
    const std::vector<std::uint8_t> expectedData = {0x00, 0x64, 0x00, 0x01};
    QCOMPARE(frame->data, expectedData);
}

void SerialSessionTest::enc02_invalidQuantity()
{
    for (const std::uint16_t quantity : {std::uint16_t{0}, std::uint16_t{126}}) {
        const auto result =
            modbuslens::core::encodeReadHoldingRegistersRequest(0x01, 0x0000, quantity);
        QVERIFY(as<ModbusRtuFrame>(result).has_value() == false);
        const auto error = as<modbuslens::core::Function03EncodeError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, modbuslens::core::Function03EncodeErrorCode::InvalidQuantity);
    }
}

void SerialSessionTest::enc03_wireMatchesCodec()
{
    const auto result = modbuslens::core::encodeReadHoldingRegistersRequest(0x01, 0x0000, 0x0002);
    const auto frame = as<ModbusRtuFrame>(result);
    QVERIFY(frame.has_value());
    QCOMPARE(encodeRtuFrame(*frame), kRequestWire);
}

// ---- SERIAL-A ----

void SerialSessionTest::a01_startFc03()
{
    SerialTransactionSession session;
    const auto result = session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
    const auto start = as<SerialRequestStart>(result);
    QVERIFY(start.has_value());
    QCOMPARE(start->requestWire, kRequestWire);
    QCOMPARE(start->requestFrame.functionCode, std::uint8_t{0x03});
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);
}

void SerialSessionTest::a02_normalOneChunk()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
    const auto result = session.feedResponseBytes(kGoodResponse9, ms{25});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
    QCOMPARE(analysis->elapsed, ms{25});
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void SerialSessionTest::a03_splitResponse()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});

    const auto r1 = session.feedResponseBytes(wire({0x01, 0x03}), ms{5});
    QVERIFY(as<AwaitingMoreData>(r1).has_value());

    const auto r2 = session.feedResponseBytes(wire({0x04, 0x00, 0x64}), ms{12});
    QVERIFY(as<AwaitingMoreData>(r2).has_value());

    const auto r3 = session.feedResponseBytes(wire({0x00, 0xC8, 0xBA, 0x7A}), ms{25});
    const auto analysis = as<TransactionAnalysis>(r3);
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void SerialSessionTest::a04_exception()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
    const auto result = session.feedResponseBytes(kException5, ms{18});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Exception);
    QVERIFY(analysis->exceptionCode.has_value());
    QCOMPARE(*analysis->exceptionCode, std::uint8_t{0x02});
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void SerialSessionTest::a05_crcError()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
    const auto result = session.feedResponseBytes(kBadCrcResponse9, ms{17});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::CrcError);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void SerialSessionTest::a06_noResponseTimeout()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
    const auto result = session.onResponseTimeout(ms{1000});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Timeout);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void SerialSessionTest::a07_partialResponseTimeout()
{
    // Scenario 1: 4 bytes. NOTE: 4 is exactly the minimum legal RTU frame
    // shape (addr+fn+crc), so the codec interprets 01 03 | 04 00 as a frame
    // with CRC 04 00 -> CrcMismatch -> CrcError. This is the REAL
    // wire-truth: partial bytes at timeout are a diagnosis, never Timeout.
    {
        SerialTransactionSession session;
        session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
        const auto r1 = session.feedResponseBytes(wire({0x01, 0x03, 0x04, 0x00}), ms{400});
        QVERIFY(as<AwaitingMoreData>(r1).has_value());

        const auto result = session.onResponseTimeout(ms{1000});
        const auto analysis = as<TransactionAnalysis>(result);
        QVERIFY(analysis.has_value());
        QCOMPARE(analysis->status, TransactionStatus::CrcError);
        QCOMPARE(session.state(), SerialTransactionState::Idle);
    }
    // Scenario 2: 2 bytes (below the minimum frame) -> FrameTooShort ->
    // ProtocolError. Locks the same principle from the other end.
    {
        SerialTransactionSession session;
        session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
        const auto r1 = session.feedResponseBytes(wire({0x01, 0x03}), ms{400});
        QVERIFY(as<AwaitingMoreData>(r1).has_value());

        const auto result = session.onResponseTimeout(ms{1000});
        const auto analysis = as<TransactionAnalysis>(result);
        QVERIFY(analysis.has_value());
        QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
        QCOMPARE(session.state(), SerialTransactionState::Idle);
    }
}

void SerialSessionTest::a08_busy()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});

    const auto second = session.beginReadHoldingRegisters(0x01, 0x0000, 0x0001, ms{1000});
    const auto error = as<SerialTransactionError>(second);
    QVERIFY(error.has_value());
    QCOMPARE(error->code, SerialTransactionErrorCode::Busy);

    // First transaction fully intact: still AwaitingResponse and completable.
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);
    const auto result = session.feedResponseBytes(kGoodResponse9, ms{25});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

void SerialSessionTest::a09_resetAfterCompletion()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
    session.feedResponseBytes(kGoodResponse9, ms{25});
    QCOMPARE(session.state(), SerialTransactionState::Idle);

    // Second transaction starts cleanly — no leftover buffer pollution.
    const auto second = session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
    const auto start = as<SerialRequestStart>(second);
    QVERIFY(start.has_value());
    QCOMPARE(start->requestWire, kRequestWire);
}

void SerialSessionTest::a10_cancel()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
    session.feedResponseBytes(wire({0x01, 0x03, 0x04}), ms{100});
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);

    session.cancel();
    QCOMPARE(session.state(), SerialTransactionState::Idle);

    // Fresh transaction unaffected by the cancelled garbage prefix.
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});
    const auto result = session.feedResponseBytes(kGoodResponse9, ms{25});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    QCOMPARE(analysis->status, TransactionStatus::Success);
}

void SerialSessionTest::a11_wrongAddress()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});

    // Valid-CRC response from device 2 (codec-built, no hardcoded CRC KAT).
    const auto foreign = encodeFrame(0x02, 0x03, {0x04, 0x00, 0x64, 0x00, 0xC8});
    QCOMPARE(foreign.size(), std::size_t{9});

    const auto result = session.feedResponseBytes(foreign, ms{30});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    // Serial framing completed the candidate; T007 owns the mismatch verdict.
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
}

void SerialSessionTest::a12_invalidQuantity()
{
    for (const std::uint16_t quantity : {std::uint16_t{0}, std::uint16_t{126}}) {
        SerialTransactionSession session;
        const auto result =
            session.beginReadHoldingRegisters(0x01, 0x0000, quantity, ms{1000});
        const auto error = as<SerialTransactionError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, SerialTransactionErrorCode::InvalidQuantity);
        QCOMPARE(session.state(), SerialTransactionState::Idle);
    }
}

void SerialSessionTest::a13_invalidAddress()
{
    for (const std::uint8_t address : {std::uint8_t{0}, std::uint8_t{248}}) {
        SerialTransactionSession session;
        const auto result =
            session.beginReadHoldingRegisters(address, 0x0000, 0x0002, ms{1000});
        const auto error = as<SerialTransactionError>(result);
        QVERIFY(error.has_value());
        QCOMPARE(error->code, SerialTransactionErrorCode::InvalidAddress);
        QCOMPARE(session.state(), SerialTransactionState::Idle);
    }
}

void SerialSessionTest::a14_oversizedBuffer()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});

    // One chunk of 10 bytes: 9-byte candidate + 1 trailing byte. Oversized
    // buffers are never truncated into a Success — wait for the timeout.
    auto oversized = kGoodResponse9;
    oversized.push_back(0xFF);
    const auto r1 = session.feedResponseBytes(oversized, ms{30});
    QVERIFY(as<AwaitingMoreData>(r1).has_value());
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);

    const auto result = session.onResponseTimeout(ms{1000});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    // Entire 10-byte buffer -> CrcMismatch (not a truncated Success).
    QCOMPARE(analysis->status, TransactionStatus::CrcError);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void SerialSessionTest::a15_wrongExceptionFunction()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});

    // 0x84 = exception reply to 0x04. Framing must NOT hardcode 0x83: any
    // function with bit7 set is an exception-FORMAT 5-byte candidate.
    const auto foreign = encodeFrame(0x01, 0x84, {0x02});
    QCOMPARE(foreign.size(), std::size_t{5});

    // Candidate completes at 5 bytes immediately (not at the timeout).
    QCOMPARE(session.state(), SerialTransactionState::AwaitingResponse);
    const auto result = session.feedResponseBytes(foreign, ms{20});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    // T007: 0x84 is not the 0x03 exception reply -> ProtocolError.
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

void SerialSessionTest::a16_wrongByteCount()
{
    SerialTransactionSession session;
    session.beginReadHoldingRegisters(0x01, 0x0000, 0x0002, ms{1000});

    // byteCount=2 with only ONE register (7 wire bytes, valid CRC).
    const auto mismatched = encodeFrame(0x01, 0x03, {0x02, 0x00, 0x64});
    QCOMPARE(mismatched.size(), std::size_t{7});

    // Candidate length comes from RESPONSE byteCount (5+2=7), not from the
    // request quantity (which would demand 9) — completes immediately.
    const auto result = session.feedResponseBytes(mismatched, ms{20});
    const auto analysis = as<TransactionAnalysis>(result);
    QVERIFY(analysis.has_value());
    // T007 semantic analysis: request quantity 2 vs response values 1.
    QCOMPARE(analysis->status, TransactionStatus::ProtocolError);
    QCOMPARE(session.state(), SerialTransactionState::Idle);
}

QTEST_GUILESS_MAIN(SerialSessionTest)
#include "test_serial_session.moc"