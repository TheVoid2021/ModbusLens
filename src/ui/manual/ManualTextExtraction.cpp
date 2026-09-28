#include "ui/manual/ManualTextExtraction.h"

namespace modbuslens::ui {

const char *manualExtractionStatusToken(ManualExtractionStatus status)
{
    switch (status) {
    case ManualExtractionStatus::Ok:
        return "ok";
    case ManualExtractionStatus::NoExtractableText:
        return "no_extractable_text";
    case ManualExtractionStatus::MalformedDocument:
        return "malformed_document";
    case ManualExtractionStatus::EncryptedOrPasswordProtected:
        return "encrypted_or_password_protected";
    case ManualExtractionStatus::ResourceLimitExceeded:
        return "resource_limit_exceeded";
    case ManualExtractionStatus::UnsupportedDocumentFeature:
        return "unsupported_document_feature";
    case ManualExtractionStatus::InternalError:
        return "internal_error";
    case ManualExtractionStatus::DependencyError:
        return "dependency_error";
    }
    return "internal_error";
}

} // namespace modbuslens::ui
