#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <functional>
#include <vector>

#include "core/manual/ManualDocument.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C1a/C1b: Manual persistence (Qt side).
//
// Split mirrors the M12-A/B layering: the domain model + tokens live in the
// Zero-Qt core (core/manual/ManualDocument.h); this layer owns exactly the
// parts that need Qt — hashing (QCryptographicHash), strict UTF-8 decoding
// (QStringDecoder), atomic file writes (QSaveFile) and the platform
// user-data location (QStandardPaths).
//
// HUMAN-FROZEN semantics (T027 §48, §60):
//   · HYBRID STORAGE: a successful import copies the source into the
//     app-managed directory. `originalPath` is persisted as PROVENANCE and is
//     NEVER read again — after a successful import the original file may be
//     moved, renamed or deleted without losing the deterministic source.
//   · CONTENT IDENTITY: contentHash = SHA-256 of the imported source bytes,
//     lowercase hex. It — and only it — keys the managed copy and the
//     extracted-text cache. filename / mtime / original path are never used
//     as cache truth. document identity != contentHash: the same bytes may be
//     imported from different original paths and form SEPARATE records
//     (T027 §60.3) while the cache payload is reused.
//   · FORMATS: C1a = TXT and Markdown (semantics ZERO CHANGE). C1b second
//     slice (T027 §60, HUMAN-APPROVED) adds PDF (existing text layer only, no
//     OCR) and DOCX (main document story) routed by extension; the routed
//     extractor still validates the content strictly. A PDF without a text
//     layer is an IMPORT SUCCESS with extraction state no_extractable_text;
//     corrupt/fake/malformed/encrypted/resource-limit failures make the WHOLE
//     IMPORT an atomic failure (no partial artifacts — same discipline as
//     C1a).
//
// Layout under the managed root (never a hard-coded user path):
//   <root>/manuals/documents/<documentId>.json   metadata (the index)
//   <root>/manuals/source/<contentHash>.bin      managed byte copy
//   <root>/manuals/text/<contentHash>.txt        extracted-text cache (UTF-8)
//                                                (TXT / Markdown / DOCX)
//   <root>/manuals/text/<contentHash>.json       PDF per-page truth (pages[])
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

    // M12-C C1b second slice (T027 §60.4): the per-page truth of a PDF,
    // loaded from text/<hash>.json. The page headers shown by the UI are
    // presentation-only and are NEVER part of this data.
    [[nodiscard]] static QStringList loadPdfPages(const QString &contentHash,
                                                  bool *ok = nullptr);

    // ------------------------------------------------------------------
    // M12-C ML-2 (T027 §91, P0-ML-D/F): authoritative DELETE of one manual
    // library record. The metadata record documents/<documentId>.json is the
    // library-record VISIBILITY point: when its removal fails the record
    // stays and nothing else is touched (no GC, no fake success). After a
    // successful removal the managed artifacts of the deleted content
    // identity are cleaned ONLY when no remaining document still references
    // the same contentHash (shared artifacts MUST survive), using the
    // document's own type for the cache artifact path. Missing artifacts are
    // already clean; the Human's ORIGINAL external file is never touched by
    // this class at all. This type knows nothing about Candidates,
    // extractions, QML or profiles - policy lives above.
    // ------------------------------------------------------------------
    enum class ManualDeleteOutcome {
        Success,                   // token: manual_delete_success
        SuccessWithCleanupWarning, // token: manual_delete_cleanup_warning
        FailureDocumentNotFound,   // token: manual_delete_not_found
        FailureMetadataRemove,     // token: manual_delete_metadata_failed
    };

    struct ManualDeleteResult {
        ManualDeleteOutcome outcome{ManualDeleteOutcome::Success};
        [[nodiscard]] bool ok() const
        {
            return outcome == ManualDeleteOutcome::Success
                || outcome == ManualDeleteOutcome::SuccessWithCleanupWarning;
        }
        bool operator==(const ManualDeleteResult &) const = default;
    };

    [[nodiscard]] static ManualDeleteResult deleteDocument(
        const QString &documentId);

    // TEST/AUTOMATION seam (same discipline as setManagedRootOverride):
    // when set, every file removal performed by deleteDocument routes
    // through this predicate instead of QFile::remove, so automated tests
    // can force deterministic metadata/GC failures without touching real
    // directories or permissions. Empty (the default) restores direct
    // QFile::remove behavior. No generalized filesystem abstraction.
    using RemoveInterposer = std::function<bool(const QString &path)>;
    static void setRemoveInterposerForAutomation(RemoveInterposer interposer);

private:
    [[nodiscard]] static QByteArray serializeMetadata(
        const modbuslens::core::ManualDocument &document);
    [[nodiscard]] static bool writeFileAtomic(const QString &path,
                                              const QByteArray &bytes);
};

} // namespace modbuslens::ui
