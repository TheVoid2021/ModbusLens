#pragma once

#include <QObject>

#include "ui/ai/IAiCredentialStore.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// T034-B2.1 — production credential store backed by the WINDOWS CREDENTIAL
// MANAGER (Human §6).
//
// Exact mechanics (Human-approved wording):
//   · API: CredWriteW / CredReadW / CredDeleteW with CRED_TYPE_GENERIC,
//     linked through Advapi32 (a WINDOWS SYSTEM LIBRARY — not a third-party
//     dependency);
//   · persistence: CRED_PERSIST_LOCAL_MACHINE — the credential belongs to the
//     CURRENT WINDOWS USER's credential set and persists across future logon
//     sessions on the same machine; it is NOT a machine-wide secret shared
//     between Windows users;
//   · target namespacing: "ModbusLens/AI/<provider-token>" — credentials are
//     strictly per provider, so switching provider can never reuse or leak
//     another provider's secret.
//
// The secret is written as the credential blob (UTF-8 bytes) and is never
// logged by this class.
// ---------------------------------------------------------------------------
class WindowsAiCredentialStore final : public QObject, public IAiCredentialStore
{
    Q_OBJECT

public:
    explicit WindowsAiCredentialStore(QObject *parent = nullptr);

    [[nodiscard]] bool hasCredential(AiProviderKind kind) override;
    [[nodiscard]] bool saveCredential(AiProviderKind kind,
                                      std::string_view secret) override;
    [[nodiscard]] bool deleteCredential(AiProviderKind kind) override;
    [[nodiscard]] AiSecretResult readCredential(AiProviderKind kind) override;

private:
    // "ModbusLens/AI/modelscope" / "ModbusLens/AI/deepseek" — per provider.
    [[nodiscard]] static std::wstring credentialTargetName(AiProviderKind kind);
};

} // namespace modbuslens::ui
