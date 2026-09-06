#include <QtTest>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "core/protocol/Function03.h"

using modbuslens::core::Function03DecodeError;
using modbuslens::core::Function03DecodeErrorCode;
using modbuslens::core::ModbusExceptionResponse;
using modbuslens::core::ModbusRtuFrame;
using modbuslens::core::ReadHoldingRegistersRequest;
using modbuslens::core::ReadHoldingRegistersResponse;
using modbuslens::core::decodeReadHoldingRegistersException;
using modbuslens::core::decodeReadHoldingRegistersRequest;
using modbuslens::core::decodeReadHoldingRegistersResponse;

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

ModbusRtuFrame requestFrame(std::vector<std::uint8_t> data)
{
    return ModbusRtuFrame{.address = 0x01, .functionCode = 0x03, .data = std::move(data)};
}

class Function03Test : public QObject
{
    Q_OBJECT

private slots:
    // -- Request ------------------------------------------------------------
    void b01_decodeValidRequest();          // P0
    void b02_decodeOfficialRequestExample();// P0: V1.1b3 section 6.3 sample
    void b03_invalidRequestLength();        // P0
    void b04_invalidQuantity();             // P0: 0 and 126 both rejected
    void b05_wrongFunctionCode();           // P1
    // -- Normal response ----------------------------------------------------
    void b06_decodeOneRegisterResponse();   // P0
    void b07_decodeTwoRegisterResponse();   // P0
    void b08_decodeOfficialResponseExample();// P0: V1.1b3 section 6.3 sample
    void b09_byteCountMismatch();           // P0
    void b10a_oddByteCount();               // P0: byteCount = 3
    void b10b_zeroByteCount();              // P0: byteCount = 0 (illegal per
                                            // quantity 1..125 -> byteCount >= 2)
    // -- Exception ----------------------------------------------------------
    void b11_validExceptionShape();         // P0
    void b12_invalidExceptionLength();      // P1
};

void Function03Test::b01_decodeValidRequest()
{
    const auto request =
        as<ReadHoldingRegistersRequest>(decodeReadHoldingRegistersRequest(requestFrame({0x00, 0x00, 0x00, 0x01})));
    QVERIFY(request.has_value());
    QCOMPARE(request->startAddress, std::uint16_t{0});
    QCOMPARE(request->quantity, std::uint16_t{1});
}

void Function03Test::b02_decodeOfficialRequestExample()
{
    const auto request =
        as<ReadHoldingRegistersRequest>(decodeReadHoldingRegistersRequest(requestFrame({0x00, 0x6B, 0x00, 0x03})));
    QVERIFY(request.has_value());
    QCOMPARE(request->startAddress, std::uint16_t{107});
    QCOMPARE(request->quantity, std::uint16_t{3});
}

void Function03Test::b03_invalidRequestLength()
{
    const auto emptyError =
        as<Function03DecodeError>(decodeReadHoldingRegistersRequest(requestFrame({})));
    QVERIFY(emptyError.has_value());
    QCOMPARE(emptyError->code, Function03DecodeErrorCode::InvalidRequestLength);

    const auto shortError =
        as<Function03DecodeError>(decodeReadHoldingRegistersRequest(requestFrame({0x00, 0x00, 0x00})));
    QVERIFY(shortError.has_value());
    QCOMPARE(shortError->code, Function03DecodeErrorCode::InvalidRequestLength);
}

void Function03Test::b04_invalidQuantity()
{
    const auto zero =
        as<Function03DecodeError>(decodeReadHoldingRegistersRequest(requestFrame({0x00, 0x00, 0x00, 0x00})));
    QVERIFY(zero.has_value());
    QCOMPARE(zero->code, Function03DecodeErrorCode::InvalidQuantity);

    const auto over =
        as<Function03DecodeError>(decodeReadHoldingRegistersRequest(requestFrame({0x00, 0x00, 0x00, 0x7E})));
    QVERIFY(over.has_value());
    QCOMPARE(over->code, Function03DecodeErrorCode::InvalidQuantity);
}

void Function03Test::b05_wrongFunctionCode()
{
    const ModbusRtuFrame wrongFunction{
        .address = 0x01, .functionCode = 0x04, .data = {0x00, 0x00, 0x00, 0x01}};
    const auto error = as<Function03DecodeError>(decodeReadHoldingRegistersRequest(wrongFunction));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, Function03DecodeErrorCode::WrongFunctionCode);
}

