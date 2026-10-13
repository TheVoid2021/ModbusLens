#pragma once

#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>
#include <QString>
#include <QStringList>

#include "ui/ai/AiTestConnection.h"

namespace modbuslens::ui {

class AiRuntimeConfiguration;
class IChatCompletionsHttpClient;
class IAsyncTestConnectionClient;

// ---------------------------------------------------------------------------
// T034-B2.1 — the AI SETTINGS page controller (Human §2/§3/§8).
//
// QML SECRET BOUNDARY (the enforceable version, Human §8):
//   · the QML page may transiently hold the NEWLY ENTERED key inside the
//     password field;
//   · on submit the plaintext is handed to saveCredential() ONCE; this
//     controller keeps NO long-lived copy (the pending value is cleared as
//     soon as the save or the test-connection exchange is done);
//   · stored credential plaintext is NEVER returned to QML: there is no
//     getter, no Q_PROPERTY and no invokable that exposes it — the page only
//     ever sees hasCredentialForCurrentProvider (bool) and a saved-state
//     notice;
//   · the secret never enters logs, error strings, QSettings or forensic
//     output.
//
// The page owns the FORM state (provider/model text); saveConfiguration()
// persists it through AiRuntimeConfiguration (revision++ ⇒ runtime hot
// switch). testConnection() uses the CURRENT FORM values (an entered key is
// tested before saving) with a FIXED synthetic probe and no business data.
// ---------------------------------------------------------------------------
class AiSettingsController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    // Frozen v1 whitelist (Human §3): ModelScope / DeepSeek only.
    Q_PROPERTY(QStringList availableProviders READ availableProviders CONSTANT)
    Q_PROPERTY(QString providerName READ providerName WRITE setProviderName
                   NOTIFY formChanged)
    // HV2-F1-F1: REACTIVE ComboBox projection. providerName() stays the
    // canonical CONFIG TOKEN ("modelscope"/"deepseek"); this property maps the
    // canonical provider kind to its index inside availableProviders() through
    // the existing AiProviderProfile table (no second hardcoded name list).
    // NOTIFY is formChanged — the same signal the provider identity already
    // uses — so the ComboBox RECOMPUTES after any provider change instead of
    // being a one-shot initialization assignment. Unknown/empty form identity
    // falls back to index 0 (ModelScope), preserving the no-saved-config
    // default. The authority chain is unchanged: this is a DISPLAY projection
    // only; nothing here persists, reads a secret, or touches the credential
    // store.
    Q_PROPERTY(int providerIndex READ providerIndex NOTIFY formChanged)
    Q_PROPERTY(QString modelText READ modelText WRITE setModelText NOTIFY
                   formChanged)
    Q_PROPERTY(bool hasCredentialForCurrentProvider READ
                   hasCredentialForCurrentProvider NOTIFY credentialStateChanged)
    Q_PROPERTY(QString connectionStatusToken READ connectionStatusToken NOTIFY
                   connectionStateChanged)
    Q_PROPERTY(QString connectionStatusText READ connectionStatusText NOTIFY
                   connectionStateChanged)
    // D7-SEAMFIX: QML wiring for the Global AI Configuration owner. The REAL
    // page assigns this object reference once at instantiation
    // (runtimeConfiguration: aiSettingsRoot.runtimeConfiguration); without
    // this Q_PROPERTY the page cannot instantiate. QObject* matches the
    // EXISTING getter/setter signature exactly. Deliberately NO NOTIFY: the
    // only consumer is a creation-time WRITE; no production reader requires
    // change notification. This exposes the OWNER REFERENCE only — the B3a
    // meta-object restriction on AiRuntimeConfiguration mutation methods is
    // untouched (QML still cannot call saveConfiguration/saveCredential/
    // deleteCredential/readStoredCredential).
    Q_PROPERTY(QObject *runtimeConfiguration READ runtimeConfiguration WRITE
                   setRuntimeConfiguration)
    // U2a: sanitized Unified Save result surface. Tokens are stable
    // machine-readable outcomes; saveStatusText is fixed/sanitized text
    // (never a secret, path, or raw error).
    Q_PROPERTY(QString saveStatusToken READ saveStatusToken NOTIFY
                   saveStatusChanged)
    Q_PROPERTY(QString saveStatusText READ saveStatusText NOTIFY
                   saveStatusChanged)

public:
    // PRODUCTION: owns the real network client.
    explicit AiSettingsController(QObject *parent = nullptr);
    // TEST-ONLY injection: deterministic capture client, zero network.
    explicit AiSettingsController(IChatCompletionsHttpClient &http,
                                  QObject *parent = nullptr);
    ~AiSettingsController() override; // E1a: best-effort cancel of an in-flight probe

