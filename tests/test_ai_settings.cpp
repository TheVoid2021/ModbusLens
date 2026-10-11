// T034-B2.1 — the AI Settings SET matrix (Human §20), deterministic offline.
//
// Every test uses an ISOLATED QSettings INI file in a temp dir and the
// FakeAiCredentialStore: the developer's real settings and the real Windows
// Credential Manager are NEVER touched. Secrets are the fake token
// FAKE_MODBUSLENS_SECRET_B2_DO_NOT_LEAK; the leak audit (SET-19/§24) asserts
// it never reaches QSettings, the forensic log, errors or any file output.
#include <QtTest>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <string>

#include "FakeAiCredentialStore.h"
#include "ui/ai/AiRuntimeConfiguration.h"
#include "ui/ai/AiSettingsController.h"
#include "ui/ai/AiTestConnection.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/candidate/CandidateProviderRunner.h"

using modbuslens::ui::AiProviderKind;
using modbuslens::ui::AiRuntimeConfiguration;
using modbuslens::ui::AiSettingsController;
using modbuslens::ui::AiTestConnectionRequest;
using modbuslens::ui::AiTestConnectionResult;
using modbuslens::ui::FakeAiCredentialStore;
using modbuslens::ui::QtAsyncTestConnectionClient;

namespace {

constexpr char kFakeSecret[] = "FAKE_MODBUSLENS_SECRET_B2_DO_NOT_LEAK";
constexpr char kFakeSecret2[] = "FAKE_MODBUSLENS_SECRET_B2_SECOND_HALF";
constexpr char kU2OldFakeSecret[] = "FAKE_U2_OLD_SECRET";
constexpr char kU2NewFakeSecret[] = "FAKE_U2_NEW_SECRET";
constexpr char kForensicsLogName[] = "/modbuslens_provider_forensics.log";
constexpr char kManualText[] = "Manufacturer: ACME Power Systems Ltd.\n";

[[nodiscard]] QString forensicsLogPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + QLatin1String(kForensicsLogName);
}

// Capture-only HTTP seam (same shape as the frozen transport tests).
class FakeHttpClient : public modbuslens::ui::IChatCompletionsHttpClient
{
public:
    struct Call {
        std::string url;
        std::string bearerToken;
        std::string body;
    };

    modbuslens::ui::ChatCompletionsHttpResult post(
        const modbuslens::ui::ChatCompletionsHttpRequest &request) override
    {
        ++callCount;
        requests.push_back(Call{request.url, request.bearerToken, request.body});
        modbuslens::ui::ChatCompletionsHttpResult result;
        result.status = scriptedStatus;
        result.transportOk = transportOk;
        result.timedOut = scriptedTimedOut;
        result.body = body;
        return result;
    }

    int callCount{0};
    int scriptedStatus{200};
    bool transportOk{true};
    bool scriptedTimedOut{false};
    std::string body;
    std::vector<Call> requests;
};

// Capture-only extraction runner: records applyExplicitConfig calls so the
// controller→runner configuration handoff is observable (SET-22).
class SpyCandidateRunner : public modbuslens::ui::ICandidateExtractionRunner
{
public:
    struct Applied {
        AiProviderKind kind;
        std::string model;
        std::string secret;
    };

    [[nodiscard]] bool begin(const modbuslens::core::ExtractionRequest &,
                             std::uint64_t generation,
                             const CompletionHandler &onDone) override
    {
        ++beginCount_;
        if (failBegin) {
            return false;
        }
        modbuslens::ui::ICandidateExtractionRunner::Completion completion;
        completion.generation = generation;
        completion.ok = scriptedOk;
        completion.failure = scriptedFailure;
        completion.proposals = scriptedProposals;
        lastOnDone = onDone;
        if (!deferCompletion) {
            onDone(completion);
        }
        heldCompletion = completion;
        return true;
    }

    [[nodiscard]] int beginCount() const override { return beginCount_; }

    void applyExplicitConfig(AiProviderKind kind, std::string model,
                             std::string secret) override
    {
        applied.push_back(Applied{kind, std::move(model), std::move(secret)});
    }
    void clearExplicitConfig() override { clearedExplicit = true; }

    int beginCount_{0};
    bool failBegin{false};
    bool scriptedOk{true};
    bool deferCompletion{false};
    modbuslens::core::ProviderExtractionFailure scriptedFailure{
        modbuslens::core::ProviderExtractionFailure::None};
    std::vector<modbuslens::core::CandidateProposal> scriptedProposals;
    CompletionHandler lastOnDone;
    Completion heldCompletion;
    std::vector<Applied> applied;
    bool clearedExplicit{false};
};

[[nodiscard]] modbuslens::core::ManualDocument makeDocument()
{
    modbuslens::core::ManualDocument document;
    document.documentId = "aaaaaaaa-1111-2222-3333-444444444444";
    document.originalFileName = "inverter-manual.txt";
    document.documentType = modbuslens::core::ManualDocumentType::Txt;
    document.originalPath = "Z:/provenance-only/never-read.txt";
    document.contentHash =
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    document.status = modbuslens::core::ManualDocumentStatus::Ready;
    return document;
}

[[nodiscard]] std::string manufacturerProposalContent()
{
    return "{\"proposals\":[{\"target\":\"manufacturer\","
           "\"value\":\"ACME Power Systems Ltd.\","
           "\"evidence\":\"Manufacturer: ACME Power Systems Ltd.\"}]}";
}

} // namespace

class AiSettingsTest : public QObject
{
    Q_OBJECT

    QTemporaryDir settingsDir_;
    FakeAiCredentialStore store_;

    [[nodiscard]] QString settingsPath() const
    {
        return settingsDir_.filePath("ai-settings.ini");
    }

    [[nodiscard]] std::unique_ptr<AiRuntimeConfiguration> makeConfig()
    {
        auto configuration =
            std::make_unique<AiRuntimeConfiguration>(store_, settingsPath());
        configuration->loadPersisted();
        return configuration;
    }

private slots:
    void init()
    {
        // Isolated environment per test: no provider env, no forensic output,
        // clean settings file.
        for (const char *name :
             {"MODELSCOPE_API_KEY", "DEEPSEEK_API_KEY",
              "MODBUSLENS_AI_PROVIDER", "MODBUSLENS_AI_MODEL",
              "MODBUSLENS_MODELSCOPE_MODEL", "MODBUSLENS_PROVIDER_FORENSICS"}) {
            qunsetenv(name);
        }
        QFile::remove(settingsPath());
        QFile::remove(forensicsLogPath());
        store_.clearAll(); // no leftover credential across slots
        store_.failSave = false; // U2a: failure injection never leaks across slots
    }

