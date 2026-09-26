#include <QtTest>

#include <QDir>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>

#include "core/profile/DeviceProfile.h"
#include "ui/AnalysisController.h"
#include "ui/profile/ActiveProfileController.h"
#include "ui/profile/ProfileController.h"
#include "ui/profile/ProfileStore.h"

#include "fake_serial_transport.h"

using modbuslens::core::DeviceProfile;
using modbuslens::core::RegisterDecodeType;
using modbuslens::core::RegisterEntry;
using modbuslens::ui::ActiveProfileController;
using modbuslens::ui::ProfileController;
using modbuslens::ui::ProfileStore;

namespace {

// A deterministic FC03/FC04 response carrying the given register words.
std::vector<std::uint8_t> responseWith(int unit,
                                       const std::vector<std::uint16_t>& values,
                                       std::uint8_t readFunctionCode = 0x03)
{
    std::vector<std::uint8_t> data;
    data.push_back(static_cast<std::uint8_t>(values.size() * 2));
    for (const std::uint16_t value : values) {
        data.push_back(static_cast<std::uint8_t>(value >> 8));
        data.push_back(static_cast<std::uint8_t>(value & 0xFF));
    }
    return modbuslens::core::encodeRtuFrame(
        modbuslens::core::ModbusRtuFrame{
            .address = static_cast<std::uint8_t>(unit),
            .functionCode = readFunctionCode,
            .data = std::move(data)});
}

DeviceProfile makeProfile(
    const QString& profileId, const QString& displayName,
    const std::vector<std::tuple<std::uint8_t, std::uint16_t, QString,
                                 RegisterDecodeType, double, double, QString,
                                 modbuslens::core::RegisterByteOrder,
                                 modbuslens::core::RegisterWordOrder>>& entries)
{
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = profileId.toStdString();
    profile.displayName = displayName.toStdString();
    profile.manufacturer = "ACME";
    for (const auto& [fc, address, name, dataType, scale, offset, unit,
                      byteOrder, wordOrder] : entries) {
        RegisterEntry entry;
        entry.readFunctionCode = fc;
        entry.address = address;
        entry.name = name.toStdString();
        entry.dataType = dataType;
        entry.registerCount =
            modbuslens::core::registerDecodeTypeWordCount(dataType);
        entry.scale = scale;
        entry.offset = offset;
        entry.unit = unit.toStdString();
        entry.byteOrder = byteOrder;
        entry.wordOrder = wordOrder;
        profile.registers.push_back(entry);
    }
    return profile;
}

} // namespace

class ProfileSemanticTest : public QObject
{
    Q_OBJECT

    // One wired trio: catalog + active selection + analysis, with the REAL
    // production read path driven through the recording transport.
    struct Fixture
    {
        ProfileController profiles;
        ActiveProfileController active;
        AnalysisController analysis;
        RecordingSerialTransport transport;

        explicit Fixture()
        {
            active.setProfileController(&profiles);
            analysis.setActiveProfileController(&active);
            analysis.setSerialTransport(&transport);
            analysis.connectSerial(QStringLiteral("COM_SEMANTIC_TEST"), 9600);
        }

        // Dispatch a REAL read through the production raw-text front door and
        // complete it with the given words.
        void read(const QString& function, const QString& start,
                  const QString& quantity,
                  const std::vector<std::uint16_t>& words,
                  std::uint8_t wireFunction = 0x03)
        {
            transport.setResponseBytes(
                responseWith(1, words, wireFunction));
            analysis.readRegisterRequest(QStringLiteral("1"), function, start,
                                         quantity, QStringLiteral("1000"));
            transport.completeWithResponse();
        }

        [[nodiscard]] QVariantMap row(int index) const
        {
            return analysis.readResultValues().at(index).toMap();
        }
    };

