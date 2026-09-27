#include "core/manual/ManualDocument.h"

namespace modbuslens::core {

namespace {

constexpr std::string_view kTypeTxt = "txt";
constexpr std::string_view kTypeMarkdown = "md";
constexpr std::string_view kStatusReady = "ready";
constexpr std::string_view kStatusUnavailable = "unavailable";

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
    }
    return {};
}

} // namespace modbuslens::core
