#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <string>
#include <vector>

#include "core/analysis/RegisterDecode.h"
#include "core/profile/DeviceProfile.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::core::DeviceProfile;
using modbuslens::core::ProfileValidationCode;
using modbuslens::core::RegisterByteOrder;
using modbuslens::core::RegisterDecodeType;
using modbuslens::core::RegisterEntry;
using modbuslens::core::RegisterWordOrder;
using modbuslens::ui::ProfileLoadResult;
using modbuslens::ui::ProfileSaveResult;
using modbuslens::ui::ProfileStore;
using modbuslens::ui::ProfileStoreError;

namespace {

DeviceProfile makeValidProfile()
{
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = "11111111-2222-3333-4444-555555555555";
    profile.displayName = "Demo inverter";
    profile.manufacturer = "ACME";
    profile.model = "INV-1000";
    profile.revision = "rev A";
    profile.description = "fixture profile";
    RegisterEntry entry;
    entry.address = 1000;
    entry.name = "Frequency";
    entry.description = "output frequency";
    entry.dataType = RegisterDecodeType::UInt16;
    entry.registerCount = 1;
    entry.byteOrder = RegisterByteOrder::Normal;
    entry.wordOrder = RegisterWordOrder::HighWordFirst;
    entry.scale = 0.1;
    entry.offset = 0.0;
    entry.unit = "Hz";
    profile.registers.push_back(entry);
    return profile;
}

QByteArray validJson()
{
    return ProfileStore::serializeToJson(makeValidProfile());
}

ProfileLoadResult parse(QByteArray json)
{
    return ProfileStore::parseFromJson(json);
}

} // namespace

class DeviceProfileTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- P01/P02: profile identity ----
    void p01_profileIdentityRequired();
    void p02_optionalIdentityFieldsEmptyAccepted();

    // ---- P03: register map construction ----
    void p03_registerEntryConstruction();

    // ---- P04-P07: type / word-count validation ----
    void p04_allM11DataTypesAccepted();
    void p05_wordCountOneWordTypes();
    void p06_wordCountTwoWordTypes();
    void p07_registerCountMismatchRejected();

    // ---- P08-P11: address rules ----
    void p08_addressLowerBoundary();
    void p09_addressUpperBoundary();
    void p10_twoWordSpanOverflowRejected();
    void p11_duplicateAddressRejected();

    // ---- P12-P16: scaling / offset / unit defaults and round-trip ----
    void p12_scaleDefault();
    void p13_offsetDefault();
    void p14_unitDefault();
    void p15_scaleZeroAccepted();
    void p16_freeTextUnitRoundTrip();

    // ---- P17/P18: JSON determinism and file round-trip ----
    void p17_jsonSerializationDeterministic();
    void p18_jsonFileRoundTrip();

    // ---- P19-P23: schema version / malformed / unknown fields / no mutation ----
    void p19_schemaVersionMissingRejected();
    void p20_schemaVersionTooNewRejected();
    void p21_malformedJsonRejected();
    void p22_unknownFieldsIgnored();
    void p23_malformedLoadDoesNotMutateExisting();

    // ---- P24/P25: truth preservation and enum binding ----
    void p24_serializationPathTouchesNoWireOrDecode();
    void p25_m11EnumTokensRoundTrip();

    // ---- P26-P29: semantic formula (frozen P0-c) ----
    void p26_semanticFormula();
    void p27_scaleZeroYieldsOffset();
    void p28_offsetApplicationOrder();
    void p29_specialValuesPreserved();

    // ---- validation surface ----
    void v1_validationTokens();
    void v2_saveRefusesInvalidProfile();
};

void DeviceProfileTest::p01_profileIdentityRequired()
{
    DeviceProfile profile = makeValidProfile();
    profile.profileId.clear();
    QCOMPARE(modbuslens::core::validateDeviceProfile(profile).code,
             ProfileValidationCode::ProfileIdMissing);
    profile = makeValidProfile();
    profile.displayName.clear();
    QCOMPARE(modbuslens::core::validateDeviceProfile(profile).code,
             ProfileValidationCode::DisplayNameMissing);
}