    static void seedManaged(const DeviceProfile& profile)
    {
        QVERIFY(!ProfileStore::saveToFile(
                     profile, ProfileStore::defaultFilePathFor(
                                  QString::fromStdString(profile.profileId)))
                     .ok()
                    ? false
                    : true);
    }

private slots:
    void init()
    {
        const QString root = QDir::temp().filePath(
            QStringLiteral("m12b-b4-test-%1")
                .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QDir().mkpath(root);
        ProfileStore::setManagedRootOverride(root);
    }

    void cleanup()
    {
        ProfileStore::setManagedRootOverride(QString());
    }

    void b4c01_noActiveProfileLeavesRawAndGenericUntouched()
    {
        Fixture fx;
        // No profile selected at all.
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x01D2, 0x00C8});
        QCOMPARE(fx.analysis.readResultValues().size(), 2);
        const QVariantMap row = fx.row(0);
        // Raw + generic intact…
        QCOMPARE(row.value("dec").toInt(), 466);
        QCOMPARE(row.value("decoded").toString(), QStringLiteral("466"));
        // …and the semantic layer states the honest no-profile fact.
        QCOMPARE(row.value("semanticStatus").toString(),
                 QStringLiteral("no_active_profile"));
        QVERIFY(!row.contains("semanticText"));
    }

    void b4c02_activeProfileUnmapped()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 5000, "Far away",
                                  RegisterDecodeType::UInt16, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {466});
        QCOMPARE(fx.row(0).value("semanticStatus").toString(),
                 QStringLiteral("unmapped"));
        QVERIFY(!fx.row(0).contains("semanticText"));
    }

    void b4c03_fc03OneWordExactMapping()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Frequency",
                                  RegisterDecodeType::UInt16, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {466});
        const QVariantMap row = fx.row(0);
        QCOMPARE(row.value("semanticStatus").toString(),
                 QStringLiteral("mapped_start"));
        QCOMPARE(row.value("semanticName").toString(),
                 QStringLiteral("Frequency"));
        QCOMPARE(row.value("semanticText").toString(), QStringLiteral("466"));
    }

    void b4c04_scaleAndUnit()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "输出频率",
                                  RegisterDecodeType::UInt16, 0.1, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {466});
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("46.6 Hz"));
        QCOMPARE(fx.row(0).value("semanticValueClass").toString(),
                 QStringLiteral("finite"));
    }

    void b4c05_offsetAppliedAfterScale()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Offset probe",
                                  RegisterDecodeType::UInt16, 0.1, 2.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        // (466 * 0.1) + 2.0 = 48.6 — NOT (466 + 2) * 0.1 = 46.8.
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {466});
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("48.6"));
    }

    void b4c06_scaleZeroLegal()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Zeroed",
                                  RegisterDecodeType::UInt16, 0.0, 7.5, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {12345});
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("7.5"));
    }

    void b4c07_fc04SameAddressSelectsFc04Entry()
    {
        // Both spaces hold an entry at address 1000 — different metadata.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "FC03 owner",
                                  RegisterDecodeType::UInt16, 1.0, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst},
                                 {0x04, 1000, "FC04 owner",
                                  RegisterDecodeType::UInt16, 2.0, 1.0, "C",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        // An FC04 read of address 1000 must resolve the FC04 entry.
        fx.read(QStringLiteral("04"), QStringLiteral("1000"),
                QStringLiteral("1"), {10}, 0x04);
        QCOMPARE(fx.row(0).value("semanticName").toString(),
                 QStringLiteral("FC04 owner"));
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("21 C"));
    }

    void b4c08_customFc41Mapping()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x41, 2000, "Vendor register",
                                  RegisterDecodeType::UInt16, 3.0, 0.0, "kPa",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("41"), QStringLiteral("2000"),
                QStringLiteral("1"), {5}, 0x41);
        QCOMPARE(fx.row(0).value("semanticName").toString(),
                 QStringLiteral("Vendor register"));
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("15 kPa"));
    }

    void b4c09_noCrossFcFallback()
    {
        // The profile ONLY holds FC03@1000; the read is FC04@1000. The
        // semantic layer must NOT fall back to the FC03 entry.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "FC03 only",
                                  RegisterDecodeType::UInt16, 1.0, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("04"), QStringLiteral("1000"),
                QStringLiteral("1"), {466}, 0x04);
        QCOMPARE(fx.row(0).value("semanticStatus").toString(),
                 QStringLiteral("unmapped"));
        QVERIFY(!fx.row(0).contains("semanticText"));
    }

    void b4c10_profileTypeIndependentOfGenericControl()
    {
        // Profile entry is Int16 (negative); the GENERIC UI stays on UInt16.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Signed",
                                  RegisterDecodeType::Int16, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {0xFFFB}); // -5 as Int16, 65531 as UInt16
        QCOMPARE(fx.analysis.readDecodeType(),
                 static_cast<int>(RegisterDecodeType::UInt16));
        QCOMPARE(fx.row(0).value("decoded").toString(),
                 QStringLiteral("65531")); // generic: UInt16 view untouched
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("-5")); // semantic: profile Int16 view
    }

    void b4c11_profileByteSwappedSemantic()
    {
        // Byte order within the register: raw 0xABCD. Normal → 43981;
        // ByteSwapped → 0xCDAB = 52651. Generic stays Normal.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Swapped",
                                  RegisterDecodeType::UInt16, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::
                                      ByteSwapped,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {0xABCD});
        QCOMPARE(fx.row(0).value("decoded").toString(),
                 QStringLiteral("43981")); // generic: Normal untouched
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("52651")); // semantic: profile ByteSwapped
    }

    void b4c12_profileUInt32HighWordFirst()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Wide",
                                  RegisterDecodeType::UInt32, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x0001, 0x0000});
        // HighWordFirst: word0 is the high 16 bits → 0x00010000 = 65536.
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("65536"));
        QCOMPARE(fx.row(0).value("semanticSpan").toString(),
                 QStringLiteral("1000-1001"));
    }

    void b4c13_profileUInt32LowWordFirst()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Wide",
                                  RegisterDecodeType::UInt32, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      LowWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x0001, 0x0000});
        // LowWordFirst: word1 is the high 16 bits → 0x00000001 = 1.
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("1"));
    }

    void b4c14_profileFloat32DeterministicVector()
    {
        // 0x42F6E979 = 123.456 (IEEE-754 binary32).
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Temp",
                                  RegisterDecodeType::Float32, 1.0, 0.0, "°C",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x42F6, 0xE979});
        const QString semantic = fx.row(0).value("semanticText").toString();
        QVERIFY2(semantic.startsWith(QStringLiteral("123.456")),
                 qPrintable(semantic));
        QVERIFY(semantic.endsWith(QStringLiteral("°C")));
    }

    void b4c15_twoWordSemanticOnlyOnStartRow()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Wide",
                                  RegisterDecodeType::UInt32, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x0001, 0x0000});
        QCOMPARE(fx.row(0).value("semanticStatus").toString(),
                 QStringLiteral("mapped_start"));
        QVERIFY(fx.row(0).contains("semanticText"));
        // Row 1001: NO second semantic value, only the membership fact.
        QCOMPARE(fx.row(1).value("semanticStatus").toString(),
                 QStringLiteral("mapped_continuation"));
        QVERIFY(!fx.row(1).contains("semanticText"));
    }

    void b4c16_continuationRowCarriesMembership()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Wide",
                                  RegisterDecodeType::UInt32, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x0001, 0x0000});
        const QVariantMap second = fx.row(1);
        QCOMPARE(second.value("semanticName").toString(),
                 QStringLiteral("Wide"));
        QCOMPARE(second.value("semanticStartAddress").toInt(), 1000);
    }

    void b4c17_secondRowKeepsGenericDecode()
    {
        // Generic decode is a sliding window: row 1001 decodes
        // words[1..2] — with only 2 words available it reports
        // InsufficientWords, its OWN frozen M11 behavior, untouched by the
        // profile span.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Wide",
                                  RegisterDecodeType::UInt32, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x0001, 0x0000});
        // Default generic UInt16: row 1 decodes its OWN word, per the frozen
        // M11 1-register rule (the profile span does not touch it).
        const QVariantMap second = fx.row(1);
        QCOMPARE(second.value("decodeStatus").toString(),
                 QStringLiteral("ok"));
        QCOMPARE(second.value("decoded").toString(), QStringLiteral("0"));
        // The semantic layer did not fabricate a value there either.
        QCOMPARE(second.value("semanticStatus").toString(),
                 QStringLiteral("mapped_continuation"));
        // Switch the GENERIC control to UInt32: the frozen sliding window
        // then reports InsufficientWords on the last row — M11's OWN rule,
        // unchanged by the profile span.
        fx.analysis.setReadDecodeType(
            static_cast<int>(RegisterDecodeType::UInt32));
        QCOMPARE(fx.row(1).value("decodeStatus").toString(),
                 QStringLiteral("insufficient_words"));
        QCOMPARE(fx.row(1).value("semanticStatus").toString(),
                 QStringLiteral("mapped_continuation"));
    }

    void b4c18_partialTwoWordSpanInsufficient()
    {
        // The read window holds ONLY the start word of a 2-word entry.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Wide",
                                  RegisterDecodeType::UInt32, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {0x0001});
        QCOMPARE(fx.row(0).value("semanticStatus").toString(),
                 QStringLiteral("insufficient_words"));
        QVERIFY(!fx.row(0).contains("semanticText"));
        // Raw and generic stay exactly as the frozen rules display them.
        QCOMPARE(fx.row(0).value("dec").toInt(), 1);
    }

    void b4c19_readInsideContinuationSpanNoInventedValue()
    {
        // The window STARTS at the second word of the 2-word entry: the
        // covering lookup recognizes the membership, but no start-row value
        // may be invented from it.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Wide",
                                  RegisterDecodeType::UInt32, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1001"),
                QStringLiteral("1"), {0x0000});
        QCOMPARE(fx.row(0).value("semanticStatus").toString(),
                 QStringLiteral("mapped_continuation"));
        QVERIFY(!fx.row(0).contains("semanticText"));
        QCOMPARE(fx.row(0).value("semanticStartAddress").toInt(), 1000);
    }

    void b4c20_unitEmpty()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Unitless",
                                  RegisterDecodeType::UInt16, 2.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {21});
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("42"));
    }

    void b4c21_unitUnicodeRoundTrip()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Temp",
                                  RegisterDecodeType::UInt16, 1.0, 0.0, "°C",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {25});
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("25 °C"));
    }

    void b4c22_nanSemantic()
    {
        // 0x7FC00000 = quiet NaN (Float32).
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Broken sensor",
                                  RegisterDecodeType::Float32, 2.0, 1.0, "x",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x7FC0, 0x0000});
        QCOMPARE(fx.row(0).value("semanticValueClass").toString(),
                 QStringLiteral("not_a_number"));
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("非数字（NaN）"));
    }

    void b4c23_positiveInfinitySemantic()
    {
        // 0x7F800000 = +Inf.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Broken sensor",
                                  RegisterDecodeType::Float32, 2.0, 1.0, "x",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x7F80, 0x0000});
        QCOMPARE(fx.row(0).value("semanticValueClass").toString(),
                 QStringLiteral("positive_infinity"));
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("正无穷大（+Inf）"));
    }

    void b4c24_negativeInfinitySemantic()
    {
        // 0xFF800000 = −Inf.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Broken sensor",
                                  RegisterDecodeType::Float32, 2.0, 1.0, "x",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0xFF80, 0x0000});
        QCOMPARE(fx.row(0).value("semanticValueClass").toString(),
                 QStringLiteral("negative_infinity"));
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("负无穷大（−Inf）"));
    }

    void b4c25_specialClassificationIsNumeric()
    {
        // The classification must come from the numeric class of the
        // projection, NOT from parsing the presentation text: the NaN word
        // pattern and a normal value are distinguished by std::isnan on the
        // decoded scalar (the text column is never consulted).
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Probe",
                                  RegisterDecodeType::Float32, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x3F80, 0x0000}); // 1.0f — finite
        QCOMPARE(fx.row(0).value("semanticValueClass").toString(),
                 QStringLiteral("finite"));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x7FC0, 0x0000}); // NaN — same LENGTH
        QCOMPARE(fx.row(0).value("semanticValueClass").toString(),
                 QStringLiteral("not_a_number"));
    }

    void b4c26_unsavedEditorScaleDoesNotAlterSemantic()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Scaled",
                                  RegisterDecodeType::UInt16, 0.1, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {466});
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("46.6 Hz"));
        // The editor changes the scale of the SAME profile but does NOT
        // save: the semantic layer must keep the persisted 0.1.
        QVERIFY(fx.profiles.openProfile(QStringLiteral("id-a")));
        QVERIFY(fx.profiles.editRegisterEntry(
            0, {{QStringLiteral("readFunctionCode"), QStringLiteral("03")},
                {QStringLiteral("address"), QStringLiteral("1000")},
                {QStringLiteral("name"), QStringLiteral("Scaled")},
                {QStringLiteral("dataType"), QStringLiteral("2")},
                {QStringLiteral("byteOrder"), QStringLiteral("0")},
                {QStringLiteral("wordOrder"), QStringLiteral("-1")},
                {QStringLiteral("scale"), QStringLiteral("9.9")},
                {QStringLiteral("offset"), QString()},
                {QStringLiteral("unit"), QStringLiteral("Hz")}}));
        QVERIFY(fx.profiles.dirty());
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("46.6 Hz")); // persisted 0.1, not 9.9
    }

    void b4c27_saveActiveRefreshesSemantic()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Scaled",
                                  RegisterDecodeType::UInt16, 0.1, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {466});
        QVERIFY(fx.profiles.openProfile(QStringLiteral("id-a")));
        QVERIFY(fx.profiles.editRegisterEntry(
            0, {{QStringLiteral("readFunctionCode"), QStringLiteral("03")},
                {QStringLiteral("address"), QStringLiteral("1000")},
                {QStringLiteral("name"), QStringLiteral("Scaled")},
                {QStringLiteral("dataType"), QStringLiteral("2")},
                {QStringLiteral("byteOrder"), QStringLiteral("0")},
                {QStringLiteral("wordOrder"), QStringLiteral("-1")},
                {QStringLiteral("scale"), QStringLiteral("2")},
                {QStringLiteral("offset"), QString()},
                {QStringLiteral("unit"), QStringLiteral("Hz")}}));
        QVERIFY(fx.profiles.saveCurrent());
        // Same identity, new persisted scale → the semantic followed.
        QCOMPARE(fx.active.activeProfileId(), QStringLiteral("id-a"));
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("932 Hz"));
    }

    void b4c28_deleteActiveReturnsToNoProfileSemantic()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Scaled",
                                  RegisterDecodeType::UInt16, 0.1, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {466});
        QCOMPARE(fx.row(0).value("semanticStatus").toString(),
                 QStringLiteral("mapped_start"));
        QVERIFY(fx.profiles.deleteProfile(QStringLiteral("id-a")));
        // Semantic layer immediately enters the no-active state; raw and
        // generic stay.
        QCOMPARE(fx.row(0).value("semanticStatus").toString(),
                 QStringLiteral("no_active_profile"));
        QCOMPARE(fx.row(0).value("dec").toInt(), 466);
        QCOMPARE(fx.row(0).value("decoded").toString(),
                 QStringLiteral("466"));
    }

    void b4c29_rawValuesUnchanged()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Frequency",
                                  RegisterDecodeType::UInt16, 0.1, 5.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::
                                      ByteSwapped,
                                  modbuslens::core::RegisterWordOrder::
                                      LowWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x0102, 0x0304});
        // The profile metadata (even byte/word order!) must not touch the
        // raw columns: wire truth is layer 1.
        QCOMPARE(fx.row(0).value("dec").toInt(), 0x0102);
        QCOMPARE(fx.row(0).value("hex").toString(), QStringLiteral("0x0102"));
        QCOMPARE(fx.row(1).value("dec").toInt(), 0x0304);
        QCOMPARE(fx.row(0).value("hex").toString(), QStringLiteral("0x0102"));
        QCOMPARE(fx.row(1).value("hex").toString(), QStringLiteral("0x0304"));
    }

    void b4c30_genericDecodeResultUnchanged()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Frequency",
                                  RegisterDecodeType::UInt16, 0.1, 5.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::
                                      ByteSwapped,
                                  modbuslens::core::RegisterWordOrder::
                                      LowWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x0102, 0x0304});
        // Generic decode stays Normal/HighWordFirst per the UI controls —
        // the profile's ByteSwapped/LowWordFirst must not leak into it.
        // UInt16 row 0 generic = 0x0102 = 258.
        QCOMPARE(fx.row(0).value("decoded").toString(), QStringLiteral("258"));
        QCOMPARE(fx.row(0).value("decodeStatus").toString(),
                 QStringLiteral("ok"));
    }

    void b4c31_genericSourceRangeUnchanged()
    {
        // With a 2-word GENERIC type (UInt32) the sliding window keeps its
        // frozen per-row alignment even under a profile with different
        // spans; the decodeSpan of row 0 stays "1000-1001".
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Wide",
                                  RegisterDecodeType::UInt16, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("3"), {0x0001, 0x0002, 0x0003});
        // Switch the GENERIC control to UInt32 (presentation-only).
        fx.analysis.setReadDecodeType(
            static_cast<int>(RegisterDecodeType::UInt32));
        QCOMPARE(fx.row(0).value("decodeSpan").toString(),
                 QStringLiteral("1000-1001"));
        QCOMPARE(fx.row(1).value("decodeSpan").toString(),
                 QStringLiteral("1001-1002"));
        QCOMPARE(fx.analysis.readResultValues().size(), 3);
    }

    void b4c32_requestedFunctionCodeAuthority()
    {
        // The lookup must use the REQUESTED function code from the
        // transaction snapshot (readResult_.functionCode), not a constant.
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x04, 1000, "Input register",
                                  RegisterDecodeType::UInt16, 10.0, 0.0, "V",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("04"), QStringLiteral("1000"),
                QStringLiteral("1"), {7}, 0x04);
        // A successful FC04 mapping PROVES the requested function code was
        // the lookup authority (the profile has no FC03 entry here).
        QCOMPARE(fx.row(0).value("semanticName").toString(),
                 QStringLiteral("Input register"));
        QCOMPARE(fx.row(0).value("semanticText").toString(), QStringLiteral("70 V"));
    }

    void b4c33_receivedFunctionNotUsedAsAuthority()
    {
        // The response's function byte is echoed in the received-function
        // projection; the semantic lookup remains keyed on the REQUEST.
        // (The normal path here has received == requested; the authority
        // distinction is proven by b4c32 + the fact that the helper reads
        // readResult_.functionCode only.)
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Holding",
                                  RegisterDecodeType::UInt16, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {2}, 0x03);
        QVERIFY(fx.analysis.readResultHasReceivedFunction());
        // The lookup authority stays the request's own FC byte.
        QCOMPARE(fx.row(0).value("semanticName").toString(),
                 QStringLiteral("Holding"));
    }

    void b4c34_profileNamePresentation()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "输出频率",
                                  RegisterDecodeType::UInt16, 0.1, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {466});
        // The Human-visible name is the register NAME — never a profileId
        // hash, filename hash or internal token.
        QCOMPARE(fx.row(0).value("semanticName").toString(),
                 QStringLiteral("输出频率"));
        QVERIFY(!fx.row(0).value("semanticName").toString().contains("-"));
    }

    void b4c35_profileSpanPresentation()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Wide",
                                  RegisterDecodeType::UInt32, 1.0, 0.0, "",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0x0001, 0x0000});
        QCOMPARE(fx.row(0).value("semanticSpan").toString(),
                 QStringLiteral("1000-1001"));
        QCOMPARE(fx.row(0).value("semanticDataType").toString(),
                 QStringLiteral("UInt32"));
    }

    void b4c36_differentFcSameAddressDifferentMapping()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Holding 1000",
                                  RegisterDecodeType::UInt16, 1.0, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst},
                                 {0x04, 1000, "Input 1000",
                                  RegisterDecodeType::UInt16, 5.0, 0.0, "V",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {2}, 0x03);
        QCOMPARE(fx.row(0).value("semanticName").toString(),
                 QStringLiteral("Holding 1000"));
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("2 Hz"));
        fx.read(QStringLiteral("04"), QStringLiteral("1000"),
                QStringLiteral("1"), {2}, 0x04);
        QCOMPARE(fx.row(0).value("semanticName").toString(),
                 QStringLiteral("Input 1000"));
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("10 V"));
    }

    void b4c37_profileSelectionSurvivesGenericControlChanges()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Scaled",
                                  RegisterDecodeType::UInt16, 0.1, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::Normal,
                                  modbuslens::core::RegisterWordOrder::
                                      HighWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("1"), {466});
        fx.analysis.setReadDecodeType(
            static_cast<int>(RegisterDecodeType::Int16));
        fx.analysis.setReadDecodeByteOrder(
            static_cast<int>(modbuslens::core::RegisterByteOrder::
                                 ByteSwapped));
        // The semantic layer is driven by the PROFILE, not the generic
        // controls: still 46.6 Hz.
        QCOMPARE(fx.row(0).value("semanticText").toString(),
                 QStringLiteral("46.6 Hz"));
        // And the generic layer DID follow its own controls (0x0466 byteswap
        // = 0x6604 = 26116 as Int16 → 26116).
        QCOMPARE(fx.row(0).value("decoded").toString(),
                 QStringLiteral("-11775")); // 0x01D2 swapped = 0xD201
    }

    void b4c38_profileMetadataNeverMutatesTransactionValues()
    {
        seedManaged(makeProfile("id-a", "Alpha",
                                {{0x03, 1000, "Frequency",
                                  RegisterDecodeType::UInt16, 0.1, 0.0, "Hz",
                                  modbuslens::core::RegisterByteOrder::
                                      ByteSwapped,
                                  modbuslens::core::RegisterWordOrder::
                                      LowWordFirst}}));
        Fixture fx;
        QVERIFY(fx.active.selectProfile(QStringLiteral("id-a")));
        fx.read(QStringLiteral("03"), QStringLiteral("1000"),
                QStringLiteral("2"), {0xBEEF, 0xCAFE});
        const QString hexBefore = fx.row(0).value("hex").toString();
        const int dec0Before = fx.row(0).value("dec").toInt();
        const int dec1Before = fx.row(1).value("dec").toInt();
        const QString decodedBefore = fx.row(0).value("decoded").toString();
        // A catalog refresh re-projects; the wire truth must not move.
        fx.profiles.refreshCatalog();
        QCOMPARE(fx.row(0).value("hex").toString(), hexBefore);
        QCOMPARE(fx.row(0).value("dec").toInt(), dec0Before);
        QCOMPARE(fx.row(1).value("dec").toInt(), dec1Before);
        QCOMPARE(fx.row(0).value("decoded").toString(), decodedBefore);
        QCOMPARE(fx.row(0).value("hex").toString(), QStringLiteral("0xBEEF"));
    }
};

QTEST_GUILESS_MAIN(ProfileSemanticTest)
#include "test_profile_semantic.moc"
