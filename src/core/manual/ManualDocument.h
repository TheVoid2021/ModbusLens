#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace modbuslens::core {

// ---------------------------------------------------------------------------
// M12-C C1a: Manual Document / Evidence domain model (Pure C++20, Zero Qt).
//
// Canonical basis (T027 §48, HUMAN-FROZEN):
//   · a successful import produces an application-OWNED managed copy; the
//     original path is PROVENANCE ONLY and never stays part of the read /
//     preview / cache / evidence path;
//   · the extracted text is cached by CONTENT identity — contentHash =
//     SHA-256 of the imported source bytes (hex). filename / mtime /
//     original path must never masquerade as content identity;
//   · C1a covers TXT and Markdown only. PDF / DOCX belong to C1b
//     (NOT STARTED / DEPENDENCY DECISION DEFERRED) and OCR stays deferred;
//     none of them is removed from the M12-C canonical scope.
//
// This layer owns NO file I/O, no decoding and no hashing: those need Qt and
// live in ui/manual. It only carries the domain shape, the frozen tokens and
// the resource limits so every future surface shares one vocabulary.
// ---------------------------------------------------------------------------

// Resource limits — IMPLEMENTATION SAFETY LIMITS, deliberately NOT
// Human-frozen product constants (T027 §48.1 item 4: the frozen rule is
// "reject explicitly, never silently truncate"; the values are ours).
inline constexpr std::size_t kManualMaxSourceBytes = 5u * 1024u * 1024u;
inline constexpr std::size_t kManualMaxTextChars = 2000000u;
inline constexpr int kManualMaxExcerptChars = 240;

enum class ManualDocumentType {
    Txt,
    Markdown,
    Pdf,  // M12-C C1b second slice (T027 §60.1): routed to the PDFium extractor
    Docx, // M12-C C1b second slice (T027 §60.1): routed to the libzip+XML extractor
};

[[nodiscard]] std::string_view manualDocumentTypeToken(ManualDocumentType type);

// Extension-driven type discrimination (ASCII, case-insensitive). Anything
// outside the frozen type matrix yields false — the caller must refuse, never
// guess. The extension only ROUTES (T027 §60.1); the routed extractor still
// validates the actual content strictly.
[[nodiscard]] bool manualDocumentTypeFromExtension(std::string_view fileName,
                                                   ManualDocumentType &out);

enum class ManualDocumentStatus {
    Ready,       // managed copy + text cache + metadata all resolvable
    Unavailable, // persisted record exists but a managed artifact is missing
};

[[nodiscard]] std::string_view manualDocumentStatusToken(
    ManualDocumentStatus status);

// M12-C C1b second slice (T027 §60.2, HUMAN-APPROVED): a PDF without an
// existing text layer is an IMPORT SUCCESS — the managed copy and the record
// are kept — but its extraction state is recorded explicitly instead of
// pretending an ordinary extraction happened. OCR / guessing stays forbidden.
enum class ManualExtractionState {
    Extracted,           // deterministic extracted text is cached
    NoExtractableText,   // valid document, but no existing text layer (PDF) /
                         // empty main story (DOCX): never OCR'd, never guessed
};

[[nodiscard]] std::string_view manualExtractionStateToken(
    ManualExtractionState state);

enum class ManualImportError {
    Ok,
    UnsupportedType,      // extension outside the frozen type matrix
    FileNotFound,
    ReadFailed,
    TooLarge,             // exceeds kManualMaxSourceBytes
    BinaryContent,        // NUL-containing content
    InvalidEncoding,      // not strict UTF-8
    UnsupportedEncoding,  // UTF-16 BOM: unsupported in C1a v1, not forever
    EmptyContent,
    StorageFailed,        // managed copy / cache / metadata write failed
    // M12-C C1b second slice (T027 §60.2): extraction-side failures make the
    // WHOLE IMPORT an atomic failure.
    MalformedContent,     // corrupt / fake / malformed PDF or DOCX
    EncryptedOrPasswordProtected,
};

// Stable machine tokens ("unsupported_type", ...) — never human UI prose.
[[nodiscard]] std::string_view manualImportErrorToken(ManualImportError error);

struct ManualDocument {
    int schemaVersion{1};
    std::string documentId;       // program-generated stable identity
    std::string originalFileName; // human-visible provenance name
    ManualDocumentType documentType{ManualDocumentType::Txt};
    std::string originalPath;     // PROVENANCE ONLY — never read again
    std::string contentHash;      // SHA-256 hex of the imported source bytes
    std::uint64_t byteSize{0};
    std::size_t charCount{0};
    ManualDocumentStatus status{ManualDocumentStatus::Ready};
    std::string statusToken;      // machine token; empty while Ready
    // M12-C C1b second slice (T027 §60.2): "extracted" | "no_extractable_text".
    std::string extractionStateToken{
        std::string(manualExtractionStateToken(
            ManualExtractionState::Extracted))};
};

// ---------------------------------------------------------------------------
// Evidence foundation (T027 §48.3F / §11).
//
// The point of this struct is that a future Candidate can NEVER degenerate
// into "AI says this came from file X": it must be able to re-attach to the
// managed deterministic source (documentId + contentHash) and to a concrete
// span of the deterministically extracted text.
//
// Offset convention (frozen for C1a): textStart / textEnd are CHARACTER
// offsets into the extracted text (QString UTF-16 code units), half open
// [start, end), -1 when not applicable. pageNumber is -1 for TXT / Markdown
// — a page number is NEVER fabricated for a format that has none.
// ---------------------------------------------------------------------------
struct ManualEvidenceReference {
    std::string documentId;
    std::string contentHash;
    int pageNumber{-1};
    std::string section;
    std::int64_t textStart{-1};
    std::int64_t textEnd{-1};
    std::string excerpt;
};

} // namespace modbuslens::core