void DeviceProfileTest::p02_optionalIdentityFieldsEmptyAccepted()
{
    DeviceProfile profile = makeValidProfile();
    profile.manufacturer.clear();
    profile.model.clear();
    profile.revision.clear();
    profile.description.clear();
    QVERIFY(modbuslens::core::validateDeviceProfile(profile).ok());
    const ProfileLoadResult loaded = parse(ProfileStore::serializeToJson(profile));
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.profile, profile);
}

void DeviceProfileTest::p03_registerEntryConstruction()
{
    DeviceProfile profile = makeValidProfile();
    RegisterEntry second;
    second.address = 1001;
    second.name = "Current";
    second.dataType = RegisterDecodeType::Int16;
    second.registerCount = 1;
    second.unit = "A";
    profile.registers.push_back(second);
    QVERIFY(modbuslens::core::validateDeviceProfile(profile).ok());
    const ProfileLoadResult loaded = parse(ProfileStore::serializeToJson(profile));
    QVERIFY(loaded.ok());
    QCOMPARE(int(loaded.profile.registers.size()), 2);
    QCOMPARE(loaded.profile.registers.at(1).name, std::string("Current"));
    QCOMPARE(loaded.profile, profile);
}

void DeviceProfileTest::p04_allM11DataTypesAccepted()
{
    const RegisterDecodeType types[] = {
        RegisterDecodeType::Hex,     RegisterDecodeType::Binary,
        RegisterDecodeType::UInt16,  RegisterDecodeType::Int16,
        RegisterDecodeType::UInt32,  RegisterDecodeType::Int32,
        RegisterDecodeType::Float32,
    };
    for (const RegisterDecodeType type : types) {
        DeviceProfile profile;
        profile.profileId = "id";
        profile.displayName = "name";
        RegisterEntry entry;
        entry.address = 100;
        entry.name = "reg";
        entry.dataType = type;
        entry.registerCount =
            modbuslens::core::registerDecodeTypeWordCount(type);
        profile.registers.push_back(entry);
        QVERIFY2(modbuslens::core::validateDeviceProfile(profile).ok(),
                 modbuslens::core::profileDataTypeToken(type).data());
        const ProfileLoadResult loaded =
            parse(ProfileStore::serializeToJson(profile));
        QVERIFY(loaded.ok());
        QCOMPARE(loaded.profile.registers.at(0).dataType, type);
    }
    // A value outside the M11 matrix (caller bug) is refused, never guessed.
    DeviceProfile broken;
    broken.profileId = "id";
    broken.displayName = "name";
    RegisterEntry entry;
    entry.name = "reg";
    entry.dataType = static_cast<RegisterDecodeType>(99);
    broken.registers.push_back(entry);
    QCOMPARE(modbuslens::core::validateDeviceProfile(broken).code,
             ProfileValidationCode::UnsupportedDataType);
}

void DeviceProfileTest::p05_wordCountOneWordTypes()
{
    for (const RegisterDecodeType type :
         {RegisterDecodeType::Hex, RegisterDecodeType::Binary,
          RegisterDecodeType::UInt16, RegisterDecodeType::Int16}) {
        QCOMPARE(modbuslens::core::registerDecodeTypeWordCount(type), 1);
        DeviceProfile profile;
        profile.profileId = "id";
        profile.displayName = "name";
        RegisterEntry entry;
        entry.address = 0;
        entry.name = "reg";
        entry.dataType = type;
        entry.registerCount = 1;
        profile.registers.push_back(entry);
        QVERIFY(modbuslens::core::validateDeviceProfile(profile).ok());
    }
}

