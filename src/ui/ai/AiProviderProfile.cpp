#include "ui/ai/AiProviderProfile.h"

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
