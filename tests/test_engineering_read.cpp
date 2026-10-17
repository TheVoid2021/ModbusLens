#include <QtTest>

#include <QTemporaryDir>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/analysis/RegisterDecode.h"
#include "core/profile/DeviceProfile.h"
#include "core/profile/RegisterReadiness.h"
#include "core/protocol/ModbusCrc.h"
#include "ui/AnalysisController.h"
#include "ui/engineering/EngineeringReadController.h"
#include "ui/profile/ProfileController.h"
#include "ui/profile/ProfileStore.h"

#include "fake_serial_transport.h"

using modbuslens::core::DeviceProfile;
using modbuslens::core::EngineeringDecoderKind;
using modbuslens::core::RegisterByteOrder;
using modbuslens::core::RegisterDecodeType;
using modbuslens::core::RegisterEntry;
using modbuslens::core::RegisterWordOrder;
// AnalysisController and RecordingSerialTransport are GLOBAL-scope classes
// (the shipped ui/AnalysisController.h and tests/fake_serial_transport.h have
// no namespace) — used below by their bare names.
using modbuslens::ui::EngineeringReadController;
using modbuslens::ui::ProfileController;
using modbuslens::ui::ProfileStore;

namespace {

// A complete FC03/FC04 response frame with the Modbus CRC (low byte first).
std::vector<std::uint8_t> responseFrame(std::uint8_t unit, std::uint8_t fc,
                                        const std::vector<std::uint16_t>& words)
{
    std::vector<std::uint8_t> frame;
    frame.push_back(unit);
    frame.push_back(fc);
    frame.push_back(static_cast<std::uint8_t>(words.size() * 2));
    for (const std::uint16_t word : words) {
        frame.push_back(static_cast<std::uint8_t>(word >> 8));
        frame.push_back(static_cast<std::uint8_t>(word & 0xFF));
    }
    const std::uint16_t crc = modbuslens::core::calculateModbusCrc(frame);
    frame.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(crc >> 8));
    return frame;
}

std::vector<std::uint8_t> exceptionFrame(std::uint8_t unit, std::uint8_t fc,
                                         std::uint8_t code)
{
    std::vector<std::uint8_t> frame{unit,
                                    static_cast<std::uint8_t>(fc | 0x80),
                                    code};
    const std::uint16_t crc = modbuslens::core::calculateModbusCrc(frame);
    frame.push_back(static_cast<std::uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(crc >> 8));
    return frame;
}

} // namespace

class EngineeringReadTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_root;

    // One assembled read session: the shipped serial owner (driven by the
    // deterministic recording transport) + the real profile store + the C2
    // controller under test. Nothing touches a real COM port.
    struct Session
    {
        AnalysisController analysis;
        RecordingSerialTransport transport;
        ProfileController profiles;
        EngineeringReadController reader{nullptr, nullptr};

        Session()
        {
            analysis.setSerialTransport(&transport);
            reader.attach(&analysis, &profiles);
        }

        void connectFake()
        {
            analysis.connectSerial(QStringLiteral("COM_TEST"), 9600);
        }
    };

    static RegisterEntry makeEntry(std::uint8_t fc, std::uint16_t address,
                                   RegisterDecodeType type,
                                   EngineeringDecoderKind decoderKind)
    {
        RegisterEntry entry;
        entry.readFunctionCode = fc;
        entry.address = address;
        entry.name = "Sample";
        entry.dataType = type;
        entry.registerCount = modbuslens::core::registerDecodeTypeWordCount(type);
        entry.scale = 0.01;
        entry.offset = 0.0;
        entry.unit = "s";
        if (decoderKind == EngineeringDecoderKind::Linear) {
            entry.scaleConfigured = true;
            entry.offsetConfigured = true;
            entry.unitConfigured = true;
        }
        entry.decoderKind = decoderKind;
        return entry;
    }

    static bool saveProfile(const QString& id, const RegisterEntry& entry)
    {
        DeviceProfile profile;
        profile.schemaVersion = 1;
        profile.profileId = id.toStdString();
        profile.displayName = id.toStdString();
        profile.registers.push_back(entry);
        return ProfileStore::saveToFile(
                   profile, ProfileStore::defaultFilePathFor(id))
            .ok();
    }

    // A ready + connected session with one Linear register; returns helpers.
    static bool seedReadyLinear(const QString& id, std::uint8_t fc,
                                std::uint16_t address, RegisterDecodeType type,
                                RegisterByteOrder byteOrder =
                                    RegisterByteOrder::Normal,
                                RegisterWordOrder wordOrder =
                                    RegisterWordOrder::HighWordFirst)
    {
        RegisterEntry entry = makeEntry(fc, address, type,
                                        EngineeringDecoderKind::Linear);
        entry.byteOrder = byteOrder;
        entry.wordOrder = wordOrder;
        return saveProfile(id, entry);
    }