void DeviceProfileTest::p06_wordCountTwoWordTypes()
{
    for (const RegisterDecodeType type : {RegisterDecodeType::UInt32,
                                          RegisterDecodeType::Int32,
                                          RegisterDecodeType::Float32}) {
        QCOMPARE(modbuslens::core::registerDecodeTypeWordCount(type), 2);
        DeviceProfile profile;
        profile.profileId = "id";
        profile.displayName = "name";
        RegisterEntry entry;
        entry.address = 100;
        entry.name = "reg";
        entry.dataType = type;
        entry.registerCount = 2;
        profile.registers.push_back(entry);
        QVERIFY(modbuslens::core::validateDeviceProfile(profile).ok());
    }
}

void DeviceProfileTest::p07_registerCountMismatchRejected()
{
    // 1-word type claiming 2 registers, and 2-word type claiming 1: both are
    // validation errors — registerCount is never auto-corrected.
    DeviceProfile profile = makeValidProfile();
    profile.registers.at(0).registerCount = 2;
    QCOMPARE(modbuslens::core::validateDeviceProfile(profile).code,
             ProfileValidationCode::RegisterCountMismatch);
    QCOMPARE(modbuslens::core::validateDeviceProfile(profile).registerIndex, 0);

    profile = makeValidProfile();
    profile.registers.at(0).dataType = RegisterDecodeType::UInt32;
    profile.registers.at(0).registerCount = 1;
    QCOMPARE(modbuslens::core::validateDeviceProfile(profile).code,
             ProfileValidationCode::RegisterCountMismatch);
    QCOMPARE(modbuslens::core::validateDeviceProfile(profile).registerIndex, 0);
}

void DeviceProfileTest::p08_addressLowerBoundary()
{
    DeviceProfile profile = makeValidProfile();
    profile.registers.at(0).address = 0;
    QVERIFY(modbuslens::core::validateDeviceProfile(profile).ok());
    const ProfileLoadResult loaded = parse(ProfileStore::serializeToJson(profile));
    QVERIFY(loaded.ok());
    QCOMPARE(int(loaded.profile.registers.at(0).address), 0);
}

void DeviceProfileTest::p09_addressUpperBoundary()
{
    // 65535 is a legal PDU address for a 1-register entry.
    DeviceProfile profile = makeValidProfile();
    profile.registers.at(0).address = 65535;
    QVERIFY(modbuslens::core::validateDeviceProfile(profile).ok());
    const ProfileLoadResult loaded = parse(ProfileStore::serializeToJson(profile));
    QVERIFY(loaded.ok());
    QCOMPARE(int(loaded.profile.registers.at(0).address), 65535);

    // 65536 cannot be represented and is refused at parse time.
    QByteArray json = validJson();
    json.replace("\"address\":1000", "\"address\":65536");
    QVERIFY(json.contains("65536"));
    const ProfileLoadResult overflow = parse(json);
    QCOMPARE(overflow.error, ProfileStoreError::InvalidProfile);
    QCOMPARE(overflow.validationCode, ProfileValidationCode::AddressOutOfRange);
}

void DeviceProfileTest::p10_twoWordSpanOverflowRejected()
{
    // address + registerCount - 1 must stay inside PDU 0..65535.
    DeviceProfile profile = makeValidProfile();
    profile.registers.at(0).dataType = RegisterDecodeType::UInt32;
    profile.registers.at(0).registerCount = 2;
    profile.registers.at(0).address = 65535; // 65535 + 1 -> 65536 > 65535
    QCOMPARE(modbuslens::core::validateDeviceProfile(profile).code,
             ProfileValidationCode::SpanOutOfRange);
    // One lower it is legal again.
    profile.registers.at(0).address = 65534;
    QVERIFY(modbuslens::core::validateDeviceProfile(profile).ok());
}

