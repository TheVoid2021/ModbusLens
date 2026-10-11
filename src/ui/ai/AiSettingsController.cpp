#include "ui/ai/AiSettingsController.h"

#include <QPointer>

#include "ui/ai/AiRuntimeConfiguration.h"
#include "ui/ai/AiTestConnection.h"
#include "ui/candidate/ChatCompletionsHttpClient.h"

namespace modbuslens::ui {

namespace {
constexpr int kTestConnectionTimeoutMs = 45000;

// E1a: adapter that exposes an injected SYNCHRONOUS capture client
// (FakeHttpClient) through the ASYNC Test Connection seam. Completion is
// delivered inside start() — the same observable behavior the sanitized
// status-mapping tests had before the async conversion.
class SyncAdapterTestConnectionClient final
    : public QObject,
      public IAsyncTestConnectionClient
{
public:
    explicit SyncAdapterTestConnectionClient(
        IChatCompletionsHttpClient &http)
        : http_(http)
    {
    }

    [[nodiscard]] int start(
        const AiTestConnectionRequest &request,
        const std::function<void(const AiTestConnectionResult &)> &onDone)
        override
    {
        const AiTestConnectionResult result = runAiTestConnection(
            http_, request.kind, request.model, request.secret,
            request.timeoutMs);
        if (onDone) {
            onDone(result);
        }
        return ++nextId_;
    }

    void cancel(int requestId) override { ++cancelCount; }

private:
    IChatCompletionsHttpClient &http_;
    int nextId_{0};
    int cancelCount{0};
};

// U2a: sanitized Unified Save status text. Fixed strings only — never a
// secret, a path, a raw provider response, or a raw OS/store error.
QString aiSaveStatusText(const QString &token)
{
    if (token == QLatin1String("saved")) {
        return QStringLiteral("已保存");
    }
    if (token == QLatin1String("validation_failed")) {
        return QStringLiteral("无法保存：填写无效");
    }
    if (token == QLatin1String("credential_required")) {
        return QStringLiteral("尚未保存该 Provider 的 API Key");
    }
    if (token == QLatin1String("credential_save_failed")) {
        return QStringLiteral("API Key 保存失败");
    }
    if (token == QLatin1String("configuration_save_failed")) {
        return QStringLiteral("配置保存失败");
    }
    if (token == QLatin1String(
            "configuration_save_failed_after_credential_saved")) {
        return QStringLiteral("API Key 已保存，但配置保存失败");
    }
    return QString(); // not_saved / unknown: no attempt text
}
} // namespace

AiSettingsController::AiSettingsController(QObject *parent)
    : QObject(parent),
      ownedAsyncTestClient_(
          std::make_unique<QtAsyncTestConnectionClient>()),
      asyncTestClient_(ownedAsyncTestClient_.get())
{
    // T034-B2.1-CORR-ASYNC-TESTCONNECTION (B2.1-E production wiring): the
    // QML-created production controller now owns the REAL async Test
    // Connection client (QNetworkAccessManager + single QTimer timeout
    // authority + generation/late-completion guards). No test endpoint
    // resolver is injected here, so start() dispatches to the provider
    // profile HTTPS endpoint and returns the request id WITHOUT waiting —
    // the synchronous nested-event-loop network wait (SyncAdapter →
    // runAiTestConnection → QtChatCompletionsHttpClient::post) is
    // structurally removed from the production path.
    // ownedHttp_/http_ stay null in this constructor: their only consumer
    // was the SyncAdapter Test Connection adapter, which the production
    // path no longer selects. The IChatCompletionsHttpClient& TEST
    // constructor below keeps that adapter for deterministic unit tests.
}

AiSettingsController::AiSettingsController(IChatCompletionsHttpClient &http,
                                           QObject *parent)
    : QObject(parent), http_(&http)
{
    // E1a TEST-ONLY injection: the deterministic sync FakeHttpClient is
    // adapted to the async seam so existing sanitized-mapping tests keep
    // their synchronous observable behavior (completion inside start()).
    ownedAsyncTestClient_ =
        std::make_unique<SyncAdapterTestConnectionClient>(http);
    asyncTestClient_ = ownedAsyncTestClient_.get();
}

AiSettingsController::~AiSettingsController()
{
    // E1a: best-effort network cancellation only. Stale-safety does NOT
    // depend on this — the completion callback is guarded by a QPointer, so
    // a late completion after destruction is a harmless no-op.
    if (testInFlight_ && asyncTestClient_ != nullptr) {
        asyncTestClient_->cancel(activeTestRequestId_);
    }
}

QObject *AiSettingsController::runtimeConfiguration() const
{
    return runtimeConfiguration_;
}

void AiSettingsController::setRuntimeConfiguration(QObject *configuration)
{
    auto *casted = qobject_cast<AiRuntimeConfiguration *>(configuration);
    if (runtimeConfiguration_ == casted) {
        return;
    }
    runtimeConfiguration_ = casted;
    refreshFormFromSavedConfiguration();
}

QStringList AiSettingsController::availableProviders() const
{
    // Frozen v1 whitelist (Human §3); display names in the UI language policy
    // (brand tokens stay English).
    return {QStringLiteral("ModelScope"), QStringLiteral("DeepSeek")};
}

QString AiSettingsController::providerName() const
{
    return providerName_;
}

void AiSettingsController::setProviderName(const QString &name)
{
    // Accepts the display name ("DeepSeek") or the config token ("deepseek").
    QString normalized = name.trimmed();
    if (normalized.compare(QStringLiteral("ModelScope"), Qt::CaseInsensitive) == 0) {
        normalized = QStringLiteral("modelscope");
    } else if (normalized.compare(QStringLiteral("DeepSeek"), Qt::CaseInsensitive)
               == 0) {
        normalized = QStringLiteral("deepseek");
    }
    if (providerName_ == normalized) {
        return;
    }
    providerName_ = normalized;
    // E1a: an actual provider identity change invalidates an in-flight probe
    // (same-value setters never reach this point — no-op is preserved).
    invalidateTestConnection();
    // U2b: the edited form is no longer the saved one — a stale "saved"
    // result must not keep claiming the edited form is saved.
    if (saveStatusToken_ != QStringLiteral("not_saved")) {
        saveStatusToken_ = QStringLiteral("not_saved");
        emit saveStatusChanged();
    }
    emit formChanged();
    emit credentialStateChanged(); // credential presence is per provider
}

QString AiSettingsController::modelText() const
{
    return modelText_;
}

void AiSettingsController::setModelText(const QString &model)
{
    if (modelText_ == model) {
        return;
    }
    modelText_ = model;
    // E1a: an actual model change invalidates an in-flight probe (same-value
    // setters never reach this point — no-op is preserved).
    invalidateTestConnection();
    // U2b: form edit invalidates a stale "saved" result (same rule as the
    // provider identity change).
    if (saveStatusToken_ != QStringLiteral("not_saved")) {
        saveStatusToken_ = QStringLiteral("not_saved");
        emit saveStatusChanged();
    }
    emit formChanged();
}

bool AiSettingsController::hasCredentialForCurrentProvider() const
{
    if (runtimeConfiguration_ == nullptr) {
        return false;
    }
    const std::optional<AiProviderKind> kind = currentKind();
    // U2b correctness fix: the indicator must follow the CURRENT FORM
    // provider identity — NOT the saved configuration's provider. The
    // provider-scoped NON-SECRET hasCredentialFor() answers existence for
    // any requested kind without reading plaintext.
    return kind.has_value() && runtimeConfiguration_->hasCredentialFor(*kind);
}

void AiSettingsController::refreshFormFromSavedConfiguration()
{
    if (runtimeConfiguration_ == nullptr) {
        return;
    }
    if (runtimeConfiguration_->hasSavedConfiguration()) {
        providerName_ = QString::fromUtf8(aiProviderConfigTokenFor(
            *runtimeConfiguration_->savedProviderKind()));
        modelText_ = runtimeConfiguration_->savedModel();
    }
    emit formChanged();
    emit credentialStateChanged();
}

std::optional<AiProviderKind> AiSettingsController::currentKind() const
{
    return aiProviderKindFromConfigToken(providerName_.toStdString());
}

bool AiSettingsController::submitCredential(const QString &secret)
{
    // U2b: RETIRED from the QML meta-object — ordinary public C++ method for
    // existing C++ callers. Consumed immediately; nothing is retained as a
    // member and nothing is logged.
    const std::optional<AiProviderKind> kind = currentKind();
    if (!kind.has_value() || runtimeConfiguration_ == nullptr) {
        emit credentialSaveFailed();
        return false;
    }
    if (runtimeConfiguration_->saveCredential(*kind, secret)) {
        emit credentialStateChanged();
        emit credentialSaved();
        return true;
    }
    emit credentialSaveFailed();
    return false;
}

QString AiSettingsController::resolveStoredTestSecret() const
{
    // E1a: the U2a pendingSecret_ dead path was RETIRED (it had no writer).
    // The async Test Connection secret source is the stored credential for
    // the CURRENT FORM provider — resolved fresh, never retained as a member.
    const std::optional<AiProviderKind> kind = currentKind();
    if (kind.has_value() && runtimeConfiguration_ != nullptr) {
        const AiSecretResult stored = runtimeConfiguration_->readStoredCredential(*kind);
        if (stored.ok) {
            return QString::fromStdString(stored.secret);
        }
    }
    return {};
}

void AiSettingsController::setAsyncTestConnectionClientForAutomation(
    IAsyncTestConnectionClient *client)
{
    // E1a TEST-ONLY automation seam (nullptr restores the production client).
    // If a probe is in flight on the old client, invalidate first — the old
    // completion can never publish after the generation advance.
    invalidateTestConnection();
    if (client != nullptr) {
        asyncTestClient_ = client;
        return;
    }
    asyncTestClient_ = ownedAsyncTestClient_.get();
}

void AiSettingsController::testConnection()
{
    // E1a: the no-arg entry keeps the real QML working unchanged — stored
    // credential for the CURRENT FORM provider, dispatched asynchronously.
    testConnectionCore(QString());
}

void AiSettingsController::testConnection(const QString &transientSecret)
{
    // E1a: prepared transient-secret overload. E1b will wire the REAL QML
    // to this; E1a tests exercise it directly.
    testConnectionCore(transientSecret);
}

void AiSettingsController::notifyTestConnectionInputChanged()
{
    // E1b: field-dirty / navigation-away invalidation (Human decision §4/§6).
    // Non-secret by construction: no argument, no member copy — pure
    // Test Connection authority advance.
    invalidateTestConnection();
}

void AiSettingsController::testConnectionCore(const QString &transientSecret)
{
    // E1a SINGLE-IN-FLIGHT (Human-frozen): while a probe is testing, a second
    // call dispatches NOTHING — no queue, no parallel request, no restart.
    // The status remains "testing" until that request terminates or the
    // context invalidates it. Controller safety never depends on QML button
    // disabling.
    if (testInFlight_) {
        return;
    }
    if (asyncTestClient_ == nullptr || runtimeConfiguration_ == nullptr) {
        connectionStatusToken_ = QStringLiteral("model_or_params_rejected");
        emit connectionStateChanged();
        return;
    }
    const std::optional<AiProviderKind> kind = currentKind();
    if (!kind.has_value()) {
        connectionStatusToken_ = QStringLiteral("model_or_params_rejected");
        emit connectionStateChanged();
        return;
    }

    // E1a SECRET PRECEDENCE (frozen): non-empty transient secret wins and is
    // NEVER saved; empty → stored credential for the CURRENT FORM provider;
    // absent → local auth_failed with ZERO dispatch; no env fallback; never
    // a credential belonging to another provider.
    QString secret;
    if (!transientSecret.isEmpty()) {
        secret = transientSecret;
    } else {
        secret = resolveStoredTestSecret();
        if (secret.isEmpty()) {
            connectionStatusToken_ = QStringLiteral("auth_failed");
            emit connectionStateChanged();
            return;
        }
    }
    const QString model = modelText_.trimmed();
    if (model.isEmpty()) {
        connectionStatusToken_ = QStringLiteral("model_or_params_rejected");
        emit connectionStateChanged();
        return;
    }

    ++testGeneration_; // new authoritative request epoch
    testInFlight_ = true;
    connectionStatusToken_ = QStringLiteral("testing");
    emit connectionStateChanged();

    const AiTestConnectionRequest request{*kind, model, secret,
                                          kTestConnectionTimeoutMs};
    // Completion callback: QPointer guard makes a completion arriving after
    // controller destruction a harmless no-op; the generation check makes a
    // stale/late completion unable to publish.
    QPointer<AiSettingsController> self(this);
    const qint64 generation = testGeneration_;
    const int requestId = asyncTestClient_->start(
        request, [self, generation](const AiTestConnectionResult &result) {
            if (self.isNull()) {
                return; // controller destroyed: harmless no-op
            }
            self->handleTestConnectionCompletion(generation, result);
        });
    if (requestId < 0) {
        // Dispatch failed (invalid endpoint): zero network I/O happened.
        testInFlight_ = false;
        connectionStatusToken_ = QStringLiteral("network_error");
        emit connectionStateChanged();
        return;
    }
    activeTestRequestId_ = requestId;
}

void AiSettingsController::handleTestConnectionCompletion(
    qint64 generation, const AiTestConnectionResult &result)
{
    // E1a STALE-COMPLETION AUTHORITY: a completion may change the connection
    // status ONLY if it still belongs to the currently authoritative request
    // epoch AND the controller still has it in flight. Late completions
    // after timeout/cancel/invalidation are harmless no-ops.
    if (!testInFlight_ || generation != testGeneration_) {
        return;
    }
    testInFlight_ = false;
    activeTestRequestId_ = -1;
    connectionStatusToken_ = result.token;
    emit connectionStateChanged();
}

void AiSettingsController::invalidateTestConnection()
{
    // E1a CONTEXT INVALIDATION (A/B/C/D) + E1b field-dirty/navigation
    // authority: ANY invalidation advances the generation (an old completion
    // loses publication authority FOREVER), best-effort cancels an in-flight
    // probe, and rests the connection status at not_tested — including after
    // a TERMINAL result (E1b: a result for a field value that has since
    // changed is no longer authoritative). Zero credential/config/revision
    // mutation.
    ++testGeneration_;
    if (testInFlight_) {
        testInFlight_ = false;
        if (asyncTestClient_ != nullptr) {
            asyncTestClient_->cancel(activeTestRequestId_);
        }
        activeTestRequestId_ = -1;
    }
    if (connectionStatusToken_ != QStringLiteral("not_tested")) {
        connectionStatusToken_ = QStringLiteral("not_tested");
        emit connectionStateChanged();
    }
}

bool AiSettingsController::saveConfiguration()
{
    // U2b: RETIRED from the QML meta-object — ordinary public C++ method for
    // existing C++ callers.
    const std::optional<AiProviderKind> kind = currentKind();
    if (!kind.has_value() || runtimeConfiguration_ == nullptr) {
        return false;
    }
    if (runtimeConfiguration_->saveConfiguration(*kind, modelText_)) {
        emit configurationSaved();
        return true;
    }
    return false;
}

void AiSettingsController::deleteSavedCredential()
{
    // E1a: a Delete invocation invalidates any in-flight probe FIRST (§10 D)
    // while keeping the delete's own credential/revision semantics intact.
    invalidateTestConnection();
    const std::optional<AiProviderKind> kind = currentKind();
    if (!kind.has_value() || runtimeConfiguration_ == nullptr) {
        return;
    }
    runtimeConfiguration_->deleteCredential(*kind);
    emit credentialStateChanged();
}

QString AiSettingsController::saveStatusText() const
{
    return aiSaveStatusText(saveStatusToken_);
}

void AiSettingsController::saveSettings(const QString &transientSecret)
{
    // E1a: a Unified Save invocation invalidates any in-flight probe FIRST
    // (§10 C) — the sanitized save result must never be overwritten by a
    // stale probe completion.
    invalidateTestConnection();
    // U2a: Unified Save orchestration (Human-frozen order). No rollback, no
    // plaintext retention beyond the immediate call parameter, no raw error
    // exposure. Blank-key presence uses the provider-scoped NON-SECRET
    // hasCredentialFor() — NEVER a plaintext read.
    if (runtimeConfiguration_ == nullptr) {
        saveStatusToken_ = QStringLiteral("validation_failed");
        emit saveStatusChanged();
        return;
    }
    const std::optional<AiProviderKind> kind = currentKind();
    if (!kind.has_value()) {
        saveStatusToken_ = QStringLiteral("validation_failed");
        emit saveStatusChanged();
        return;
    }
    const QString trimmedModel = modelText_.trimmed();
    if (trimmedModel.isEmpty()) {
        saveStatusToken_ = QStringLiteral("validation_failed");
        emit saveStatusChanged();
        return;
    }

    bool credentialChanged = false;
    if (transientSecret.isEmpty()) {
        // Blank key: NON-SECRET presence check for the CURRENT FORM provider.
        if (!runtimeConfiguration_->hasCredentialFor(*kind)) {
            saveStatusToken_ = QStringLiteral("credential_required");
            emit saveStatusChanged();
            return;
        }
    } else {
        if (!runtimeConfiguration_->saveCredential(*kind, transientSecret)) {
            saveStatusToken_ = QStringLiteral("credential_save_failed");
            emit saveStatusChanged();
            return;
        }
        credentialChanged = true;
        emit credentialStateChanged();
    }

    if (!runtimeConfiguration_->saveConfiguration(*kind, trimmedModel)) {
        saveStatusToken_ =
            credentialChanged
                ? QStringLiteral(
                      "configuration_save_failed_after_credential_saved")
                : QStringLiteral("configuration_save_failed");
        emit saveStatusChanged();
        return;
    }
    saveStatusToken_ = QStringLiteral("saved");
    emit saveStatusChanged();
}

} // namespace modbuslens::ui