    // SET-01: provider/model save + reload in a FRESH instance (restart path).
    void set01_saveAndRestartReload()
    {
        {
            auto configuration = makeConfig();
            configuration->loadPersisted();
            QVERIFY(configuration->saveConfiguration(
                AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
            QCOMPARE(configuration->configRevision(), qint64(2));
        }
        {
            auto configuration = makeConfig();
            configuration->loadPersisted();
            QCOMPARE(*configuration->savedProviderKind(),
                     AiProviderKind::DeepSeek);
            QCOMPARE(configuration->savedModel(), QStringLiteral("deepseek-chat"));
        }
    }

    // SET-02: the API key NEVER lands in QSettings (ini content scan).
    void set02_apiKeyNeverEntersQSettings()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret)));
        // Read-back verification: settings carry provider/model and NEVER the
        // secret (QSettings read-back is timing-independent).
        QSettings persisted(settingsPath(), QSettings::IniFormat);
        QCOMPARE(persisted.value(QStringLiteral("ai/provider")).toString(),
                 QStringLiteral("deepseek"));
        QCOMPARE(persisted.value(QStringLiteral("ai/model")).toString(),
                 QStringLiteral("deepseek-chat"));
        QVERIFY(!persisted.contains(QStringLiteral("ai/apiKey")));
        QVERIFY(!persisted.contains(QStringLiteral("ai/token")));
        QVERIFY(!persisted.contains(QStringLiteral("ai/secret")));
        QVERIFY(!QSettings()
                     .value(QStringLiteral("ai/apiKey"),
                            QStringLiteral("x"))
                     .toString()
                     .contains(QString::fromUtf8(kFakeSecret)));
    }

    // SET-03: saveCredential → hasCredential=true.
    void set03_saveCredentialHasCredential()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(!configuration->hasSavedCredential());
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret)));
        QVERIFY(configuration->hasSavedCredential());
    }

    // SET-04: the stored plaintext has NO QML-reachable accessor. Enforced
    // structurally (the settings controller exposes no secret getter) plus
    // behaviorally (controller state after save carries no secret anywhere).
    void set04_storedSecretNotReachableFromQmlSurface()
    {
        FakeHttpClient http;
        AiSettingsController controller(http);
        auto configuration = makeConfig();
        configuration->loadPersisted();
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProviderName(QStringLiteral("deepseek"));
        controller.setModelText(QStringLiteral("deepseek-chat"));
        controller.submitCredential(QString::fromUtf8(kFakeSecret));

        const QMetaObject *meta = controller.metaObject();
        for (int index = 0; index < meta->propertyCount(); ++index) {
            const QMetaProperty property = meta->property(index);
            QVERIFY(property.typeName() != nullptr);
            // No QString-typed property may carry a secret-named surface.
            QVERIFY(!QString::fromLatin1(property.name()).contains(
                QStringLiteral("key"), Qt::CaseInsensitive));
            QVERIFY(!QString::fromLatin1(property.name()).contains(
                QStringLiteral("secret"), Qt::CaseInsensitive));
            if (property.metaType().id() == QMetaType::QString) {
                const QString value = property.read(&controller).toString();
                QVERIFY(!value.contains(QString::fromUtf8(kFakeSecret)));
            }
        }
        for (int index = 0; index < meta->methodCount(); ++index) {
            QVERIFY(!QString::fromLatin1(meta->method(index).name()).contains(
                QStringLiteral("apiKey")));
            QVERIFY(!QString::fromLatin1(meta->method(index).name()).contains(
                QStringLiteral("getSecret")));
        }
    }

    // SET-05: replace credential (save → save with a NEW key wins).
    void set05_replaceCredential()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        configuration->saveCredential(AiProviderKind::DeepSeek,
                                      QString::fromUtf8(kFakeSecret));
        configuration->saveCredential(AiProviderKind::DeepSeek,
                                      QString::fromUtf8(kFakeSecret2));
        const modbuslens::ui::AiSecretResult stored =
            configuration->readStoredCredential(AiProviderKind::DeepSeek);
        QVERIFY(stored.ok);
        QCOMPARE(QString::fromStdString(stored.secret),
                 QString::fromUtf8(kFakeSecret2));
    }

    // SET-06: delete credential → fail-closed unconfigured.
    void set06_deleteCredential()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        configuration->saveCredential(AiProviderKind::DeepSeek,
                                      QString::fromUtf8(kFakeSecret));
        QVERIFY(configuration->hasSavedCredential());
        configuration->deleteCredential(AiProviderKind::DeepSeek);
        QVERIFY(!configuration->hasSavedCredential());
        // Saved provider/model but no credential → resolve = nullopt.
        QVERIFY(!configuration->resolveRequestConfig().has_value());
    }

    // SET-07: per-provider credential isolation.
    void set07_providerCredentialIsolation()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        configuration->saveCredential(AiProviderKind::ModelScope,
                                      QString::fromUtf8(kFakeSecret));
        // DeepSeek must NOT see the ModelScope secret.
        QVERIFY(!configuration->hasSavedCredential()); // saved provider unset
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(!configuration->hasSavedCredential());
        QVERIFY(!configuration->readStoredCredential(
                     AiProviderKind::DeepSeek).ok);
        QVERIFY(configuration->readStoredCredential(
                     AiProviderKind::ModelScope).ok);
    }

    // SET-08: saved GUI config precedence > legacy env.
    void set08_savedConfigBeatsLegacyEnv()
    {
        // Legacy env points at ModelScope with a credential…
        qputenv("MODELSCOPE_API_KEY", "legacy-env-key");
        auto configuration = makeConfig();
        configuration->loadPersisted();
        // …but the USER saved DeepSeek + its own key in the GUI.
        configuration->saveConfiguration(AiProviderKind::DeepSeek,
                                         QStringLiteral("deepseek-chat"));
        configuration->saveCredential(AiProviderKind::DeepSeek,
                                      QString::fromUtf8(kFakeSecret));
        const auto resolved = configuration->resolveRequestConfig();
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->kind, AiProviderKind::DeepSeek);
        QCOMPARE(QString::fromStdString(resolved->model),
                 QStringLiteral("deepseek-chat"));
        QCOMPARE(QString::fromStdString(resolved->secret),
                 QString::fromUtf8(kFakeSecret)); // NOT the legacy env key
    }

    // SET-09: without a saved config the legacy env fallback still works.
    void set09_envFallbackWithoutSavedConfig()
    {
        qputenv("MODELSCOPE_API_KEY", "legacy-env-key");
        auto configuration = makeConfig();
        configuration->loadPersisted();
        const auto resolved = configuration->resolveRequestConfig();
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->kind, AiProviderKind::ModelScope);
        QCOMPARE(QString::fromStdString(resolved->secret),
                 QStringLiteral("legacy-env-key"));
    }

    // SET-10: unknown/corrupt saved provider fails closed.
    void set10_corruptSavedProviderFailsClosed()
    {
        QSettings settings(settingsPath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("ai/provider"),
                          QStringLiteral("not-a-provider"));
        settings.setValue(QStringLiteral("ai/model"),
                          QStringLiteral("whatever"));
        settings.sync();
        auto configuration = makeConfig();
        configuration->loadPersisted();
        // The corrupt token must not resolve — and must not fall back to env.
        qputenv("MODELSCOPE_API_KEY", "legacy-env-key");
        QVERIFY(!configuration->resolveRequestConfig().has_value());
    }

    // SET-11/12: Test Connection uses the FIXED synthetic payload only —
    // no manual text, no profile, no transaction data.
    void set11_testConnectionSyntheticPayloadOnly()
    {
        FakeHttpClient http;
        AiSettingsController controller(http);
        auto configuration = makeConfig();
        configuration->loadPersisted();
        configuration->saveConfiguration(AiProviderKind::DeepSeek,
                                         QStringLiteral("deepseek-chat"));
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProviderName(QStringLiteral("deepseek"));
        controller.setModelText(QStringLiteral("deepseek-chat"));
        QSignalSpy savedSpy(&controller,
                            &AiSettingsController::credentialSaved);
        QSignalSpy failedSpy(&controller,
                             &AiSettingsController::credentialSaveFailed);
        controller.submitCredential(QString::fromUtf8(kFakeSecret));
        QCOMPARE(savedSpy.count(), 1); // submitCredential must succeed
        QCOMPARE(failedSpy.count(), 0);

        controller.testConnection();

        QCOMPARE(http.callCount, 1);
        const std::string body = http.requests.front().body;
        const QJsonObject parsed =
            QJsonDocument::fromJson(QByteArray::fromStdString(body)).object();
        const QString userContent =
            parsed.value(QStringLiteral("messages"))
                .toArray()
                .at(0)
                .toObject()
                .value(QStringLiteral("content"))
                .toString();
        // The probe is the fixed synthetic instruction — and nothing else.
        QCOMPARE(userContent, QStringLiteral("Reply with the single word OK."));
        // No business data may leak into the probe.
        QVERIFY(!QString::fromStdString(body).contains(
            QStringLiteral("ACME")));
        QVERIFY(!QString::fromStdString(body).contains(
            QStringLiteral("Manufacturer")));
        QVERIFY(!QString::fromStdString(body).contains(
            QStringLiteral("register")));
        QVERIFY(!QString::fromStdString(body).contains(
            QStringLiteral("Modbus")));
        // Credential goes ONLY to the Authorization header of the saved
        // provider endpoint.
        QCOMPARE(http.requests.front().bearerToken, std::string(kFakeSecret));
        QVERIFY(http.requests.front().url.find("api.deepseek.com")
                != std::string::npos);
    }

    // SET-13..17: user-friendly status mapping.
    void set13_authFailedMapping()
    {
        runConnectionMapping(401, QStringLiteral("auth_failed"));
    }
    void set14_insufficientBalanceMapping()
    {
        runConnectionMapping(402, QStringLiteral("insufficient_balance"));
    }
    void set15_rateLimitedMapping()
    {
        runConnectionMapping(429, QStringLiteral("rate_limited"));
    }
    void set16_providerServerErrorMapping()
    {
        runConnectionMapping(503, QStringLiteral("provider_error"));
    }
    void set17_timeoutMapping()
    {
        FakeHttpClient http;
        AiSettingsController controller(http);
        auto configuration = makeConfig();
        configuration->loadPersisted();
        configuration->saveConfiguration(AiProviderKind::DeepSeek,
                                         QStringLiteral("deepseek-chat"));
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProviderName(QStringLiteral("deepseek"));
        controller.setModelText(QStringLiteral("deepseek-chat"));
        controller.submitCredential(QString::fromUtf8(kFakeSecret));
        http.transportOk = false;
        http.scriptedTimedOut = true;
        controller.testConnection();
        QCOMPARE(controller.connectionStatusToken(), QStringLiteral("timeout"));
        // Human-readable wording exists (never a raw provider token).
        QVERIFY(controller.connectionStatusText()
                == QStringLiteral("请求超时"));
    }

    // SET-18 companion (controller level): default-off forensic stays silent
    // on the settings path (the transport-level proof lives in the provider
    // matrix fn01/fn02).
    void set18_forensicDefaultOffSettingsPath()
    {
        FakeHttpClient http;
        AiSettingsController controller(http);
        auto configuration = makeConfig();
        configuration->loadPersisted();
        configuration->saveConfiguration(AiProviderKind::DeepSeek,
                                         QStringLiteral("deepseek-chat"));
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProviderName(QStringLiteral("deepseek"));
        controller.setModelText(QStringLiteral("deepseek-chat"));
        controller.submitCredential(QString::fromUtf8(kFakeSecret));
        controller.testConnection();
        QVERIFY(!QFile::exists(forensicsLogPath()));
    }

    // SET-19: the fake secret never reaches the forensic log or errors.
    void set19_secretLeakAudit()
    {
        FakeHttpClient http;
        AiSettingsController controller(http);
        auto configuration = makeConfig();
        configuration->loadPersisted();
        configuration->saveConfiguration(AiProviderKind::DeepSeek,
                                         QStringLiteral("deepseek-chat"));
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProviderName(QStringLiteral("deepseek"));
        controller.setModelText(QStringLiteral("deepseek-chat"));
        controller.submitCredential(QString::fromUtf8(kFakeSecret));
        // A failing connection must not echo the credential.
        http.scriptedStatus = 401;
        http.body = "{\"error\":{\"message\":\"x\",\"type\":"
                    "\"authentication_error\"}}";
        controller.testConnection();
        QVERIFY(!QFile::exists(forensicsLogPath()));
        QVERIFY(controller.connectionStatusText()
                    .contains(QString::fromUtf8(kFakeSecret)) == false);
    }

    // SET-20: saveConfiguration increments the revision.
    void set20_revisionIncrements()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        const qint64 before = configuration->configRevision();
        configuration->saveConfiguration(AiProviderKind::ModelScope,
                                         QStringLiteral("Qwen/Qwen3.5-27B"));
        QCOMPARE(configuration->configRevision(), before + 1);
    }

    // SET-21 / C1 convergence: a completion produced under an older revision
    // is stale — and the PROVIDER/MODEL revision case must converge exactly
    // like the credential cases: saveConfiguration while A is in flight
    // immediately rests the controller at idle, and the old-config completion
    // stays unpublishable. This proves the convergence rule is NOT
    // credential-specific.
    void set21_oldRevisionCompletionIsStale()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();

        SpyCandidateRunner runner;
        runner.deferCompletion = true; // keep request A in flight
        runner.scriptedOk = true;
        runner.scriptedProposals = manufacturerProposals();
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(configuration.get());
        QVERIFY(controller.runtimeConfiguration() != nullptr); // cast sanity

        // Attempt under revision N (the controller snapshots it)…
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QVERIFY(runner.beginCount() == 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("running"));
        // …the user hot-switches the provider/model configuration (revision
        // bumps)…
        configuration->saveConfiguration(AiProviderKind::ModelScope,
                                         QStringLiteral("Qwen/Qwen3.5-27B"));
        // C1 CONVERGENCE (immediately): the obsolete attempt leaves Running
        // and rests at idle; busy is false.
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QVERIFY(!controller.busy());
        // …and the OLD-config completion arrives late: it must be DISCARDED.
        // State remains idle, nothing is published, no failure is claimed.
        QVERIFY(runner.lastOnDone != nullptr);
        runner.lastOnDone(runner.heldCompletion);
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QCOMPARE(controller.candidateCount(), 0);
        QCOMPARE(controller.failureToken(), QString());
    }

    // =====================================================================
    // STALE-CRED-01/02/03 (T034-B2.1): credential-only mutations enter the
    // same invalidation epoch as provider/model saves (B1 slice added the
    // revision bumps) — these slots prove the Candidate revision guard
    // actually consumes that epoch for CREDENTIAL-ONLY changes, closing the
    // evidence gap left by set21 (which covers the config-save variant).
    // All three use the deferred/spy runner harness: request A starts under
    // revision R, stays in flight, the credential mutation happens, and only
    // then does A's completion arrive.
    // =====================================================================

    // STALE-CRED-01 / STATE-01 (C1 convergence): credential REPLACEMENT while
    // A is in flight. Human-frozen semantics: a successful credential mutation
    // is an invalidation-epoch change — the obsolete attempt must leave
    // Running and rest at idle IMMEDIATELY (not when the dead network
    // completion happens to arrive). The old completion must remain
    // unpublishable afterwards: no result, no falsely assigned failure, no
    // mutation of DeviceProfile/Manual/cache state (this controller wires no
    // ProfileController and never writes Manual/cache by construction).
    void staleCred01_credentialReplacementInvalidatesInFlight()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret)));
        // Revision R is the epoch under which request A starts: after the
        // initial config + credential setup, before the in-flight invalidating
        // mutation (same fixture pattern as staleCred02).
        const qint64 revisionR = configuration->configRevision();

        SpyCandidateRunner runner;
        runner.deferCompletion = true; // keep request A in flight
        runner.scriptedOk = true;
        runner.scriptedProposals = manufacturerProposals();
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(configuration.get());
        QVERIFY(controller.runtimeConfiguration() != nullptr);

        // Start request A under revision R.
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("running"));
        QCOMPARE(configuration->configRevision(), revisionR);

        // Credential-only replacement (OLD -> NEW): revision R -> R+1.
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret2)));
        QCOMPARE(configuration->configRevision(), revisionR + 1);

        // C1 CONVERGENCE (immediately after the revision change, NOT at
        // completion time): the obsolete attempt leaves Running at idle;
        // busy is false; nothing is published; no failure is claimed; no
        // auto-restart happens.
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QVERIFY(!controller.busy());
        QCOMPARE(controller.candidateCount(), 0);
        QCOMPARE(controller.failureToken(), QString());

        // The OLD completion arrives afterwards: it must stay unpublishable —
        // the controller rests at idle, the Candidate set is unchanged and no
        // failure token was invented for the discarded attempt.
        QVERIFY(runner.lastOnDone != nullptr);
        runner.lastOnDone(runner.heldCompletion);
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QCOMPARE(controller.candidateCount(), 0);
        QCOMPARE(controller.failureToken(), QString());
    }

    // STALE-CRED-02 / STATE-02 (C1 convergence): credential DELETION while A
    // is in flight. The saved GUI configuration now resolves NOT CONFIGURED
    // (saved config + missing credential, no env revival — AUTH-05 semantics).
    // Convergence: Running -> idle immediately on the revision change; the
    // old completion stays unpublishable; the SAME document can then be
    // explicitly retried (not swallowed by the single-flight guard) and the
    // retry reaches the EXISTING not_configured behavior — no new token.
    void staleCred02_credentialDeletionInvalidatesInFlight()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret)));
        const qint64 revisionR = configuration->configRevision();

        SpyCandidateRunner runner;
        runner.deferCompletion = true;
        runner.scriptedOk = true;
        runner.scriptedProposals = manufacturerProposals();
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(configuration.get());

        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("running"));

        // Delete-blocker regression (§5): while A is Running for THIS document
        // the frozen P0-ML-C guard blocks its deletion.
        const QVariantMap documentMap{
            {QStringLiteral("documentId"),
             QString::fromStdString(makeDocument().documentId)}};
        QCOMPARE(controller.checkManualDeleteAllowed(documentMap)
                     .value(QStringLiteral("token")).toString(),
                 QStringLiteral("manual_delete_blocked_running"));

        // Credential-only DELETION: revision R -> R+1.
        QVERIFY(configuration->deleteCredential(AiProviderKind::DeepSeek));
        QCOMPARE(configuration->configRevision(), revisionR + 1);
        // Saved config + credential missing + no env revival => the new
        // configuration is NOT CONFIGURED (AUTH-05 semantics, asserted here
        // only as context for the convergence).
        QVERIFY(!configuration->resolveRequestConfig().has_value());

        // C1 CONVERGENCE (immediately): Running -> idle; busy false; and the
        // Running delete blocker for this document disappears with the
        // obsolete attempt (no permanent deletion lock). The pure-read check
        // answers with the explicit ALLOWED envelope: ok=true, empty token.
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QVERIFY(!controller.busy());
        const QVariantMap deleteCheck =
            controller.checkManualDeleteAllowed(documentMap);
        QCOMPARE(deleteCheck.value(QStringLiteral("ok")).toBool(), true);
        QVERIFY(deleteCheck.value(QStringLiteral("token")).toString().isEmpty());

        // The OLD completion arrives afterwards: ignored — state stays idle,
        // nothing is published, no failure is claimed.
        QVERIFY(runner.lastOnDone != nullptr);
        runner.lastOnDone(runner.heldCompletion);
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QCOMPARE(controller.candidateCount(), 0);
        QCOMPARE(controller.failureToken(), QString());

        // Same-document explicit retry (§4): NOT swallowed by the single-flight
        // guard (the controller is idle now). The credential is gone, so the
        // provider runner cannot start an attempt: the EXISTING
        // not_configured path (beginAttempt -> !started) must be reached
        // through the ordinary request-start logic — no invented token.
        runner.failBegin = true; // models the unconfigured runner refusing to start
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        QCOMPARE(runner.beginCount(), 2); // the retry was accepted
        QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
        QCOMPARE(controller.failureToken(), QStringLiteral("not_configured"));
    }

    // STALE-CRED-03 (control): a FAILED credential mutation must NOT bump the
    // revision, so in-flight request A completes and publishes normally —
    // the invalidation epoch changes only on SUCCESSFUL mutations.
    void staleCred03_failedCredentialMutationKeepsInFlightValid()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        const qint64 revisionR = configuration->configRevision();

        SpyCandidateRunner runner;
        runner.deferCompletion = true;
        runner.scriptedOk = true;
        runner.scriptedProposals = manufacturerProposals();
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(configuration.get());

        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("running"));

        // FAILED credential replacement (empty secret is rejected by
        // AiRuntimeConfiguration::saveCredential before any store access and
        // before any revision change). No fake-store failure seam is needed:
        // the product-level empty-secret refusal IS the real failure path.
        QVERIFY(!configuration->saveCredential(AiProviderKind::DeepSeek,
                                               QString()));
        QCOMPARE(configuration->configRevision(), revisionR); // unchanged = R

        QVERIFY(runner.lastOnDone != nullptr);
        runner.lastOnDone(runner.heldCompletion);

        // Revision unchanged => A is NOT stale: it publishes exactly like a
        // normal successful completion.
        QCOMPARE(controller.candidateCount(), 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("succeeded"));
        QCOMPARE(controller.failureToken(), QString());
    }

    // =====================================================================
    // C1.2 (T034-B2.1): RUNTIME CONFIGURATION OWNER-SWAP INVALIDATION.
    // Human-frozen PROVENANCE RULE: an in-flight request belongs to the
    // owner that existed when it started. Owner identity change A -> B or
    // A -> nullptr while Running = the attempt is IMMEDIATELY obsolete
    // (invalidate identity + state idle). The epoch is NEVER rebased to the
    // new owner's revision; the old callback stays permanently stale.
    // =====================================================================

    // C1.2-01: A -> B owner swap while X is in flight. The rebase defect
    // (P0): pendingConfigRevision_ must NEVER adopt B's revision — X was
    // authorized by A and must never publish under B.
    void ownerSwap_invalidatesInFlightAttempt()
    {
        // Owner A: fully configured (saved provider/model/credential).
        auto ownerA = makeConfig();
        ownerA->loadPersisted();
        QVERIFY(ownerA->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(ownerA->saveCredential(AiProviderKind::DeepSeek,
                                       QString::fromUtf8(kFakeSecret)));

        SpyCandidateRunner runner;
        runner.deferCompletion = true; // keep request X in flight
        runner.scriptedOk = true;
        runner.scriptedProposals = manufacturerProposals();
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(ownerA.get());

        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("running"));
        QVERIFY(controller.busy());
        const std::uint64_t generationX = controller.currentGeneration();

        // Owner B: an entirely DIFFERENT AiRuntimeConfiguration object (own
        // isolated settings + provider slot), fully configured.
        auto ownerB = makeConfig();
        ownerB->loadPersisted();
        QVERIFY(ownerB->saveConfiguration(
            AiProviderKind::ModelScope, QStringLiteral("Qwen/Qwen3.5-27B")));
        QVERIFY(ownerB->saveCredential(AiProviderKind::ModelScope,
                                       QString::fromUtf8(kFakeSecret2)));

        // The swap: NO new Candidate request is started.
        controller.setRuntimeConfiguration(ownerB.get());

        // Immediate convergence: X is obsolete the moment ownership moves.
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QVERIFY(!controller.busy());

        // The A-origin completion arrives afterwards: permanently stale —
        // it must NOT publish, must NOT claim a failure, must NOT restart
        // anything (generation counter unchanged), must NOT mutate state.
        QVERIFY(runner.lastOnDone != nullptr);
        runner.lastOnDone(runner.heldCompletion);
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QCOMPARE(controller.candidateCount(), 0);
        QCOMPARE(controller.failureToken(), QString());
        QCOMPARE(controller.currentGeneration(), generationX);

        // Connection ownership (§6): A is disconnected — a later A mutation
        // must NOT affect the controller.
        QVERIFY(ownerA->saveCredential(AiProviderKind::DeepSeek,
                                       QString::fromUtf8(kFakeSecret2)));
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));

        // A new request now runs UNDER B and B's own epoch changes DO
        // converge it (B connection live, exactly once).
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 2);
        QCOMPARE(controller.stateToken(), QStringLiteral("running"));
        QVERIFY(ownerB->saveCredential(AiProviderKind::ModelScope,
                                       QString::fromUtf8(kFakeSecret)));
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));

        // Owner swap while NOT Running must not rewrite terminal state (§5).
        controller.setRuntimeConfiguration(ownerA.get());
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QCOMPARE(controller.candidateCount(), 0);
    }

    // C1.2-02: A -> nullptr while X is in flight. The bypass defect (P0):
    // the revision guard is conditional on runtimeConfiguration_ != nullptr,
    // so old-callback safety must come from the invalidated attempt identity
    // and state — never from that conditional guard.
    void nullOwner_invalidatesInFlightAttempt()
    {
        auto ownerA = makeConfig();
        ownerA->loadPersisted();
        QVERIFY(ownerA->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(ownerA->saveCredential(AiProviderKind::DeepSeek,
                                       QString::fromUtf8(kFakeSecret)));

        SpyCandidateRunner runner;
        runner.deferCompletion = true;
        runner.scriptedOk = true;
        runner.scriptedProposals = manufacturerProposals();
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(ownerA.get());

        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("running"));
        QVERIFY(controller.busy());

        controller.setRuntimeConfiguration(nullptr);

        // Immediate convergence.
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QVERIFY(!controller.busy());

        // A is still alive and may keep mutating ITS epoch — the controller
        // is detached and must remain unaffected.
        QVERIFY(ownerA->saveCredential(AiProviderKind::DeepSeek,
                                       QString::fromUtf8(kFakeSecret2)));
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));

        // The A-origin completion: permanently stale (identity invalidated).
        QVERIFY(runner.lastOnDone != nullptr);
        runner.lastOnDone(runner.heldCompletion);
        QCOMPARE(controller.stateToken(), QStringLiteral("idle"));
        QCOMPARE(controller.candidateCount(), 0);
        QCOMPARE(controller.failureToken(), QString());
    }

    // C1.2-03 (control): re-assigning the SAME owner while Running is a
    // no-op — no invalidation, no connection churn, request stays valid and
    // may complete normally. Protects the same-pointer early-return.
    void sameOwner_reassignmentIsNoOp()
    {
        auto ownerA = makeConfig();
        ownerA->loadPersisted();
        QVERIFY(ownerA->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(ownerA->saveCredential(AiProviderKind::DeepSeek,
                                       QString::fromUtf8(kFakeSecret)));

        SpyCandidateRunner runner;
        runner.deferCompletion = true;
        runner.scriptedOk = true;
        runner.scriptedProposals = manufacturerProposals();
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(ownerA.get());

        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(runner.beginCount(), 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("running"));
        QVERIFY(controller.busy());

        // Same pointer again: exactly as today — nothing may change.
        controller.setRuntimeConfiguration(ownerA.get());
        QCOMPARE(controller.stateToken(), QStringLiteral("running"));
        QVERIFY(controller.busy());

        // X completes normally under the unchanged epoch: publishes.
        QVERIFY(runner.lastOnDone != nullptr);
        runner.lastOnDone(runner.heldCompletion);
        QCOMPARE(controller.stateToken(), QStringLiteral("succeeded"));
        QCOMPARE(controller.candidateCount(), 1);
        QCOMPARE(controller.failureToken(), QString());
    }

    // =====================================================================
    // B3A-01 (T034-B2.1 structural security slice): the Global AI
    // Configuration owner must NOT expose configuration/credential MUTATION
    // through its meta-object. QML holds the object reference (context
    // property) but the mutation authority is strictly
    // QML -> AiSettingsController -> AiRuntimeConfiguration. Enumerate the
    // meta-object methods (methodOffset()..methodCount(), no normalized
    // enum-signature assumptions) and assert the mutation method names are
    // ABSENT while the C++-consumed signals REMAIN present. The C++-only
    // secret read (readStoredCredential) must never appear either. This
    // test invokes NO credential operation.
    // =====================================================================
    void b3a01_runtimeConfigurationMutationMethodsNotQmlInvokable()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();

        const QMetaObject *meta = configuration->metaObject();
        QStringList methodNames;
        methodNames.reserve(meta->methodCount() - meta->methodOffset());
        for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
            methodNames << QString::fromLatin1(meta->method(i).name());
        }

        // Pre-fix these three were public slots: QML-callable mutation
        // bypass around AiSettingsController (the confirmed defect).
        QVERIFY(!methodNames.contains(QStringLiteral("saveConfiguration")));
        QVERIFY(!methodNames.contains(QStringLiteral("saveCredential")));
        QVERIFY(!methodNames.contains(QStringLiteral("deleteCredential")));
        // The C++-only secret read must never cross the meta-object either.
        QVERIFY(!methodNames.contains(QStringLiteral("readStoredCredential")));

        // Control (§4): the C++ signal surface must remain intact — the
        // Candidate controller and the settings controller consume these.
        QVERIFY(methodNames.contains(QStringLiteral("configurationChanged")));
        QVERIFY(methodNames.contains(QStringLiteral("credentialStateChanged")));
    }

    // SET-22: after a save, the NEXT request uses the new provider/model/key.
    void set22_newRequestUsesNewConfig()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();

        SpyCandidateRunner runner;
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(configuration.get());

        // First request: env fallback (nothing saved) → ModelScope. The
        // unified resolution path applies the resolved env config explicitly.
        qputenv("MODELSCOPE_API_KEY", "legacy-env-key");
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QVERIFY(!runner.applied.empty());
        QCOMPARE(runner.applied.back().kind, AiProviderKind::ModelScope);
        QCOMPARE(QString::fromStdString(runner.applied.back().secret),
                 QStringLiteral("legacy-env-key"));
        // The user saves DeepSeek + its own key in the GUI…
        configuration->saveConfiguration(AiProviderKind::DeepSeek,
                                         QStringLiteral("deepseek-chat"));
        configuration->saveCredential(AiProviderKind::DeepSeek,
                                      QString::fromUtf8(kFakeSecret));
        // …and the NEXT request must resolve through the saved configuration.
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QVERIFY(!runner.applied.empty());
        const auto &applied = runner.applied.back();
        QCOMPARE(applied.kind, AiProviderKind::DeepSeek);
        QCOMPARE(QString::fromStdString(applied.model),
                 QStringLiteral("deepseek-chat"));
        QCOMPARE(QString::fromStdString(applied.secret),
                 QString::fromUtf8(kFakeSecret));
    }

    // =====================================================================
    // T034-B2.1 SECURITY CORRECTION SLICE B1 — credential authority
    // (Human-frozen CASE A/B/C semantics) + credential-mutation revision.
    // =====================================================================

    // AUTH-01: saved DeepSeek config + missing stored credential + legacy
    // DeepSeek env key present => resolveRequestConfig() = nullopt. The
    // legacy env key must have ZERO effect inside the saved-config branch.
    void auth01_savedConfigMissingCredentialNoEnvFallback()
    {
        qputenv("DEEPSEEK_API_KEY",
                "FAKE_LEGACY_DEEPSEEK_KEY_SHOULD_NOT_BE_USED");
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(!configuration->hasSavedCredential()); // store: missing

        const auto resolved = configuration->resolveRequestConfig();
        // Pre-fix RED: the saved-config branch falls back to
        // DEEPSEEK_API_KEY and resolves with the legacy env key.
        QVERIFY(!resolved.has_value()); // NOT CONFIGURED, fail closed
    }

    // AUTH-02: same frozen semantics for the ModelScope profile.
    void auth02_savedConfigMissingCredentialNoEnvFallbackModelScope()
    {
        qputenv("MODELSCOPE_API_KEY",
                "FAKE_LEGACY_MODELSCOPE_KEY_SHOULD_NOT_BE_USED");
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::ModelScope, QStringLiteral("fake-model")));
        QVERIFY(!configuration->hasSavedCredential());

        const auto resolved = configuration->resolveRequestConfig();
        QVERIFY(!resolved.has_value()); // NOT CONFIGURED, fail closed
    }

    // AUTH-03 (control): NO saved config => transitional env fallback works.
    void auth03_envFallbackWithoutSavedConfig()
    {
        qputenv("MODBUSLENS_AI_PROVIDER", "deepseek");
        qputenv("MODBUSLENS_AI_MODEL", "fake-deepseek-model");
        qputenv("DEEPSEEK_API_KEY", "FAKE_ENV_KEY");
        auto configuration = makeConfig();
        configuration->loadPersisted();

        const auto resolved = configuration->resolveRequestConfig();
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->kind, AiProviderKind::DeepSeek);
        QCOMPARE(QString::fromStdString(resolved->model),
                 QStringLiteral("fake-deepseek-model"));
        QCOMPARE(QString::fromStdString(resolved->secret),
                 QStringLiteral("FAKE_ENV_KEY"));
    }

    // AUTH-04 (control): corrupt saved token fails closed even when a valid
    // complete env fallback exists — env must NOT rescue corrupt state.
    void auth04_corruptSavedProviderFailsClosedWithEnvPresent()
    {
        qputenv("MODBUSLENS_AI_PROVIDER", "deepseek");
        qputenv("MODBUSLENS_AI_MODEL", "fake-deepseek-model");
        qputenv("DEEPSEEK_API_KEY", "FAKE_ENV_KEY");
        {
            QSettings settings(settingsPath(), QSettings::IniFormat);
            settings.setValue(QStringLiteral("ai/provider"),
                              QStringLiteral("not-a-provider"));
            settings.setValue(QStringLiteral("ai/model"),
                              QStringLiteral("whatever"));
            settings.sync();
        }
        auto configuration = makeConfig();
        configuration->loadPersisted();

        QVERIFY(!configuration->resolveRequestConfig().has_value());
    }

    // AUTH-05: Delete API Key must DISABLE the saved GUI configuration —
    // deleting the stored credential must not silently re-enable resolution
    // through the legacy DEEPSEEK_API_KEY env variable.
    void auth05_deleteCredentialDisablesSavedConfig()
    {
        qputenv("DEEPSEEK_API_KEY",
                "FAKE_LEGACY_DEEPSEEK_KEY_SHOULD_NOT_BE_USED");
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret)));
        QVERIFY(configuration->resolveRequestConfig().has_value());

        QVERIFY(configuration->deleteCredential(AiProviderKind::DeepSeek));
        // Pre-fix RED: resolve falls back to the legacy env key.
        QVERIFY(!configuration->resolveRequestConfig().has_value());
    }

    // REV-01: first successful credential save bumps the revision.
    void rev01_credentialSaveBumpsRevision()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        const qint64 before = configuration->configRevision();
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret)));
        // Pre-fix RED: saveCredential does not touch the revision.
        QCOMPARE(configuration->configRevision(), before + 1);
    }

    // REV-02: credential replacement bumps the revision again.
    void rev02_credentialReplaceBumpsRevision()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        const qint64 before = configuration->configRevision();
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret)));
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret2)));
        QCOMPARE(configuration->configRevision(), before + 2);
    }

    // REV-03: successful delete of an EXISTING credential bumps revision.
    void rev03_credentialDeleteBumpsRevision()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        QVERIFY(configuration->saveCredential(AiProviderKind::DeepSeek,
                                              QString::fromUtf8(kFakeSecret)));
        const qint64 before = configuration->configRevision();
        QVERIFY(configuration->deleteCredential(AiProviderKind::DeepSeek));
        // Pre-fix RED: deleteCredential does not touch the revision.
        QCOMPARE(configuration->configRevision(), before + 1);
    }

    // REV-04 (control): failed credential save leaves revision unchanged.
    void rev04_failedCredentialSaveNoRevisionChange()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        const qint64 before = configuration->configRevision();
        QVERIFY(!configuration->saveCredential(AiProviderKind::DeepSeek,
                                               QString())); // empty = failure
        QCOMPARE(configuration->configRevision(), before);
    }

    // REV-05 (control): failed delete (nothing stored) leaves revision
    // unchanged.
    void rev05_failedCredentialDeleteNoRevisionChange()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat")));
        const qint64 before = configuration->configRevision();
        QVERIFY(!configuration->deleteCredential(AiProviderKind::DeepSeek));
        QCOMPARE(configuration->configRevision(), before);
    }

    // =====================================================================
    // U1 (T034-B2.1 Unified Save prerequisite): CONFIGURATION PERSISTENCE
    // TRUTH. saveConfiguration must not publish a new active provider/model
    // or revision when QSettings persistence actually fails. Deterministic
    // filesystem fixture: a regular FILE placed where the INI's parent
    // directory would have to be created makes the target path unwritable.
    // =====================================================================

    // U1-01: persistence failure on a dead location must return false and
    // publish NOTHING (no in-memory config, no revision, no signal).
    // Fixture (Human-approved, proven reliable in U1-02): the target INI
    // PATHNAME itself is replaced by a same-named DIRECTORY — QSettings sync
    // then cannot open it for writing and reports non-NoError. (The earlier
    // parent-directory-blocked shape was REJECTED: QSettings silently swallows
    // writes when the parent directory cannot be created and status() stays
    // NoError.)
    void u1_configPersistenceFailureDoesNotPublish()
    {
        QTemporaryDir u1Dir;
        QVERIFY(u1Dir.isValid());
        const QString iniPath = u1Dir.filePath(QStringLiteral("settings.ini"));
        // Fresh temp dir: no regular INI file exists at the pathname; create
        // a DIRECTORY at that exact pathname instead.
        QVERIFY(QDir().mkpath(iniPath));

        // Fixture sanity FIRST: independent probe must deterministically fail
        // (non-NoError status). If this fails, the RED below is meaningless
        // and the slice STOPs.
        {
            QSettings probe(iniPath, QSettings::IniFormat);
            probe.setValue(QStringLiteral("probe"), 1);
            probe.sync();
            QVERIFY2(probe.status() != QSettings::NoError,
                     "U1 fixture broken: directory path unexpectedly writable");
        }

        AiRuntimeConfiguration blocked(store_, iniPath);
        blocked.loadPersisted();
        QCOMPARE(blocked.hasSavedConfiguration(), false);
        const qint64 revisionR = blocked.configRevision();

        int changedCount = 0;
        const QMetaObject::Connection conn =
            connect(&blocked, &AiRuntimeConfiguration::configurationChanged,
                    this, [&changedCount](qint64) { ++changedCount; });

        const bool ok = blocked.saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat"));

        // U1 FINAL contract (pre-fix REAL RED lives here: current code
        // returns true and publishes runtime state despite the failure).
        QCOMPARE(ok, false);
        QCOMPARE(blocked.hasSavedConfiguration(), false);
        QVERIFY(!blocked.savedProviderKind().has_value());
        QCOMPARE(blocked.savedModel(), QString());
        QCOMPARE(blocked.configRevision(), revisionR);
        QCOMPARE(changedCount, 0);
        disconnect(conn);
    }

    // U1-02 (strong control): a FAILED replacement attempt must preserve the
    // previously persisted/active configuration A and its revision. Fixture:
    // after A is persisted, the INI FILE is replaced by a same-named
    // DIRECTORY so subsequent sync cannot open it for writing.
    void u1_failedReplacementPreservesActiveConfiguration()
    {
        QTemporaryDir u1Dir;
        QVERIFY(u1Dir.isValid());
        const QString iniPath = u1Dir.filePath(QStringLiteral("settings.ini"));

        AiRuntimeConfiguration config(store_, iniPath);
        config.loadPersisted();
        // Success control (§5): A persists AND is immediately readable by a
        // FRESH QSettings reader without destroying the writer.
        QVERIFY(config.saveConfiguration(AiProviderKind::ModelScope,
                                         QStringLiteral("Qwen/Qwen3.5-27B")));
        QCOMPARE(config.savedModel(), QStringLiteral("Qwen/Qwen3.5-27B"));
        {
            QSettings freshReader(iniPath, QSettings::IniFormat);
            QCOMPARE(freshReader.value(QStringLiteral("ai/provider")).toString(),
                     QStringLiteral("modelscope"));
            QCOMPARE(freshReader.value(QStringLiteral("ai/model")).toString(),
                     QStringLiteral("Qwen/Qwen3.5-27B"));
        }
        const qint64 revisionA = config.configRevision();

        int changedCount = 0;
        const QMetaObject::Connection conn =
            connect(&config, &AiRuntimeConfiguration::configurationChanged,
                    this, [&changedCount](qint64) { ++changedCount; });

        // Break persistence: INI file → same-named directory.
        QVERIFY(QFile(iniPath).remove());
        QVERIFY(QDir().mkpath(iniPath));
        {
            QSettings probe(iniPath, QSettings::IniFormat);
            probe.setValue(QStringLiteral("probe"), 1);
            probe.sync();
            QVERIFY2(probe.status() != QSettings::NoError,
                     "U1 fixture broken: directory path unexpectedly writable");
        }

        const bool ok = config.saveConfiguration(
            AiProviderKind::DeepSeek, QStringLiteral("deepseek-chat"));

        QCOMPARE(ok, false);
        // Configuration A remains the active/persisted truth.
        QVERIFY(config.savedProviderKind().has_value());
        QCOMPARE(*config.savedProviderKind(), AiProviderKind::ModelScope);
        QCOMPARE(config.savedModel(), QStringLiteral("Qwen/Qwen3.5-27B"));
        QCOMPARE(config.configRevision(), revisionA);
        QCOMPARE(changedCount, 0);
        disconnect(conn);
    }

    // =====================================================================
    // U2a (T034-B2.1 Unified Save): CONTROLLER UNIFIED-SAVE CORE.
    // Authority chain frozen: QML → AiSettingsController →
    // AiRuntimeConfiguration. Result tokens are stable machine-readable
    // outcomes; status text is fixed/sanitized (never a secret, path, or raw
    // error). Semantic slots invoke the capability DYNAMICALLY so they stay
    // compile-safe pre-fix: before implementation they fail at the
    // capability gate and are classified BLOCKED, not independent REDs.
    // =====================================================================

    // U2a capability RED (§6): the controller must own the Unified Save
    // authority — one QML-facing method plus a sanitized status surface.
    // Inspected through the meta-object so this stays compile-safe pre-fix.
    void u2a_capabilityUnifiedSaveMethodAndStatusPresent()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());

        const QMetaObject *meta = controller.metaObject();
        QVERIFY2(meta->indexOfMethod("saveSettings(QString)") >= 0,
                 "Unified Save controller capability is absent");
        QVERIFY2(meta->indexOfProperty("saveStatusToken") >= 0,
                 "saveStatusToken property is absent");
        QVERIFY2(meta->indexOfProperty("saveStatusText") >= 0,
                 "saveStatusText property is absent");
        QVERIFY2(meta->indexOfSignal("saveStatusChanged()") >= 0,
                 "saveStatusChanged signal is absent");
        // U2a continuation: the provider-scoped presence query must ALSO be
        // invisible to the QML meta-object (ordinary public C++ only).
        QVERIFY2(meta->indexOfMethod("hasCredentialFor(AiProviderKind)") < 0,
                 "hasCredentialFor must not be QML-visible");
    }

    // U2a dynamic invocation helper: compile-safe pre-fix; returns false
    // while the capability is absent (BLOCKED classification gate).
    [[nodiscard]] static bool invokeUnifiedSave(
        AiSettingsController &controller, const QString &transientSecret)
    {
        return QMetaObject::invokeMethod(&controller, "saveSettings",
                                         Q_ARG(QString, transientSecret));
    }

    [[nodiscard]] static QString saveStatusTokenOf(
        AiSettingsController &controller)
    {
        return controller.property("saveStatusToken").toString();
    }

    // U1-proven persistence-failure fixture: replace the configured INI file
    // with a same-named DIRECTORY, then verify via an independent probe.
    void breakPersistence(const QString &iniPath)
    {
        QVERIFY(QFile(iniPath).remove());
        QVERIFY(QDir().mkpath(iniPath));
        QSettings probe(iniPath, QSettings::IniFormat);
        probe.setValue(QStringLiteral("probe"), 1);
        probe.sync();
        QVERIFY2(probe.status() != QSettings::NoError,
                 "U2a fixture broken: directory path unexpectedly writable");
    }

    // US-01 (blank + existing credential): reuse the stored credential,
    // publish provider/model, configuration revision +1 only, token=saved.
    void us01_blankKeyWithExistingCredentialSavesConfigOnly()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        QVERIFY(configuration->saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        const qint64 revisionBefore = configuration->configRevision();
        const int savesBefore = store_.saveCalls;

        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-b"));

        const bool invoked =
            invokeUnifiedSave(controller, QString()); // BLANK transient key
        QVERIFY2(invoked, "Unified Save capability absent (BLOCKED)");

        QCOMPARE(saveStatusTokenOf(controller), QStringLiteral("saved"));
        QVERIFY(!controller.property("saveStatusText").toString().isEmpty());
        QCOMPARE(store_.saveCalls, savesBefore); // zero credential writes
        QVERIFY(store_.hasCredential(
            modbuslens::ui::AiProviderKind::DeepSeek));
        QCOMPARE(configuration->savedModel(), QStringLiteral("model-b"));
        QCOMPARE(configuration->configRevision(), revisionBefore + 1);
    }

    // US-02 (blank + NO credential): reject BEFORE any publication —
    // fail-closed; the previous active configuration survives untouched.
    void us02_blankKeyWithoutCredentialFailsClosed()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        const qint64 revisionBefore = configuration->configRevision();
        const int savesBefore = store_.saveCalls;
        int changedCount = 0;
        const QMetaObject::Connection conn =
            connect(configuration.get(),
                    &AiRuntimeConfiguration::configurationChanged, this,
                    [&changedCount](qint64) { ++changedCount; });

        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProperty("providerName", QStringLiteral("modelscope"));
        controller.setProperty("modelText", QStringLiteral("model-b"));

        const bool invoked = invokeUnifiedSave(controller, QString());
        QVERIFY2(invoked, "Unified Save capability absent (BLOCKED)");

        QCOMPARE(saveStatusTokenOf(controller),
                 QStringLiteral("credential_required"));
        QCOMPARE(store_.saveCalls, savesBefore); // no credential write
        // Previous active provider/model remains; nothing published.
        QCOMPARE(configuration->savedModel(), QStringLiteral("model-a"));
        QVERIFY(configuration->savedProviderKind().has_value());
        QCOMPARE(*configuration->savedProviderKind(),
                 modbuslens::ui::AiProviderKind::DeepSeek);
        QCOMPARE(configuration->configRevision(), revisionBefore);
        QCOMPARE(changedCount, 0);
        disconnect(conn);
    }

    // US-03 (new credential + config success): credential +1, config +1,
    // total +2, token=saved.
    void us03_newCredentialAndConfigSave()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        const qint64 revisionBefore = configuration->configRevision();
        int changedCount = 0;
        const QMetaObject::Connection conn =
            connect(configuration.get(),
                    &AiRuntimeConfiguration::configurationChanged, this,
                    [&changedCount](qint64) { ++changedCount; });

        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-c"));

        const bool invoked =
            invokeUnifiedSave(controller, QString::fromUtf8(kU2NewFakeSecret));
        QVERIFY2(invoked, "Unified Save capability absent (BLOCKED)");

        QCOMPARE(saveStatusTokenOf(controller), QStringLiteral("saved"));
        QCOMPARE(store_.saveCalls, 1); // exactly one credential save
        QVERIFY(store_.hasCredential(
            modbuslens::ui::AiProviderKind::DeepSeek));
        QCOMPARE(configuration->savedModel(), QStringLiteral("model-c"));
        QCOMPARE(configuration->configRevision(), revisionBefore + 2);
        QCOMPARE(changedCount, 2);
        disconnect(conn);
    }

    // US-03B (replacement + config success): replacement exactly once,
    // total revision +2, token=saved. Replacement proven with a BOOLEAN
    // equality check — no fake secret is ever echoed on failure.
    void us03b_credentialReplacementAndConfigSave()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        QVERIFY(configuration->saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        const int savesBefore = store_.saveCalls; // setup save (OLD) baseline
        const qint64 revisionBefore = configuration->configRevision();

        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-b"));

        const bool invoked = invokeUnifiedSave(
            controller, QString::fromUtf8(kU2NewFakeSecret));
        QVERIFY2(invoked, "Unified Save capability absent (BLOCKED)");

        QCOMPARE(saveStatusTokenOf(controller), QStringLiteral("saved"));
        QCOMPARE(store_.saveCalls, savesBefore + 1); // replacement exactly once
        const auto stored = store_.readCredential(
            modbuslens::ui::AiProviderKind::DeepSeek);
        QVERIFY(stored.ok);
        // Non-echoing replacement proof: boolean equality only.
        QVERIFY(stored.secret == std::string(kU2NewFakeSecret));
        QVERIFY(stored.secret != std::string(kU2OldFakeSecret));
        QCOMPARE(configuration->savedModel(), QStringLiteral("model-b"));
        QCOMPARE(configuration->configRevision(), revisionBefore + 2);
    }

    // US-04 (credential save failure): the configuration save must NOT be
    // attempted; previous active config and revision remain untouched.
    void us04_credentialSaveFailureBlocksConfiguration()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        const qint64 revisionBefore = configuration->configRevision();
        const int savesBefore = store_.saveCalls;
        int changedCount = 0;
        const QMetaObject::Connection conn =
            connect(configuration.get(),
                    &AiRuntimeConfiguration::configurationChanged, this,
                    [&changedCount](qint64) { ++changedCount; });

        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-b"));
        store_.failSave = true;

        const bool invoked = invokeUnifiedSave(
            controller, QString::fromUtf8(kU2NewFakeSecret));
        QVERIFY2(invoked, "Unified Save capability absent (BLOCKED)");
        store_.failSave = false;

        QCOMPARE(saveStatusTokenOf(controller),
                 QStringLiteral("credential_save_failed"));
        QCOMPARE(configuration->savedModel(), QStringLiteral("model-a"));
        QCOMPARE(configuration->configRevision(), revisionBefore);
        QCOMPARE(changedCount, 0);
        disconnect(conn);
    }

    // US-05 (replacement failure): OLD credential remains authoritative, NEW
    // credential does not replace it. Boolean comparisons only — no echo.
    void us05_replacementFailurePreservesOldCredential()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        QVERIFY(configuration->saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        const qint64 revisionBefore = configuration->configRevision();

        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-b"));
        store_.failSave = true;

        const bool invoked = invokeUnifiedSave(
            controller, QString::fromUtf8(kU2NewFakeSecret));
        QVERIFY2(invoked, "Unified Save capability absent (BLOCKED)");
        store_.failSave = false;

        QCOMPARE(saveStatusTokenOf(controller),
                 QStringLiteral("credential_save_failed"));
        const auto stored = store_.readCredential(
            modbuslens::ui::AiProviderKind::DeepSeek);
        QVERIFY(stored.ok);
        QVERIFY(stored.secret == std::string(kU2OldFakeSecret));
        QVERIFY(stored.secret != std::string(kU2NewFakeSecret));
        QCOMPARE(configuration->savedModel(), QStringLiteral("model-a"));
        QCOMPARE(configuration->configRevision(), revisionBefore);
    }

    // US-06 (CRITICAL partial-failure truth): credential save SUCCEEDS, then
    // configuration persistence FAILS (U1 directory fixture). The frozen
    // wording: old active provider/model preserved, but the credential epoch
    // MAY already have changed. Never "old configuration completely
    // preserved".
    void us06_configFailureAfterCredentialSaved()
    {
        // Dedicated isolated INI path: this slot deliberately corrupts its
        // own persistence location, so it must never share the shared
        // settingsPath() with other slots.
        QTemporaryDir us06Dir;
        QVERIFY(us06Dir.isValid());
        const QString iniPath = us06Dir.filePath(QStringLiteral("settings.ini"));
        AiRuntimeConfiguration configuration(store_, iniPath);
        configuration.loadPersisted();
        QVERIFY(configuration.saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        const int savesBefore = store_.saveCalls; // setup save (OLD) baseline
        const qint64 revisionBefore = configuration.configRevision();
        int changedCount = 0;
        const QMetaObject::Connection conn =
            connect(&configuration,
                    &AiRuntimeConfiguration::configurationChanged, this,
                    [&changedCount](qint64) { ++changedCount; });

        AiSettingsController controller;
        controller.setRuntimeConfiguration(&configuration);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-b"));

        breakPersistence(iniPath);

        const bool invoked = invokeUnifiedSave(
            controller, QString::fromUtf8(kU2NewFakeSecret));
        QVERIFY2(invoked, "Unified Save capability absent (BLOCKED)");

        QCOMPARE(saveStatusTokenOf(controller),
                 QStringLiteral(
                     "configuration_save_failed_after_credential_saved"));
        // Credential epoch committed exactly once (setup baseline + 1).
        QCOMPARE(store_.saveCalls, savesBefore + 1);
        const auto stored = store_.readCredential(
            modbuslens::ui::AiProviderKind::DeepSeek);
        QVERIFY(stored.ok);
        QVERIFY(stored.secret == std::string(kU2NewFakeSecret));
        // Old active provider/model preserved; NO configuration publication.
        QCOMPARE(configuration.savedModel(), QStringLiteral("model-a"));
        // Revision movement = credential epoch ONLY (+1, not config +1).
        QCOMPARE(configuration.configRevision(), revisionBefore + 1);
        QCOMPARE(changedCount, 1);
        disconnect(conn);
    }

    // US-06B (config failure WITHOUT new credential): blank key + existing
    // credential + persistence failure → credential untouched, nothing
    // published, revision unchanged.
    void us06b_configFailureWithoutNewCredential()
    {
        // Dedicated isolated INI path (same isolation rule as us06).
        QTemporaryDir us06bDir;
        QVERIFY(us06bDir.isValid());
        const QString iniPath = us06bDir.filePath(QStringLiteral("settings.ini"));
        AiRuntimeConfiguration configuration(store_, iniPath);
        configuration.loadPersisted();
        QVERIFY(configuration.saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        const qint64 revisionBefore = configuration.configRevision();
        const int savesBefore = store_.saveCalls;
        int changedCount = 0;
        const QMetaObject::Connection conn =
            connect(&configuration,
                    &AiRuntimeConfiguration::configurationChanged, this,
                    [&changedCount](qint64) { ++changedCount; });

        AiSettingsController controller;
        controller.setRuntimeConfiguration(&configuration);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-b"));

        breakPersistence(iniPath);

        const bool invoked = invokeUnifiedSave(controller, QString());
        QVERIFY2(invoked, "Unified Save capability absent (BLOCKED)");

        QCOMPARE(saveStatusTokenOf(controller),
                 QStringLiteral("configuration_save_failed"));
        QCOMPARE(store_.saveCalls, savesBefore);
        const auto stored = store_.readCredential(
            modbuslens::ui::AiProviderKind::DeepSeek);
        QVERIFY(stored.ok);
        QVERIFY(stored.secret == std::string(kU2OldFakeSecret));
        QCOMPARE(configuration.savedModel(), QStringLiteral("model-a"));
        QCOMPARE(configuration.configRevision(), revisionBefore);
        QCOMPARE(changedCount, 0);
        disconnect(conn);
    }

    // US-07 (validation failure BEFORE credential dispatch): whitespace model
    // and an unresolvable provider identity must fail closed with ZERO
    // credential/store/config mutation.
    void us07_validationFailureBeforeCredentialDispatch()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        const qint64 revisionBefore = configuration->configRevision();
        const int savesBefore = store_.saveCalls;

        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("   "));

        const bool invoked = invokeUnifiedSave(
            controller, QString::fromUtf8(kU2NewFakeSecret));
        QVERIFY2(invoked, "Unified Save capability absent (BLOCKED)");

        QCOMPARE(saveStatusTokenOf(controller),
                 QStringLiteral("validation_failed"));
        QCOMPARE(store_.saveCalls, savesBefore);
        QCOMPARE(configuration->configRevision(), revisionBefore);
        QVERIFY(!configuration->hasSavedConfiguration());

        // Unresolvable provider identity (naturally representable state).
        controller.setProperty("providerName", QStringLiteral("bogus-provider"));
        controller.setProperty("modelText", QStringLiteral("model-x"));
        const bool invoked2 = invokeUnifiedSave(
            controller, QString::fromUtf8(kU2NewFakeSecret));
        QVERIFY(invoked2);
        QCOMPARE(saveStatusTokenOf(controller),
                 QStringLiteral("validation_failed"));
        QCOMPARE(store_.saveCalls, savesBefore);
        QCOMPARE(configuration->configRevision(), revisionBefore);
    }

    // U2b (§20): ONE QML persistence authority. The controller meta-object
    // exposes saveSettings; the obsolete QML-callable persistence actions
    // (submitCredential / saveConfiguration) are retired to ordinary public
    // C++ methods. The AiRuntimeConfiguration B3a surface stays closed
    // (including the U2a hasCredentialFor presence query).
    void u2b_metaObjectPersistenceSurface()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());

        const QMetaObject *meta = controller.metaObject();
        QVERIFY(meta->indexOfMethod("saveSettings(QString)") >= 0);
        QVERIFY2(meta->indexOfMethod("submitCredential(QString)") < 0,
                 "submitCredential must not be QML-callable after U2b");
        QVERIFY2(meta->indexOfMethod("saveConfiguration()") < 0,
                 "saveConfiguration must not be QML-callable after U2b");

        const QMetaObject *runtimeMeta = configuration->metaObject();
        QVERIFY2(runtimeMeta->indexOfMethod(
                     "saveConfiguration(AiProviderKind,QString)") < 0,
                 "runtime config save must stay invisible to QML");
        QVERIFY2(runtimeMeta->indexOfMethod(
                     "saveCredential(AiProviderKind,QString)") < 0,
                 "runtime credential save must stay invisible to QML");
        QVERIFY2(runtimeMeta->indexOfMethod(
                     "deleteCredential(AiProviderKind)") < 0,
                 "runtime credential delete must stay invisible to QML");
        QVERIFY2(runtimeMeta->indexOfMethod(
                     "readStoredCredential(AiProviderKind)") < 0,
                 "runtime secret read must stay invisible to QML");
        QVERIFY2(runtimeMeta->indexOfMethod(
                     "hasCredentialFor(AiProviderKind)") < 0,
                 "runtime presence query must stay invisible to QML");
    }