void DeviceProfileTest::p11_duplicateAddressRejected()
{
    DeviceProfile profile = makeValidProfile();
    RegisterEntry second = profile.registers.at(0);
    second.name = "Duplicate";
    profile.registers.push_back(second);
    const auto result = modbuslens::core::validateDeviceProfile(profile);
    QCOMPARE(result.code, ProfileValidationCode::DuplicateAddress);
    QCOMPARE(result.registerIndex, 1);
}

void DeviceProfileTest::p12_scaleDefault()
{
    const RegisterEntry entry;
    QCOMPARE(entry.scale, 1.0);
    // Parsing a document without "scale" yields the same default.
    QByteArray json = validJson();
    json.replace("\"scale\":0.1", "\"scale\":1");
    const ProfileLoadResult loaded = parse(json);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.profile.registers.at(0).scale, 1.0);
}

void DeviceProfileTest::p13_offsetDefault()
{
    const RegisterEntry entry;
    QCOMPARE(entry.offset, 0.0);
    // The serialized fixture always carries offset; a document without the key
    // also falls back to 0.
    const QByteArray json =
        "{\"schemaVersion\":1,\"profileId\":\"id\",\"displayName\":\"n\","
        "\"registers\":[{\"address\":10,\"name\":\"r\",\"dataType\":\"Int16\","
        "\"registerCount\":1,\"byteOrder\":\"Normal\","
        "\"wordOrder\":\"HighWordFirst\"}]}";
    const ProfileLoadResult loaded = parse(json);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.profile.registers.at(0).offset, 0.0);
    QCOMPARE(loaded.profile.registers.at(0).scale, 1.0);
}

void DeviceProfileTest::p14_unitDefault()
{
    const RegisterEntry entry;
    QVERIFY(entry.unit.empty());
    const ProfileLoadResult loaded = parse(
        "{\"schemaVersion\":1,\"profileId\":\"id\",\"displayName\":\"n\","
        "\"registers\":[{\"address\":10,\"name\":\"r\",\"dataType\":\"UInt16\","
        "\"registerCount\":1,\"byteOrder\":\"Normal\","
        "\"wordOrder\":\"HighWordFirst\"}]}");
    QVERIFY(loaded.ok());
    QVERIFY(loaded.profile.registers.at(0).unit.empty());
}

void DeviceProfileTest::p15_scaleZeroAccepted()
{
    DeviceProfile profile = makeValidProfile();
    profile.registers.at(0).scale = 0.0;
    QVERIFY(modbuslens::core::validateDeviceProfile(profile).ok());
    const ProfileLoadResult loaded = parse(ProfileStore::serializeToJson(profile));
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.profile.registers.at(0).scale, 0.0);
}

void DeviceProfileTest::p16_freeTextUnitRoundTrip()
{
    const char* units[] = {"Hz", "\xC2\xB0""C", "m\xC2\xB3/h"};
    for (const char* unit : units) {
        DeviceProfile profile = makeValidProfile();
        profile.registers.at(0).unit = unit;
        const ProfileLoadResult loaded =
            parse(ProfileStore::serializeToJson(profile));
        QVERIFY(loaded.ok());
        QCOMPARE(loaded.profile.registers.at(0).unit, std::string(unit));
        QCOMPARE(loaded.profile, profile);
    }
}

void DeviceProfileTest::p17_jsonSerializationDeterministic()
{
    const DeviceProfile profile = makeValidProfile();
    const QByteArray first = ProfileStore::serializeToJson(profile);
    const QByteArray second = ProfileStore::serializeToJson(profile);
    QCOMPARE(first, second);
    // save -> load -> save is byte-stable for the v1 shape.
    const ProfileLoadResult loaded = parse(first);
    QVERIFY(loaded.ok());
    QCOMPARE(ProfileStore::serializeToJson(loaded.profile), first);
}

void DeviceProfileTest::p18_jsonFileRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("p.json"));
    const DeviceProfile profile = makeValidProfile();
    const ProfileSaveResult saved = ProfileStore::saveToFile(profile, path);
    QVERIFY(saved.ok());
    QVERIFY(QFile::exists(path));
    const ProfileLoadResult loaded = ProfileStore::loadFromFile(path);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.profile, profile);
}

