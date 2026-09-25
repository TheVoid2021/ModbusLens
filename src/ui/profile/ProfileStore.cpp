#include "ui/profile/ProfileStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>

#include <cmath>

namespace modbuslens::ui {

namespace {

using modbuslens::core::DeviceProfile;
using modbuslens::core::ProfileValidationCode;
using modbuslens::core::RegisterEntry;

constexpr int kSupportedSchemaVersion = 1;

// Strict integer extraction: JSON doubles must be exact integers inside the
// int range — a fractional address is never silently truncated.
bool strictInt(const QJsonValue& value, int& out)
{
    if (!value.isDouble()) {
        return false;
    }
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number) {
        return false;
    }
    if (number < -2147483648.0 || number > 2147483647.0) {
        return false;
    }
    out = static_cast<int>(number);
    return true;
}

ProfileLoadResult malformed()
{
    ProfileLoadResult result;
    result.error = ProfileStoreError::MalformedJson;
    return result;
}

ProfileLoadResult invalid(ProfileValidationCode code)
{
    ProfileLoadResult result;
    result.error = ProfileStoreError::InvalidProfile;
    result.validationCode = code;
    return result;
}

ProfileLoadResult parseRegisterEntry(const QJsonObject& object,
                                     RegisterEntry& out)
{
    // Required keys: address, name, dataType, registerCount, byteOrder,
    // wordOrder (v1 shape, T027 §25). Optional: description, scale, offset,
    // unit. Wrong types / unknown enum tokens are structural violations.
    if (!object.contains(QStringLiteral("address"))
        || !object.contains(QStringLiteral("name"))
        || !object.contains(QStringLiteral("dataType"))
        || !object.contains(QStringLiteral("registerCount"))
        || !object.contains(QStringLiteral("byteOrder"))
        || !object.contains(QStringLiteral("wordOrder"))) {
        return malformed();
    }

    int address = 0;
    if (!strictInt(object.value(QStringLiteral("address")), address)) {
        return malformed();
    }
    if (address < 0 || address > 65535) {
        // Out-of-range addresses cannot be represented in the model; the PDU
        // authority range itself is the validation rule.
        return invalid(ProfileValidationCode::AddressOutOfRange);
    }
    out.address = static_cast<std::uint16_t>(address);

    if (!object.value(QStringLiteral("name")).isString()) {
        return malformed();
    }
    out.name = object.value(QStringLiteral("name")).toString().toStdString();

    if (object.contains(QStringLiteral("description"))) {
        if (!object.value(QStringLiteral("description")).isString()) {
            return malformed();
        }
        out.description =
            object.value(QStringLiteral("description")).toString().toStdString();
    }

    if (!object.value(QStringLiteral("dataType")).isString()
        || !modbuslens::core::profileDataTypeFromToken(
            object.value(QStringLiteral("dataType")).toString().toStdString(),
            out.dataType)) {
        return malformed();
    }

    int registerCount = 0;
    if (!strictInt(object.value(QStringLiteral("registerCount")),
                   registerCount)) {
        return malformed();
    }
    out.registerCount = registerCount;

    if (!object.value(QStringLiteral("byteOrder")).isString()
        || !modbuslens::core::profileByteOrderFromToken(
            object.value(QStringLiteral("byteOrder")).toString().toStdString(),
            out.byteOrder)) {
        return malformed();
    }
    if (!object.value(QStringLiteral("wordOrder")).isString()
        || !modbuslens::core::profileWordOrderFromToken(
            object.value(QStringLiteral("wordOrder")).toString().toStdString(),
            out.wordOrder)) {
        return malformed();
    }

    if (object.contains(QStringLiteral("scale"))) {
        if (!object.value(QStringLiteral("scale")).isDouble()) {
            return malformed();
        }
        out.scale = object.value(QStringLiteral("scale")).toDouble();
        if (!std::isfinite(out.scale)) {
            return invalid(ProfileValidationCode::NonFiniteScale);
        }
    }
    if (object.contains(QStringLiteral("offset"))) {
        if (!object.value(QStringLiteral("offset")).isDouble()) {
            return malformed();
        }
        out.offset = object.value(QStringLiteral("offset")).toDouble();
        if (!std::isfinite(out.offset)) {
            return invalid(ProfileValidationCode::NonFiniteOffset);
        }
    }
    if (object.contains(QStringLiteral("unit"))) {
        if (!object.value(QStringLiteral("unit")).isString()) {
            return malformed();
        }
        out.unit = object.value(QStringLiteral("unit")).toString().toStdString();
    }
    // Unknown keys inside an entry are ignored on purpose (T027 §24.2).
    return ProfileLoadResult{};
}

} // namespace

