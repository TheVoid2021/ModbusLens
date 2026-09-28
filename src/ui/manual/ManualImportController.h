#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqml.h>

#include <vector>

#include "core/manual/ManualDocument.h"
#include "ui/manual/ManualStore.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C1a/C1b: the QML-facing Manual Import controller.
//
// Contract boundaries (T027 §48, §60):
//   · DETERMINISTIC ONLY: no AI, no network, no credential, no cloud. This
//     class never touches QNetworkAccessManager, any provider client, any
//     environment secret, and never constructs a Candidate.
//   · PROFILE ISOLATION: it owns no ProfileController / ActiveProfileController
//     reference by construction — a manual import can therefore never change
//     the editor draft, the dirty state, the Active Profile or a persisted
//     DeviceProfile JSON.
//   · NO AUTO-BINDING: it never selects, infers or binds a profile — not by
//     COM port, slave address, function code or manufacturer/model.
//   · FORMATS (T027 §60, HUMAN-APPROVED): TXT / Markdown (C1a semantics ZERO
//     CHANGE) plus PDF (existing text layer only, never OCR) and DOCX (main
//     document story) routed by extension. A PDF without a text layer imports
//     SUCCESSFULLY and is surfaced through previewStateToken =
//     "no_extractable_text" — never a fake empty-success preview.
// ---------------------------------------------------------------------------

class ManualImportController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList manualDocuments READ manualDocuments
                   NOTIFY documentsChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedDocument READ selectedDocument
                   NOTIFY selectionChanged)
    Q_PROPERTY(QString previewText READ previewText NOTIFY selectionChanged)
    // M12-C C1b second slice (T027 §60.4): "text" | "no_extractable_text".
    // The UI must render an explicit no-text state for the latter — a blank
    // generic preview would fake an ordinary extraction success.
    Q_PROPERTY(QString previewStateToken READ previewStateToken
                   NOTIFY selectionChanged)
    Q_PROPERTY(QString lastErrorText READ lastErrorText
                   NOTIFY documentsChanged)
    Q_PROPERTY(QString lastErrorToken READ lastErrorToken
                   NOTIFY documentsChanged)
    Q_PROPERTY(QString scopeText READ scopeText CONSTANT)

public:
    explicit ManualImportController(QObject *parent = nullptr);

    [[nodiscard]] QVariantList manualDocuments() const;
    [[nodiscard]] int selectedIndex() const;
    [[nodiscard]] QVariantMap selectedDocument() const;
    [[nodiscard]] QString previewText() const;
    [[nodiscard]] QString previewStateToken() const;
    [[nodiscard]] QString lastErrorText() const;
    [[nodiscard]] QString lastErrorToken() const;
    // Frozen scope wording: what this slice supports and what belongs to a
    // later slice. Never a promise that a format was dropped from scope.
    [[nodiscard]] QString scopeText() const;

    // Imports one local file into the managed store. Returns false with a
    // human text + machine token on any refusal; a refused import leaves the
    // document list byte-identical.
    Q_INVOKABLE bool importManualFile(const QUrl &fileUrl);
    Q_INVOKABLE void selectDocument(int index);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void refresh();

    // Evidence foundation: a deterministic reference into the imported source.
    // start/end are CHARACTER offsets (half open) into the extracted text;
    // pageNumber is ALWAYS -1 for TXT / Markdown (never fabricated).
    Q_INVOKABLE QVariantMap evidenceReferenceAt(int start, int end) const;

signals:
    void documentsChanged();
    void selectionChanged();

private:
    void setError(modbuslens::core::ManualImportError error);
    void clearError();
    // Rebuilds m_previewText / m_previewStateToken for the selected document.
    void refreshPreview();

    std::vector<modbuslens::core::ManualDocument> m_documents;
    int m_selectedIndex{-1};
    QString m_previewText;
    QString m_previewStateToken{QStringLiteral("text")};
    QString m_lastErrorText;
    QString m_lastErrorToken;
};

} // namespace modbuslens::ui
