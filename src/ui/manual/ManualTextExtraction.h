#pragma once

#include <QString>
#include <QVector>

#include <QtGlobal>

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-C C1b: deterministic PDF / DOCX text extraction — shared result model.
//
// This layer is Qt-enabled on purpose (QString + QXmlStreamReader live here); the
// Zero-Qt boundary of src/core/manual is untouched (T027 §58.4 / §6).
//
// Status vocabulary is deliberately stable and provider-neutral: the raw dependency
// error code (for example PDFium's FPDF_GetLastError) is kept only as diagnostics and
// is never the product-level error identity. This slice does NOT decide the final UI
// wording, and it does NOT map "no extractable text" onto an import/UI policy —
// rejecting a manual with no text layer versus storing one with empty extracted text is
// still an open product decision (T027 §58.5).
// ---------------------------------------------------------------------------

enum class ManualExtractionStatus {
    Ok,
    NoExtractableText,
    MalformedDocument,
    EncryptedOrPasswordProtected,
    ResourceLimitExceeded,
    UnsupportedDocumentFeature,
    InternalError,
    DependencyError,
};

// Stable machine tokens (never human UI prose).
[[nodiscard]] const char *manualExtractionStatusToken(ManualExtractionStatus status);

struct ManualExtractionResult {
    ManualExtractionStatus status{ManualExtractionStatus::InternalError};

    // PDF: per-page text. Page-boundary / user-visible concatenation semantics are NOT
    // frozen (T027 §58.6), so this slice never builds a concatenated document string.
    QVector<QString> pageTexts;

    // DOCX: the main document story text (semantics frozen in T027 §58.3).
    QString mainStoryText;

    int pageCount{0};
    qint64 totalChars{0};

    // Diagnostics only — never the product-level error identity.
    QString diagnosticToken;
    qint64 rawDependencyError{0};

    [[nodiscard]] bool ok() const {
        return status == ManualExtractionStatus::Ok;
    }
};

// ---------------------------------------------------------------------------
// Resource safety limits.
//
// These are IMPLEMENTATION SAFETY PARAMETERS, not Human-frozen product constants: the
// authorization fixed the requirement (reject explicitly, never silently truncate) and
// left the numbers to the implementation (T027 §58 / §55.4). They are centralized here so
// tests can assert the boundary deterministically.
// ---------------------------------------------------------------------------

// Container size accepted by the extractors (PDF file / DOCX archive bytes).
inline constexpr qint64 kManualExtractionMaxSourceBytes = 32LL * 1024LL * 1024LL;
// Aggregate extracted text across the whole document.
inline constexpr qint64 kManualExtractionMaxTextChars = 4LL * 1024LL * 1024LL;
// PDF structural bounds.
inline constexpr int kManualExtractionMaxPdfPages = 2000;
// DOCX structural bounds.
inline constexpr int kManualDocxMaxZipEntries = 512;
inline constexpr qint64 kManualDocxMaxEntryUncompressedBytes = 32LL * 1024LL * 1024LL;
inline constexpr qint64 kManualDocxMaxAggregateUncompressedBytes = 64LL * 1024LL * 1024LL;
inline constexpr qint64 kManualDocxMaxMainXmlBytes = 16LL * 1024LL * 1024LL;

} // namespace modbuslens::ui
