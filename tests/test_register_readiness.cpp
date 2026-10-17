#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "core/analysis/RegisterDecode.h"
#include "core/profile/DeviceProfile.h"
#include "core/profile/RegisterReadiness.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::core::DeviceProfile;
using modbuslens::core::EngineeringDecoderKind;
using modbuslens::core::RegisterDecodeType;
using modbuslens::core::RegisterEntry;
using modbuslens::core::RegisterReadinessLevel;
using modbuslens::core::RegisterReadinessResult;
using modbuslens::core::validateRegisterForEngineeringDisplay;
using modbuslens::ui::ProfileLoadResult;
using modbuslens::ui::ProfileStore;

namespace {

// A Readable-by-default entry: FC04 @0, UInt16, one word, engine fields at
// their frozen defaults, NOT configured, decoderKind Unknown (the C1.2
// default — the tests below opt into Linear explicitly when they mean it).
RegisterEntry makeEntry()
{
    RegisterEntry entry;
    entry.readFunctionCode = 0x04;
    entry.address = 0;
    entry.name = "Sample";
    entry.dataType = RegisterDecodeType::UInt16;
    entry.registerCount = 1;
    entry.scale = 1.0;
    entry.offset = 0.0;
    entry.unit.clear();
    entry.scaleConfigured = false;
    entry.offsetConfigured = false;
    entry.unitConfigured = false;
    entry.decoderKind = EngineeringDecoderKind::Unknown;
    return entry;
}

// The same entry with every engineering field explicitly configured and the
// decoder declared Linear — the shape the Linear contract checks.
RegisterEntry makeLinearReadyEntry()
{
    RegisterEntry entry = makeEntry();
    entry.scale = 0.5;
    entry.offset = -1.0;
    entry.unit = "s";
    entry.scaleConfigured = true;
    entry.offsetConfigured = true;
    entry.unitConfigured = true;
    entry.decoderKind = EngineeringDecoderKind::Linear;
    return entry;
}

DeviceProfile wrapEntry(const RegisterEntry &entry)
{
    DeviceProfile profile;
    profile.profileId = "readiness-test";
    profile.displayName = "Readiness Test";
    profile.registers.push_back(entry);
    return profile;
}

bool contains(const std::vector<std::string> &haystack,
              const std::string &needle)
{
    for (const std::string &s : haystack) {
        if (s == needle) {
            return true;
        }
    }
    return false;
}

bool anyReasonMentions(const std::vector<std::string> &reasons,
                       const std::string &needle)
{
    for (const std::string &reason : reasons) {
        if (reason.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

class RegisterReadinessTest : public QObject
{
    Q_OBJECT

private slots:

    // =====================================================================
    // C-series (T035-C C1, UPDATED by C1.2): the decoder kind now comes from
    // the entry itself. The original assertions are preserved — only the
    // decoder plumbing changed to the real product contract.
    // =====================================================================

    // ------------------------------------------------------------- C03 --
    void c03_functionCodeWhitelist()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());

        RegisterEntry fc04 = makeEntry();
        fc04.decoderKind = EngineeringDecoderKind::Linear;
        auto r = validateRegisterForEngineeringDisplay(profile, fc04);
        QVERIFY(r.readable);

        RegisterEntry fc03 = makeEntry();
        fc03.readFunctionCode = 0x03;
        fc03.decoderKind = EngineeringDecoderKind::Linear;
        r = validateRegisterForEngineeringDisplay(profile, fc03);
        QVERIFY(r.readable);

        for (const std::uint8_t fc : {std::uint8_t{0x01}, std::uint8_t{0x06},
                                      std::uint8_t{0x10}, std::uint8_t{0x41},
                                      std::uint8_t{0x00}}) {
            RegisterEntry other = makeEntry();
            other.readFunctionCode = fc;
            r = validateRegisterForEngineeringDisplay(profile, other);
            QVERIFY2(!r.readable,
                     qPrintable(QStringLiteral("FC %1 was deemed readable")
                                    .arg(int(fc))));
            QVERIFY(!r.engineeringReady);
            QVERIFY(contains(r.missingFields, "supported_read_function_code"));
            QVERIFY(!r.reasons.empty());
        }
    }

    // ------------------------------------------------------------- C04 --
    void c04_addressSpanOutOfRange()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());

        RegisterEntry tail = makeEntry();
        tail.address = 65535;
        auto r = validateRegisterForEngineeringDisplay(profile, tail);
        QVERIFY(r.readable);

        RegisterEntry overflow = makeEntry();
        overflow.address = 65535;
        overflow.dataType = RegisterDecodeType::Float32;
        overflow.registerCount = 2;
        r = validateRegisterForEngineeringDisplay(profile, overflow);
        QVERIFY(!r.readable);
        QVERIFY(contains(r.invalidFields, "address_span_out_of_range"));

        RegisterEntry fits = makeEntry();
        fits.address = 65534;
        fits.dataType = RegisterDecodeType::Float32;
        fits.registerCount = 2;
        r = validateRegisterForEngineeringDisplay(profile, fits);
        QVERIFY(r.readable);
    }

