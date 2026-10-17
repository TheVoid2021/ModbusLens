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
// T035-A Round K (Plan A, Human-authorized EXPERIMENT): the register_map
// target gets a dedicated, LONGER timeout because the 90 s budget is the one
// CONFIRMED failure mechanism of the two real register extractions
// (2026-10-15 12:38:42: elapsed_ms=90003 timed_out=1). This is a verification
// configuration, NOT a proven performance fix: whether it improves the real
// M04 success rate is exactly what the next human acceptance round tests.
constexpr int kRegisterTimeoutMs = 180000;
constexpr qsizetype kMaxOutputTokens = 768;
// T035-A Round L (Human-authorized EXPERIMENT): the register_map target gets
// a dedicated output budget. Real replay 2026-10-15 14:28:30 (Round K EXE):
// elapsed_ms=2753, finish_reason=length, content_len=2169, structured JSON
// parse FAIL => the shared 768 budget truncated the register answer. 4096 is
// a verification value; whether it removes the truncation is what the next
// real acceptance round tests. Manufacturer keeps 768.
constexpr qsizetype kRegisterMaxOutputTokens = 4096;

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

std::string CandidateProviderRunner::officialEndpoint()
{
    return aiProviderProfile(AiProviderKind::ModelScope).chatCompletionsEndpoint;
}

int CandidateProviderRunner::productionTimeoutMs()
{
    return kProductionTimeoutMs;
}

int CandidateProviderRunner::registerTimeoutMs()
{
    return kRegisterTimeoutMs;
}

qsizetype CandidateProviderRunner::registerMaxOutputTokens()
{
    return kRegisterMaxOutputTokens;
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
    // T035-A Round K (Plan A): TWO parallel owned pairs are wired from the
    // SAME provider/model — the profile-field pair keeps the frozen 90000 ms
    // and the register pair carries 180000 ms. Timeout is instance identity,
    // so no per-request timeout state exists anywhere.
    // T035-A Round L: the register pair ALSO carries its own output budget
    // (4096 vs the frozen 768) — same instance-identity discipline, so the
    // wire body's max_tokens comes from the config of the adapter that was
    // actually dispatched. stream/thinking/response_format stay profile-level
    // and identical for both.
    transport_ = std::make_unique<ChatCompletionsTransport>(
        *http_, aiProviderProfile(kind).chatCompletionsEndpoint,
        kProductionTimeoutMs);
    registerTransport_ = std::make_unique<ChatCompletionsTransport>(
        *http_, aiProviderProfile(kind).chatCompletionsEndpoint,
        kRegisterTimeoutMs);
    CandidateAdapterConfig config;
    config.kind = kind;
    config.modelId = model;
    config.maxOutputTokens = static_cast<std::size_t>(kMaxOutputTokens);
    CandidateAdapterConfig registerConfig;
    registerConfig.kind = kind;
    registerConfig.modelId = model;
    registerConfig.maxOutputTokens =
        static_cast<std::size_t>(kRegisterMaxOutputTokens);
    adapter_ =
        std::make_unique<CandidateProviderAdapter>(*transport_, config);
    registerAdapter_ = std::make_unique<CandidateProviderAdapter>(
        *registerTransport_, std::move(registerConfig));
}

int CandidateProviderRunner::dispatchCount() const
{
    // Aggregate over BOTH owned transports: the count semantic ("provider
    // dispatches performed by this runner") is unchanged — every fail-closed
    // path still yields 0 because no begin() reaches either transport.
    const int manufacturerDispatches =
        transport_ ? transport_->dispatchCount() : 0;
    const int registerDispatches =
        registerTransport_ ? registerTransport_->dispatchCount() : 0;
    return manufacturerDispatches + registerDispatches;
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
    // T034-B2-LC2: the frozen B1 environment fallback is REMOVED. Without an
    // EXPLICIT resolved configuration (from the saved global AI configuration
    // + the provider-scoped credential store) there is no Candidate authority
    // at all: no env provider, no env model, no env secret is read and begin()
    // fails closed with zero provider dispatches.
    if (!explicitConfig_) {
        return false; // NOT CONFIGURED — fail closed
    }
    const AiProviderKind kind = explicitKind_;
    const std::string model = explicitModel_;
    // ONE-SHOT consumption (T034-B2.1 SLICE B2): take the pending secret for
    // THIS request and immediately empty the runner-held copy. All
    // early-return paths below keep the member empty; a subsequent begin
    // without a fresh credential fails closed (LIFE-01).
    std::string secret = explicitSecret_;
    explicitSecret_.clear();
    if (secret.empty()) {
        return false;
    }

    ensureWired(kind, model);
    if (adapter_ == nullptr || registerAdapter_ == nullptr) {
        return false;
    }
    // T035-A Round K (Plan A): the ONLY routing key stays the target token.
    // Timeout is INSTANCE IDENTITY: the register branch hands the one-shot
    // credential to (and dispatches through) the dedicated 180000 ms pair;
    // every other target uses the frozen 90000 ms pair. No shared timeout
    // state exists, so M03→M04→M03 (and the mirror) cannot inherit values.
    const bool isRegisterTarget =
        request.targetFieldToken == core::kRegisterMapTargetToken;
    ChatCompletionsTransport &targetTransport =
        isRegisterTarget ? *registerTransport_ : *transport_;
    // Hand the one-shot token to the transport right before dispatch.
    targetTransport.setOneShotBearerToken(std::move(secret));

    ++beginCount_;
    // The accepted SESSION N chain does the real work: wire body, envelope
    // validation, strict parser. The raw provider response stays inside this
    // call and is never retained.
    // T035-A.3.2.2: the ONLY routing key is the request's target token —
    // register_map goes to the register prompt/strict register parser path
    // (extractRegister); every other token keeps the byte-for-byte
    // manufacturer path (extract).
    Completion completion;
    completion.generation = generation;
    if (isRegisterTarget) {
        const RegisterProviderExtractionResult registerResult =
            registerAdapter_->extractRegister(request);
        completion.ok = registerResult.ok;
        completion.failure = registerResult.failure;
        completion.registerProposals = std::move(registerResult.proposals);
    } else {
        const core::ProviderExtractionResult result = adapter_->extract(request);
        completion.ok = result.ok;
        completion.failure = result.failure;
        completion.proposals = result.proposals;
    }
    if (onDone) {
        onDone(completion);
    }
    return true;
}

} // namespace modbuslens::ui
