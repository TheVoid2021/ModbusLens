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
    // Machine-stable companion of lastActionError (the frozen core token, e.g.
    // "duplicate_address"): the QML shows the human text, the tests pin the
    // token. Empty whenever the last action succeeded.
    Q_PROPERTY(QString lastActionErrorToken READ lastActionErrorToken NOTIFY editorChanged)
    // The current draft's register map as a deterministic display projection
    // (sorted by readFunctionCode, then address — T027 §17): each row carries
    // its DRAFT index (the editing locator, stable across re-sorts) plus the
    // summary columns the list shows.
    Q_PROPERTY(QVariantList registerMap READ registerMap NOTIFY editorChanged)

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
    [[nodiscard]] QString lastActionErrorToken() const;
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

    // ------------------------------------------------------------------
    // Register Map editor (M12-B second slice, T027 §31/§33 Group 1-B).
    // The CURRENT DRAFT is the only editing truth: every mutation goes
    // through a candidate copy of the whole draft + the frozen profile
    // validation, and only a VALID candidate is committed back. A failed
    // add/edit leaves the draft byte-identical (human text in lastActionError,
    // frozen token in lastActionErrorToken). registerCount is NOT an input:
    // it is derived from dataType (Group 1-B) exactly as validation requires.
    // ------------------------------------------------------------------

    // Display projection of m_draft.registers (see the property comment).
    [[nodiscard]] QVariantList registerMap() const;
    // Full metadata of one draft entry (the entry editor dialog's init data).
    // Returns an empty map when the index is out of range or nothing is open.
    Q_INVOKABLE QVariantMap registerEntryAt(int draftIndex);
    // Parses the raw field texts (QML hands over exactly what the user typed,
    // M10 authority discipline), builds a candidate RegisterEntry, adds it to
    // a candidate copy of the draft and commits only on full validation.
    Q_INVOKABLE bool addRegisterEntry(const QVariantMap &fields);
    // Same candidate discipline, replacing the draft entry at draftIndex.
    // A -1 wordOrder means "not applicable / unchanged" (the 1-word wordOrder
    // control is disabled and must not pollute the data).
    Q_INVOKABLE bool editRegisterEntry(int draftIndex,
                                       const QVariantMap &fields);
    // Removes the draft entry at draftIndex (draft-only mutation; the managed
    // JSON changes only through Save). Discard restores the persisted map.
    Q_INVOKABLE bool removeRegisterEntry(int draftIndex);
    // Clears the last action error/token (the entry editor calls it when it
    // opens, so a stale rejection text never pre-fills the dialog).
    Q_INVOKABLE void clearActionError();

    // ------------------------------------------------------------------
    // M12-C C3 controlled candidate write (T027 §81, C3-H2/H10). The ONLY
    // entry point through which an AI Candidate value may reach the editor
    // draft. Same candidate-copy discipline as the register editor: the whole
    // draft is copied, exactly one supported field is applied, the FULL
    // profile is validated, and only a valid copy is committed. No Save, no
    // auto-persist, no second pipeline. v1 whitelist = {"manufacturer"} (the
    // only target the C2 first slice can produce); anything else is an
    // explicit refusal with the draft untouched.
    // ------------------------------------------------------------------
    Q_INVOKABLE bool applyCandidateField(const QString &fieldToken,
                                         const QString &value);

signals:
    void catalogChanged();
    void editorChanged();

private:
    void setDraftFrom(const modbuslens::core::DeviceProfile &profile);
    void clearEditor();
    void emitEditorChanged();
    [[nodiscard]] bool openFromDisk(const QString &profileId);
    // Parses the raw editor fields into a RegisterEntry (registerCount derived
    // from dataType; empty scale/offset/unit fall back to the frozen defaults
    // 1 / 0 / ""). On any parse failure: false + human text + token, no entry.
    [[nodiscard]] bool parseEntryFields(const QVariantMap &fields,
                                        modbuslens::core::RegisterEntry &out);
    // Human-readable validation failure text (T027 §24: never a bare token).
    [[nodiscard]] QString validationHumanText(
        const modbuslens::core::DeviceProfile &candidate,
        const modbuslens::core::ProfileValidationResult &validation) const;

    modbuslens::core::DeviceProfile m_persisted; // last created/loaded/saved
    modbuslens::core::DeviceProfile m_draft;     // working copy
    bool m_hasOpenProfile{false};
    QVariantList m_catalog;
    int m_catalogIssueCount{0};
    QStringList m_catalogIssueNames;
    QString m_lastActionError;
    QString m_lastActionErrorToken;
};

} // namespace modbuslens::ui