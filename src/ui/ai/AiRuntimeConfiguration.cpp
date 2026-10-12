#include "ui/ai/AiRuntimeConfiguration.h"

#include <QSettings>

#include <cstdlib>

namespace modbuslens::ui {

namespace {
constexpr char kSettingsProviderKey[] = "ai/provider";
constexpr char kSettingsModelKey[] = "ai/model";
} // namespace

AiRuntimeConfiguration::AiRuntimeConfiguration(IAiCredentialStore &store,
                                               const QString &settingsFilePath,
                                               QObject *parent)
    : QObject(parent), store_(store), settingsFilePath_(settingsFilePath)
{
}

void AiRuntimeConfiguration::loadPersisted()
{
    // Tests pass a dedicated INI file; production uses the default
    // QSettings location (org/app = ModbusLens/ModbusLens, set in main()).
    const QSettings settings(
        settingsFilePath_.isEmpty()
            ? QStringLiteral("unused-placeholder")
            : settingsFilePath_,
        settingsFilePath_.isEmpty() ? QSettings::InvalidFormat
                                    : QSettings::IniFormat);
    const QString providerToken =
        settingsFilePath_.isEmpty() ? QSettings().value(kSettingsProviderKey).toString()
                                    : settings.value(kSettingsProviderKey).toString();
    const QString model =
        settingsFilePath_.isEmpty() ? QSettings().value(kSettingsModelKey).toString()
                                    : settings.value(kSettingsModelKey).toString();

    // Unknown/corrupt saved tokens fail CLOSED (SET-10): they never resolve
    // and they never degrade into the environment fallback.
    if (providerToken.isEmpty()) {
        savedProviderKind_ = std::nullopt; // nothing saved => env fallback
        savedConfigCorrupt_ = false;
        savedModel_.clear();
        return;
    }
    savedProviderKind_ = parseSavedProvider(providerToken);
    savedConfigCorrupt_ = !savedProviderKind_.has_value();
    savedModel_ = savedProviderKind_.has_value() ? model.trimmed() : QString();
}

std::optional<AiProviderKind> AiRuntimeConfiguration::parseSavedProvider(
    const QString &token)
{
    if (token.isEmpty()) {
        return std::nullopt; // nothing saved is a legal state (env fallback)
    }
    return aiProviderKindFromConfigToken(token.toStdString());
}

bool AiRuntimeConfiguration::hasSavedCredential() const
{
    if (!savedProviderKind_.has_value()) {
        return false;
    }
    return store_.hasCredential(*savedProviderKind_);
}

std::optional<AiRuntimeConfiguration::ResolvedRequestConfig>
AiRuntimeConfiguration::resolveRequestConfig() const
{
    ResolvedRequestConfig resolved;
    resolved.revision = configRevision_;

    // ---- precedence 2: saved UI configuration ------------------------------
    if (savedConfigCorrupt_) {
        return std::nullopt; // SET-10: corrupt saved provider fails closed
    }
    if (hasSavedConfiguration()) {
        resolved.kind = *savedProviderKind_;
        resolved.model = savedModel_.toStdString();
        // CASE A (Human-frozen B2.1 security semantics): credential authority
        // for a saved GUI configuration is ONLY the Credential Manager. A
        // missing stored credential means NOT CONFIGURED — the provider's
        // legacy env secret must have ZERO effect here (no envSecretFor in
        // this branch), otherwise deleting the stored key would silently
        // re-enable resolution through the environment (AUTH-05).
        if (!store_.hasCredential(resolved.kind)) {
            return std::nullopt; // NOT CONFIGURED, fail closed
        }
        const AiSecretResult stored = store_.readCredential(resolved.kind);
        if (!stored.ok || stored.secret.empty()) {
            return std::nullopt; // fail closed; never invent a secret
        }
        resolved.secret = stored.secret;
        return resolved;
    }

    // ---- precedence 3: NOT CONFIGURED (T034-B2-LC2) ------------------------
    // The legacy environment fallback is REMOVED. Final authority is:
    //   saved configuration + provider-scoped credential store, or NOTHING.
    // With no saved configuration there is no AI authority at all: no provider
    // env, no model env and no secret env is read, and the caller fails
    // closed. A present-but-incomplete saved configuration (missing stored
    // credential) likewise never degrades into an environment read.
    return std::nullopt;
}

bool AiRuntimeConfiguration::saveConfiguration(AiProviderKind kind,
                                               const QString &model)
{
    const QString trimmedModel = model.trimmed();
    if (trimmedModel.isEmpty()) {
        return false; // a saved configuration without a model is invalid
    }
    // U1 (T034-B2.1 Unified Save prerequisite): PERSISTENCE TRUTH. BOTH
    // QSettings branches write, sync, and check status BEFORE any runtime
    // publication. A persistence failure returns false and must NOT:
    // assign savedProviderKind_, assign savedModel_, bump configRevision_,
    // or emit configurationChanged. (Runtime publication on detected
    // persistence failure = BLOCKED; durable cross-key atomicity is NOT
    // guaranteed by this check.)
    if (settingsFilePath_.isEmpty()) {
        QSettings settings;
        settings.setValue(kSettingsProviderKey,
                          QString::fromUtf8(aiProviderConfigTokenFor(kind)));
        settings.setValue(kSettingsModelKey, trimmedModel);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            return false;
        }
    } else {
        QSettings settings(settingsFilePath_, QSettings::IniFormat);
        settings.setValue(kSettingsProviderKey,
                          QString::fromUtf8(aiProviderConfigTokenFor(kind)));
        settings.setValue(kSettingsModelKey, trimmedModel);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            return false;
        }
    }
    savedProviderKind_ = kind;
    savedModel_ = trimmedModel;
    ++configRevision_;
    emit configurationChanged(configRevision_);
    return true;
}

bool AiRuntimeConfiguration::saveCredential(AiProviderKind kind,
                                            const QString &secret)
{
    const std::string bytes = secret.toStdString();
    if (bytes.empty()) {
        return false;
    }
    if (!store_.saveCredential(kind, bytes)) {
        return false;
    }
    // T034-B2.1 SECURITY CORRECTION SLICE B1: a successful credential
    // save/replace is a request-authentication epoch change — it enters the
    // same invalidation epoch as provider/model saves so stale completions
    // authorized by the OLD credential are rejected by the revision guard.
    // Failed saves leave the revision unchanged (REV-04).
    ++configRevision_;
    emit configurationChanged(configRevision_);
    emit credentialStateChanged();
    return true;
}

bool AiRuntimeConfiguration::deleteCredential(AiProviderKind kind)
{
    const bool deleted = store_.deleteCredential(kind);
    if (deleted) {
        // Successful delete of an EXISTING credential is an authentication
        // epoch change (REV-03); a failed delete leaves it unchanged (REV-05).
        ++configRevision_;
        emit configurationChanged(configRevision_);
    }
    emit credentialStateChanged();
    return deleted;
}

} // namespace modbuslens::ui
