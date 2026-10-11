#include "ui/ai/WindowsAiCredentialStore.h"

#include <QString>

#include <windows.h>
#include <wincred.h>

namespace modbuslens::ui {

WindowsAiCredentialStore::WindowsAiCredentialStore(QObject *parent)
    : QObject(parent)
{
}

std::wstring WindowsAiCredentialStore::credentialTargetName(AiProviderKind kind)
{
    const char *token = nullptr;
    switch (kind) {
    case AiProviderKind::ModelScope:
        token = "modelscope";
        break;
    case AiProviderKind::DeepSeek:
        token = "deepseek";
        break;
    }
    const QString target = QStringLiteral("ModbusLens/AI/%1")
                               .arg(QString::fromUtf8(token));
    return target.toStdWString();
}

bool WindowsAiCredentialStore::hasCredential(AiProviderKind kind)
{
    const std::wstring target = credentialTargetName(kind);
    PCREDENTIALW raw = nullptr;
    const BOOL read = CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &raw);
    if (read) {
        CredFree(raw);
        return true;
    }
    return false;
}

bool WindowsAiCredentialStore::saveCredential(AiProviderKind kind,
                                              std::string_view secret)
{
    if (secret.empty()) {
        return false; // an empty secret must never overwrite a stored key
    }
    const std::wstring target = credentialTargetName(kind);
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t *>(target.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
    // The store intentionally keeps the UTF-8 bytes as the generic blob.
    // CREDENTIAL appears in the user's credential set (Credential Manager),
    // never in any file this project owns.
    credential.CredentialBlob =
        reinterpret_cast<LPBYTE>(const_cast<char *>(secret.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    const BOOL written = CredWriteW(&credential, 0);
    return written != 0;
}

bool WindowsAiCredentialStore::deleteCredential(AiProviderKind kind)
{
    const std::wstring target = credentialTargetName(kind);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != 0;
}

AiSecretResult WindowsAiCredentialStore::readCredential(AiProviderKind kind)
{
    AiSecretResult result;
    const std::wstring target = credentialTargetName(kind);
    PCREDENTIALW raw = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &raw)) {
        return result; // ok=false, empty secret
    }
    result.secret.assign(reinterpret_cast<const char *>(raw->CredentialBlob),
                         raw->CredentialBlobSize);
    result.ok = true;
    CredFree(raw);
    return result;
}

} // namespace modbuslens::ui