void DeviceProfileTest::p19_schemaVersionMissingRejected()
{
    const ProfileLoadResult loaded = parse(
        "{\"profileId\":\"id\",\"displayName\":\"n\"}");
    QCOMPARE(loaded.error, ProfileStoreError::MissingSchemaVersion);
    QVERIFY(!loaded.ok());
}

void DeviceProfileTest::p20_schemaVersionTooNewRejected()
{
    QByteArray json = validJson();
    json.replace("\"schemaVersion\":1", "\"schemaVersion\":2");
    const ProfileLoadResult loaded = parse(json);
    QCOMPARE(loaded.error, ProfileStoreError::SchemaVersionTooNew);
    QVERIFY(!loaded.ok());
    // version 0 is unsupported too (not silently treated as v1).
    json.replace("\"schemaVersion\":2", "\"schemaVersion\":0");
    QCOMPARE(parse(json).error, ProfileStoreError::UnsupportedSchemaVersion);
}

void DeviceProfileTest::p21_malformedJsonRejected()
{
    QCOMPARE(parse("not json").error, ProfileStoreError::MalformedJson);
    QCOMPARE(parse("{\"schemaVersion\":1,").error,
             ProfileStoreError::MalformedJson);
    QCOMPARE(parse("[1,2,3]").error, ProfileStoreError::MalformedJson);
    // Structurally wrong entries are refused as well.
    QCOMPARE(parse("{\"schemaVersion\":1,\"profileId\":\"id\","
                   "\"displayName\":\"n\",\"registers\":[{\"address\":\"x\"}]}")
                 .error,
             ProfileStoreError::MalformedJson);
    // Unknown enum tokens never map to a guessed type.
    QByteArray json = validJson();
    json.replace("\"UInt16\"", "\"uint16\"");
    QCOMPARE(parse(json).error, ProfileStoreError::MalformedJson);
}

void DeviceProfileTest::p22_unknownFieldsIgnored()
{
    QByteArray json = validJson();
    json.replace("{\"schemaVersion\"",
                 "{\"futureTopLevel\":{\"x\":1},\"schemaVersion\"");
    json.replace("\"address\":1000",
                 "\"address\":1000,\"vendorNote\":\"ignored\"");
    const ProfileLoadResult loaded = parse(json);
    QVERIFY(loaded.ok());
    QCOMPARE(loaded.profile, makeValidProfile());
    // No round-trip promise for unknown fields: the re-serialized document
    // simply does not contain them.
    QVERIFY(!ProfileStore::serializeToJson(loaded.profile)
                 .contains("futureTopLevel"));
}

void DeviceProfileTest::p23_malformedLoadDoesNotMutateExisting()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString goodPath = dir.filePath(QStringLiteral("good.json"));
    const QString badPath = dir.filePath(QStringLiteral("bad.json"));
    const DeviceProfile profile = makeValidProfile();
    QVERIFY(ProfileStore::saveToFile(profile, goodPath).ok());

    const ProfileLoadResult existing = ProfileStore::loadFromFile(goodPath);
    QVERIFY(existing.ok());

    // The refusal returns a fresh error value; the caller's object is a copy
    // and stays byte-identical (the API is parse->validate->return, there is
    // no partial-application state).
    DeviceProfile retained = existing.profile;
    QFile bad(badPath);
    QVERIFY(bad.open(QIODevice::WriteOnly));
    bad.write("{\"schemaVersion\":1,\"profileId\":\"x\",");
    bad.close();
    const ProfileLoadResult refused = ProfileStore::loadFromFile(badPath);
    QVERIFY(!refused.ok());
    QCOMPARE(refused.error, ProfileStoreError::MalformedJson);
    QCOMPARE(retained, existing.profile);
    QCOMPARE(retained, profile);
    // A save to a path inside a directory that cannot be created also fails
    // cleanly (the caller keeps its validated object untouched).
    QCOMPARE(ProfileStore::saveToFile(profile,
                                      dir.filePath(QStringLiteral("gone/nested/p.json")))
                 .error,
             ProfileStoreError::Ok); // mkpath creates it — allowed
}