    // The Global AI Configuration owner (context property from main.cpp).
    [[nodiscard]] QObject *runtimeConfiguration() const;
    void setRuntimeConfiguration(QObject *configuration);

    [[nodiscard]] QStringList availableProviders() const;
    [[nodiscard]] QString providerName() const;
    void setProviderName(const QString &name);
    // HV2-F1-F1: index of the current form provider inside availableProviders()
    // (canonical token → AiProviderKind → profile display name → index).
    [[nodiscard]] int providerIndex() const;
    [[nodiscard]] QString modelText() const;
    void setModelText(const QString &model);
    [[nodiscard]] bool hasCredentialForCurrentProvider() const;
    [[nodiscard]] QString connectionStatusToken() const
    {
        return connectionStatusToken_;
    }
    [[nodiscard]] QString connectionStatusText() const
    {
        return aiConnectionStatusText(connectionStatusToken_);
    }
    // U2a: sanitized Unified Save result surface.
    [[nodiscard]] QString saveStatusToken() const { return saveStatusToken_; }
    [[nodiscard]] QString saveStatusText() const;

    // Prefill the form from the persisted configuration (page load).
    Q_INVOKABLE void refreshFormFromSavedConfiguration();

    // U2a: the ONE Unified Save authority entry (QML → controller →
    // AiRuntimeConfiguration). Orchestration: validate form → optional
    // credential save (non-empty transient key) → configuration save.
    // Result lands in saveStatusToken/saveStatusText; no rollback, no
    // plaintext retention, no raw error exposure.
    Q_INVOKABLE void saveSettings(const QString &transientSecret);

    // U2b RETIRED from the QML meta-object (kept as ordinary public C++
    // methods for existing C++ tests/runtime): the legacy split persistence
    // entries. The ONE QML persistence action is saveSettings(). Do NOT
    // delete these implementations — existing C++ callers remain authorized.
    bool submitCredential(const QString &secret);
    bool saveConfiguration();
    // E1a: async Test Connection entries. The no-arg form keeps the real QML
    // working unchanged (stored credential for the CURRENT FORM provider);
    // the transient-secret overload prepares the E1b real-QML wiring. BOTH
    // delegate to the SAME async core (single in-flight, generation guard).
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE void testConnection(const QString &transientSecret);

    // E1b: NON-SECRET invalidation hook for REAL Human edits of the API-key
    // field (wired from the page's onTextEdited) and for navigation-away.
    // Takes NO secret argument, holds NO copy of the field text — it only
    // advances the Test Connection invalidation authority (generation +
    // best-effort cancel if testing + status → not_tested).
    Q_INVOKABLE void notifyTestConnectionInputChanged();

    // E1a TEST-ONLY automation seam: install a deterministic async
    // Test Connection client (nullptr restores the production client).
    // Same class of minimal automation hook as setRunnerForAutomation.
    void setAsyncTestConnectionClientForAutomation(
        IAsyncTestConnectionClient *client);
    // Stays intentionally QML-callable: Delete API Key is a separate explicit
    // destructive action.
    Q_INVOKABLE void deleteSavedCredential();

signals:
    void formChanged();
    void credentialStateChanged();
    void connectionStateChanged();
    void savedConfigurationChanged();
    void credentialSaved();
    void credentialSaveFailed();
    void credentialDeleted();
    void configurationSaved();
    // U2a: Unified Save result surface.
    void saveStatusChanged();

private:
    [[nodiscard]] std::optional<AiProviderKind> currentKind() const;
    // The stored credential for the CURRENT FORM provider (Test Connection
    // E1a secret source; the U2a pendingSecret_ dead path was retired).
    [[nodiscard]] QString resolveStoredTestSecret() const;

    // E1a core + guards.
    void testConnectionCore(const QString &transientSecret);
    void handleTestConnectionCompletion(
        qint64 generation, const AiTestConnectionResult &result);
    void invalidateTestConnection();

    AiRuntimeConfiguration *runtimeConfiguration_{nullptr};
    std::unique_ptr<IChatCompletionsHttpClient> ownedHttp_;
    IChatCompletionsHttpClient *http_{nullptr};
    std::unique_ptr<IAsyncTestConnectionClient> ownedAsyncTestClient_;
    IAsyncTestConnectionClient *asyncTestClient_{nullptr};
    QString providerName_;   // form state (config token: "modelscope"/…)
    QString modelText_;      // form state
    QString connectionStatusToken_{QStringLiteral("not_tested")};
    // U2a: Unified Save result (machine token; initial = no attempt yet).
    QString saveStatusToken_{QStringLiteral("not_saved")};
    // E1a: single-in-flight + generation authority for Test Connection.
    bool testInFlight_{false};
    qint64 testGeneration_{0};
    int activeTestRequestId_{-1};
};

} // namespace modbuslens::ui
