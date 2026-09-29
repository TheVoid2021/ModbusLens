#include "ui/candidate/ModelScopeCandidateRunner.h"

#include <QString>

#include <cstdlib>

namespace modbuslens::ui {
namespace {

// Reused, NOT invented: this is the canonical credential name the accepted M6
// ModelScope client already reads (buildModelScopeProductionConfig in
// src/ui/ai/ModelScopeDiagnosisClient.cpp). C2 reads the same name without
// coupling to the Diagnosis-specific config/error types.
constexpr char kCanonicalApiKeyEnv[] = "MODELSCOPE_API_KEY";

} // namespace

bool ModelScopeCandidateRunner::hasConfiguredCredential()
{
    // Qt-free on purpose: this translation unit must not depend on the
    // Diagnosis client, so the environment is read through the C runtime.
    const char *value = std::getenv(kCanonicalApiKeyEnv);
    if (value == nullptr) {
        return false;
    }
    return !QString::fromUtf8(value).trimmed().isEmpty();
}

bool ModelScopeCandidateRunner::begin(const core::ExtractionRequest & /*request*/,
                                       std::uint64_t /*generation*/,
                                       const CompletionHandler & /*onDone*/)
{
    // Cannot start => return false, onDone is NEVER invoked, and the provider
    // call counter stays at zero. The controller records its own
    // "not_configured" failure, so the two gaps below are never mistaken for a
    // provider outcome and can never degrade into an authenticated request.
    //
    //   1. no credential (canonical environment name only), or
    //   2. no concrete provider transport: SESSION N deliberately ships the
    //      adapter boundary without a network transport, so SESSION O performs
    //      no provider call at all. The live transport belongs to the live
    //      extraction slice, not to this one.
    if (!hasConfiguredCredential()) {
        return false;
    }
    return false;
}

} // namespace modbuslens::ui