void DeviceProfileTest::p24_serializationPathTouchesNoWireOrDecode()
{
    // The canonical raw analog + an M11 decode result taken BEFORE any profile
    // work must be untouched afterwards: the profile path is a pure
    // configuration layer with no reference to wire / decode structures.
    const std::vector<std::uint16_t> rawWords = {0x1234, 0xFFFF, 0x0080};
    const auto before = modbuslens::core::decodeRegisterView(
        rawWords, 0, RegisterDecodeType::UInt32, RegisterByteOrder::Normal,
        RegisterWordOrder::HighWordFirst);

    const DeviceProfile profile = makeValidProfile();
    const QByteArray json = ProfileStore::serializeToJson(profile);
    QVERIFY(parse(json).ok());
    QVERIFY(modbuslens::core::validateDeviceProfile(profile).ok());
    (void)modbuslens::core::profileSemanticValue(4660.0, profile.registers.at(0).scale,
                                                 profile.registers.at(0).offset);

    const auto after = modbuslens::core::decodeRegisterView(
        rawWords, 0, RegisterDecodeType::UInt32, RegisterByteOrder::Normal,
        RegisterWordOrder::HighWordFirst);
    QCOMPARE(rawWords.size(), std::size_t(3));
    QCOMPARE(int(rawWords.at(0)), 0x1234);
    QCOMPARE(int(rawWords.at(1)), 0xFFFF);
    QCOMPARE(int(rawWords.at(2)), 0x0080);
    QCOMPARE(after.status, before.status);
    QCOMPARE(after.text, before.text);
    // words[0..1] = 0x1234FFFF (HighWordFirst) — the generic decode is
    // byte-identical before and after every profile operation above.
    QCOMPARE(QString::fromStdString(after.text),
             QString::number(0x1234FFFFu));
}

void DeviceProfileTest::p25_m11EnumTokensRoundTrip()
{
    // Exact contract spellings (T027 §25).
    QCOMPARE(QString::fromStdString(std::string(
                 modbuslens::core::profileDataTypeToken(RegisterDecodeType::UInt16))),
             QStringLiteral("UInt16"));
    QCOMPARE(QString::fromStdString(std::string(
                 modbuslens::core::profileByteOrderToken(RegisterByteOrder::Normal))),
             QStringLiteral("Normal"));
    QCOMPARE(QString::fromStdString(std::string(
                 modbuslens::core::profileWordOrderToken(RegisterWordOrder::HighWordFirst))),
             QStringLiteral("HighWordFirst"));

    for (const RegisterDecodeType type :
         {RegisterDecodeType::Hex, RegisterDecodeType::Binary,
          RegisterDecodeType::UInt16, RegisterDecodeType::Int16,
          RegisterDecodeType::UInt32, RegisterDecodeType::Int32,
          RegisterDecodeType::Float32}) {
        RegisterDecodeType parsed{};
        QVERIFY(modbuslens::core::profileDataTypeFromToken(
            modbuslens::core::profileDataTypeToken(type), parsed));
        QCOMPARE(parsed, type);
    }
    for (const RegisterByteOrder order :
         {RegisterByteOrder::Normal, RegisterByteOrder::ByteSwapped}) {
        RegisterByteOrder parsed{};
        QVERIFY(modbuslens::core::profileByteOrderFromToken(
            modbuslens::core::profileByteOrderToken(order), parsed));
        QCOMPARE(parsed, order);
    }
    for (const RegisterWordOrder order :
         {RegisterWordOrder::HighWordFirst, RegisterWordOrder::LowWordFirst}) {
        RegisterWordOrder parsed{};
        QVERIFY(modbuslens::core::profileWordOrderFromToken(
            modbuslens::core::profileWordOrderToken(order), parsed));
        QCOMPARE(parsed, order);
    }
    // Unknown tokens are refused, never guessed.
    RegisterDecodeType out{};
    QVERIFY(!modbuslens::core::profileDataTypeFromToken("uint16", out));
    QVERIFY(modbuslens::core::profileDataTypeToken(
                static_cast<RegisterDecodeType>(99))
                .empty());
}

