#include "ui/candidate/CandidateProviderRunner.h"

#include <QString>

#include <cstdlib>
#include <optional>
#include <utility>

namespace modbuslens::ui {
namespace {

// The accepted canonical default (M6 kCandidateModel). Retained because the
// repository already carries live evidence for it; this session does NOT pick
// a new model merely because an official example shows one.
constexpr char kAcceptedDefaultModelScopeModel[] = "Qwen/Qwen3.5-27B";
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

bool CandidateProviderRunner::hasConfiguredCredential()
{
    // Qt-free on purpose: this translation unit must not depend on the
    // Diagnosis client, so the environment is read through the C runtime.
    return !envValue(kModelScopeSecretEnv).isEmpty();
}

std::string CandidateProviderRunner::configuredModelId()
{
    // ModelScope-path resolution (legacy P02 semantics, now routed through the
    // B1 precedence: MODBUSLENS_AI_MODEL wins, then the legacy override, then
    // the accepted canonical default).
    return configuredModelIdFor(AiProviderKind::ModelScope);
}

std::string CandidateProviderRunner::officialEndpoint()
{
    return aiProviderProfile(AiProviderKind::ModelScope).chatCompletionsEndpoint;
}

int CandidateProviderRunner::productionTimeoutMs()
{
    return kProductionTimeoutMs;
}

std::optional<AiProviderKind> CandidateProviderRunner::resolveProviderKind()
{
    // Frozen B1 env fallback semantics, now routed through the provider-
    // neutral profile layer (single source; the runtime configuration owner
    // reuses the exact same helper so the two paths cannot drift).
    return aiProviderEnvKind();
}

std::string CandidateProviderRunner::configuredSecretFor(AiProviderKind kind)
{
    // The ONLY credential read path: the variable name comes from the
    // provider profile, so cross-provider reads are structurally impossible
    // (no fallback, no "any key" resolution).
    const char *secretEnv = aiProviderSecretEnv(kind);
    return secretEnv != nullptr ? envValue(secretEnv).toStdString() : std::string();
}

std::string CandidateProviderRunner::configuredModelIdFor(AiProviderKind kind)
{
    // Same profile-layer helper the runtime configuration owner uses for the
    // env-fallback path: MODBUSLENS_AI_MODEL wins for both providers, the
    // legacy override and the canonical default apply ONLY to ModelScope,
    // and DeepSeek never gets an invented model.
    return aiProviderEnvModel(kind);
}

CandidateProviderRunner::CandidateProviderRunner()
{
    ownedHttp_ = std::make_unique<QtChatCompletionsHttpClient>();
    http_ = ownedHttp_.get();
}

CandidateProviderRunner::CandidateProviderRunner(IChatCompletionsHttpClient &http)
    : http_(&http)
{
}

void CandidateProviderRunner::applyExplicitConfig(AiProviderKind kind,
                                                  std::string model,
                                                  std::string secret)
{
    // Only a REAL change marks the chain for a rebuild (same-config re-apply
    // must not reset an in-flight setup).
    if (!explicitConfig_ || explicitKind_ != kind || explicitModel_ != model
        || explicitSecret_ != secret) {
        rewireNeeded_ = true;
    }
    explicitConfig_ = true;
    explicitKind_ = kind;
    explicitModel_ = std::move(model);
    explicitSecret_ = std::move(secret);
}

void CandidateProviderRunner::clearExplicitConfig()
{
    if (explicitConfig_) {
        rewireNeeded_ = true;
    }
    explicitConfig_ = false;
    explicitModel_.clear();
    explicitSecret_.clear();
}

void CandidateProviderRunner::ensureWired(AiProviderKind kind,
                                          const std::string &model)
{
    if (adapter_ != nullptr && !rewireNeeded_) {
        return;
    }
    if (http_ == nullptr) {
        return;
    }
    rewireNeeded_ = false;
    // T034-B2.1 SLICE B2: the transport no longer binds a plaintext
    // credential at construction — the bearer token is supplied one-shot per
    // request (begin() pushes it via setOneShotBearerToken immediately before
    // dispatch). Wiring depends only on provider/model (endpoint identity).
    transport_ = std::make_unique<ChatCompletionsTransport>(
        *http_, aiProviderProfile(kind).chatCompletionsEndpoint,
        kProductionTimeoutMs);
    CandidateAdapterConfig config;
    config.kind = kind;
    config.modelId = model;
    config.maxOutputTokens = static_cast<std::size_t>(kMaxOutputTokens);
    adapter_ =
        std::make_unique<CandidateProviderAdapter>(*transport_, std::move(config));
}

int CandidateProviderRunner::dispatchCount() const
{
    return transport_ ? transport_->dispatchCount() : 0;
}

bool CandidateProviderRunner::begin(const core::ExtractionRequest &request,
                                    std::uint64_t generation,
                                    const CompletionHandler &onDone)
{
    // Fail-closed gate. Every gap is checked BEFORE anything is built or sent,
    // so none can degrade into an authenticated request and the provider
    // dispatch count stays at zero.
    //
    // B2.1: with an EXPLICIT (saved GUI) configuration the provider/model/
    // secret have already been resolved by the runtime configuration owner
    // (applyExplicitConfig) — including the secret, which was read from the
    // credential store at request time. Without one, the frozen B1
    // environment fallback applies verbatim.
    std::string model;
    std::string secret;
    AiProviderKind kind = explicitKind_;
    if (explicitConfig_) {
        model = explicitModel_;
        // ONE-SHOT consumption (T034-B2.1 SLICE B2): take the pending secret
        // for THIS request and immediately empty the runner-held copy. All
        // early-return paths below keep the member empty; a subsequent begin
        // without a fresh credential fails closed (LIFE-01).
        secret = explicitSecret_;
        explicitSecret_.clear();
        if (secret.empty()) {
            return false;
        }
    } else {
        const std::optional<AiProviderKind> envKind = resolveProviderKind();
        if (!envKind.has_value()) {
            return false;
        }
        kind = *envKind;
        // Env fallback secret is read per request and never stored in a
        // runner member (same one-shot lifetime as the explicit path).
        secret = configuredSecretFor(kind);
        if (secret.empty()) {
            return false;
        }
        model = configuredModelIdFor(kind);
        if (model.empty()) {
            return false;
        }
    }

    ensureWired(kind, model);
    if (adapter_ == nullptr) {
        return false;
    }
    // Hand the one-shot token to the transport right before dispatch.
    transport_->setOneShotBearerToken(std::move(secret));

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