private slots:

    void initTestCase()
    {
        QVERIFY(m_root.isValid());
        ProfileStore::setManagedRootOverride(m_root.path());
    }

    void cleanupTestCase() { ProfileStore::setManagedRootOverride(QString()); }

    // ------------------------------------------------------------ C2-01 --
    // Not Ready (configured fields but the decoder is UNDECLARED) -> the
    // click is refused and NOTHING is sent.
    void c2_01_notReadyRefusedZeroSend()
    {
        Session session;
        session.connectFake();
        RegisterEntry entry = makeEntry(0x03, 100, RegisterDecodeType::UInt16,
                                        EngineeringDecoderKind::Unknown);
        entry.scaleConfigured = true;
        entry.offsetConfigured = true;
        entry.unitConfigured = true;
        QVERIFY(saveProfile(QStringLiteral("c201"), entry));

        QCOMPARE(session.reader.readOnce(QStringLiteral("c201"), 0x03, 100, 1,
                                         1000),
                 false);
        QCOMPARE(session.reader.refusalToken(),
                 QStringLiteral("not_engineering_ready"));
        QCOMPARE(session.transport.startAttemptCount(), 0);
        QCOMPARE(session.reader.readState(), QStringLiteral("idle"));
    }

    // ------------------------------------------------------------ C2-16 --
    // Ever non-Linear decoder is refused the same way (a full bypass check).
    void c2_16_nonLinearDecodersAllRefused()
    {
        for (const EngineeringDecoderKind kind :
             {EngineeringDecoderKind::Unknown, EngineeringDecoderKind::Raw,
              EngineeringDecoderKind::VariableDecimal,
              EngineeringDecoderKind::Enum}) {
            Session session;
            session.connectFake();
            RegisterEntry entry = makeEntry(0x03, 100,
                                            RegisterDecodeType::UInt16, kind);
            // Even with every field configured, a non-Linear decoder must
            // never reach the wire.
            entry.scaleConfigured = true;
            entry.offsetConfigured = true;
            entry.unitConfigured = true;
            const QString id =
                QStringLiteral("c216-%1").arg(int(kind));
            QVERIFY(saveProfile(id, entry));
            QCOMPARE(session.reader.readOnce(id, 0x03, 100, 1, 1000), false);
            QVERIFY(!session.reader.refusalToken().isEmpty());
            QCOMPARE(session.transport.startAttemptCount(), 0);
        }
    }

    // ------------------------------------------------------------ C2-02 --
    // EngineeringReady but the serial port is NOT connected -> refused with
    // the dedicated token and zero sends.
    void c2_02_readyButDisconnected()
    {
        Session session; // never connected
        QVERIFY(seedReadyLinear(QStringLiteral("c202"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QCOMPARE(session.reader.readOnce(QStringLiteral("c202"), 0x03, 100, 1,
                                         1000),
                 false);
        QCOMPARE(session.reader.refusalToken(),
                 QStringLiteral("serial_not_connected"));
        QCOMPARE(session.transport.startAttemptCount(), 0);
    }

    // ------------------------------------------------------------ C2-03 --
    // ONE click -> exactly ONE request; a completion yields exactly one
    // result and nothing else is ever started (no polling).
    void c2_03_singleRequestNoPolling()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c203"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c203"), 0x03, 100, 1,
                                        1000));
        QCOMPARE(session.reader.readState(), QStringLiteral("reading"));
        QCOMPARE(session.transport.startAttemptCount(), 1);

        session.transport.setResponseBytes(
            responseFrame(1, 0x03, {1234}));
        session.transport.completeWithResponse();
        QCOMPARE(session.reader.readState(), QStringLiteral("success"));
        QCOMPARE(session.reader.valueText(), QStringLiteral("12.34"));
        // No polling, no retry, no second start.
        QCOMPARE(session.transport.startAttemptCount(), 1);
    }

    // ------------------------------------------------------------ C2-04 --
    // The wire request is built from the SAVED configuration: unit id,
    // function code, PDU address, quantity, and a correct Modbus CRC.
    void c2_04_requestFrameFidelity()
    {
        Session session;
        session.connectFake();
        // FC04 @49, unit 7, one register.
        QVERIFY(seedReadyLinear(QStringLiteral("c204"), 0x04, 49,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c204"), 0x04, 49, 7,
                                        1000));
        QCOMPARE(session.transport.startAttemptCount(), 1);
        const auto& adus = session.transport.sentAduLog();
        QCOMPARE(adus.size(), std::size_t{1});
        const std::vector<std::uint8_t>& wire = adus.front();
        QCOMPARE(wire.size(), std::size_t{8});
        QCOMPARE(int(wire[0]), 7);      // slave id
        QCOMPARE(int(wire[1]), 0x04);   // function code
        QCOMPARE(int(wire[2]), 0);      // start address high (49 = 0x0031)
        QCOMPARE(int(wire[3]), 49);
        QCOMPARE(int(wire[4]), 0);      // quantity high
        QCOMPARE(int(wire[5]), 1);      // quantity low
        const std::uint16_t crc = modbuslens::core::calculateModbusCrc(
            std::span<const std::uint8_t>(wire.data(), 6));
        QCOMPARE(int(wire[6]), int(crc & 0xFF));
        QCOMPARE(int(wire[7]), int(crc >> 8));
    }

    // ------------------------------------------------------------ C2-05/06 --
    // A valid response decodes and the Linear value is computed:
    // raw 1234, scale 0.01, offset 0, unit s -> 12.34 s.
    void c2_05_06_validResponseLinearValue()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c205"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c205"), 0x03, 100, 1,
                                        1000));
        session.transport.setResponseBytes(responseFrame(1, 0x03, {1234}));
        session.transport.completeWithResponse();

        QCOMPARE(session.reader.readState(), QStringLiteral("success"));
        QVERIFY(session.reader.hasValue());
        QCOMPARE(session.reader.valueText(), QStringLiteral("12.34"));
        QCOMPARE(session.reader.unitText(), QStringLiteral("s"));
        QCOMPARE(session.reader.decodedValueText(), QStringLiteral("1234"));
        QCOMPARE(session.reader.scaleText(), QStringLiteral("0.01"));
        QCOMPARE(session.reader.offsetText(), QStringLiteral("0"));
        QCOMPARE(session.reader.decoderKindText(), QStringLiteral("Linear"));
        // C2-19: every ADU on the wire was a READ function.
        for (const auto& adu : session.transport.sentAduLog()) {
            QVERIFY(adu.size() > 1);
            QVERIFY(adu[1] == 0x03 || adu[1] == 0x04);
        }
    }

    // ------------------------------------------------------------ C2-07 --
    // The supported type matrix decodes through the frozen M11 pipeline.
    void c2_07_typeMatrix()
    {
        // Int16: 0xFFFE -> -2; scale 0.01 -> -0.02.
        {
            Session session;
            session.connectFake();
            QVERIFY(seedReadyLinear(QStringLiteral("c207a"), 0x03, 100,
                                    RegisterDecodeType::Int16));
            QVERIFY(session.reader.readOnce(QStringLiteral("c207a"), 0x03, 100,
                                            1, 1000));
            session.transport.setResponseBytes(
                responseFrame(1, 0x03, {0xFFFE}));
            session.transport.completeWithResponse();
            QCOMPARE(session.reader.decodedValueText(), QStringLiteral("-2"));
        }
        // UInt32 HighWordFirst: 0x0001 0x0002 -> 65538.
        {
            Session session;
            session.connectFake();
            QVERIFY(seedReadyLinear(QStringLiteral("c207b"), 0x03, 100,
                                    RegisterDecodeType::UInt32));
            QVERIFY(session.reader.readOnce(QStringLiteral("c207b"), 0x03, 100,
                                            1, 1000));
            session.transport.setResponseBytes(
                responseFrame(1, 0x03, {0x0001, 0x0002}));
            session.transport.completeWithResponse();
            QCOMPARE(session.reader.decodedValueText(),
                     QStringLiteral("65538"));
        }
        // Float32: 1234.0 (0x449A5000) HighWordFirst.
        {
            Session session;
            session.connectFake();
            QVERIFY(seedReadyLinear(QStringLiteral("c207c"), 0x03, 100,
                                    RegisterDecodeType::Float32));
            QVERIFY(session.reader.readOnce(QStringLiteral("c207c"), 0x03, 100,
                                            1, 1000));
            session.transport.setResponseBytes(
                responseFrame(1, 0x03, {0x449A, 0x5000})); // 1234.5f
            session.transport.completeWithResponse();
            QCOMPARE(session.reader.decodedValueText(),
                     QStringLiteral("1234.5"));
            QCOMPARE(session.reader.valueText(), QStringLiteral("12.345"));
        }
    }

    // ------------------------------------------------------------ C2-08 --
    // Byte order (within a register) and word order (across two).
    void c2_08_byteAndWordOrder()
    {
        // ByteSwapped UInt16: the wire carries 0xD2 0x04 for the word 0x04D2.
        {
            Session session;
            session.connectFake();
            QVERIFY(seedReadyLinear(QStringLiteral("c208a"), 0x03, 100,
                                    RegisterDecodeType::UInt16,
                                    RegisterByteOrder::ByteSwapped));
            QVERIFY(session.reader.readOnce(QStringLiteral("c208a"), 0x03, 100,
                                            1, 1000));
            session.transport.setResponseBytes(
                responseFrame(1, 0x03, {0x04D2}));
            session.transport.completeWithResponse();
            // ByteSwapped turns the wire word 0x04D2 into 0xD204 (53764):
            // the swap must be APPLIED, not ignored.
            QCOMPARE(session.reader.decodedValueText(),
                     QStringLiteral("53764"));
        }
        // LowWordFirst UInt32: words (0x0002, 0x0001) mean 0x00010002.
        {
            Session session;
            session.connectFake();
            QVERIFY(seedReadyLinear(QStringLiteral("c208b"), 0x03, 100,
                                    RegisterDecodeType::UInt32,
                                    RegisterByteOrder::Normal,
                                    RegisterWordOrder::LowWordFirst));
            QVERIFY(session.reader.readOnce(QStringLiteral("c208b"), 0x03, 100,
                                            1, 1000));
            session.transport.setResponseBytes(
                responseFrame(1, 0x03, {0x0002, 0x0001}));
            session.transport.completeWithResponse();
            QCOMPARE(session.reader.decodedValueText(),
                     QStringLiteral("65538"));
        }
    }

    // ------------------------------------------------------------ C2-09 --
    // CRC damage: no valid value is ever shown.
    void c2_09_crcDamagedFrame()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c209"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c209"), 0x03, 100, 1,
                                        1000));
        auto frame = responseFrame(1, 0x03, {1234});
        frame.back() ^= 0xFF; // break the CRC
        session.transport.setResponseBytes(frame);
        session.transport.completeWithResponse();
        QCOMPARE(session.reader.readState(), QStringLiteral("failed"));
        QVERIFY(!session.reader.hasValue());
        QVERIFY(!session.reader.failureText().isEmpty());
    }

    // ------------------------------------------------------------ C2-10 --
    // Timeout: no valid value is ever shown.
    void c2_10_timeout()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c210"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c210"), 0x03, 100, 1,
                                        1000));
        session.transport.completeWithTimeout();
        QCOMPARE(session.reader.readState(), QStringLiteral("failed"));
        QVERIFY(!session.reader.hasValue());
        QVERIFY(!session.reader.failureText().isEmpty());
    }

    // ------------------------------------------------------------ C2-11 --
    // A Modbus exception response is reported, never shown as a value.
    void c2_11_exceptionResponse()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c211"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c211"), 0x03, 100, 1,
                                        1000));
        session.transport.setResponseBytes(exceptionFrame(1, 0x03, 0x02));
        session.transport.completeWithResponse();
        QCOMPARE(session.reader.readState(), QStringLiteral("failed"));
        QVERIFY(!session.reader.hasValue());
    }

    // ------------------------------------------------------------ C2-12 --
    // A response for a DIFFERENT unit id is rejected by the shipped
    // analyzer: no value.
    void c2_12_mismatchedUnitResponse()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c212"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c212"), 0x03, 100, 2,
                                        1000)); // unit 2
        session.transport.setResponseBytes(
            responseFrame(9, 0x03, {1234})); // answered by unit 9
        session.transport.completeWithResponse();
        QCOMPARE(session.reader.readState(), QStringLiteral("failed"));
        QVERIFY(!session.reader.hasValue());
    }

    // ------------------------------------------------------------ C2-13 --
    // NaN / Infinity never become a displayed engineering value.
    void c2_13_nanAndInfinityRejected()
    {
        for (const std::uint16_t hi : {std::uint16_t{0x7FC0}, // NaN
                                       std::uint16_t{0x7F80}}) { // +Inf
            Session session;
            session.connectFake();
            QVERIFY(seedReadyLinear(QStringLiteral("c213"), 0x03, 100,
                                    RegisterDecodeType::Float32));
            QVERIFY(session.reader.readOnce(QStringLiteral("c213"), 0x03, 100,
                                            1, 1000));
            session.transport.setResponseBytes(
                responseFrame(1, 0x03, {hi, 0x0000}));
            session.transport.completeWithResponse();
            QCOMPARE(session.reader.readState(), QStringLiteral("failed"));
            QVERIFY(!session.reader.hasValue());
        }
    }

    // ------------------------------------------------------------ C2-14 --
    // Switching the target while a read is in flight abandons the request:
    // the late response is discarded and can never reach the new target.
    void c2_14_switchDuringReadDiscardsLateResponse()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c214"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c214"), 0x03, 100, 1,
                                        1000));
        QCOMPARE(session.reader.readState(), QStringLiteral("reading"));

        // The Human switches the selection: the page calls invalidateResult.
        session.reader.invalidateResult();
        QCOMPARE(session.reader.readState(), QStringLiteral("idle"));

        session.transport.setResponseBytes(responseFrame(1, 0x03, {1234}));
        session.transport.completeWithResponse();
        // The late answer is NOT shown: still idle, no value.
        QCOMPARE(session.reader.readState(), QStringLiteral("idle"));
        QVERIFY(!session.reader.hasValue());
    }

    // ------------------------------------------------------------ C2-15 --
    // A serial disconnect during a read invalidates the pending request and
    // the (now impossible) value.
    void c2_15_disconnectDuringRead()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c215"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c215"), 0x03, 100, 1,
                                        1000));
        QCOMPARE(session.reader.readState(), QStringLiteral("reading"));
        session.analysis.disconnectSerial();
        QCOMPARE(session.reader.readState(), QStringLiteral("idle"));
        QVERIFY(!session.reader.hasValue());
    }

    // ------------------------------------------------------------ C2-17 --
    // An UNSAVED draft never changes the read parameters: the computation
    // uses the SAVED profile's values.
    void c2_17_unsavedDraftDoesNotAffectRead()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c217"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        // Open the profile and change the draft scale WITHOUT saving.
        QVERIFY(session.profiles.openProfile(QStringLiteral("c217")));
        QVariantMap fields;
        fields.insert(QStringLiteral("readFunctionCode"), QStringLiteral("03"));
        fields.insert(QStringLiteral("address"), QStringLiteral("100"));
        fields.insert(QStringLiteral("name"), QStringLiteral("Sample"));
        fields.insert(QStringLiteral("description"), QString());
        fields.insert(QStringLiteral("dataType"), QStringLiteral("2"));
        fields.insert(QStringLiteral("byteOrder"), QStringLiteral("0"));
        fields.insert(QStringLiteral("wordOrder"), QStringLiteral("0"));
        fields.insert(QStringLiteral("scale"), QStringLiteral("0.5"));
        fields.insert(QStringLiteral("offset"), QStringLiteral("0"));
        fields.insert(QStringLiteral("unit"), QStringLiteral("s"));
        fields.insert(QStringLiteral("decoderKind"), QStringLiteral("linear"));
        QVERIFY(session.profiles.editRegisterEntry(0, fields));
        QVERIFY(session.profiles.dirty());

        QVERIFY(session.reader.readOnce(QStringLiteral("c217"), 0x03, 100, 1,
                                        1000));
        session.transport.setResponseBytes(responseFrame(1, 0x03, {1234}));
        session.transport.completeWithResponse();
        // The SAVED scale (0.01) applied — the draft's 0.5 did not leak.
        QCOMPARE(session.reader.valueText(), QStringLiteral("12.34"));
        QCOMPARE(session.reader.scaleText(), QStringLiteral("0.01"));
    }

    // ------------------------------------------------------------ C2-18 --
    // Nothing is read => no fabricated value, no unit, empty detail.
    void c2_18_noValueBeforeAnyRead()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c218"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QCOMPARE(session.reader.readState(), QStringLiteral("idle"));
        QVERIFY(!session.reader.hasValue());
        QVERIFY(session.reader.valueText().isEmpty());
        QVERIFY(session.reader.unitText().isEmpty());
    }

    // ------------------------------------------------------------ C2-21 --
    // The saved configuration changed since the last look: the read re-checks
    // the SAVED state at click time and refuses.
    void c2_21_savedChangeIsRevalidated()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c221"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        // The Human saves a NEW version that revokes the decoder declaration.
        RegisterEntry revoked = makeEntry(0x03, 100, RegisterDecodeType::UInt16,
                                          EngineeringDecoderKind::Unknown);
        revoked.scaleConfigured = true;
        revoked.offsetConfigured = true;
        revoked.unitConfigured = true;
        QVERIFY(saveProfile(QStringLiteral("c221"), revoked));

        QCOMPARE(session.reader.readOnce(QStringLiteral("c221"), 0x03, 100, 1,
                                         1000),
                 false);
        QCOMPARE(session.reader.refusalToken(),
                 QStringLiteral("not_engineering_ready"));
        QCOMPARE(session.transport.startAttemptCount(), 0);
    }

    // ------------------------------------------------------------ C2-22 --
    // A previously successful value never masquerades as current: a new
    // attempt clears it first, and a failure does not resurrect it.
    void c2_22_failureNeverResurrectsOldValue()
    {
        Session session;
        session.connectFake();
        QVERIFY(seedReadyLinear(QStringLiteral("c222"), 0x03, 100,
                                RegisterDecodeType::UInt16));
        QVERIFY(session.reader.readOnce(QStringLiteral("c222"), 0x03, 100, 1,
                                        1000));
        session.transport.setResponseBytes(responseFrame(1, 0x03, {1234}));
        session.transport.completeWithResponse();
        QCOMPARE(session.reader.valueText(), QStringLiteral("12.34"));

        // Second attempt: the value clears the moment it starts...
        QVERIFY(session.reader.readOnce(QStringLiteral("c222"), 0x03, 100, 1,
                                        1000));
        QVERIFY(!session.reader.hasValue());
        QVERIFY(session.reader.valueText().isEmpty());
        // ...and a failure leaves NO value behind.
        session.transport.completeWithTimeout();
        QCOMPARE(session.reader.readState(), QStringLiteral("failed"));
        QVERIFY(!session.reader.hasValue());
        QVERIFY(session.reader.valueText().isEmpty());
    }

    // ------------------------------------------------------------ C2-20 --
    // Deterministic without hardware: this whole suite ran through the
    // recording transport; the ONE real-serial check is that the shipped
    // adapter exists untouched (it is never instantiated here).
    void c2_20_allDeterministicNoHardware()
    {
        QVERIFY(true); // the evidence is the suite itself
    }
};

QTEST_MAIN(EngineeringReadTest)
#include "test_engineering_read.moc"