void DeviceProfileTest::p26_semanticFormula()
{
    // Frozen P0-c: decoded * scale + offset.
    const double value =
        modbuslens::core::profileSemanticValue(466.0, 0.1, 0.0);
    QVERIFY(std::abs(value - 46.6) < 1e-9);
}

void DeviceProfileTest::p27_scaleZeroYieldsOffset()
{
    QCOMPARE(modbuslens::core::profileSemanticValue(4660.0, 0.0, 5.0), 5.0);
}

void DeviceProfileTest::p28_offsetApplicationOrder()
{
    // (decoded * scale) + offset, never (decoded + offset) * scale.
    const double value =
        modbuslens::core::profileSemanticValue(466.0, 0.1, 5.0);
    QVERIFY(std::abs(value - 51.6) < 1e-9);
    QVERIFY(std::abs(value - 47.1) > 1e-6);
}

void DeviceProfileTest::p29_specialValuesPreserved()
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    QVERIFY(std::isnan(modbuslens::core::profileSemanticValue(nan, 1.0, 0.0)));
    QVERIFY(std::isnan(modbuslens::core::profileSemanticValue(nan, 0.0, 0.0)));
    QVERIFY(std::isinf(modbuslens::core::profileSemanticValue(inf, 2.0, 0.0)));
    QVERIFY(modbuslens::core::profileSemanticValue(inf, 2.0, 0.0) > 0.0);
    // Non-finite scale / offset never enter through validation.
    DeviceProfile broken = makeValidProfile();
    broken.registers.at(0).scale = nan;
    QCOMPARE(modbuslens::core::validateDeviceProfile(broken).code,
             ProfileValidationCode::NonFiniteScale);
    broken = makeValidProfile();
    broken.registers.at(0).offset = inf;
    QCOMPARE(modbuslens::core::validateDeviceProfile(broken).code,
             ProfileValidationCode::NonFiniteOffset);
}

void DeviceProfileTest::v1_validationTokens()
{
    using modbuslens::core::profileValidationCodeName;
    QCOMPARE(QString::fromStdString(std::string(
                 profileValidationCodeName(ProfileValidationCode::Ok))),
             QStringLiteral("ok"));
    QCOMPARE(QString::fromStdString(std::string(
                 profileValidationCodeName(ProfileValidationCode::RegisterCountMismatch))),
             QStringLiteral("register_count_mismatch"));
    QCOMPARE(QString::fromStdString(std::string(
                 profileValidationCodeName(ProfileValidationCode::DuplicateAddress))),
             QStringLiteral("duplicate_address"));
    QCOMPARE(modbuslens::ui::profileStoreErrorName(ProfileStoreError::SchemaVersionTooNew),
             QStringLiteral("schema_version_too_new"));
    QCOMPARE(modbuslens::ui::profileStoreErrorName(ProfileStoreError::InvalidProfile),
             QStringLiteral("invalid_profile"));
}

void DeviceProfileTest::v2_saveRefusesInvalidProfile()
{
    DeviceProfile broken = makeValidProfile();
    broken.displayName.clear();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("broken.json"));
    QCOMPARE(ProfileStore::saveToFile(broken, path).error,
             ProfileStoreError::InvalidProfile);
    QVERIFY(!QFile::exists(path));
}

QTEST_GUILESS_MAIN(DeviceProfileTest)
#include "test_device_profile.moc"