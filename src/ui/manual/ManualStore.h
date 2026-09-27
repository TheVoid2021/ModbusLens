#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <vector>

#include "core/manual/ManualDocument.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C1a: Manual persistence (Qt side).
//
// Split mirrors the M12-A/B layering: the domain model + tokens live in the
// Zero-Qt core (core/manual/ManualDocument.h); this layer owns exactly the
// parts that need Qt — hashing (QCryptographicHash), strict UTF-8 decoding
// (QStringDecoder), atomic file writes (QSaveFile) and the platform
// user-data location (QStandardPaths).
//
// HUMAN-FROZEN semantics (T027 §48):
//   · HYBRID STORAGE: a successful import copies the source into the
//     app-managed directory. `originalPath` is persisted as PROVENANCE and is
//     NEVER read again — after a successful import the original file may be
//     moved, renamed or deleted without losing the deterministic source.
//   · CONTENT IDENTITY: contentHash = SHA-256 of the imported source bytes,
//     lowercase hex. It — and only it — keys the managed copy and the
//     extracted-text cache. filename / mtime / original path are never used
//     as cache truth.
//   · C1a FORMATS: TXT and Markdown only. PDF / DOCX are C1b
//     (NOT STARTED / DEPENDENCY DECISION DEFERRED) and OCR stays deferred;
//     this layer refuses them, it does not remove them from scope.
//
// Layout under the managed root (never a hard-coded user path):
//   <root>/manuals/documents/<documentId>.json   metadata (the index)
//   <root>/manuals/source/<contentHash>.bin      managed byte copy
//   <root>/manuals/text/<contentHash>.txt        extracted-text cache (UTF-8)
// ---------------------------------------------------------------------------

struct ManualImportResult {
    modbuslens::core::ManualImportError error{
        modbuslens::core::ManualImportError::Ok};
    modbuslens::core::ManualDocument document;

    [[nodiscard]] bool ok() const {
        return error == modbuslens::core::ManualImportError::Ok;
    }
};

class ManualStore
{
public:
    // Test/automation injection (same discipline as
    // ProfileStore::setManagedRootOverride): when non-empty, ALL managed
    // directory resolution uses this root instead of the platform user-data
    // location, so automated tests and QML gates can never touch a real
    // user's data. Empty (the default) restores production behavior.
    static void setManagedRootOverride(const QString &dir);
    [[nodiscard]] static QString managedRootOverride();

    // <application user-data dir>/manuals — absolute, user-scoped.
    [[nodiscard]] static QString defaultManualsDirectory();
    [[nodiscard]] static QString managedManualsDirectory();
    [[nodiscard]] static QString documentsDirectory();
    [[nodiscard]] static QString sourceDirectory();
    [[nodiscard]] static QString textDirectory();

    // The full import transaction (read -> validate -> decode -> hash ->
    // commit). Every failure is reported as a token with NO partial persisted
    // document: nothing is written before the content has been fully
    // validated, and a later write failure rolls the earlier artifacts back.
    [[nodiscard]] static ManualImportResult importSourceFile(
        const QString &sourcePath, const QString &originalFileName);

    // The persisted index. Only metadata files count as documents; entries
    // whose managed artifacts are missing are reported as Unavailable
    // instead of being silently dropped.
    [[nodiscard]] static std::vector<modbuslens::core::ManualDocument>
    loadAll();

    [[nodiscard]] static bool hasManagedArtifacts(
        const modbuslens::core::ManualDocument &document);

    // The extracted text for a content identity (UTF-8 cache file).
    // ok==false means the cache is absent — never a silent empty string that
    // could be mistaken for an empty document.
    [[nodiscard]] static QString loadText(const QString &contentHash,
                                          bool *ok = nullptr);

private:
    [[nodiscard]] static QByteArray serializeMetadata(
        const modbuslens::core::ManualDocument &document);
    [[nodiscard]] static bool writeFileAtomic(const QString &path,
                                              const QByteArray &bytes);
};

} // namespace modbuslens::ui
