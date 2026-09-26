#include "ui/profile/ActiveProfileController.h"

#include "ui/profile/ProfileStore.h"

namespace modbuslens::ui {

using modbuslens::core::DeviceProfile;

namespace {

// Full persisted profile → QVariantMap projection (the accessor the future
// semantic slice will consume). Registers carry every frozen metadata field.
QVariantMap profileToMap(const DeviceProfile &profile)
{
    QVariantMap out;
    out.insert(QStringLiteral("profileId"),
               QString::fromStdString(profile.profileId));
    out.insert(QStringLiteral("displayName"),
               QString::fromStdString(profile.displayName));
    out.insert(QStringLiteral("manufacturer"),
               QString::fromStdString(profile.manufacturer));
    out.insert(QStringLiteral("model"),
               QString::fromStdString(profile.model));
    out.insert(QStringLiteral("revision"),
               QString::fromStdString(profile.revision));
    out.insert(QStringLiteral("description"),
               QString::fromStdString(profile.description));
    QVariantList registers;
    registers.reserve(static_cast<qsizetype>(profile.registers.size()));
    for (const auto &entry : profile.registers) {
        QVariantMap row;
        row.insert(QStringLiteral("readFunctionCode"),
                   static_cast<int>(entry.readFunctionCode));
        row.insert(QStringLiteral("address"),
                   static_cast<int>(entry.address));
        row.insert(QStringLiteral("name"),
                   QString::fromStdString(entry.name));
        row.insert(QStringLiteral("description"),
                   QString::fromStdString(entry.description));
        row.insert(QStringLiteral("dataType"),
                   QString::fromLatin1(modbuslens::core::profileDataTypeToken(
                       entry.dataType)));
        row.insert(QStringLiteral("registerCount"), entry.registerCount);
        row.insert(QStringLiteral("byteOrder"),
                   QString::fromLatin1(modbuslens::core::profileByteOrderToken(
                       entry.byteOrder)));
        row.insert(QStringLiteral("wordOrder"),
                   QString::fromLatin1(modbuslens::core::profileWordOrderToken(
                       entry.wordOrder)));
        row.insert(QStringLiteral("scale"), entry.scale);
        row.insert(QStringLiteral("offset"), entry.offset);
        row.insert(QStringLiteral("unit"),
                   QString::fromStdString(entry.unit));
        registers.append(row);
    }
    out.insert(QStringLiteral("registers"), registers);
    return out;
}

} // namespace

ActiveProfileController::ActiveProfileController(QObject *parent)
    : QObject(parent)
{
}

ProfileController *ActiveProfileController::profileController() const
{
    return m_profileController;
}

void ActiveProfileController::setProfileController(ProfileController *controller)
{
    if (m_profileController == controller) {
        return;
    }
    if (m_catalogConnection) {
        disconnect(m_catalogConnection);
        m_catalogConnection = QMetaObject::Connection();
    }
    m_profileController = controller;
    if (m_profileController) {
        // Catalog refreshes drive BOTH invalidation (§6: active id gone →
        // clear) and persisted-content refresh (§5: same id re-saved →
        // re-resolve). A delete that FAILED never emits catalogChanged, so
        // the active selection survives it (§12).
        m_catalogConnection = connect(m_profileController,
                                      &ProfileController::catalogChanged, this,
                                      &ActiveProfileController::
                                          revalidateAgainstCatalog);

        revalidateAgainstCatalog();
    }
    emit profileControllerChanged();
    emitActiveChanged();
}

bool ActiveProfileController::hasActiveProfile() const
{
    return !m_activeProfileId.isEmpty();
}

QString ActiveProfileController::activeProfileId() const
{
    return m_activeProfileId;
}

QVariantMap ActiveProfileController::activeProfile() const
{
    return m_activeProfile;
}

QString ActiveProfileController::activeError() const
{
    return m_activeError;
}

bool ActiveProfileController::catalogContains(const QString &profileId) const
{
    if (!m_profileController) {
        return false;
    }
    const QVariantList catalog = m_profileController->profileCatalog();
    for (const QVariant &row : catalog) {
        if (row.toMap().value(QStringLiteral("profileId")).toString()
            == profileId) {
            return true;
        }
    }
    return false;
}

bool ActiveProfileController::resolvePersistedContent()
{
    const auto loaded = ProfileStore::loadFromFile(
        ProfileStore::defaultFilePathFor(m_activeProfileId));
    if (!loaded.ok()) {
        return false;
    }
    m_activeProfile = profileToMap(loaded.profile);
    m_activeCoreProfile = loaded.profile;
    return true;
}

const modbuslens::core::DeviceProfile& ActiveProfileController::
    activeCoreProfile() const
{
    return m_activeCoreProfile;
}

void ActiveProfileController::setActive(const QString &profileId,
                                        const QVariantMap &profile)
{
    m_activeProfileId = profileId;
    m_activeProfile = profile;
    m_activeError.clear();
    emitActiveChanged();
}

void ActiveProfileController::clearActiveState()
{
    m_activeProfileId.clear();
    m_activeProfile = QVariantMap{};
    m_activeCoreProfile = modbuslens::core::DeviceProfile{};
    m_activeError.clear();
}

void ActiveProfileController::emitActiveChanged()
{
    emit activeChanged();
}

bool ActiveProfileController::selectProfile(const QString &profileId)
{
    m_activeError.clear();
    if (profileId.isEmpty()) {
        // The selector's "未选择设备档案" entry.
        const bool hadActive = hasActiveProfile();
        clearActiveState();
        if (hadActive) {
            emitActiveChanged();
        } else {
            emitActiveChanged(); // error text may have been cleared
        }
        return true;
    }
    if (!m_profileController) {
        m_activeError = QStringLiteral("设备档案列表不可用");
        emitActiveChanged();
        return false;
    }
    // Only a profile the catalog lists (valid + loadable at scan time) can be
    // selected — malformed/unloadable files never enter the valid list.
    if (!catalogContains(profileId)) {
        m_activeError = QStringLiteral("设备档案不存在或无法加载");
        emitActiveChanged();
        return false;
    }
    // Re-resolve the persisted content NOW: the active content is the
    // persisted profile, never an editor draft.
    const QString previousId = m_activeProfileId;
    m_activeProfileId = profileId;
    if (!resolvePersistedContent()) {
        m_activeProfileId = previousId;
        m_activeError = QStringLiteral("设备档案读取失败");
        if (m_activeProfileId.isEmpty()) {
            m_activeProfile = QVariantMap{};
        }
        emitActiveChanged();
        return false;
    }
    emitActiveChanged();
    return true;
}

void ActiveProfileController::clearActive()
{
    m_activeError.clear();
    const bool hadActive = hasActiveProfile();
    clearActiveState();
    if (hadActive || !m_activeError.isEmpty()) {
        emitActiveChanged();
    }
}

void ActiveProfileController::revalidateAgainstCatalog()
{
    if (!hasActiveProfile()) {
        return;
    }
    if (!catalogContains(m_activeProfileId)) {
        // Deleted, unloadable, or otherwise gone: No Profile Selected — no
        // stale cache, no fallback, no auto-pick-first (§6).
        clearActiveState();
        emitActiveChanged();
        return;
    }
    // Same identity, possibly a NEW persisted version (the Human saved it in
    // the editor). This is a content refresh, not a selection change.
    if (!resolvePersistedContent()) {
        clearActiveState();
        emitActiveChanged();
        return;
    }
    emitActiveChanged();
}

} // namespace modbuslens::ui