QString profileStoreErrorName(ProfileStoreError error)
{
    switch (error) {
    case ProfileStoreError::Ok:
        return QStringLiteral("ok");
    case ProfileStoreError::FileNotFound:
        return QStringLiteral("file_not_found");
    case ProfileStoreError::ReadFailed:
        return QStringLiteral("read_failed");
    case ProfileStoreError::WriteFailed:
        return QStringLiteral("write_failed");
    case ProfileStoreError::DirectoryCreateFailed:
        return QStringLiteral("directory_create_failed");
    case ProfileStoreError::MalformedJson:
        return QStringLiteral("malformed_json");
    case ProfileStoreError::MissingSchemaVersion:
        return QStringLiteral("missing_schema_version");
    case ProfileStoreError::SchemaVersionTooNew:
        return QStringLiteral("schema_version_too_new");
    case ProfileStoreError::UnsupportedSchemaVersion:
        return QStringLiteral("unsupported_schema_version");
    case ProfileStoreError::InvalidProfile:
        return QStringLiteral("invalid_profile");
    }
    // Defensive: unknown enum values never get a fabricated meaning.
    return QStringLiteral("malformed_json");
}

QString ProfileStore::defaultProfilesDirectory()
{
    // Platform application user-data location (never cwd / install tree).
    const QString base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("profiles"));
}

QString ProfileStore::defaultFilePathFor(const QString& profileId)
{
    // T027 §29.2: the default filename is DERIVED from the profileId (SHA-256
    // of its UTF-8 bytes), never built from it — Qt does not remove ".."
    // segments, so concatenation could escape the profiles root. The digest is
    // hex-only, so the produced path cannot leave the profiles directory.
    const QByteArray digest = QCryptographicHash::hash(
        profileId.toUtf8(), QCryptographicHash::Sha256);
    const QString fileName = QStringLiteral("profile-")
                             + QString::fromLatin1(digest.toHex())
                             + QStringLiteral(".json");
    return QDir(defaultProfilesDirectory()).filePath(fileName);
}

QByteArray ProfileStore::serializeToJson(const DeviceProfile& profile)
{
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), profile.schemaVersion);
    root.insert(QStringLiteral("profileId"),
                QString::fromStdString(profile.profileId));
    root.insert(QStringLiteral("displayName"),
                QString::fromStdString(profile.displayName));
    root.insert(QStringLiteral("manufacturer"),
                QString::fromStdString(profile.manufacturer));
    root.insert(QStringLiteral("model"), QString::fromStdString(profile.model));
    root.insert(QStringLiteral("revision"),
                QString::fromStdString(profile.revision));
    root.insert(QStringLiteral("description"),
                QString::fromStdString(profile.description));

    QJsonArray registers;
    for (const RegisterEntry& entry : profile.registers) {
        QJsonObject object;
        object.insert(QStringLiteral("address"),
                      static_cast<int>(entry.address));
        object.insert(QStringLiteral("name"), QString::fromStdString(entry.name));
        object.insert(QStringLiteral("description"),
                      QString::fromStdString(entry.description));
        object.insert(QStringLiteral("dataType"),
                      QString::fromStdString(std::string(
                          modbuslens::core::profileDataTypeToken(entry.dataType))));
        object.insert(QStringLiteral("registerCount"), entry.registerCount);
        object.insert(QStringLiteral("byteOrder"),
                      QString::fromStdString(std::string(
                          modbuslens::core::profileByteOrderToken(entry.byteOrder))));
        object.insert(QStringLiteral("wordOrder"),
                      QString::fromStdString(std::string(
                          modbuslens::core::profileWordOrderToken(entry.wordOrder))));
        object.insert(QStringLiteral("scale"), entry.scale);
        object.insert(QStringLiteral("offset"), entry.offset);
        object.insert(QStringLiteral("unit"), QString::fromStdString(entry.unit));
        registers.append(object);
    }
    root.insert(QStringLiteral("registers"), registers);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

