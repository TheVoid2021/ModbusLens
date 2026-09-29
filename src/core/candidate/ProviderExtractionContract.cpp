#include "core/candidate/ProviderExtractionContract.h"

namespace modbuslens::core {

std::string_view providerExtractionFailureToken(
    ProviderExtractionFailure failure)
{
    switch (failure) {
    case ProviderExtractionFailure::None:
        return "none";
    case ProviderExtractionFailure::TransportError:
        return "transport_error";
    case ProviderExtractionFailure::ProviderRejectedStatus:
        return "provider_rejected_status";
    case ProviderExtractionFailure::MalformedResponse:
        return "malformed_response";
    case ProviderExtractionFailure::SchemaViolation:
        return "schema_violation";
    }
    return "unknown";
}

ExtractionRequest buildC2FirstSliceExtractionRequest(
    const ManualDocument &document, std::string_view canonicalExtractedText)
{
    ExtractionRequest request;
    request.targetFieldToken =
        std::string(profileFieldTargetToken(kC2FirstSliceProfileField));
    request.documentTypeToken =
        std::string(manualDocumentTypeToken(document.documentType));
    request.canonicalExtractedText = std::string(canonicalExtractedText);
    return request;
}

} // namespace modbuslens::core