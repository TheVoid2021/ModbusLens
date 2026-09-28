#include "ui/manual/ManualPdfTextExtractor.h"

#include <QFileInfo>

#include <cstring>
#include <vector>

#include "fpdf_text.h"
#include "fpdfview.h"

namespace modbuslens::ui {

namespace {

// Balances FPDF_InitLibraryWithConfig / FPDF_DestroyLibrary for the lifetime of one call,
// including every early return below.
class PdfiumLibrarySession
{
public:
    PdfiumLibrarySession()
    {
        FPDF_LIBRARY_CONFIG config;
        std::memset(&config, 0, sizeof(config));
        config.version = 2;
        FPDF_InitLibraryWithConfig(&config);
    }
    ~PdfiumLibrarySession() { FPDF_DestroyLibrary(); }

    PdfiumLibrarySession(const PdfiumLibrarySession &) = delete;
    PdfiumLibrarySession &operator=(const PdfiumLibrarySession &) = delete;
};

class PdfDocumentHandle
{
public:
    explicit PdfDocumentHandle(FPDF_DOCUMENT document) : document_(document) {}
    ~PdfDocumentHandle()
    {
        if (document_ != nullptr) {
            FPDF_CloseDocument(document_);
        }
    }

    [[nodiscard]] FPDF_DOCUMENT get() const { return document_; }

    PdfDocumentHandle(const PdfDocumentHandle &) = delete;
    PdfDocumentHandle &operator=(const PdfDocumentHandle &) = delete;

private:
    FPDF_DOCUMENT document_{nullptr};
};

class PdfPageHandle
{
public:
    explicit PdfPageHandle(FPDF_PAGE page) : page_(page) {}
    ~PdfPageHandle()
    {
        if (page_ != nullptr) {
            FPDF_ClosePage(page_);
        }
    }

    [[nodiscard]] FPDF_PAGE get() const { return page_; }

    PdfPageHandle(const PdfPageHandle &) = delete;
    PdfPageHandle &operator=(const PdfPageHandle &) = delete;

private:
    FPDF_PAGE page_{nullptr};
};

class PdfTextPageHandle
{
public:
    explicit PdfTextPageHandle(FPDF_TEXTPAGE textPage) : textPage_(textPage) {}
    ~PdfTextPageHandle()
    {
        if (textPage_ != nullptr) {
            FPDFText_ClosePage(textPage_);
        }
    }

    [[nodiscard]] FPDF_TEXTPAGE get() const { return textPage_; }

    PdfTextPageHandle(const PdfTextPageHandle &) = delete;
    PdfTextPageHandle &operator=(const PdfTextPageHandle &) = delete;

private:
    FPDF_TEXTPAGE textPage_{nullptr};
};

// PDFium's documented load-failure codes (FPDF_GetLastError).
constexpr unsigned long kFpdfErrorFormat = 3;
constexpr unsigned long kFpdfErrorPassword = 4;
constexpr unsigned long kFpdfErrorSecurity = 5;

[[nodiscard]] ManualExtractionResult failure(unsigned long rawError,
                                             ManualExtractionStatus status,
                                             const char *token)
{
    ManualExtractionResult result;
    result.status = status;
    result.rawDependencyError = static_cast<qint64>(rawError);
    result.diagnosticToken = QString::fromLatin1(token);
    return result;
}

} // namespace

ManualExtractionResult ManualPdfTextExtractor::extractFromFile(const QString &path)
{
    ManualExtractionResult result;

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        return failure(0, ManualExtractionStatus::MalformedDocument, "source_missing");
    }
    if (info.size() > kManualExtractionMaxSourceBytes) {
        return failure(0, ManualExtractionStatus::ResourceLimitExceeded, "source_too_large");
    }

    PdfiumLibrarySession session;

    PdfDocumentHandle document(FPDF_LoadDocument(path.toUtf8().constData(), nullptr));
    if (document.get() == nullptr) {
        const unsigned long raw = FPDF_GetLastError();
        if (raw == kFpdfErrorPassword || raw == kFpdfErrorSecurity) {
            return failure(raw, ManualExtractionStatus::EncryptedOrPasswordProtected,
                           "password_or_security");
        }
        return failure(raw, ManualExtractionStatus::MalformedDocument, "load_failed");
    }

    const int pageCount = FPDF_GetPageCount(document.get());
    if (pageCount < 0) {
        return failure(FPDF_GetLastError(), ManualExtractionStatus::DependencyError,
                       "page_count_failed");
    }
    if (pageCount > kManualExtractionMaxPdfPages) {
        return failure(0, ManualExtractionStatus::ResourceLimitExceeded, "too_many_pages");
    }
    result.pageCount = pageCount;

    qint64 totalChars = 0;
    for (int index = 0; index < pageCount; ++index) {
        PdfPageHandle page(FPDF_LoadPage(document.get(), index));
        if (page.get() == nullptr) {
            return failure(FPDF_GetLastError(), ManualExtractionStatus::DependencyError,
                           "load_page_failed");
        }
        PdfTextPageHandle textPage(FPDFText_LoadPage(page.get()));
        if (textPage.get() == nullptr) {
            // A page without a text layer is normal: record empty text, never fail.
            result.pageTexts.push_back(QString());
            continue;
        }
        const int chars = FPDFText_CountChars(textPage.get());
        if (chars < 0) {
            return failure(FPDF_GetLastError(), ManualExtractionStatus::DependencyError,
                           "count_chars_failed");
        }
        // PDFium text is UTF-16; FPDFText_GetText writes UTF-16LE code units and the
        // conversion below is the only sanctioned path (no Latin-1 / byte-wise QString).
        std::vector<unsigned short> buffer(static_cast<std::size_t>(chars) + 1, 0);
        const int copied = chars == 0
                               ? 0
                               : FPDFText_GetText(textPage.get(), 0, chars, buffer.data());
        if (copied < 0) {
            return failure(FPDF_GetLastError(), ManualExtractionStatus::DependencyError,
                           "get_text_failed");
        }
        // FPDFText_GetText documents its return value as the number of characters
        // written INCLUDING the terminating NUL, so the NUL must not become part of the
        // extracted text.
        const int textLength = copied > 0 ? copied - 1 : 0;
        const QString pageText =
            QString::fromUtf16(reinterpret_cast<const char16_t *>(buffer.data()), textLength);
        totalChars += pageText.size();
        if (totalChars > kManualExtractionMaxTextChars) {
            return failure(0, ManualExtractionStatus::ResourceLimitExceeded, "text_too_large");
        }
        result.pageTexts.push_back(pageText);
    }

    result.totalChars = totalChars;
    if (totalChars == 0) {
        // Explicit, non-guessing outcome. Mapping this onto an import/UI policy is an open
        // product decision and is deliberately NOT made here.
        result.status = ManualExtractionStatus::NoExtractableText;
        result.diagnosticToken = QStringLiteral("no_text_layer");
        return result;
    }

    result.status = ManualExtractionStatus::Ok;
    return result;
}

} // namespace modbuslens::ui
