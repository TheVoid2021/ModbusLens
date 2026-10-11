#pragma once

#include <string>
#include <string_view>

#include "ui/ai/AiProviderProfile.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// T034-B2.1 — minimal AI credential-storage abstraction (Human §7).
//
// The Settings UI controller NEVER talks to the OS credential API directly:
// every secret read/write goes through this interface, so the storage backend
// is injectable (Windows Credential Manager in production, a deterministic
// fake in tests) and the QML secret boundary stays auditable in one place.
//
// Contract:
//   · credentials are PER-PROVIDER — the target name is derived from the
//     provider kind (ModbusLens/AI/<provider-token>); no shared bucket exists
//     and no cross-provider read is possible through this interface;
//   · readCredential is the ONLY way to obtain a stored secret, and it must
//     only be called from the C++ request path (never exposed to QML);
//   · implementers must not log the secret.
// ---------------------------------------------------------------------------

struct AiSecretResult {
    bool ok{false};
    std::string secret; // empty unless ok
};

class IAiCredentialStore
{
public:
    virtual ~IAiCredentialStore() = default;

    [[nodiscard]] virtual bool hasCredential(AiProviderKind kind) = 0;
    [[nodiscard]] virtual bool saveCredential(AiProviderKind kind,
                                              std::string_view secret) = 0;
    [[nodiscard]] virtual bool deleteCredential(AiProviderKind kind) = 0;
    [[nodiscard]] virtual AiSecretResult readCredential(AiProviderKind kind) = 0;
};

} // namespace modbuslens::ui
