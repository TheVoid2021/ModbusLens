#include "ui/ai/AiProviderProfile.h"

#include <cstdlib>

namespace modbuslens::ui {

namespace {

// Fixed OFFICIAL endpoints (compiled constants — see the header's endpoint
// security note). DeepSeek contract verified against the official Chat
// Completions API: base URL https://api.deepseek.com, endpoint
// /chat/completions, Bearer auth, OpenAI-compatible shape.
constexpr AiProviderProfile kProfiles[] = {
    {AiProviderKind::ModelScope, "ModelScope",
     "https://api-inference.modelscope.cn/v1/chat/completions",
     /*sendThinkingDisabledExtra=*/false,
     /*sendJsonOutputResponseFormat=*/false,
     /*jsonOnlyPromptInstruction=*/false},
    {AiProviderKind::DeepSeek, "DeepSeek",
     "https://api.deepseek.com/chat/completions",
     /*sendThinkingDisabledExtra=*/true,
     /*sendJsonOutputResponseFormat=*/true,
     /*jsonOnlyPromptInstruction=*/true},
};

} // namespace

const AiProviderProfile &aiProviderProfile(AiProviderKind kind)
{
    for (const AiProviderProfile &profile : kProfiles) {
        if (profile.kind == kind) {
            return profile;
        }
    }
    // Unreachable with the frozen enum; fail loud rather than invent a row.
    std::abort();
}

std::optional<AiProviderKind> aiProviderKindFromConfigToken(
    std::string_view token)
{
    if (token == "modelscope") {
        return AiProviderKind::ModelScope;
    }
    if (token == "deepseek") {
        return AiProviderKind::DeepSeek;
    }
    return std::nullopt;
}

const char *aiProviderConfigTokenFor(AiProviderKind kind)
{
    switch (kind) {
    case AiProviderKind::ModelScope:
        return "modelscope";
    case AiProviderKind::DeepSeek:
        return "deepseek";
    }
    return nullptr;
}

std::optional<AiProviderKind> aiProviderEnvKind()
{
    const char *raw = std::getenv(kAiProviderEnv);
    if (raw == nullptr) {
        return AiProviderKind::ModelScope; // unset = modelscope (B1 compat)
    }
    return aiProviderKindFromConfigToken(raw);
}

namespace {
// B1 `envValue` trimmed before use; the profile-layer helper preserves that
// exact semantics (a whitespace-only override is EMPTY, never a model id).
[[nodiscard]] std::string trimmedEnv(const char *name)
{
    const char *raw = std::getenv(name);
    if (raw == nullptr) {
        return {};
    }
    std::string value = raw;
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}
} // namespace

std::string aiProviderEnvModel(AiProviderKind kind)
{
    const std::string modern = trimmedEnv(kAiModelEnv);
    if (!modern.empty()) {
        return modern;
    }
    if (kind == AiProviderKind::ModelScope) {
        const std::string legacy = trimmedEnv(kLegacyModelScopeModelEnv);
        if (!legacy.empty()) {
            return legacy;
        }
        return "Qwen/Qwen3.5-27B"; // accepted canonical default (M6 evidence)
    }
    return {}; // DeepSeek: model is Human-configured; nothing invented
}

const char *aiProviderSecretEnv(AiProviderKind kind)
{
    switch (kind) {
    case AiProviderKind::ModelScope:
        return kModelScopeSecretEnv;
    case AiProviderKind::DeepSeek:
        return kDeepSeekSecretEnv;
    }
    return nullptr;
}

} // namespace modbuslens::ui
