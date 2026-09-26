#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include "ui/profile/ProfileController.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-B third slice (T027 §31.3/§33.3): the SESSION-LEVEL Active Profile —
// the one profile the Communication workspace will use for semantic
// interpretation in a later slice.
//
// Contract boundaries (frozen):
//   · Editor ≠ Active: selecting/creating/editing profiles in the editor
//     NEVER changes this state; this controller never touches the editor.
//   · Identity = the FULL profileId, nothing else (never displayName, never
//     the filename hash, never a list index).
//   · Content = the CURRENT persisted profile resolved through the managed
//     catalog; an unsaved editor draft can never leak into it. A successful
//     save of the active profile refreshes the persisted content under the
//     SAME identity.
//   · Lifecycle: Human-selected during the application process only. Startup
//     = No Profile Selected. Nothing is persisted anywhere (no QSettings, no
//     state JSON, no restore-last).
//   · Invalidation: after a catalog refresh, an active id that is no longer
//     in the catalog (deleted / unloadable) clears the selection — no stale
//     cache, no fallback to another profile, no auto-pick-first.
//   · No automatic binding whatsoever (COM / slave / function code /
//     response / manufacturer·model).
// ---------------------------------------------------------------------------

class ActiveProfileController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // The catalog owner this controller reads identities from and listens to
    // for refreshes. Injection keeps the two concerns separate: this class
    // never edits profiles and ProfileController never knows about active
    // selection.
    Q_PROPERTY(modbuslens::ui::ProfileController *profileController
                   READ profileController WRITE setProfileController
                       NOTIFY profileControllerChanged)
    Q_PROPERTY(bool hasActiveProfile READ hasActiveProfile NOTIFY activeChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY activeChanged)
    // Full persisted profile of the active identity (identity fields plus the
    // complete register metadata), re-resolved on every catalog refresh.
    // Empty map while No Profile Selected.
    Q_PROPERTY(QVariantMap activeProfile READ activeProfile NOTIFY activeChanged)
    // Human-readable reason of the last FAILED selection attempt; empty
    // otherwise. A failed selection never fabricates an active state.
    Q_PROPERTY(QString activeError READ activeError NOTIFY activeChanged)

public:
    explicit ActiveProfileController(QObject *parent = nullptr);

    [[nodiscard]] ProfileController *profileController() const;
    void setProfileController(ProfileController *controller);

    [[nodiscard]] bool hasActiveProfile() const;
    [[nodiscard]] QString activeProfileId() const;
    [[nodiscard]] QVariantMap activeProfile() const;
    [[nodiscard]] QString activeError() const;
    // The persisted CORE profile of the active identity (M12-B slice 4): the
    // exact object the frozen lookup/projection APIs consume. Empty when No
    // Profile Selected. Never an editor draft.
    [[nodiscard]] const modbuslens::core::DeviceProfile& activeCoreProfile()
        const;

    // Selects a profile by its FULL profileId. The id must be a VALID profile
    // currently listed in the managed catalog; the persisted file must load
    // and validate. Returns false (with activeError set, active state
    // untouched) otherwise. An empty id clears the selection.
    Q_INVOKABLE bool selectProfile(const QString &profileId);
    Q_INVOKABLE void clearActive();

signals:
    void profileControllerChanged();
    void activeChanged();

private:
    void revalidateAgainstCatalog();
    [[nodiscard]] bool catalogContains(const QString &profileId) const;
    [[nodiscard]] bool resolvePersistedContent();
    void setActive(const QString &profileId, const QVariantMap &profile);
    void clearActiveState();
    void emitActiveChanged();

    ProfileController *m_profileController{nullptr};
    QMetaObject::Connection m_catalogConnection;
    QString m_activeProfileId;
    QVariantMap m_activeProfile;
    modbuslens::core::DeviceProfile m_activeCoreProfile{};
    QString m_activeError;
};

} // namespace modbuslens::ui