private:
    // =====================================================================
    // E1a (T034-B2.1-E): ASYNC TEST CONNECTION CORE + STALE-COMPLETION
    // SAFETY. Deterministic fake async client: request capture, pending
    // state, explicit completion triggers, cancel observation, late
    // completion after cancel/timeout. No thread, no real timer, no network.
    // ( QTest discovers private slots the same way as public ones. )
    // =====================================================================

    class FakeAsyncTestConnectionClient final
        : public QObject,
          public modbuslens::ui::IAsyncTestConnectionClient
    {
    public:
        struct Pending
        {
            modbuslens::ui::AiTestConnectionRequest request;
            std::function<void(const modbuslens::ui::AiTestConnectionResult &)>
                onDone;
        };

        [[nodiscard]] int start(
            const modbuslens::ui::AiTestConnectionRequest &request,
            const std::function<void(const modbuslens::ui::AiTestConnectionResult &)>
                &onDone) override
        {
            requests.push_back(request);
            pending.push_back(Pending{request, onDone});
            return static_cast<int>(requests.size()); // ids start at 1
        }

        void cancel(int requestId) override
        {
            ++cancelCount;
            lastCancelledId = requestId;
            // Best-effort ONLY: the completion may still be delivered later
            // (late-completion safety is proven independently of cancel).
        }

        // Completes the OLDEST pending request with the given result and
        // RETAINS its callback so a LATE completion can still be delivered.
        void finishPendingWithResult(const modbuslens::ui::AiTestConnectionResult &result)
        {
            if (pending.empty()) {
                return;
            }
            Pending entry = std::move(pending.front());
            pending.erase(pending.begin());
            retainedCompletion = entry.onDone;
            if (entry.onDone) {
                entry.onDone(result);
            }
        }

        // Delivers a completion through the RETAINED callback (simulates the
        // real network finishing after cancel/timeout already published).
        void deliverLateCompletion(const modbuslens::ui::AiTestConnectionResult &result)
        {
            if (retainedCompletion) {
                retainedCompletion(result);
            }
        }

        [[nodiscard]] bool hasPending() const { return !pending.empty(); }

        std::vector<modbuslens::ui::AiTestConnectionRequest> requests;
        std::vector<Pending> pending;
        std::function<void(const modbuslens::ui::AiTestConnectionResult &)>
            retainedCompletion;
        int cancelCount{0};
        int lastCancelledId{-1};
    };

    [[nodiscard]] static modbuslens::ui::AiTestConnectionResult
    probeResult(const QString &token)
    {
        modbuslens::ui::AiTestConnectionResult result;
        result.token = token;
        return result;
    }

    // E1A-01/02 (delayed completion): testConnection returns while the fake

