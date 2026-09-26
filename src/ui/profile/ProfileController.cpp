#include "ui/profile/ProfileController.h"

#include "ui/profile/ProfileStore.h"

#include <QDir>
#include <QFileInfo>
#include <QUuid>

namespace modbuslens::ui {

using modbuslens::core::DeviceProfile;

namespace {

QString joinSecondary(const modbuslens::core::DeviceProfile &profile)
{
    QStringList parts;
    for (const std::string *field : {&profile.manufacturer, &profile.model,
                                     &profile.revision}) {
        const QString value = QString::fromStdString(*field);
        if (!value.isEmpty()) {
            parts << value;
        }
    }
    return parts.join(QStringLiteral(" · "));
}

} // namespace

ProfileController::ProfileController(QObject *parent) : QObject(parent)
{
    refreshCatalog();
}

QVariantList ProfileController::profileCatalog() const
{
    return m_catalog;
}

QString ProfileController::catalogIssueText() const
{
    if (m_catalogIssueCount == 0) {
        return QString();
    }
    return QStringLiteral("有 %1 个设备档案无法加载").arg(m_catalogIssueCount);
}

int ProfileController::catalogIssueCount() const
{
    return m_catalogIssueCount;
}

bool ProfileController::hasOpenProfile() const
{
    return m_hasOpenProfile;
}

QString ProfileController::currentProfileId() const
{
    return QString::fromStdString(m_draft.profileId);
}

QString ProfileController::displayName() const
{
    return QString::fromStdString(m_draft.displayName);
}

void ProfileController::setDisplayName(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.displayName == converted) {
        return;
    }
    m_draft.displayName = converted;
    emitEditorChanged();
}

QString ProfileController::manufacturer() const
{
    return QString::fromStdString(m_draft.manufacturer);
}

void ProfileController::setManufacturer(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.manufacturer == converted) {
        return;
    }
    m_draft.manufacturer = converted;
    emitEditorChanged();
}

QString ProfileController::model() const
{
    return QString::fromStdString(m_draft.model);
}

void ProfileController::setModel(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.model == converted) {
        return;
    }
    m_draft.model = converted;
    emitEditorChanged();
}

QString ProfileController::revision() const
{
    return QString::fromStdString(m_draft.revision);
}

void ProfileController::setRevision(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.revision == converted) {
        return;
    }
    m_draft.revision = converted;
    emitEditorChanged();
}

QString ProfileController::description() const
{
    return QString::fromStdString(m_draft.description);
}

void ProfileController::setDescription(const QString &value)
{
    const std::string converted = value.toStdString();
    if (m_draft.description == converted) {
        return;
    }
    m_draft.description = converted;
    emitEditorChanged();
}

bool ProfileController::dirty() const
{
    return m_hasOpenProfile && m_draft != m_persisted;
}

QString ProfileController::validationText() const
{
    if (!m_hasOpenProfile) {
        return QString();
    }
    const auto validation = modbuslens::core::validateDeviceProfile(m_draft);
    if (validation.code == modbuslens::core::ProfileValidationCode::Ok) {
        return QString();
    }
    if (validation.code
        == modbuslens::core::ProfileValidationCode::DisplayNameMissing) {
        return QStringLiteral("设备名称必填");
    }
    return QStringLiteral("设备档案校验未通过（%1）")
        .arg(QString::fromLatin1(
            modbuslens::core::profileValidationCodeName(validation.code)));
}

QString ProfileController::lastActionError() const
{
    return m_lastActionError;
}

void ProfileController::refreshCatalog()
{
    m_catalog.clear();
    m_catalogIssueCount = 0;
    m_catalogIssueNames.clear();

    const QString dir = ProfileStore::managedProfilesDirectory();
    const QFileInfoList files =
        QDir(dir)
            .entryInfoList(QStringList{QStringLiteral("profile-*.json")},
                           QDir::Files, QDir::Name | QDir::IgnoreCase);

    struct CatalogRow {
        QString profileId;
        QString displayPrimary;
        QString displaySecondary;
    };
    QVector<CatalogRow> rows;
    for (const QFileInfo &info : files) {
        const ProfileLoadResult loaded = ProfileStore::loadFromFile(info.absoluteFilePath());
        if (!loaded.ok()) {
            // A bad file never blocks the workspace and is never deleted or
            // repaired: it surfaces as a countable, observable issue.
            ++m_catalogIssueCount;
            m_catalogIssueNames << info.fileName();
            continue;
        }
        CatalogRow row;
        row.profileId = QString::fromStdString(loaded.profile.profileId);
        row.displayPrimary =
            QString::fromStdString(loaded.profile.displayName);
        row.displaySecondary = joinSecondary(loaded.profile);
        rows.push_back(row);
    }

    // Deterministic listing: displayName, then profileId.
    std::sort(rows.begin(), rows.end(),
              [](const CatalogRow &a, const CatalogRow &b) {
                  if (a.displayPrimary != b.displayPrimary) {
                      return a.displayPrimary < b.displayPrimary;
                  }
                  return a.profileId < b.profileId;
              });

    // Duplicate (primary, secondary) pairs get a short profileId appended so
    // two different identities can always be told apart (T027 §33.4); the
    // internal identity is the FULL profileId either way.
    for (int i = 1; i < rows.size(); ++i) {
        if (rows.at(i).displayPrimary == rows.at(i - 1).displayPrimary
            && rows.at(i).displaySecondary == rows.at(i - 1).displaySecondary) {
            const QString shortId = rows.at(i).profileId.left(8);
            if (!rows.at(i).displaySecondary.isEmpty()) {
                rows[i].displaySecondary += QStringLiteral(" · ");
            }
            rows[i].displaySecondary += shortId;
            if (rows.at(i - 1).displaySecondary == rows.at(i).displaySecondary) {
                rows[i - 1].displaySecondary += QStringLiteral(" · ")
                                               + rows.at(i - 1).profileId;
                rows[i].displaySecondary += QStringLiteral(" · ")
                                            + rows.at(i).profileId;
            }
        }
    }

    for (const CatalogRow &row : rows) {
        QVariantMap entry;
        entry.insert(QStringLiteral("profileId"), row.profileId);
        entry.insert(QStringLiteral("displayPrimary"), row.displayPrimary);
        entry.insert(QStringLiteral("displaySecondary"), row.displaySecondary);
        m_catalog.append(entry);
    }
    emit catalogChanged();
}

