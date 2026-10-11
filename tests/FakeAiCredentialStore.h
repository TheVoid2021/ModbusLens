#pragma once

#include <map>
#include <string>
#include <string_view>

#include "ui/ai/IAiCredentialStore.h"

namespace modbuslens::ui {

// T034-B2.1 — deterministic in-memory credential store for the SET matrix.
// Same per-provider contract as the production Windows store; zero OS calls.
class FakeAiCredentialStore final : public IAiCredentialStore
{
public:
    [[nodiscard]] bool hasCredential(AiProviderKind kind) override
    {
        return secrets_.find(static_cast<int>(kind)) != secrets_.end();
    }

    [[nodiscard]] bool saveCredential(AiProviderKind kind,
                                      std::string_view secret) override
    {
        ++saveCalls; // D7-SEAM: zero-mutation control observation (test-only)
        if (failSave) {
            // U2a: deterministic non-empty-credential-save failure injection
            // (test-only; default false = no impact on any other test).
            return false;
        }
        if (secret.empty()) {
            return false;
        }
        secrets_[static_cast<int>(kind)] = std::string(secret);
        return true;
    }

    [[nodiscard]] bool deleteCredential(AiProviderKind kind) override
    {
        ++deleteCalls; // D7-SEAM: zero-mutation control observation (test-only)
        return secrets_.erase(static_cast<int>(kind)) > 0;
    }

    // D7-SEAM zero-mutation control: the provider switch must never touch
    // the credential store. Counters are observation-only.
    int saveCalls{0};
    int deleteCalls{0};

    // U2a: deterministic non-empty-credential-save failure injection
    // (test-only). Default false — no impact on any other test.
    bool failSave{false};

    // Test hygiene: wipe every stored credential (init() calls this so a
    // leftover entry from a previous slot can never satisfy another test).
    // U2a: also reset the observation counters so slots asserting absolute
    // counts start from zero.
    void clearAll()
    {
        secrets_.clear();
        saveCalls = 0;
        deleteCalls = 0;
    }

    [[nodiscard]] AiSecretResult readCredential(AiProviderKind kind) override
    {
        AiSecretResult result;
        const auto it = secrets_.find(static_cast<int>(kind));
        if (it == secrets_.end()) {
            return result;
        }
        result.ok = true;
        result.secret = it->second;
        return result;
    }

private:
    std::map<int, std::string> secrets_;
};

} // namespace modbuslens::ui
