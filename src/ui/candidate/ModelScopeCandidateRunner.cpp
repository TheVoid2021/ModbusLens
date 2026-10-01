#include "ui/candidate/ModelScopeCandidateRunner.h"

#include <QString>

#include <cstdlib>
#include <utility>

namespace modbuslens::ui {
namespace {

// Reused, NOT invented: these are the canonical names the accepted M6 ModelScope
// client already reads (buildModelScopeProductionConfig in
// src/ui/ai/ModelScopeDiagnosisClient.cpp), including the endpoint constant and
// the 90 s bounded timeout. C2 reads the same contract without coupling to the
// Diagnosis-specific config / error / signal types.
constexpr char kCanonicalApiKeyEnv[] = "MODELSCOPE_API_KEY";
constexpr char kCanonicalModelEnv[] = "MODBUSLENS_MODELSCOPE_MODEL";
constexpr char kOfficialEndpoint[] =
    "https://api-inference.modelscope.cn/v1/chat/completions";
// The accepted canonical default (M6 kCandidateModel). Retained because the
// repository already carries live evidence for it; this session does NOT pick a
// new model merely because an official example shows one.
constexpr char kAcceptedDefaultModel[] = "Qwen/Qwen3.5-27B";
constexpr int kProductionTimeoutMs = 90000;
constexpr qsizetype kMaxOutputTokens = 768;

[[nodiscard]] QString envValue(const char *name)
{
    const char *value = std::getenv(name);
    if (value == nullptr) {
        return QString();
    }
    return QString::fromUtf8(value).trimmed();
}

} // namespace

bool ModelScopeCandidateRunner::hasConfiguredCredential()
{
    // Qt-free on purpose: this translation unit must not depend on the
    // Diagnosis client, so the environment is read through the C runtime.
    return !envValue(kCanonicalApiKeyEnv).isEmpty();
}

std::string ModelScopeCandidateRunner::configuredModelId()
{
    const QString fromEnv = envValue(kCanonicalModelEnv);
    if (!fromEnv.isEmpty()) {
        return fromEnv.toStdString();
    }
    return kAcceptedDefaultModel;
}

std::string ModelScopeCandidateRunner::officialEndpoint()
{
    return kOfficialEndpoint;
}

int ModelScopeCandidateRunner::productionTimeoutMs()
{
    return kProductionTimeoutMs;
}

ModelScopeCandidateRunner::ModelScopeCandidateRunner()
{
    ownedHttp_ = std::make_unique<QtModelScopeHttpClient>();
    http_ = ownedHttp_.get();
}

ModelScopeCandidateRunner::ModelScopeCandidateRunner(IModelScopeHttpClient &http)
    : http_(&http)
{
}

void ModelScopeCandidateRunner::ensureWired()
{
    if (adapter_ != nullptr || http_ == nullptr) {
        return;
    }
    transport_ = std::make_unique<ModelScopeExtractionTransport>(
        *http_, officialEndpoint(), envValue(kCanonicalApiKeyEnv).toStdString(),
        productionTimeoutMs());
    ModelScopeAdapterConfig config;
    config.modelId = configuredModelId();
    config.maxOutputTokens = static_cast<std::size_t>(kMaxOutputTokens);
    adapter_ =
        std::make_unique<ModelScopeCandidateAdapter>(*transport_, std::move(config));
}

int ModelScopeCandidateRunner::dispatchCount() const
{
    return transport_ ? transport_->dispatchCount() : 0;
}

bool ModelScopeCandidateRunner::begin(const core::ExtractionRequest &request,
                                       std::uint64_t generation,
                                       const CompletionHandler &onDone)
{
    // Fail-closed gate. Both gaps are checked BEFORE anything is built or sent,
    // so neither can degrade into an authenticated request and the provider
    // dispatch count stays at zero.
    if (!hasConfiguredCredential()) {
        return false;
    }
    if (configuredModelId().empty()) {
        return false;
    }

    ensureWired();
    if (adapter_ == nullptr) {
        return false;
    }

    ++beginCount_;
    // The accepted SESSION N chain does the real work: wire body, envelope
    // validation, strict parser. The raw provider response stays inside this
    // call and is never retained.
    const core::ProviderExtractionResult result = adapter_->extract(request);

    Completion completion;
    completion.generation = generation;
    completion.ok = result.ok;
    completion.failure = result.failure;
    completion.proposals = result.proposals;
    if (onDone) {
        onDone(completion);
    }
    return true;
}

} // namespace modbuslens::ui