void ProfileController::newProfile()
{
    // QUuid is an implementation detail (T027 §24.1): the JSON keeps the
    // logical profileId, and the managed filename derives from it.
    const QString profileId =
        QUuid::createUuid().toString(QUuid::WithoutBraces);
    DeviceProfile profile;
    profile.schemaVersion = 1;
    profile.profileId = profileId.toStdString();
    setDraftFrom(profile);
}

void ProfileController::discardCurrentChanges()
{
    if (!m_hasOpenProfile) {
        return;
    }
    m_draft = m_persisted;
    emitEditorChanged();
}

bool ProfileController::saveCurrent()
{
    m_lastActionError.clear();
    if (!m_hasOpenProfile) {
        m_lastActionError = QStringLiteral("没有打开的设备档案");
        emitEditorChanged();
        return false;
    }
    const auto validation = modbuslens::core::validateDeviceProfile(m_draft);
    if (!validation.ok()) {
        m_lastActionError = validationText();
        emitEditorChanged();
        return false;
    }
    const QString profileId = QString::fromStdString(m_draft.profileId);
    const ProfileSaveResult saved = ProfileStore::saveToFile(
        m_draft, ProfileStore::defaultFilePathFor(profileId));
    if (!saved.ok()) {
        m_lastActionError = QStringLiteral("保存失败（%1）")
                                .arg(profileStoreErrorName(saved.error));
        emitEditorChanged();
        return false;
    }
    m_persisted = m_draft;
    m_lastActionError.clear();
    refreshCatalog();
    emitEditorChanged();
    return true;
}

bool ProfileController::openProfile(const QString &profileId)
{
    m_lastActionError.clear();
    // openFromDisk only ever applies a fully loaded + validated profile; a
    // refused open leaves the current draft untouched.
    const bool opened = openFromDisk(profileId);
    emitEditorChanged();
    return opened;
}

bool ProfileController::deleteProfile(const QString &profileId)
{
    m_lastActionError.clear();
    const QString path = ProfileStore::defaultFilePathFor(profileId);
    QFile file(path);
    if (!file.exists()) {
        m_lastActionError = QStringLiteral("设备档案不存在");
        emitEditorChanged();
        return false;
    }
    if (!file.remove()) {
        m_lastActionError = QStringLiteral("删除失败（%1）")
                                .arg(file.errorString());
        emitEditorChanged();
        return false;
    }
    // Deleting the currently open profile clears the editor to
    // No Profile Opened (raw truth / M11 decode are untouched by design).
    if (m_hasOpenProfile
        && m_draft.profileId == profileId.toStdString()) {
        clearEditor();
    }
    refreshCatalog();
    emitEditorChanged();
    return true;
}

void ProfileController::setDraftFrom(const DeviceProfile &profile)
{
    m_persisted = profile;
    m_draft = profile;
    m_hasOpenProfile = true;
    m_lastActionError.clear();
    emitEditorChanged();
}

void ProfileController::clearEditor()
{
    m_persisted = DeviceProfile{};
    m_draft = DeviceProfile{};
    m_hasOpenProfile = false;
    m_lastActionError.clear();
    emitEditorChanged();
}

void ProfileController::emitEditorChanged()
{
    emit editorChanged();
}

bool ProfileController::openFromDisk(const QString &profileId)
{
    const ProfileLoadResult loaded = ProfileStore::loadFromFile(
        ProfileStore::defaultFilePathFor(profileId));
    if (!loaded.ok()) {
        m_lastActionError = QStringLiteral("打开失败（%1）")
                                .arg(profileStoreErrorName(loaded.error));
        return false;
    }
    setDraftFrom(loaded.profile);
    return true;
}

} // namespace modbuslens::ui