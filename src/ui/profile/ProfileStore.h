#pragma once

#include <QByteArray>
#include <QString>

#include "core/profile/DeviceProfile.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-A first slice: Device Profile persistence (Qt side).
//
// The domain model + validation live in the Zero-Qt core
// (core/profile/DeviceProfile.h) so every future surface shares them; this
// layer owns exactly the parts that need Qt: the JSON contract encoding
// (QJsonDocument) and file storage (QSaveFile / QStandardPaths).
//
// Frozen semantics (T027 §24.2):
//   · one JSON file = one DeviceProfile; schemaVersion is mandatory (= 1);
//   · a newer schemaVersion is refused (never silently downgraded or ignored);
//   · malformed input is refused WITHOUT touching any existing object — both
//     entry points return a fresh result value and never mutate a caller's
//     profile (the load path is parse -> build candidate -> validate ->
//     return; there is no partial-application state to corrupt);
//   · unknown JSON fields are ignored (no round-trip preservation promised);
//   · saving is an explicit, atomic, manual operation (QSaveFile commit);
//   · the default location is the platform application user-data directory
//     with a "profiles" subdirectory — never a hard-coded user path, never
//     the install tree, never the current working directory.
// ---------------------------------------------------------------------------

enum class ProfileStoreError {
    Ok,
    FileNotFound,            // load: path does not exist
    ReadFailed,              // load: file exists but cannot be read
    WriteFailed,             // save: QSaveFile could not write/commit
    DirectoryCreateFailed,   // save: the target directory could not be created
    MalformedJson,           // invalid JSON, wrong types, missing required keys
    MissingSchemaVersion,    // schemaVersion key absent
    SchemaVersionTooNew,     // schemaVersion > supported (version-too-new)
    UnsupportedSchemaVersion, // schemaVersion present but not a supported value
    InvalidProfile,          // parsed, but validation refused it (see validationCode)
};

// Stable machine tokens ("schema_version_too_new", ...) — never UI prose.
[[nodiscard]] QString profileStoreErrorName(ProfileStoreError error);

struct ProfileSaveResult {
    ProfileStoreError error{ProfileStoreError::Ok};

    [[nodiscard]] bool ok() const { return error == ProfileStoreError::Ok; }
};

struct ProfileLoadResult {
    ProfileStoreError error{ProfileStoreError::Ok};
    // Only meaningful when error == Ok (a refused load never yields an object).
    modbuslens::core::DeviceProfile profile;
    // When error == InvalidProfile: which validation rule refused it.
    modbuslens::core::ProfileValidationCode validationCode{
        modbuslens::core::ProfileValidationCode::Ok};

    [[nodiscard]] bool ok() const { return error == ProfileStoreError::Ok; }
};

class ProfileStore
{
public:
    // <application user-data dir>/profiles — absolute, user-scoped, created on
    // demand by saveToFile (never at query time).
    [[nodiscard]] static QString defaultProfilesDirectory();
    // defaultProfilesDirectory() + "/" + profileId + ".json". The profileId is
    // expected to be a program-generated identity (e.g. a UUID); this helper
    // does not sanitize it and is NOT a security boundary.
    [[nodiscard]] static QString defaultFilePathFor(const QString& profileId);

    // JSON contract encoding (T027 §25). Deterministic: every v1 key is always
    // written and QJsonObject sorts keys, so save->load->save is byte-stable.
    [[nodiscard]] static QByteArray serializeToJson(
        const modbuslens::core::DeviceProfile& profile);

    // Parse + validate a JSON document into a NEW profile. Never mutates
    // anything: unknown fields are ignored, any failure is reported as an
    // error token with no partial object.
    [[nodiscard]] static ProfileLoadResult parseFromJson(const QByteArray& json);

    // Validate, then write atomically via QSaveFile (the previous file, if
    // any, survives any failure). Creates the parent directory when missing.
    [[nodiscard]] static ProfileSaveResult saveToFile(
        const modbuslens::core::DeviceProfile& profile,
        const QString& filePath);

    // Read + parseFromJson. A refused load leaves the caller's existing state
    // untouched by construction (this call returns a fresh value).
    [[nodiscard]] static ProfileLoadResult loadFromFile(const QString& filePath);
};

} // namespace modbuslens::ui