void Function03Test::b06_decodeOneRegisterResponse()
{
    const ModbusRtuFrame frame{.address = 0x01, .functionCode = 0x03, .data = {0x02, 0x00, 0x64}};
    const auto response = as<ReadHoldingRegistersResponse>(decodeReadHoldingRegistersResponse(frame));
    QVERIFY(response.has_value());
    const ReadHoldingRegistersResponse expected{.values = {100}};
    QCOMPARE(*response, expected);
}

void Function03Test::b07_decodeTwoRegisterResponse()
{
    const ModbusRtuFrame frame{
        .address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64, 0x00, 0xC8}};
    const auto response = as<ReadHoldingRegistersResponse>(decodeReadHoldingRegistersResponse(frame));
    QVERIFY(response.has_value());
    const ReadHoldingRegistersResponse expected{.values = {100, 200}};
    QCOMPARE(*response, expected);
}

void Function03Test::b08_decodeOfficialResponseExample()
{
    // V1.1b3 section 6.3 response sample: byteCount=6, registers 022B/0000/0064.
    const ModbusRtuFrame frame{
        .address = 0x11,
        .functionCode = 0x03,
        .data = {0x06, 0x02, 0x2B, 0x00, 0x00, 0x00, 0x64},
    };
    const auto response = as<ReadHoldingRegistersResponse>(decodeReadHoldingRegistersResponse(frame));
    QVERIFY(response.has_value());
    const ReadHoldingRegistersResponse expected{.values = {555, 0, 100}};
    QCOMPARE(*response, expected);
}

void Function03Test::b09_byteCountMismatch()
{
    // byteCount claims 4 register bytes, only 2 follow.
    const ModbusRtuFrame frame{.address = 0x01, .functionCode = 0x03, .data = {0x04, 0x00, 0x64}};
    const auto error = as<Function03DecodeError>(decodeReadHoldingRegistersResponse(frame));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, Function03DecodeErrorCode::InvalidByteCount);
}

void Function03Test::b10a_oddByteCount()
{
    // byteCount = 3: a register is 2 bytes, odd counts are always invalid.
    const ModbusRtuFrame frame{
        .address = 0x01, .functionCode = 0x03, .data = {0x03, 0x00, 0x64, 0x01}};
    const auto error = as<Function03DecodeError>(decodeReadHoldingRegistersResponse(frame));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, Function03DecodeErrorCode::InvalidByteCount);
}

void Function03Test::b10b_zeroByteCount()
{
    // byteCount = 0: quantity 1..125 means a normal response has at least one
    // register, so byteCount must be >= 2 — zero is rejected here, not
    // deferred to transaction analysis.
    const ModbusRtuFrame frame{.address = 0x01, .functionCode = 0x03, .data = {0x00}};
    const auto error = as<Function03DecodeError>(decodeReadHoldingRegistersResponse(frame));
    QVERIFY(error.has_value());
    QCOMPARE(error->code, Function03DecodeErrorCode::InvalidByteCount);
}

void Function03Test::b11_validExceptionShape()
{
    const ModbusRtuFrame frame{.address = 0x01, .functionCode = 0x83, .data = {0x02}};
    const auto exception = as<ModbusExceptionResponse>(decodeReadHoldingRegistersException(frame));
    QVERIFY(exception.has_value());
    QCOMPARE(exception->exceptionCode, std::uint8_t{0x02});
}

void Function03Test::b12_invalidExceptionLength()
{
    const ModbusRtuFrame empty{.address = 0x01, .functionCode = 0x83, .data = {}};
    const auto emptyError = as<Function03DecodeError>(decodeReadHoldingRegistersException(empty));
    QVERIFY(emptyError.has_value());
    QCOMPARE(emptyError->code, Function03DecodeErrorCode::InvalidExceptionLength);

    const ModbusRtuFrame twoBytes{.address = 0x01, .functionCode = 0x83, .data = {0x02, 0x03}};
    const auto longError = as<Function03DecodeError>(decodeReadHoldingRegistersException(twoBytes));
    QVERIFY(longError.has_value());
    QCOMPARE(longError->code, Function03DecodeErrorCode::InvalidExceptionLength);
}

} // namespace

QTEST_MAIN(Function03Test)
#include "test_function03.moc"