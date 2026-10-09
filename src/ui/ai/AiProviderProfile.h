#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// T034 B1 — the compile-time PROVIDER PROFILE table (Human-authorized design,
// T034-A §D.1/D.2 with the Human design corrections of the B1 session).
//
// Deliberately minimal and DELIBERATELY NOT a framework: a fixed, compiled
// whitelist of OpenAI-compatible Chat Completions providers. Everything a
// caller may configure at runtime is the provider KIND (through the
// MODBUSLENS_AI_PROVIDER env token) and the model id; the ENDPOINT always
// comes from this table and is never user- or environment-overridable, so a
// configured credential can only ever be sent to the official host of the
// selected provider (endpoint security model, T034-A §D.4).
//
// Qt-free on purpose: the Candidate adapter (which consumes the profile) must
// stay free of network types, and the provider selection must be testable
// without any Qt module.
//
// SECRET ISOLATION (Human correction B1-C): every provider owns its OWN
// credential variable and NO cross-provider fallback exists — a request for
// DeepSeek never reads the ModelScope variable and vice versa. Vault
// (Windows Credential Manager) is B2; B1 reads the process environment only.
// ---------------------------------------------------------------------------

enum class AiProviderKind {
    ModelScope,
    DeepSeek,
};

struct AiProviderProfile {
    AiProviderKind kind;
    const char *displayName;
    // The ONLY endpoint this provider may post to (compiled constant).
    const char *chatCompletionsEndpoint;
    // Provider-specific request extras, decided by the PROFILE (never by the
    // caller): DeepSeek requires the thinking pass to be explicitly disabled
    // for structured extraction (its thinking mode defaults to ON and the
    // Candidate contract consumes ONLY final content). ModelScope sends no
    // extra in the Candidate chain — the verified wire semantics stay
    // byte-for-byte what they were.
    bool sendThinkingDisabledExtra;
    // Human fix decision (B1 JSON OUTPUT round, RCA #6): the DeepSeek profile
    // constrains the assistant output natively ("response_format": {"type":
    // "json_object"}) — the live failure was unstable output SHAPE, not the
    // strict parser. ModelScope keeps its verified semantics: NO such extra.
    bool sendJsonOutputResponseFormat;
    // DeepSeek's JSON Output contract additionally requires the PROMPT to ask
    // for JSON explicitly: the Candidate prompt gets the JSON-only instruction
    // (raw JSON only, no Markdown fence, no prose, exactly the supplied
    // schema). ModelScope: false (its verified prompt stays unchanged).
    bool jsonOnlyPromptInstruction;
};

[[nodiscard]] const AiProviderProfile &aiProviderProfile(AiProviderKind kind);

// Human Live-Test configuration tokens ("modelscope" / "deepseek"). Unknown
// tokens yield nullopt — the caller must fail closed, never guess.
[[nodiscard]] std::optional<AiProviderKind> aiProviderKindFromConfigToken(
    std::string_view token);

// ---- environment names (B1 configuration surface; no settings UI yet) -----
// Non-secret provider/model selection:
constexpr const char *kAiProviderEnv = "MODBUSLENS_AI_PROVIDER";
constexpr const char *kAiModelEnv = "MODBUSLENS_AI_MODEL";
// Secrets — one per provider, never shared, never cross-read:
constexpr const char *kModelScopeSecretEnv = "MODELSCOPE_API_KEY";
constexpr const char *kDeepSeekSecretEnv = "DEEPSEEK_API_KEY";
// LEGACY ModelScope model override (kept ONLY for ModelScope backward
// compatibility; precedence documented in the runner: MODBUSLENS_AI_MODEL
// wins, then this variable, then the accepted canonical default). A DeepSeek
// request NEVER reads it.
constexpr const char *kLegacyModelScopeModelEnv = "MODBUSLENS_MODELSCOPE_MODEL";

[[nodiscard]] const char *aiProviderSecretEnv(AiProviderKind kind);

} // namespace modbuslens::ui
