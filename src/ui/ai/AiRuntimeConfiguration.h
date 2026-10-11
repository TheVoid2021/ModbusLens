#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>
#include <QString>
#include <QSettings>

#include <optional>
#include <string>

#include "ui/ai/AiProviderProfile.h"
#include "ui/ai/IAiCredentialStore.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// T034-B2.1 — the SINGLE Global AI Configuration owner (Human §9).
//
// Owns exactly one active provider + one model (+ credential presence), and a
// monotonically increasing configuration revision used by AI request paths to
// reject stale completions after a runtime hot switch (Human §11/§12).
//
// NON-SECRET persistence (Human §5): QSettings keys `ai/provider` and
// `ai/model` ONLY. An API key is NEVER written to QSettings, files, logs or
// docs; it lives exclusively in the injected IAiCredentialStore (Windows
// Credential Manager in production) and is read into memory only inside the
// C++ request path.
//
// Frozen precedence (Human §10 as CORRECTED by the B2.1 SECURITY CORRECTION
// SLICE) for resolveRequestConfig():
//   1. explicit test-injected configuration (unit tests construct the object
//      directly / inject the fake store);
//   2. saved UI configuration (QSettings): provider/model from QSettings,
//      credential authority = Credential Manager ONLY. A missing stored
//      credential means NOT CONFIGURED — the provider's legacy env secret
//      has ZERO effect in this branch (AUTH-01/02/05);
//   3. legacy environment fallback (provider + model + env secret), allowed
//      ONLY when NO saved GUI configuration exists (CASE B transitional;
//      a legacy env var NEVER overrides a saved GUI choice);
//   4. corrupt/unknown saved provider token or not_configured (nullopt).
// ---------------------------------------------------------------------------
class AiRuntimeConfiguration : public QObject
{
    Q_OBJECT
    // Registered so QML can TYPE the required property; creation stays C++
    // (main.cpp) because the store must be injected.
    QML_ELEMENT
    QML_UNCREATABLE("AiRuntimeConfiguration is owned by main(); bind the context property instead.")

public:
    struct ResolvedRequestConfig {
        AiProviderKind kind{AiProviderKind::ModelScope};
        std::string model;
        std::string secret; // read at request time; never persisted here
        qint64 revision{0};
    };

    // `settingsFilePath`: empty = the DEFAULT QSettings location (production);
    // non-empty = a dedicated INI file (tests) so deterministic tests never
    // touch the developer's real settings.
    explicit AiRuntimeConfiguration(IAiCredentialStore &store,
                                    const QString &settingsFilePath = {},
                                    QObject *parent = nullptr);

    // Load ai/provider + ai/model from QSettings (called once at startup).
    void loadPersisted();

    [[nodiscard]] std::optional<AiProviderKind> savedProviderKind() const
    {
        return savedProviderKind_;
    }
    [[nodiscard]] QString savedModel() const { return savedModel_; }
    [[nodiscard]] bool hasSavedConfiguration() const
    {
        return savedProviderKind_.has_value() && !savedModel_.isEmpty();
    }

    // Credential presence for the CURRENTLY SAVED provider (QML-safe: bool).
    [[nodiscard]] bool hasSavedCredential() const;

    [[nodiscard]] qint64 configRevision() const { return configRevision_; }

    // C++-ONLY secret read (Test Connection with the already-stored key).
    // Deliberately NOT Q_INVOKABLE and NOT a Q_PROPERTY: the QML engine can
    // never call it, so stored plaintext cannot cross back into QML (SET-04).
    [[nodiscard]] AiSecretResult readStoredCredential(AiProviderKind kind) const
    {
        return store_.readCredential(kind);
    }

    // U2a: provider-scoped NON-SECRET credential presence (Unified Save
    // blank-key branch). Ordinary public C++ method ONLY — NOT Q_INVOKABLE,
    // NOT a slot, NOT a Q_PROPERTY getter: invisible to the QML meta-object
    // (B3a boundary). Returns bool only; reads NO plaintext; never touches
    // QSettings, configRevision, signals or the environment fallback; does
    // not depend on savedProviderKind_ (answers for ANY requested kind).
    [[nodiscard]] bool hasCredentialFor(AiProviderKind kind) const
    {
        return store_.hasCredential(kind);
    }

    // The ONE request-time resolution path (Human §10 precedence). Reads the
    // secret from the credential store (or the provider-specific env fallback)
    // ONLY here — the result never becomes a long-lived config member.
    [[nodiscard]] std::optional<ResolvedRequestConfig> resolveRequestConfig() const;

    // T034-B2.1 slice B3a: these three mutation methods are deliberately
    // ORDINARY public C++ methods — NOT slots, NOT Q_INVOKABLE, NOT property
    // writers. The meta-object must NOT expose them, so QML can hold/pass
    // the object reference but can never mutate configuration/credentials
    // directly (Human-frozen authority chain: QML -> AiSettingsController ->
    // AiRuntimeConfiguration). Callable from C++ exactly as before
    // (AiSettingsController, tests); implementation semantics unchanged.
public:
    // Persist provider+model (non-secret) and bump the revision. Hot switch:
    // the next request uses the new configuration without a restart.
    bool saveConfiguration(AiProviderKind kind, const QString &model);
    // Store the credential for a provider (Credential Manager). Returns false
    // on empty secret / OS failure. Bumps nothing (secret is not a config
    // value) but notifies credential-state observers.
    bool saveCredential(AiProviderKind kind, const QString &secret);
    bool deleteCredential(AiProviderKind kind);

signals:
    void configurationChanged(qint64 newRevision);
    void credentialStateChanged();

private:
    [[nodiscard]] static std::optional<AiProviderKind> parseSavedProvider(
        const QString &token);
    [[nodiscard]] static std::string envSecretFor(AiProviderKind kind);

    IAiCredentialStore &store_;
    std::optional<AiProviderKind> savedProviderKind_;
    // SET-10: a persisted `ai/provider` token that is NOT a known provider
    // must FAIL CLOSED — it must never silently degrade into the environment
    // fallback (the user explicitly chose a provider we cannot resolve).
    bool savedConfigCorrupt_{false};
    QString savedModel_;
    qint64 configRevision_{1};
    QString settingsFilePath_; // empty = default QSettings
};

} // namespace modbuslens::ui
