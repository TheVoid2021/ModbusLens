#include <QtTest>

#include <chrono>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "core/protocol/ModbusRtuCodec.h"
#include "core/simulator/SimulationFault.h"

using modbuslens::core::DeliveredWire;
using modbuslens::core::DroppedResponse;
using modbuslens::core::RtuDecodeError;
using modbuslens::core::RtuDecodeErrorCode;
using modbuslens::core::SimulationFaultConfig;
using modbuslens::core::SimulationFaultMode;
using modbuslens::core::SimulatedDelivery;
using modbuslens::core::applySimulationFault;
using modbuslens::core::decodeRtuFrame;

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

const std::vector<std::uint8_t> kValidResponseWire{
    0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7A};

class SimulationFaultTest : public QObject
{
    Q_OBJECT

private slots:
    // FAULT-T01 (P0): Normal preserves the wire byte-for-byte, delay = 0.
    void t01_normalPreservesWire();
    // FAULT-T02 (P0): DropResponse — and the variant must NOT be a
    // DeliveredWire (dropping is not delivering an empty packet).
    void t02_dropResponse();
    // FAULT-T03 (P0): CorruptCrc — payload untouched, last CRC byte flipped,
    // decode then reports CrcMismatch.
    void t03_corruptCrc();
    // FAULT-T04 (P0): ArtificialDelay — metadata only, no real waiting.
    void t04_artificialDelay();
    // FAULT-T05 (P1): determinism (twice, identical) + mode isolation
    // (delay field only belongs to the ArtificialDelay mode).
    void t05_determinismAndModeIsolation();
};

void SimulationFaultTest::t01_normalPreservesWire()
{
    const auto result = applySimulationFault(
        kValidResponseWire, SimulationFaultConfig{.mode = SimulationFaultMode::None});

    const auto delivered = as<DeliveredWire>(result);
    QVERIFY(delivered.has_value());
    QCOMPARE(delivered->bytes, kValidResponseWire);
    QCOMPARE(delivered->artificialDelay, std::chrono::milliseconds{0});
}

void SimulationFaultTest::t02_dropResponse()
{
    const auto result = applySimulationFault(
        kValidResponseWire, SimulationFaultConfig{.mode = SimulationFaultMode::DropResponse});

    const auto dropped = as<DroppedResponse>(result);
    QVERIFY(dropped.has_value());

    // Dropping must not look like "delivering a 0-byte packet".
    const auto delivered = as<DeliveredWire>(result);
    QVERIFY(!delivered.has_value());
}

void SimulationFaultTest::t03_corruptCrc()
{
    const auto result = applySimulationFault(
        kValidResponseWire, SimulationFaultConfig{.mode = SimulationFaultMode::CorruptCrc});

    const auto delivered = as<DeliveredWire>(result);
    QVERIFY(delivered.has_value());

    const std::vector<std::uint8_t> expected{
        0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8, 0xBA, 0x7B};
    QCOMPARE(delivered->bytes, expected);

    // Payload (everything before the two CRC bytes) must be untouched.
    const std::vector<std::uint8_t> expectedPayload{
        0x01, 0x03, 0x04, 0x00, 0x64, 0x00, 0xC8};
    const std::vector<std::uint8_t> actualPayload(
        delivered->bytes.begin(), delivered->bytes.begin() + 7);
    QCOMPARE(actualPayload, expectedPayload);
    QCOMPARE(delivered->artificialDelay, std::chrono::milliseconds{0});

    // The corrupted wire must be rejected by the T004A decoder.
    const auto decodeResult = decodeRtuFrame(delivered->bytes);
    const auto decodeError = as<RtuDecodeError>(decodeResult);
    QVERIFY(decodeError.has_value());
    QCOMPARE(decodeError->code, RtuDecodeErrorCode::CrcMismatch);
}

void SimulationFaultTest::t04_artificialDelay()
{
    const auto result = applySimulationFault(
        kValidResponseWire,
        SimulationFaultConfig{
            .mode = SimulationFaultMode::ArtificialDelay,
            .artificialDelay = std::chrono::milliseconds{500}});

    const auto delivered = as<DeliveredWire>(result);
    QVERIFY(delivered.has_value());
    QCOMPARE(delivered->bytes, kValidResponseWire);
    QCOMPARE(delivered->artificialDelay, std::chrono::milliseconds{500});
    // No real waiting: the suite runtime stays in milliseconds — the
    // implementation contains no sleep/timer/thread (see T006 task doc).
}

void SimulationFaultTest::t05_determinismAndModeIsolation()
{
    const SimulationFaultConfig corruptConfig{
        .mode = SimulationFaultMode::CorruptCrc};

    // Same input + config, twice: results must be identical (no randomness).
    const auto first = applySimulationFault(kValidResponseWire, corruptConfig);
    const auto second = applySimulationFault(kValidResponseWire, corruptConfig);
    QCOMPARE(first, second);

    // And both must really end in ...BA 7B, not just share the variant type.
    const auto firstWire = as<DeliveredWire>(first);
    QVERIFY(firstWire.has_value());
    QCOMPARE(firstWire->bytes.back(), std::uint8_t{0x7B});
    const auto secondWire = as<DeliveredWire>(second);
    QVERIFY(secondWire.has_value());
    QCOMPARE(secondWire->bytes.back(), std::uint8_t{0x7B});

    // Mode isolation: delay metadata only belongs to ArtificialDelay —
    // CorruptCrc and None must always report 0ms even if the config carries
    // a delay value (no combined faults in v1).
    const auto corruptWithDelay = as<DeliveredWire>(applySimulationFault(
        kValidResponseWire,
        SimulationFaultConfig{
            .mode = SimulationFaultMode::CorruptCrc,
            .artificialDelay = std::chrono::milliseconds{500}}));
    QVERIFY(corruptWithDelay.has_value());
    QCOMPARE(corruptWithDelay->artificialDelay, std::chrono::milliseconds{0});

    const auto noneWithDelay = as<DeliveredWire>(applySimulationFault(
        kValidResponseWire,
        SimulationFaultConfig{
            .mode = SimulationFaultMode::None,
            .artificialDelay = std::chrono::milliseconds{500}}));
    QVERIFY(noneWithDelay.has_value());
    QCOMPARE(noneWithDelay->artificialDelay, std::chrono::milliseconds{0});
}

} // namespace

QTEST_MAIN(SimulationFaultTest)
#include "test_simulation_fault.moc"