ProfileLoadResult ProfileStore::parseFromJson(const QByteArray& json)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return malformed();
    }
    const QJsonObject root = document.object();

    if (!root.contains(QStringLiteral("schemaVersion"))) {
        ProfileLoadResult result;
        result.error = ProfileStoreError::MissingSchemaVersion;
        return result;
    }
    int schemaVersion = 0;
    if (!strictInt(root.value(QStringLiteral("schemaVersion")), schemaVersion)) {
        return malformed();
    }
    if (schemaVersion > kSupportedSchemaVersion) {
        // Version-too-new: refuse outright — never downgrade or ignore.
        ProfileLoadResult result;
        result.error = ProfileStoreError::SchemaVersionTooNew;
        return result;
    }
    if (schemaVersion != kSupportedSchemaVersion) {
        ProfileLoadResult result;
        result.error = ProfileStoreError::UnsupportedSchemaVersion;
        return result;
    }

    DeviceProfile profile;
    profile.schemaVersion = schemaVersion;
    if (root.contains(QStringLiteral("profileId"))) {
        if (!root.value(QStringLiteral("profileId")).isString()) {
            return malformed();
        }
        profile.profileId =
            root.value(QStringLiteral("profileId")).toString().toStdString();
    }
    if (root.contains(QStringLiteral("displayName"))) {
        if (!root.value(QStringLiteral("displayName")).isString()) {
            return malformed();
        }
        profile.displayName =
            root.value(QStringLiteral("displayName")).toString().toStdString();
    }
    const struct {
        const char* key;
        std::string* target;
    } optionalStrings[] = {
        {"manufacturer", &profile.manufacturer},
        {"model", &profile.model},
        {"revision", &profile.revision},
        {"description", &profile.description},
    };
    for (const auto& field : optionalStrings) {
        const QString key = QString::fromLatin1(field.key);
        if (root.contains(key)) {
            if (!root.value(key).isString()) {
                return malformed();
            }
            *field.target = root.value(key).toString().toStdString();
        }
    }

    if (root.contains(QStringLiteral("registers"))) {
        if (!root.value(QStringLiteral("registers")).isArray()) {
            return malformed();
        }
        const QJsonArray registers =
            root.value(QStringLiteral("registers")).toArray();
        for (const QJsonValue& value : registers) {
            if (!value.isObject()) {
                return malformed();
            }
            RegisterEntry entry;
            ProfileLoadResult entryResult =
                parseRegisterEntry(value.toObject(), entry);
            if (entryResult.error != ProfileStoreError::Ok) {
                return entryResult;
            }
            profile.registers.push_back(std::move(entry));
        }
    }
    // Unknown top-level keys are ignored on purpose (T027 §24.2).

    const auto validation = modbuslens::core::validateDeviceProfile(profile);
    if (!validation.ok()) {
        return invalid(validation.code);
    }

    ProfileLoadResult result;
    result.profile = std::move(profile);
    return result;
}

ProfileSaveResult ProfileStore::saveToFile(const DeviceProfile& profile,
                                           const QString& filePath)
{
    // Never write a profile that validation refuses: a bad document on disk
    // would surface much later, far from the mistake.
    if (!modbuslens::core::validateDeviceProfile(profile).ok()) {
        ProfileSaveResult result;
        result.error = ProfileStoreError::InvalidProfile;
        return result;
    }

    const QString directory = QFileInfo(filePath).absolutePath();
    if (!directory.isEmpty() && !QDir().mkpath(directory)) {
        ProfileSaveResult result;
        result.error = ProfileStoreError::DirectoryCreateFailed;
        return result;
    }

    // QSaveFile: the previous file survives any failure (atomic commit).
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        ProfileSaveResult result;
        result.error = ProfileStoreError::WriteFailed;
        return result;
    }
    const QByteArray bytes = serializeToJson(profile);
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        ProfileSaveResult result;
        result.error = ProfileStoreError::WriteFailed;
        return result;
    }
    if (!file.commit()) {
        ProfileSaveResult result;
        result.error = ProfileStoreError::WriteFailed;
        return result;
    }
    return ProfileSaveResult{};
}

ProfileLoadResult ProfileStore::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.exists()) {
        ProfileLoadResult result;
        result.error = ProfileStoreError::FileNotFound;
        return result;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        ProfileLoadResult result;
        result.error = ProfileStoreError::ReadFailed;
        return result;
    }
    const QByteArray bytes = file.readAll();
    // Failure is reported, never applied: the caller's existing profile (if
    // any) is untouched because this call only ever returns a fresh value.
    return parseFromJson(bytes);
}

} // namespace modbuslens::ui