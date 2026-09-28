#include "core/manual/ManualDocument.h"

namespace modbuslens::core {

namespace {

constexpr std::string_view kTypeTxt = "txt";
constexpr std::string_view kTypeMarkdown = "md";
constexpr std::string_view kTypePdf = "pdf";
constexpr std::string_view kTypeDocx = "docx";
constexpr std::string_view kStatusReady = "ready";
constexpr std::string_view kStatusUnavailable = "unavailable";
constexpr std::string_view kExtractionExtracted = "extracted";
constexpr std::string_view kExtractionNoText = "no_extractable_text";

[[nodiscard]] char asciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] bool endsWithLower(std::string_view haystack,
                                 std::string_view suffix)
{
    if (suffix.size() > haystack.size()) {
        return false;
    }
    const std::size_t at = haystack.size() - suffix.size();
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        if (asciiLower(haystack[at + i]) != suffix[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string_view manualDocumentTypeToken(ManualDocumentType type)
{
    switch (type) {
    case ManualDocumentType::Txt:
        return kTypeTxt;
    case ManualDocumentType::Markdown:
        return kTypeMarkdown;
    case ManualDocumentType::Pdf:
        return kTypePdf;
    case ManualDocumentType::Docx:
        return kTypeDocx;
    }
    return {};
}

bool manualDocumentTypeFromExtension(std::string_view fileName,
                                     ManualDocumentType &out)
{
    if (endsWithLower(fileName, ".txt")) {
        out = ManualDocumentType::Txt;
        return true;
    }
    if (endsWithLower(fileName, ".md") || endsWithLower(fileName, ".markdown")) {
        out = ManualDocumentType::Markdown;
        return true;
    }
    // M12-C C1b second slice (T027 §60.1, HUMAN-APPROVED): the extension only
    // ROUTES to an extractor family. The routed extractor still validates the
    // actual content strictly, so a correct extension never makes bytes trusted.
    if (endsWithLower(fileName, ".pdf")) {
        out = ManualDocumentType::Pdf;
        return true;
    }
    if (endsWithLower(fileName, ".docx")) {
        out = ManualDocumentType::Docx;
        return true;
    }
    return false;
}

std::string_view manualDocumentStatusToken(ManualDocumentStatus status)
{
    switch (status) {
    case ManualDocumentStatus::Ready:
        return kStatusReady;
    case ManualDocumentStatus::Unavailable:
        return kStatusUnavailable;
    }
    return {};
}

std::string_view manualExtractionStateToken(ManualExtractionState state)
{
    switch (state) {
    case ManualExtractionState::Extracted:
        return kExtractionExtracted;
    case ManualExtractionState::NoExtractableText:
        return kExtractionNoText;
    }
    return {};
}

std::string_view manualImportErrorToken(ManualImportError error)
{
    switch (error) {
    case ManualImportError::Ok:
        return "ok";
    case ManualImportError::UnsupportedType:
        return "unsupported_type";
    case ManualImportError::FileNotFound:
        return "file_not_found";
    case ManualImportError::ReadFailed:
        return "read_failed";
    case ManualImportError::TooLarge:
        return "too_large";
    case ManualImportError::BinaryContent:
        return "binary_content";
    case ManualImportError::InvalidEncoding:
        return "invalid_encoding";
    case ManualImportError::UnsupportedEncoding:
        return "unsupported_encoding";
    case ManualImportError::EmptyContent:
        return "empty_content";
    case ManualImportError::StorageFailed:
        return "storage_failed";
    case ManualImportError::MalformedContent:
        return "malformed_content";
    case ManualImportError::EncryptedOrPasswordProtected:
        return "encrypted_or_password_protected";
    }
    return {};
}

} // namespace modbuslens::core