    // ------------------------------------------------------------- C05 --
    void c05_registerCountMismatch()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());

        RegisterEntry bad = makeEntry();
        bad.dataType = RegisterDecodeType::Float32;
        bad.registerCount = 1;
        auto r = validateRegisterForEngineeringDisplay(profile, bad);
        QVERIFY(!r.readable);
        QVERIFY(contains(r.invalidFields, "register_count_mismatch"));

        bad.registerCount = 2;
        r = validateRegisterForEngineeringDisplay(profile, bad);
        QVERIFY(r.readable);
    }

    // ------------------------------------------------------------- C06 --
    void c06_byteWordOrderInvalid()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());

        RegisterEntry badByte = makeEntry();
        badByte.byteOrder = static_cast<modbuslens::core::RegisterByteOrder>(99);
        auto r = validateRegisterForEngineeringDisplay(profile, badByte);
        QVERIFY(!r.readable);
        QVERIFY(contains(r.invalidFields, "byte_order_out_of_enum"));

        RegisterEntry badWord = makeEntry();
        badWord.dataType = RegisterDecodeType::UInt32;
        badWord.registerCount = 2;
        badWord.wordOrder =
            static_cast<modbuslens::core::RegisterWordOrder>(99);
        r = validateRegisterForEngineeringDisplay(profile, badWord);
        QVERIFY(!r.readable);
        QVERIFY(contains(r.invalidFields, "word_order_out_of_enum"));

        RegisterEntry good = makeEntry();
        good.dataType = RegisterDecodeType::UInt32;
        good.registerCount = 2;
        good.wordOrder = modbuslens::core::RegisterWordOrder::LowWordFirst;
        r = validateRegisterForEngineeringDisplay(profile, good);
        QVERIFY(r.readable);
    }

    // ------------------------------------------------------------- C07 --
    void c07_readableButEngineeringNotConfigured()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeEntry();
        entry.decoderKind = EngineeringDecoderKind::Linear;
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(r.readable);
        QVERIFY(!r.engineeringReady);
        QVERIFY(r.level == RegisterReadinessLevel::Readable);
    }

    // ------------------------------------------------------------- C08 --
    void c08_defaultsDoNotCountAsConfigured()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeEntry();
        entry.decoderKind = EngineeringDecoderKind::Linear;
        entry.scale = 1.0;
        entry.offset = 0.0;
        entry.unit.clear();
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(r.readable);
        QVERIFY(!r.engineeringReady);
    }

    // ------------------------------------------------------------- C09 --
    void c09_explicitlyConfiguredDefaultsAreReady()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeEntry();
        entry.decoderKind = EngineeringDecoderKind::Linear;
        entry.scale = 1.0;
        entry.offset = 0.0;
        entry.unit = "s";
        entry.scaleConfigured = true;
        entry.offsetConfigured = true;
        entry.unitConfigured = true;
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(r.readable);
        QVERIFY(r.engineeringReady);
        QVERIFY(r.level == RegisterReadinessLevel::EngineeringReady);
        QVERIFY(r.missingFields.empty());
        QVERIFY(r.invalidFields.empty());
    }

    // ------------------------------------------------------------- C10 --
    void c10_nonFiniteAndEmptyUnit()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());

        RegisterEntry nanScale = makeEntry();
        nanScale.decoderKind = EngineeringDecoderKind::Linear;
        nanScale.scaleConfigured = true;
        nanScale.scale = std::numeric_limits<double>::quiet_NaN();
        nanScale.offsetConfigured = true;
        nanScale.unit = "s";
        nanScale.unitConfigured = true;
        auto r = validateRegisterForEngineeringDisplay(profile, nanScale);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.invalidFields, "scale_not_finite"));

        RegisterEntry infOffset = makeEntry();
        infOffset.decoderKind = EngineeringDecoderKind::Linear;
        infOffset.offsetConfigured = true;
        infOffset.offset = std::numeric_limits<double>::infinity();
        infOffset.scaleConfigured = true;
        infOffset.unit = "s";
        infOffset.unitConfigured = true;
        r = validateRegisterForEngineeringDisplay(profile, infOffset);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.invalidFields, "offset_not_finite"));

        RegisterEntry emptyUnit = makeEntry();
        emptyUnit.decoderKind = EngineeringDecoderKind::Linear;
        emptyUnit.scaleConfigured = true;
        emptyUnit.offsetConfigured = true;
        emptyUnit.unitConfigured = true;
        emptyUnit.unit.clear(); // "configured" yet empty: contradiction
        r = validateRegisterForEngineeringDisplay(profile, emptyUnit);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.invalidFields, "unit_empty"));
    }

    // ------------------------------------------------------------- C11 --
    // (Superseded in scope by D09 — kept, updated to the decoder-on-entry
    // contract: Raw stays readable-only; a declared Linear stays ready.)
    void c11_linearAndRawVerdictsOnEntry()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeLinearReadyEntry();

        auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(r.engineeringReady); // explicit Linear

        RegisterEntry raw = entry;
        raw.decoderKind = EngineeringDecoderKind::Raw;
        r = validateRegisterForEngineeringDisplay(profile, raw);
        QVERIFY(r.readable);
        QVERIFY(!r.engineeringReady);
        QVERIFY(r.level == RegisterReadinessLevel::Readable);
    }

    // ------------------------------------------------------------- C15 --
    // A legacy entry (flags absent => false, decoder absent => Unknown) is
    // conservatively NOT EngineeringReady and names the UNDECLARED decoder
    // as an explicit reason.
    void c15_legacyEntryConservativelyNotReady()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry legacy; // defaults: 1 / 0 / "" flags false, Unknown
        legacy.readFunctionCode = 0x03;
        legacy.address = 100;
        legacy.name = "Legacy";
        legacy.dataType = RegisterDecodeType::UInt16;
        legacy.registerCount = 1;
        const auto r = validateRegisterForEngineeringDisplay(profile, legacy);
        QVERIFY(r.readable);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.missingFields, "decoder_kind"));
        QVERIFY(anyReasonMentions(r.reasons, "未确认"));
    }

    // ------------------------------------------------------------- C16 --
    void c16_deterministicAndSideEffectFree()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeLinearReadyEntry();

        const auto snapshotDir = [](const QString &dir) {
            QStringList rows;
            const QFileInfoList infos =
                QDir(dir).entryInfoList(QDir::Files | QDir::Dirs
                                            | QDir::NoDotAndDotDot,
                                        QDir::Name);
            for (const QFileInfo &info : infos) {
                rows << QStringLiteral("%1|%2|%3")
                            .arg(info.fileName())
                            .arg(info.size())
                            .arg(info.lastModified().toMSecsSinceEpoch());
            }
            return rows;
        };
        // FIX-01 (SOURCE-REPRO-R4): probe a TEST-OWNED directory instead of the
        // machine-wide TEMP. A full ctest run is a shared-machine context, so
        // any co-resident process (another test's QTemporaryFile, the OS,
        // background tools) could mutate TEMP and make this "no side effects"
        // assertion fail for reasons unrelated to the code under test.
        // Detection power is preserved and PROVEN IN-BAND by the sentinel
        // negative control below, so it cannot silently degrade to PASS.
        QTemporaryDir probeRoot;
        QVERIFY(probeRoot.isValid());
        const QString probeDir = probeRoot.path();
        const QString sentinelPath =
            QDir(probeDir).filePath(QStringLiteral("side-effect-sentinel.txt"));
        {
            QFile sentinel(sentinelPath);
            QVERIFY(sentinel.open(QIODevice::WriteOnly));
            QVERIFY(sentinel.write("sentinel") == 8);
        }
        const QStringList withSentinel = snapshotDir(probeDir);
        QVERIFY(QFile::remove(sentinelPath));
        const QStringList withoutSentinel = snapshotDir(probeDir);
        // Negative control: the checker MUST observe the sentinel's effect.
        QVERIFY(withSentinel != withoutSentinel);
        const QStringList before = withoutSentinel;

        const auto r1 = validateRegisterForEngineeringDisplay(profile, entry);
        const auto r2 = validateRegisterForEngineeringDisplay(profile, entry);
        QCOMPARE(r1.readable, r2.readable);
        QCOMPARE(r1.engineeringReady, r2.engineeringReady);
        QVERIFY(r1.level == r2.level);
        QVERIFY(r1.missingFields == r2.missingFields);
        QVERIFY(r1.invalidFields == r2.invalidFields);
        QVERIFY(r1.reasons == r2.reasons);
        QVERIFY(r1.engineeringReady);

        const QStringList after = snapshotDir(probeDir);
        QCOMPARE(after, before);
    }

    // ------------------------------------------------------------- C17 --
    void c17_listsEveryMissingField()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeEntry();
        entry.decoderKind = EngineeringDecoderKind::Linear;
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QCOMPARE(int(r.missingFields.size()), 3);
        QVERIFY(contains(r.missingFields, "scale_configured"));
        QVERIFY(contains(r.missingFields, "offset_configured"));
        QVERIFY(contains(r.missingFields, "unit_configured"));
        QVERIFY(r.reasons.size() >= 3);

        RegisterEntry mixed = makeEntry();
        mixed.decoderKind = EngineeringDecoderKind::Linear;
        mixed.scaleConfigured = true;
        mixed.scale = std::numeric_limits<double>::quiet_NaN();
        const auto r2 = validateRegisterForEngineeringDisplay(profile, mixed);
        QVERIFY(contains(r2.invalidFields, "scale_not_finite"));
        QVERIFY(contains(r2.missingFields, "offset_configured"));
        QVERIFY(contains(r2.missingFields, "unit_configured"));
    }

    // ------------------------------------------------------------- C18 --
    void c18_saveReloadKeepsVerdictStable()
    {
        DeviceProfile profile = wrapEntry(makeLinearReadyEntry());
        const QByteArray json = ProfileStore::serializeToJson(profile);
        const ProfileLoadResult loaded = ProfileStore::parseFromJson(json);
        QVERIFY(loaded.ok());
        QCOMPARE(loaded.profile.registers.size(), std::size_t{1});
        const RegisterEntry &reloaded = loaded.profile.registers.front();
        QVERIFY(reloaded.scaleConfigured);
        QVERIFY(reloaded.offsetConfigured);
        QVERIFY(reloaded.unitConfigured);
        QVERIFY(reloaded.decoderKind == EngineeringDecoderKind::Linear);
        const auto r = validateRegisterForEngineeringDisplay(loaded.profile,
                                                             reloaded);
        QVERIFY(r.engineeringReady);

        DeviceProfile conservative = wrapEntry(makeEntry());
        const ProfileLoadResult conservativeLoaded = ProfileStore::parseFromJson(
            ProfileStore::serializeToJson(conservative));
        QVERIFY(conservativeLoaded.ok());
        const auto r2 = validateRegisterForEngineeringDisplay(
            conservativeLoaded.profile,
            conservativeLoaded.profile.registers.front());
        QVERIFY(r2.readable);
        QVERIFY(!r2.engineeringReady);
    }

    // =====================================================================
    // D-series (T035-C C1.2): the persisted decoderKind contract.
    // =====================================================================

    // ------------------------------------------------------------- D01 --
    void d01_newEntryDefaultsToUnknown()
    {
        const RegisterEntry fresh;
        QVERIFY(fresh.decoderKind == EngineeringDecoderKind::Unknown);
        const RegisterEntry made = makeEntry();
        QVERIFY(made.decoderKind == EngineeringDecoderKind::Unknown);
    }

    // ------------------------------------------------------------- D02 --
    void d02_legacyProfileWithoutDecoderLoadsUnknown()
    {
        // A legacy JSON entry (no decoderKind key at all) must load with
        // Unknown — never Linear, never inferred.
        const QByteArray legacy = R"({
  "schemaVersion": 1,
  "profileId": "legacy-d02",
  "displayName": "Legacy D02",
  "registers": [
    {
      "readFunctionCode": 4,
      "address": 0,
      "name": "Channel 1",
      "description": "",
      "dataType": "UInt16",
      "registerCount": 1,
      "byteOrder": "Normal",
      "wordOrder": "HighWordFirst",
      "scale": 1,
      "offset": 0,
      "unit": ""
    }
  ]
})";
        const ProfileLoadResult loaded = ProfileStore::parseFromJson(legacy);
        QVERIFY(loaded.ok());
        QCOMPARE(loaded.profile.registers.size(), std::size_t{1});
        QVERIFY(loaded.profile.registers.front().decoderKind
                == EngineeringDecoderKind::Unknown);
    }

    // ------------------------------------------------------------- D03 --
    void d03_configuredButUnknownIsNotEngineeringReady()
    {
        // The C1.1 risk sample: every engineering field explicitly
        // configured — but the decoder is UNDECLARED. Not EngineeringReady,
        // and the missing list names the decoder explicitly.
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeLinearReadyEntry();
        entry.decoderKind = EngineeringDecoderKind::Unknown;
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(r.readable);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.missingFields, "decoder_kind"));
        QVERIFY(anyReasonMentions(r.reasons, "未确认"));
    }

    // ------------------------------------------------------------- D04 --
    void d04_rawIsReadableButNeverEngineeringReady()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeLinearReadyEntry();
        entry.decoderKind = EngineeringDecoderKind::Raw;
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(r.readable);
        QVERIFY(!r.engineeringReady);
        QVERIFY(r.level == RegisterReadinessLevel::Readable);
    }

    // ------------------------------------------------------------- D05 --
    void d05_explicitLinearAllValidIsReady()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        const RegisterEntry entry = makeLinearReadyEntry();
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(r.readable);
        QVERIFY(r.engineeringReady);
        QVERIFY(r.level == RegisterReadinessLevel::EngineeringReady);
    }

    // ------------------------------------------------------------- D06 --
    void d06_explicitLinearWithoutScaleConfigured()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeLinearReadyEntry();
        entry.scaleConfigured = false;
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.missingFields, "scale_configured"));
    }

    // ------------------------------------------------------------- D07 --
    void d07_explicitLinearWithoutOffsetConfigured()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeLinearReadyEntry();
        entry.offsetConfigured = false;
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.missingFields, "offset_configured"));
    }

    // ------------------------------------------------------------- D08 --
    void d08_explicitLinearWithoutUnitConfigured()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry entry = makeLinearReadyEntry();
        entry.unitConfigured = false;
        const auto r = validateRegisterForEngineeringDisplay(profile, entry);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.missingFields, "unit_configured"));
    }

    // ------------------------------------------------------------- D09 --
    void d09_variableDecimalAndEnumNeverReady()
    {
        const DeviceProfile profile = wrapEntry(makeEntry());
        RegisterEntry vd = makeLinearReadyEntry();
        vd.decoderKind = EngineeringDecoderKind::VariableDecimal;
        auto r = validateRegisterForEngineeringDisplay(profile, vd);
        QVERIFY(r.readable);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.missingFields,
                         "decoder_support_variable_decimal"));
        QVERIFY(anyReasonMentions(r.reasons, "VariableDecimal"));

        RegisterEntry en = makeLinearReadyEntry();
        en.decoderKind = EngineeringDecoderKind::Enum;
        r = validateRegisterForEngineeringDisplay(profile, en);
        QVERIFY(r.readable);
        QVERIFY(!r.engineeringReady);
        QVERIFY(contains(r.missingFields, "decoder_support_enum"));
    }

    // ------------------------------------------------------------- D10 --
    void d10_decoderKindRoundTripsThroughSaveReload()
    {
        for (const EngineeringDecoderKind kind :
             {EngineeringDecoderKind::Raw, EngineeringDecoderKind::Linear,
              EngineeringDecoderKind::VariableDecimal,
              EngineeringDecoderKind::Enum,
              EngineeringDecoderKind::Unknown}) {
            DeviceProfile profile = wrapEntry(makeEntry());
            profile.registers.front().decoderKind = kind;
            const ProfileLoadResult loaded = ProfileStore::parseFromJson(
                ProfileStore::serializeToJson(profile));
            QVERIFY(loaded.ok());
            QVERIFY2(loaded.profile.registers.front().decoderKind == kind,
                     qPrintable(QStringLiteral("decoder %1 did not survive "
                                               "the round trip")
                                    .arg(int(kind))));
        }
    }

    // ------------------------------------------------------------- D12 --
    void d12_invalidDecoderTokenFailsClosed()
    {
        // An unrecognized token is a STRUCTURAL ERROR — never a silent
        // conversion (least of all to Linear).
        const QByteArray bad = R"({
  "schemaVersion": 1,
  "profileId": "bad-d12",
  "displayName": "Bad D12",
  "registers": [
    {
      "readFunctionCode": 4,
      "address": 0,
      "name": "Channel 1",
      "description": "",
      "dataType": "UInt16",
      "registerCount": 1,
      "byteOrder": "Normal",
      "wordOrder": "HighWordFirst",
      "scale": 1,
      "offset": 0,
      "unit": "",
      "decoderKind": "LinearPlease"
    }
  ]
})";
        const ProfileLoadResult loaded = ProfileStore::parseFromJson(bad);
        QVERIFY(!loaded.ok());
    }

    // ------------------------------------------------------------- D18 --
    void d18_wholeValidationChainHasNoSideEffects()
    {
        // FIX-01 (SOURCE-REPRO-R4): same rationale as c16 — a test-owned probe
        // directory plus an in-band sentinel negative control that proves the
        // checker is still able to detect filesystem side effects.
        QTemporaryDir probeRoot;
        QVERIFY(probeRoot.isValid());
        const QString probeDir = probeRoot.path();
        const auto snapshotDir = [&]() {
            QStringList rows;
            const QFileInfoList infos =
                QDir(probeDir).entryInfoList(QDir::Files | QDir::Dirs
                                                 | QDir::NoDotAndDotDot,
                                             QDir::Name);
            for (const QFileInfo &info : infos) {
                rows << QStringLiteral("%1|%2|%3")
                            .arg(info.fileName())
                            .arg(info.size())
                            .arg(info.lastModified().toMSecsSinceEpoch());
            }
            return rows;
        };
        const QString sentinelPath =
            QDir(probeDir).filePath(QStringLiteral("side-effect-sentinel.txt"));
        {
            QFile sentinel(sentinelPath);
            QVERIFY(sentinel.open(QIODevice::WriteOnly));
            QVERIFY(sentinel.write("sentinel") == 8);
        }
        const QStringList withSentinel = snapshotDir();
        QVERIFY(QFile::remove(sentinelPath));
        const QStringList withoutSentinel = snapshotDir();
        // Negative control: the checker MUST observe the sentinel's effect.
        QVERIFY(withSentinel != withoutSentinel);
        const QStringList before = withoutSentinel;

        // The full chain: serialize -> parse -> validate, several kinds.
        for (const EngineeringDecoderKind kind :
             {EngineeringDecoderKind::Unknown, EngineeringDecoderKind::Raw,
              EngineeringDecoderKind::Linear}) {
            DeviceProfile profile = wrapEntry(makeLinearReadyEntry());
            profile.registers.front().decoderKind = kind;
            const ProfileLoadResult loaded =
                ProfileStore::parseFromJson(
                    ProfileStore::serializeToJson(profile));
            QVERIFY(loaded.ok());
            (void)validateRegisterForEngineeringDisplay(
                loaded.profile, loaded.profile.registers.front());
        }

        QCOMPARE(snapshotDir(), before);
    }
};

QTEST_GUILESS_MAIN(RegisterReadinessTest)
#include "test_register_readiness.moc"
