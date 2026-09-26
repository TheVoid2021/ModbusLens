#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQml/qqml.h>

#include "core/profile/DeviceProfile.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-B first slice: Device Profile workspace controller (T027 §31/§33).
//
// Owns the managed catalog and the CURRENT EDITOR DRAFT. Contract boundaries:
//   · Editor ≠ Active: nothing here selects an Active Profile or touches the
//     Communication page — the Active Profile service does not exist yet.
//   · The draft is a UI-layer working copy; the canonical DeviceProfile is
//     only produced by validation + save. Opening a file that fails never
//     touches the draft (load returns a fresh value and is applied only on
//     success).
//   · dirty = draft differs from the last persisted/created state
//     (m_persisted) — computed, never a stale flag.
//   · readFunctionCode: new drafts default to the INVALID sentinel 0; the
//     register-map editor (a later slice) must assign it explicitly. The
//     identity editor in this slice does not expose it.
//   · No register-map editing, no AI, no import, no Q&A (hard slice boundary).
// ---------------------------------------------------------------------------

class ProfileController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList profileCatalog READ profileCatalog NOTIFY catalogChanged)
    Q_PROPERTY(QString catalogIssueText READ catalogIssueText NOTIFY catalogChanged)
    Q_PROPERTY(int catalogIssueCount READ catalogIssueCount NOTIFY catalogChanged)
    Q_PROPERTY(bool hasOpenProfile READ hasOpenProfile NOTIFY editorChanged)
    Q_PROPERTY(QString currentProfileId READ currentProfileId NOTIFY editorChanged)
    Q_PROPERTY(QString displayName READ displayName WRITE setDisplayName NOTIFY editorChanged)
    Q_PROPERTY(QString manufacturer READ manufacturer WRITE setManufacturer NOTIFY editorChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY editorChanged)
    Q_PROPERTY(QString revision READ revision WRITE setRevision NOTIFY editorChanged)
    Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY editorChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY editorChanged)
    Q_PROPERTY(QString validationText READ validationText NOTIFY editorChanged)
    Q_PROPERTY(QString lastActionError READ lastActionError NOTIFY editorChanged)

public:
    explicit ProfileController(QObject *parent = nullptr);

    [[nodiscard]] QVariantList profileCatalog() const;
    [[nodiscard]] QString catalogIssueText() const;
    [[nodiscard]] int catalogIssueCount() const;
    [[nodiscard]] bool hasOpenProfile() const;
    [[nodiscard]] QString currentProfileId() const;
    [[nodiscard]] QString displayName() const;
    void setDisplayName(const QString &value);
    [[nodiscard]] QString manufacturer() const;
    void setManufacturer(const QString &value);
    [[nodiscard]] QString model() const;
    void setModel(const QString &value);
    [[nodiscard]] QString revision() const;
    void setRevision(const QString &value);
    [[nodiscard]] QString description() const;
    void setDescription(const QString &value);
    [[nodiscard]] bool dirty() const;
    // "" when the draft validates; otherwise the first frozen validation rule
    // it breaks (e.g. the required displayName is empty).
    [[nodiscard]] QString validationText() const;
    [[nodiscard]] QString lastActionError() const;

    // Rescans the managed root (valid profiles + unloadable-file issues).
    Q_INVOKABLE void refreshCatalog();
    // Creates a fresh in-memory draft with a program-generated stable
    // profileId (QUuid, implementation detail) and empty identity; never
    // auto-activates anything and never writes to disk.
    Q_INVOKABLE void newProfile();
    // Reverts the draft to the last persisted/created state (Discard).
    Q_INVOKABLE void discardCurrentChanges();
    // Validates + writes the draft to its hash-derived managed file. Returns
    // false on failure (lastActionError carries the reason); dirty stays true.
    Q_INVOKABLE bool saveCurrent();
    // Opens a managed profile by its logical identity. Returns false when the
    // profile cannot be loaded (missing / malformed / invalid); the current
    // draft stays untouched in that case.
    Q_INVOKABLE bool openProfile(const QString &profileId);
    // Removes the managed JSON of profileId. Returns false on failure. If the
    // deleted profile is the one currently open, the editor clears to
    // No Profile Opened.
    Q_INVOKABLE bool deleteProfile(const QString &profileId);

signals:
    void catalogChanged();
    void editorChanged();

private:
    void setDraftFrom(const modbuslens::core::DeviceProfile &profile);
    void clearEditor();
    void emitEditorChanged();
    [[nodiscard]] bool openFromDisk(const QString &profileId);

    modbuslens::core::DeviceProfile m_persisted; // last created/loaded/saved
    modbuslens::core::DeviceProfile m_draft;     // working copy
    bool m_hasOpenProfile{false};
    QVariantList m_catalog;
    int m_catalogIssueCount{0};
    QStringList m_catalogIssueNames;
    QString m_lastActionError;
};

} // namespace modbuslens::ui