private slots:
    // request is still pending (token=testing, exactly one request, no
    // terminal result); completing it publishes connection_ok exactly once.
    void e1a01_delayedRequestReturnsWhilePending()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection(); // REAL no-arg entry (stored credential)

        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("testing"));
        QCOMPARE(fake.requests.size(), std::size_t{1});
        QVERIFY(fake.hasPending());
        QCOMPARE(store_.saveCalls, 1); // setup save only

        // E1A-02: terminal completion publishes exactly once.
        fake.finishPendingWithResult(probeResult(QStringLiteral("connection_ok")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("connection_ok"));
        QVERIFY(!fake.hasPending());
    }

    // E1A-03 (non-empty transient secret argument): the fake request uses the
    // supplied transient secret for the CURRENT FORM provider/model; stored
    // credential is not substituted; credential store unchanged; revision
    // unchanged. Boolean comparisons only — secrets are never echoed.
    void e1a03_transientSecretArgumentUsedWithoutPersistence()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        const qint64 revisionBefore = configuration->configRevision();
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));
        // NOTE: no stored credential for DeepSeek (store empty) — proves the
        // transient secret is used and NOT read from the store.

        controller.testConnection(QString::fromUtf8(kU2NewFakeSecret));

        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("testing"));
        QCOMPARE(fake.requests.size(), std::size_t{1});
        QVERIFY(fake.requests.back().secret == std::string(kU2NewFakeSecret));
        QVERIFY(fake.requests.back().secret != std::string(kU2OldFakeSecret));
        QCOMPARE(store_.saveCalls, 0); // no credential persistence
        QCOMPARE(configuration->configRevision(), revisionBefore);
        QVERIFY(!store_.hasCredential(
            modbuslens::ui::AiProviderKind::DeepSeek));
    }

    // E1A-04 (empty transient secret + stored credential): the stored
    // credential for the CURRENT FORM provider is used; one request.
    void e1a04_emptySecretUsesStoredCredential()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection(); // BLANK transient secret

        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("testing"));
        QCOMPARE(fake.requests.size(), std::size_t{1});
        QVERIFY(fake.requests.back().secret == std::string(kU2OldFakeSecret));
    }

    // E1A-05 (empty transient secret + no stored credential): local
    // auth_failed, ZERO dispatch, no environment fallback.
    void e1a05_emptySecretNoCredentialFailsLocally()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection(); // BLANK + nothing stored

        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("auth_failed"));
        QCOMPARE(fake.requests.size(), std::size_t{0});
        QVERIFY(!fake.hasPending());
    }

    // E1A-06 (double Test while pending): single-in-flight — no queue, no
    // second request; status remains testing.
    void e1a06_doubleTestWhilePendingStaysSingleFlight()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection();
        controller.testConnection(); // second call while testing

        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("testing"));
        QCOMPARE(fake.requests.size(), std::size_t{1});
        QCOMPARE(fake.pending.size(), std::size_t{1});
    }

    // E1A-07 (provider change invalidates): old request loses publication
    // authority; status = not_tested; late completion remains not_tested.
    void e1a07_providerChangeInvalidatesPendingProbe()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection();
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("testing"));

        controller.setProperty("providerName", QStringLiteral("modelscope"));

        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("not_tested"));
        QCOMPARE(fake.cancelCount, 1); // best-effort cancel observed

        // LATE completion of the invalidated request: must be ignored.
        fake.finishPendingWithResult(
            probeResult(QStringLiteral("connection_ok")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("not_tested"));
    }

    // E1A-08 (model change invalidates): same stale-completion contract.
    void e1a08_modelChangeInvalidatesPendingProbe()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection();
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("testing"));

        controller.setProperty("modelText", QStringLiteral("model-b"));

        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("not_tested"));
        QCOMPARE(fake.cancelCount, 1);

        fake.finishPendingWithResult(
            probeResult(QStringLiteral("connection_ok")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("not_tested"));
    }

    // E1A-09 (saveSettings invalidates): the probe loses publication
    // authority; the unified save then publishes normally; a late probe
    // completion must NOT overwrite the save result.
    void e1a09_saveSettingsInvalidatesPendingProbe()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-b"));

        controller.testConnection(); // probe pending (blank → stored OLD)
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("testing"));

        controller.saveSettings(QString()); // unified save while probe pending

        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("not_tested"));
        QCOMPARE(controller.property("saveStatusToken").toString(),
                 QStringLiteral("saved"));

        // LATE probe completion: must not overwrite the save result.
        fake.finishPendingWithResult(
            probeResult(QStringLiteral("connection_ok")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("not_tested"));
        QCOMPARE(controller.property("saveStatusToken").toString(),
                 QStringLiteral("saved"));
    }

    // E1A-10 (deleteSavedCredential invalidates): probe loses authority;
    // delete retains its existing credential/revision semantics; late
    // completion ignored.
    void e1a10_deleteSavedCredentialInvalidatesPendingProbe()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        QVERIFY(configuration->saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        const qint64 revisionBefore = configuration->configRevision();
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection();
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("testing"));

        controller.deleteSavedCredential();

        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("not_tested"));
        QVERIFY(!store_.hasCredential(
            modbuslens::ui::AiProviderKind::DeepSeek));
        QCOMPARE(configuration->configRevision(), revisionBefore + 1);

        fake.finishPendingWithResult(
            probeResult(QStringLiteral("connection_ok")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("not_tested"));
    }

    // E1A-11 (timeout): the timeout publishes its terminal result once; the
    // late network completion after the timeout must be ignored.
    void e1a11_timeoutPublishesOnceAndIgnoresLateCompletion()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection();

        // The production client's timeout owner publishes the terminal
        // timeout result (modeled by the fake delivering it).
        fake.finishPendingWithResult(probeResult(QStringLiteral("timeout")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("timeout"));

        // LATE completion after the timeout: ignored.
        fake.deliverLateCompletion(
            probeResult(QStringLiteral("connection_ok")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("timeout"));
    }

    // E1A-12 (sanitized error mapping controls): representative taxonomy
    // entries still classify through the async core; no raw body/error.
    void e1a12_sanitizedErrorMappingRetained()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection();
        fake.finishPendingWithResult(probeResult(QStringLiteral("auth_failed")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("auth_failed"));

        controller.testConnection();
        fake.finishPendingWithResult(
            probeResult(QStringLiteral("rate_limited")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("rate_limited"));

        controller.testConnection();
        fake.finishPendingWithResult(
            probeResult(QStringLiteral("provider_error")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("provider_error"));

        controller.testConnection();
        fake.finishPendingWithResult(
            probeResult(QStringLiteral("network_error")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("network_error"));

        controller.testConnection();
        fake.finishPendingWithResult(
            probeResult(QStringLiteral("invalid_response")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("invalid_response"));
        // Fixed sanitized text exists for every terminal token above.
        QVERIFY(!controller.property("connectionStatusText").toString().isEmpty());
    }

    // E1A-13 (side effects): across success and failure the probe touches
    // NO persisted state: saved model, provider kind and revision unchanged;
    // credential store presence unchanged.
    void e1a13_probeHasZeroPersistenceSideEffects()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(configuration->saveConfiguration(
            modbuslens::ui::AiProviderKind::DeepSeek,
            QStringLiteral("model-a")));
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        const qint64 revisionBefore = configuration->configRevision();
        FakeAsyncTestConnectionClient fake;
        AiSettingsController controller;
        controller.setRuntimeConfiguration(configuration.get());
        controller.setAsyncTestConnectionClientForAutomation(&fake);
        controller.setProperty("providerName", QStringLiteral("deepseek"));
        controller.setProperty("modelText", QStringLiteral("model-a"));

        controller.testConnection(); // blank → stored credential
        fake.finishPendingWithResult(
            probeResult(QStringLiteral("connection_ok")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("connection_ok"));
        QCOMPARE(configuration->savedModel(), QStringLiteral("model-a"));
        QVERIFY(configuration->savedProviderKind().has_value());
        QCOMPARE(*configuration->savedProviderKind(),
                 modbuslens::ui::AiProviderKind::DeepSeek);
        QCOMPARE(configuration->configRevision(), revisionBefore);
        QVERIFY(store_.hasCredential(
            modbuslens::ui::AiProviderKind::DeepSeek));

        controller.testConnection(); // failure path
        fake.finishPendingWithResult(probeResult(QStringLiteral("auth_failed")));
        QCOMPARE(controller.property("connectionStatusToken").toString(),
                 QStringLiteral("auth_failed"));
        QCOMPARE(configuration->savedModel(), QStringLiteral("model-a"));
        QCOMPARE(configuration->configRevision(), revisionBefore);
        QVERIFY(store_.hasCredential(
            modbuslens::ui::AiProviderKind::DeepSeek));
    }

    // E1A-14 (destruction while pending): the controller is destroyed with a
    // probe still pending; the retained completion then fires into a dead
    // controller — must be a harmless no-op (QPointer guard), no crash.
    void e1a14_destructionWhilePendingIsHarmless()
    {
        auto configuration = makeConfig();
        configuration->loadPersisted();
        QVERIFY(store_.saveCredential(modbuslens::ui::AiProviderKind::DeepSeek,
                                      std::string_view(kU2OldFakeSecret)));
        FakeAsyncTestConnectionClient fake;
        {
            AiSettingsController controller;
            controller.setRuntimeConfiguration(configuration.get());
            controller.setAsyncTestConnectionClientForAutomation(&fake);
            controller.setProperty("providerName", QStringLiteral("deepseek"));
            controller.setProperty("modelText", QStringLiteral("model-a"));
            controller.testConnection();
            QVERIFY(fake.hasPending());
        } // controller destroyed with the probe pending

        // The retained completion fires AFTER controller destruction: the
        // QPointer guard makes it a no-op (no crash, no publication).
        fake.finishPendingWithResult(
            probeResult(QStringLiteral("connection_ok")));
        QVERIFY(!fake.hasPending());
    }

    // =====================================================================
private:
    // E2 (T034-B2.1-E): PRODUCTION QT ASYNC BACKEND — LOOPBACK RUNTIME
    // ACCEPTANCE. Drives the REAL QtAsyncTestConnectionClient against a
    // minimal 127.0.0.1-only loopback HTTP fake (QTcpServer, ephemeral
    // port). Verifies the REAL QNetworkAccessManager / QNetworkReply /
    // QTimer lifecycle: async dispatch, finished callback, timeout abort,
    // late-completion suppression, per-request cleanup. The endpoint
    // resolver ctor is the TEST-ONLY seam (loopback-only guard); the
    // production default ctor path is untouched.
    // =====================================================================

    // Minimal loopback HTTP fake: binds QHostAddress::LocalHost on an
    // ephemeral port ONLY. Supports: accept, request capture, immediate /
    // delayed / no response, scripted status+body, late attempt after
    // timeout. Deliberately NOT a general HTTP framework.
    class LoopbackHttpServer final : public QObject
    {
    public:
        struct PlannedResponse
        {
            bool respond{true}; // false = accept but never reply (timeout)
            int status{200};
            QByteArray body{"{\"choices\":[{\"message\":{\"role\":"
                            "\"assistant\",\"content\":\"OK\"}}]}"};
            int delayMs{0}; // >0 = delayed write (real async return window)
            bool closeAfterWrite{true};
        };

        [[nodiscard]] bool start()
        {
            if (!server_.listen(QHostAddress::LocalHost, 0)) {
                return false;
            }
            QObject::connect(&server_, &QTcpServer::newConnection, &server_,
                             [this] {
                                 while (QTcpSocket *socket =
                                            server_.nextPendingConnection()) {
                                     ++connectionCount_;
                                     sockets_.push_back(socket);
                                     lastSocket_ = socket;
                                     QObject::connect(
                                         socket, &QTcpSocket::disconnected,
                                         socket, [this, socket] {
                                             sockets_.removeOne(socket);
                                         });
                                     QObject::connect(
                                         socket, &QTcpSocket::readyRead, socket,
                                         [this, socket] {
                                             buffers_[socket] += socket->readAll();
                                             tryProcessRequest(socket);
                                         });
                                 }
                             });
            return true;
        }

        [[nodiscard]] QUrl url() const
        {
            return QUrl(QStringLiteral("http://127.0.0.1:%1/v1/chat/completions")
                            .arg(server_.serverPort()));
        }

        void enqueueResponse(const PlannedResponse &response)
        {
            planned_.enqueue(response);
        }

        [[nodiscard]] int connectionCount() const { return connectionCount_; }
        [[nodiscard]] int requestCount() const { return requestCount_; }
        [[nodiscard]] bool hasPlanned() const { return !planned_.isEmpty(); }

        // E2-07: after a timeout, attempt a late reply on the last
        // connection (may fail silently if the client already aborted /
        // disconnected — that IS the observable behavior under test).
        void attemptLateReply(const PlannedResponse &response)
        {
            if (lastSocket_ == nullptr) {
                return;
            }
            const QByteArray payload = buildHttpResponse(response);
            lastSocket_->write(payload);
            lastSocket_->flush();
        }

        // E2-C1: DETERMINISTIC REQUEST-IDENTITY ROUTING. A route binds a
        // response plan to a NON-SECRET model marker found in the request
        // body — never to TCP arrival order. hold=true captures the
        // connection; releaseHeldByModel() sends the response later.
        struct ModelRoute
        {
            QByteArray modelMarker;
            bool hold{false};
            PlannedResponse response;
        };

        void routeByModelMarker(const QByteArray &modelMarker, bool hold,
                                const PlannedResponse &response)
        {
            ModelRoute route;
            route.modelMarker = modelMarker;
            route.hold = hold;
            route.response = response;
            modelRoutes_.push_back(route);
        }

        [[nodiscard]] int heldCount() const
        {
            return static_cast<int>(held_.size());
        }

        // Releases the FIRST held connection whose request carried this
        // model marker. Returns false when nothing is held for it.
        bool releaseHeldByModel(const QByteArray &modelMarker)
        {
            for (int i = 0; i < held_.size(); ++i) {
                if (held_.at(i).marker == modelMarker) {
                    const HeldConnection connection = held_.takeAt(i);
                    if (connection.socket != nullptr
                        && connection.socket->state()
                               == QAbstractSocket::ConnectedState) {
                        connection.socket->write(connection.payload);
                        if (connection.payload.contains("Connection: close")) {
                            connection.socket->disconnectFromHost();
                        }
                    }
                    return true;
                }
            }
            return false;
        }

        // E2-07 variant: close the last connection without replying.
        void closeLastConnection()
        {
            if (lastSocket_ != nullptr) {
                lastSocket_->disconnectFromHost();
            }
        }

    private:
        // Non-secret identity marker extraction: reads the JSON "model"
        // string from the request body. NEVER logs Authorization/body.
        [[nodiscard]] static QByteArray extractModelMarker(
            const QByteArray &body)
        {
            const QByteArray key = "\"model\":\"";
            const int idx = body.indexOf(key);
            if (idx < 0) {
                return {};
            }
            const int start = idx + key.size();
            const int end = body.indexOf('"', start);
            if (end < 0) {
                return {};
            }
            return body.mid(start, end - start);
        }

        static QByteArray buildHttpResponse(const PlannedResponse &response)
        {
            const QByteArray statusLine =
                "HTTP/1.1 " + QByteArray::number(response.status) +
                (response.status < 400 ? " OK" : " Error") + "\r\n";
            QByteArray head = statusLine;
            head += "Content-Type: application/json\r\n";
            head += "Content-Length: " + QByteArray::number(response.body.size())
                    + "\r\n";
            if (response.closeAfterWrite) {
                head += "Connection: close\r\n";
            }
            head += "\r\n";
            return head + response.body;
        }

        void tryProcessRequest(QTcpSocket *socket)
        {
            QByteArray &buffer = buffers_[socket];
            const int headerEnd = buffer.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
            }
            int contentLength = 0;
            const QList<QByteArray> headerLines =
                buffer.left(headerEnd).split('\n');
            for (const QByteArray &line : headerLines) {
                if (line.toLower().startsWith("content-length:")) {
                    contentLength = line.mid(15).trimmed().toInt();
                }
            }
            if (buffer.size() - (headerEnd + 4) < contentLength) {
                return; // wait for the full body
            }
            lastRequest_ = buffer;
            ++requestCount_;
            buffers_.remove(socket);
            // E2-C1: identity routing FIRST (deterministic; never TCP
            // arrival order). Non-secret marker = the model string.
            const QByteArray modelMarker = extractModelMarker(buffer);
            for (const ModelRoute &route : modelRoutes_) {
                if (modelMarker != route.modelMarker) {
                    continue;
                }
                if (route.hold) {
                    HeldConnection held;
                    held.socket = socket;
                    held.marker = route.modelMarker;
                    held.payload = buildHttpResponse(route.response);
                    held_.push_back(held);
                } else {
                    socket->write(buildHttpResponse(route.response));
                    socket->disconnectFromHost();
                }
                return;
            }
            if (planned_.isEmpty()) {
                return; // unplanned connection: accept and hold
            }
            const PlannedResponse response = planned_.dequeue();
            if (!response.respond) {
                return; // timeout scenario: hold the connection silently
            }
            const QByteArray payload = buildHttpResponse(response);
            if (response.delayMs > 0) {
                QTimer::singleShot(response.delayMs, socket,
                                   [socket, payload] {
                                       if (socket->state()
                                           == QAbstractSocket::ConnectedState) {
                                           socket->write(payload);
                                           if (payload.contains(
                                                   "Connection: close")) {
                                               socket->disconnectFromHost();
                                           }
                                       }
                                   });
            } else {
                socket->write(payload);
                if (payload.contains("Connection: close")) {
                    socket->disconnectFromHost();
                }
            }
        }

        struct HeldConnection
        {
            QTcpSocket *socket{nullptr};
            QByteArray marker;
            QByteArray payload;
        };

        QTcpServer server_;
        QList<QTcpSocket *> sockets_;
        QHash<QTcpSocket *, QByteArray> buffers_;
        QQueue<PlannedResponse> planned_;
        QTcpSocket *lastSocket_{nullptr};
        int connectionCount_{0};
        int requestCount_{0};
        QByteArray lastRequest_;
        QList<ModelRoute> modelRoutes_;
        QList<HeldConnection> held_;
    };

    [[nodiscard]] static QByteArray validProbeBody()
    {
        return QByteArrayLiteral(
            "{\"choices\":[{\"message\":{\"role\":\"assistant\","
            "\"content\":\"OK\"}}]}");
    }

    // E2-00 (PUBLIC NETWORK HARD FAIL GUARD): a test resolver returning a
private slots:
    // production-looking / non-loopback URL must be REJECTED before any
    // network dispatch (start == -1, no callback, no loopback connection).
    void e2_00_loopbackHardGuard()
    {
        // Loopback server running so any accidental real dispatch to a
        // non-loopback host is provably unrelated to it.
        LoopbackHttpServer server;
        QVERIFY(server.start());
        FakeAiCredentialStore credentialStore; // unused here; presence guard
        Q_UNUSED(credentialStore);

        int callbackCount = 0;
        AiTestConnectionRequest request;
        request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
        request.model = QStringLiteral("model-a");
        request.secret = QStringLiteral("fake-test-token");
        request.timeoutMs = 80;

        // Production-looking HTTPS URL → rejected.
        {
            QtAsyncTestConnectionClient client(
                [](AiProviderKind) {
                    return QUrl(QStringLiteral(
                        "https://api.deepseek.com/v1/chat/completions"));
                });
            const int id = client.start(
                request, [&callbackCount](const AiTestConnectionResult &) {
                    ++callbackCount;
                });
            QCOMPARE(id, -1);
        }
        // Non-loopback HTTP LAN URL → rejected.
        {
            QtAsyncTestConnectionClient client(
                [](AiProviderKind) {
                    return QUrl(QStringLiteral(
                        "http://192.168.1.5/v1/chat/completions"));
                });
            const int id = client.start(
                request, [&callbackCount](const AiTestConnectionResult &) {
                    ++callbackCount;
                });
            QCOMPARE(id, -1);
        }
        // Legitimate loopback URL with the SAME resolver-mode → accepted.
        {
            QtAsyncTestConnectionClient client(
                [&server](AiProviderKind) { return server.url(); });
            const int id = client.start(
                request, [&callbackCount](const AiTestConnectionResult &) {
                    ++callbackCount;
                });
            QVERIFY(id >= 1);
        }
        // Nothing may have connected to the loopback fake from the rejected
        // attempts, and no callback fired for them.
        QTest::qWait(30);
        QCOMPARE(callbackCount, 0);
        QCOMPARE(server.requestCount(), 0);
    }

    // E2-01 + E2-02 (real async dispatch + 200 success): start() returns
    // while the server holds the delayed response (token=testing, no
    // terminal callback); completion then publishes connection_ok exactly
    // once and stays published.
    void e2_01_e2_02_realAsyncDispatchAndSuccess()
    {
        LoopbackHttpServer server;
        QVERIFY(server.start());
        QtAsyncTestConnectionClient client(
            [&server](AiProviderKind) { return server.url(); });

        AiTestConnectionRequest request;
        request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
        request.model = QStringLiteral("model-a");
        request.secret = QStringLiteral("fake-test-token");
        request.timeoutMs = 2000;

        int callbackCount = 0;
        QString publishedToken;
        // Delayed server response: async return window is observable.
        server.enqueueResponse(LoopbackHttpServer::PlannedResponse{
            true, 200, validProbeBody(), 150});

        const int id = client.start(
            request, [&callbackCount, &publishedToken](
                         const AiTestConnectionResult &result) {
                ++callbackCount;
                publishedToken = result.token;
            });
        QVERIFY(id >= 1);

        // Async return window: server may have received the request, but the
        // delayed response has NOT been sent yet.
        QTRY_COMPARE(server.requestCount(), 1);
        QCOMPARE(callbackCount, 0);
        // Completion arrives through the real event loop.
        QTRY_COMPARE(callbackCount, 1);
        QCOMPARE(publishedToken, QStringLiteral("connection_ok"));

        // Extra event churn: still exactly one terminal publication.
        QTest::qWait(30);
        QCOMPARE(callbackCount, 1);
    }

    // E2-03 (auth failure taxonomy through the REAL backend).
    void e2_03_authFailure()
    {
        LoopbackHttpServer server;
        QVERIFY(server.start());
        QtAsyncTestConnectionClient client(
            [&server](AiProviderKind) { return server.url(); });

        AiTestConnectionRequest request;
        request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
        request.model = QStringLiteral("model-a");
        request.secret = QStringLiteral("fake-test-token");
        request.timeoutMs = 80;

        int callbackCount = 0;
        QString publishedToken;
        server.enqueueResponse(LoopbackHttpServer::PlannedResponse{
            true, 401, QByteArrayLiteral("{\"error\":{}}")});

        client.start(request,
                     [&callbackCount, &publishedToken](
                         const AiTestConnectionResult &result) {
                         ++callbackCount;
                         publishedToken = result.token;
                     });

        QTRY_COMPARE(callbackCount, 1);
        QCOMPARE(publishedToken, QStringLiteral("auth_failed"));
        QTest::qWait(20);
        QCOMPARE(callbackCount, 1);
    }

    // E2-04 (rate limit taxonomy through the REAL backend).
    void e2_04_rateLimited()
    {
        LoopbackHttpServer server;
        QVERIFY(server.start());
        QtAsyncTestConnectionClient client(
            [&server](AiProviderKind) { return server.url(); });

        AiTestConnectionRequest request;
        request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
        request.model = QStringLiteral("model-a");
        request.secret = QStringLiteral("fake-test-token");
        request.timeoutMs = 80;

        int callbackCount = 0;
        QString publishedToken;
        server.enqueueResponse(LoopbackHttpServer::PlannedResponse{
            true, 429, QByteArrayLiteral("{\"error\":{}}")});

        client.start(request,
                     [&callbackCount, &publishedToken](
                         const AiTestConnectionResult &result) {
                         ++callbackCount;
                         publishedToken = result.token;
                     });

        QTRY_COMPARE(callbackCount, 1);
        QCOMPARE(publishedToken, QStringLiteral("rate_limited"));
    }

    // E2-05 (malformed 200 payload): the REAL backend must surface
    // invalid_response without changing the parser/classifier contract.
    void e2_05_invalidResponse()
    {
        LoopbackHttpServer server;
        QVERIFY(server.start());
        QtAsyncTestConnectionClient client(
            [&server](AiProviderKind) { return server.url(); });

        AiTestConnectionRequest request;
        request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
        request.model = QStringLiteral("model-a");
        request.secret = QStringLiteral("fake-test-token");
        request.timeoutMs = 80;

        int callbackCount = 0;
        QString publishedToken;
        server.enqueueResponse(LoopbackHttpServer::PlannedResponse{
            true, 200, QByteArrayLiteral("this is not json")});

        client.start(request,
                     [&callbackCount, &publishedToken](
                         const AiTestConnectionResult &result) {
                         ++callbackCount;
                         publishedToken = result.token;
                     });

        QTRY_COMPARE(callbackCount, 1);
        QCOMPARE(publishedToken, QStringLiteral("invalid_response"));
    }

    // E2-06 (REAL timeout): the server accepts but never replies; a SHORT
    // test timeout (80ms) drives the real QTimer owner → abort + terminal
    // timeout publication.
    void e2_06_realTimeout()
    {
        LoopbackHttpServer server;
        QVERIFY(server.start());
        QtAsyncTestConnectionClient client(
            [&server](AiProviderKind) { return server.url(); });

        AiTestConnectionRequest request;
        request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
        request.model = QStringLiteral("model-a");
        request.secret = QStringLiteral("fake-test-token");
        request.timeoutMs = 80; // SHORT test timeout; NOT the 45s production

        int callbackCount = 0;
        QString publishedToken;
        server.enqueueResponse(LoopbackHttpServer::PlannedResponse{
            false, 200, validProbeBody()}); // accept, never reply

        client.start(request,
                     [&callbackCount, &publishedToken](
                         const AiTestConnectionResult &result) {
                         ++callbackCount;
                         publishedToken = result.token;
                     });

        QCOMPARE(server.requestCount(), 0); // not yet processed
        QTRY_COMPARE(callbackCount, 1);
        QCOMPARE(publishedToken, QStringLiteral("timeout"));
        QVERIFY(server.requestCount() >= 1); // the request DID arrive
    }

    // E2-07 (late server behavior after timeout): after the terminal timeout,
    // a late server reply / close / further events must NOT double-publish.
    void e2_07_lateAfterTimeoutDoesNotDoublePublish()
    {
        LoopbackHttpServer server;
        QVERIFY(server.start());
        QtAsyncTestConnectionClient client(
            [&server](AiProviderKind) { return server.url(); });

        AiTestConnectionRequest request;
        request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
        request.model = QStringLiteral("model-a");
        request.secret = QStringLiteral("fake-test-token");
        request.timeoutMs = 80;

        int callbackCount = 0;
        QString publishedToken;
        server.enqueueResponse(LoopbackHttpServer::PlannedResponse{
            false, 200, validProbeBody()}); // never replies in time

        client.start(request,
                     [&callbackCount, &publishedToken](
                         const AiTestConnectionResult &result) {
                         ++callbackCount;
                         publishedToken = result.token;
                     });

        QTRY_COMPARE(callbackCount, 1);
        QCOMPARE(publishedToken, QStringLiteral("timeout"));

        // Late server behavior: reply attempt + close + event churn.
        server.attemptLateReply(LoopbackHttpServer::PlannedResponse{
            true, 200, validProbeBody()});
        server.closeLastConnection();
        QTest::qWait(40);

        QCOMPARE(callbackCount, 1); // NO second terminal publication
        QCOMPARE(publishedToken, QStringLiteral("timeout"));
    }

    // E2-08 (EXPLICIT CANCEL CONTRACT, source-verified): cancel() itself does
    // NOT publish — it aborts the reply; the subsequent finished enters the
    // normal completion path and classifies the aborted exchange as
    // network_error. The caller's generation guard owns stale suppression
    // (already proven in E1A-07..10).
    void e2_08_explicitCancelContract()
    {
        LoopbackHttpServer server;
        QVERIFY(server.start());
        QtAsyncTestConnectionClient client(
            [&server](AiProviderKind) { return server.url(); });

        AiTestConnectionRequest request;
        request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
        request.model = QStringLiteral("model-a");
        request.secret = QStringLiteral("fake-test-token");
        request.timeoutMs = 80;

        int callbackCount = 0;
        QString publishedToken;
        server.enqueueResponse(LoopbackHttpServer::PlannedResponse{
            true, 200, validProbeBody(), 300}); // delayed; cancel first

        const int id = client.start(
            request, [&callbackCount, &publishedToken](
                         const AiTestConnectionResult &result) {
                ++callbackCount;
                publishedToken = result.token;
            });
        QVERIFY(id >= 1);
        QTRY_COMPARE(server.requestCount(), 1); // request reached the server

        client.cancel(id); // best-effort abort — NOT a publication

        QTRY_COMPARE(callbackCount, 1); // the aborted reply publishes ONCE
        QCOMPARE(publishedToken, QStringLiteral("network_error"));
        QTest::qWait(30);
        QCOMPARE(callbackCount, 1); // no second callback
    }

    // E2-09 (multi-request isolation): two independent requests on ONE
    // client; ids distinct; each callback exactly once; B completes BEFORE A
    // (reverse order) without cross-request interference.
    void e2_09_multiRequestIsolation()
    {
        // E2-C1 CORRECTED VERSION: request-identity routing + explicit
        // HOLD/RELEASE. The old FIFO-by-arrival-order plan assignment raced
        // with the two start() calls (the accepted FINAL-gate RED). The fake
        // server now binds responses to NON-SECRET model markers found in
        // the request body — deterministic regardless of TCP arrival order.
        LoopbackHttpServer server;
        QVERIFY(server.start());
        QtAsyncTestConnectionClient client(
            [&server](AiProviderKind) { return server.url(); });

        // Identity routing: A = HELD until explicit release; B = immediate.
        server.routeByModelMarker(
            QByteArrayLiteral("fake-model-a"), true,
            LoopbackHttpServer::PlannedResponse{true, 200, validProbeBody()});
        server.routeByModelMarker(
            QByteArrayLiteral("fake-model-b"), false,
            LoopbackHttpServer::PlannedResponse{true, 200, validProbeBody()});

        AiTestConnectionRequest request;
        request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
        request.secret = QStringLiteral("fake-test-token");
        request.timeoutMs = 2000;

        int aCount = 0;
        int bCount = 0;
        QString aToken;
        QString bToken;

        // 1. start A (held by the server — no response yet).
        request.model = QStringLiteral("fake-model-a");
        const int idA = client.start(
            request, [&aCount, &aToken](const AiTestConnectionResult &result) {
                ++aCount;
                aToken = result.token;
            });
        // 2. start B (independent request; may arrive before OR after A).
        request.model = QStringLiteral("fake-model-b");
        const int idB = client.start(
            request, [&bCount, &bToken](const AiTestConnectionResult &result) {
                ++bCount;
                bToken = result.token;
            });

        // 3. Request identities: distinct, both dispatched.
        QVERIFY(idA >= 1);
        QVERIFY(idB >= 1);
        QVERIFY(idA != idB);

        // 4. Wait for BOTH requests to reach the fake server and be routed
        // (A held, B answered). Order-independent by construction. B's
        // immediate response delivery may lag one event-loop iteration
        // behind the server-side write, hence QTRY for the client-side
        // callback. A is HELD — aCount==0 is timing-independent.
        QTRY_COMPARE(server.requestCount(), 2);
        QTRY_COMPARE(bCount, 1); // B answered immediately
        QCOMPARE(bToken, QStringLiteral("connection_ok"));
        QCOMPARE(aCount, 0); // A still HELD — no cross-request leakage
        QCOMPARE(server.heldCount(), 1);

        // 5. Explicit RELEASE of A by its identity marker.
        QVERIFY(server.releaseHeldByModel(
            QByteArrayLiteral("fake-model-a")));

        // 6. A completes after release: exactly once, connection_ok.
        QTRY_COMPARE(aCount, 1);
        QCOMPARE(aToken, QStringLiteral("connection_ok"));
        QCOMPARE(bCount, 1); // B untouched by A's release
        QCOMPARE(bToken, QStringLiteral("connection_ok"));
        QCOMPARE(server.heldCount(), 0);

        // 7. Extra event churn: counts stay exactly one each.
        QTest::qWait(30);
        QCOMPARE(aCount, 1);
        QCOMPARE(bCount, 1);
    }

    // E2-10 (client destruction while pending): no crash, no dangling
    // callback, no publication after destruction.
    void e2_10_clientDestructionWhilePending()
    {
        LoopbackHttpServer server;
        QVERIFY(server.start());

        int callbackCount = 0;
        {
            QtAsyncTestConnectionClient client(
                [&server](AiProviderKind) { return server.url(); });
            AiTestConnectionRequest request;
            request.kind = modbuslens::ui::AiProviderKind::DeepSeek;
            request.model = QStringLiteral("model-a");
            request.secret = QStringLiteral("fake-test-token");
            request.timeoutMs = 80;
            server.enqueueResponse(LoopbackHttpServer::PlannedResponse{
                true, 200, validProbeBody(), 300}); // delayed, outlives client
            const int id = client.start(
                request,
                [&callbackCount](const AiTestConnectionResult &) {
                    ++callbackCount;
                });
            QVERIFY(id >= 1);
            QTRY_COMPARE(server.requestCount(), 1);
            // Destroy the client while the request is still pending.
        }

        // Server-side delayed write attempt + event churn after destruction.
        QTest::qWait(200);
        QCOMPARE(callbackCount, 0); // no publication from a destroyed client
    }

private:
    void runConnectionMapping(int status, const QString &expectedToken)
    {
        FakeHttpClient http;
        AiSettingsController controller(http);
        auto configuration = makeConfig();
        configuration->loadPersisted();
        configuration->saveConfiguration(AiProviderKind::DeepSeek,
                                         QStringLiteral("deepseek-chat"));
        controller.setRuntimeConfiguration(configuration.get());
        controller.setProviderName(QStringLiteral("deepseek"));
        controller.setModelText(QStringLiteral("deepseek-chat"));
        controller.submitCredential(QString::fromUtf8(kFakeSecret));
        http.scriptedStatus = status;
        http.body = status >= 400
                        ? "{\"error\":{\"message\":\"x\",\"type\":\"e\"}}"
                        : manufacturerProposalContent();
        controller.testConnection();
        QCOMPARE(controller.connectionStatusToken(), expectedToken);
        // The human-readable mapping exists for every token (§15).
        QVERIFY(!controller.connectionStatusText().isEmpty());
        QVERIFY(controller.connectionStatusText()
                    != QStringLiteral("provider_rejected_status"));
    }

    [[nodiscard]] static std::vector<modbuslens::core::CandidateProposal>
    manufacturerProposals()
    {
        modbuslens::core::CandidateProposal proposal;
        proposal.target = modbuslens::core::ProfileFieldTarget::Manufacturer;
        proposal.proposedValue = "ACME Power Systems Ltd.";
        proposal.evidenceExcerpt = "Manufacturer: ACME Power Systems Ltd.";
        return {proposal};
    }
};

QTEST_GUILESS_MAIN(AiSettingsTest)
#include "test_ai_settings.moc"
