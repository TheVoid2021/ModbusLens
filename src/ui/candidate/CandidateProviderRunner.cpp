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
    // empty/unset = modelscope (backward compatibility with the pre-B1 world);
    // anything that is not a known token = FAIL CLOSED (T034-B1 test G).
    const QString token = envValue(kAiProviderEnv);
    if (token.isEmpty()) {
        return AiProviderKind::ModelScope;
    }
    return aiProviderKindFromConfigToken(token.toStdString());
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
    const QString modern = envValue(kAiModelEnv);
    if (!modern.isEmpty()) {
        return modern.toStdString();
    }
    if (kind == AiProviderKind::ModelScope) {
        // LEGACY override: read ONLY in the ModelScope profile.
        const QString legacy = envValue(kLegacyModelScopeModelEnv);
        if (!legacy.isEmpty()) {
            return legacy.toStdString();
        }
        return kAcceptedDefaultModelScopeModel;
    }
    // DeepSeek: the model id is Human-configured; nothing is invented.
    return std::string();
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

void CandidateProviderRunner::ensureWired(AiProviderKind kind,
                                          const std::string &secret,
                                          const std::string &model)
{
    if (adapter_ != nullptr || http_ == nullptr) {
        return;
    }
    const AiProviderProfile &profile = aiProviderProfile(kind);
    transport_ = std::make_unique<ChatCompletionsTransport>(
        *http_, profile.chatCompletionsEndpoint, secret, kProductionTimeoutMs);
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
    const std::optional<AiProviderKind> kind = resolveProviderKind();
    if (!kind.has_value()) {
        return false;
    }
    const std::string secret = configuredSecretFor(*kind);
    if (secret.empty()) {
        return false;
    }
    const std::string model = configuredModelIdFor(*kind);
    if (model.empty()) {
        return false;
    }

    ensureWired(*kind, secret, model);
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
