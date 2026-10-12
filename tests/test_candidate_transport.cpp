// M12-C C2 FOURTH SLICE tests (T027 §76): the production ModelScope transport,
// the process-environment credential/config seam and the end-to-end
// consent -> transport -> envelope -> strict parser -> Evidence chain.
//
// Determinism is structural: the production transport code path IS exercised,
// but the HTTP exchange is captured by an injected fake client, so no socket is
// ever opened and no token is ever real.
//
// The matrix is P01..P24 from the session contract.
#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/candidate/CandidateExtraction.h"
#include "core/candidate/ProviderExtractionContract.h"
#include "ui/ai/CandidateProviderAdapter.h"
#include "ui/ai/AiRuntimeConfiguration.h"
#include "ui/ai/IAiCredentialStore.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/candidate/CandidateProviderRunner.h"
#include "ui/candidate/ChatCompletionsTransport.h"
#include "ui/candidate/ChatCompletionsHttpClient.h"
#include "ui/profile/ProfileStore.h"

#include "fake_chat_completions_server.h"

using modbuslens::core::CandidateProposal;
using modbuslens::core::ManualDocument;
using modbuslens::core::ProfileFieldTarget;
using modbuslens::ui::CandidateExtractionController;
using modbuslens::ui::IChatCompletionsHttpClient;
using modbuslens::ui::CandidateProviderRunner;
using modbuslens::ui::ChatCompletionsTransport;
using modbuslens::ui::ChatCompletionsHttpRequest;
using modbuslens::ui::ChatCompletionsHttpResult;
using modbuslens::ui::QtChatCompletionsHttpClient;
using modbuslens::ui::ProfileStore;

namespace {

constexpr char kManualText[] =
    "ACME Power Systems Ltd. - Inverter Manual\n"
    "\n"
    "1. Identity\n"
    "Manufacturer: ACME Power Systems Ltd.\n"
    "Model: INV-1000\n";

constexpr char kManufacturerExcerpt[] = "Manufacturer: ACME Power Systems Ltd.";
constexpr char kManufacturerValue[] = "ACME Power Systems Ltd.";
constexpr char kDocumentId[] = "cccccccc-1111-2222-3333-444444444444";
constexpr char kContentHash[] =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
// A distinctive sentinel that must never appear in an outbound payload.
constexpr char kForbiddenSentinel[] = "ORIGINAL-BINARY-BYTES-SENTINEL";
constexpr char kFakeToken[] = "fake-token-not-a-real-secret-0001";

ManualDocument makeDocument()
{
    ManualDocument document;
    document.schemaVersion = 1;
    document.documentId = kDocumentId;
    document.originalFileName = "inverter-manual.txt";
    document.documentType = modbuslens::core::ManualDocumentType::Txt;
    document.originalPath = "Z:/provenance-only/" + std::string{kForbiddenSentinel};
    document.contentHash = kContentHash;
    document.byteSize = sizeof(kManualText);
    document.charCount = sizeof(kManualText) - 1;
    document.status = modbuslens::core::ManualDocumentStatus::Ready;
    return document;
}

std::string extractionContent(std::string value, std::string excerpt)
{
    std::string content = "{\"proposals\":[{\"target\":\"manufacturer\",\"value\":\"";
    content += value;
    content += "\",\"evidence\":\"";
    content += excerpt;
    content += "\"}]}";
    return content;
}

// A ModelScope-shaped Chat Completions envelope (the accepted M6 shape).
std::string envelope(std::string_view assistantContent)
{
    std::string escaped;
    for (const char c : assistantContent) {
        if (c == '"') {
            escaped += "\\\"";
        } else if (c == '\\') {
            escaped += "\\\\";
        } else if (c == '\n') {
            escaped += "\\n";
        } else {
            escaped.push_back(c);
        }
    }
    return std::string{"{\"id\":\"cmpl-1\",\"choices\":[{\"index\":0,"
                       "\"message\":{\"role\":\"assistant\",\"content\":\""}
        + escaped + "\"}}]}";
}

// -------------------------------------------------- TEST-ONLY HTTP client --
// Captures the exact request the production transport would have dispatched.
class FakeHttpClient : public IChatCompletionsHttpClient
{
public:
    std::vector<ChatCompletionsHttpRequest> requests;
    int status{200};
    std::string body;
    bool transportOk{true};

    ChatCompletionsHttpResult post(const ChatCompletionsHttpRequest &request) override
    {
        requests.push_back(request);
        ChatCompletionsHttpResult result;
        result.status = status;
        result.body = body;
        result.transportOk = transportOk;
        return result;
    }

    [[nodiscard]] int callCount() const
    {
        return static_cast<int>(requests.size());
    }
};

QStringList snapshotTree(const QString &root)
{
    QStringList entries;
    if (!QDir(root).exists()) {
        entries << "<absent>";
        return entries;
    }
    QDirIterator iterator(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo info(iterator.next());
        entries << info.fileName() + ":" + QString::number(info.size());
    }
    entries.sort();
    return entries;
}

} // namespace

// ---------------------------------------------------------------------------
// T034-B2-LC2-T1-F1: per-test SAVED GLOBAL AI CONFIGURATION fixture.
//
// After LC2 the Candidate controller's only authority is the saved global
// AiRuntimeConfiguration (+ provider-scoped credential store). These three
// controller-level tests drive REAL dispatches, so each one wires a private,
// isolated authority: an in-memory credential store + a private temporary INI
// (never the developer's QSettings, never the Windows credential store, never
// a public provider).
// ---------------------------------------------------------------------------
class FakeCandidateCredentialStore final
    : public modbuslens::ui::IAiCredentialStore
{
public:
    [[nodiscard]] bool hasCredential(
        modbuslens::ui::AiProviderKind kind) override
    {
        return secrets_.count(kind) != 0 && !secrets_.at(kind).empty();
    }
    [[nodiscard]] bool saveCredential(modbuslens::ui::AiProviderKind kind,
                                      std::string_view secret) override
    {
        if (secret.empty()) {
            return false;
        }
        secrets_[kind] = std::string(secret);
        return true;
    }
    [[nodiscard]] bool deleteCredential(
        modbuslens::ui::AiProviderKind kind) override
    {
        return secrets_.erase(kind) > 0;
    }
    [[nodiscard]] modbuslens::ui::AiSecretResult readCredential(
        modbuslens::ui::AiProviderKind kind) override
    {
        const auto it = secrets_.find(kind);
        if (it == secrets_.end() || it->second.empty()) {
            return {};
        }
        return modbuslens::ui::AiSecretResult{true, it->second};
    }

private:
    std::map<modbuslens::ui::AiProviderKind, std::string> secrets_;
};

class SavedGlobalAiConfig
{
public:
    explicit SavedGlobalAiConfig(bool preloadModelScope = true)
        : runtime_(store_,
                   QDir(dir_.path()).filePath(QStringLiteral("ai.ini")))
    {
        if (preloadModelScope) {
            runtime_.saveConfiguration(
                modbuslens::ui::AiProviderKind::ModelScope,
                QStringLiteral("Qwen/Qwen3.5-27B"));
            runtime_.saveCredential(
                modbuslens::ui::AiProviderKind::ModelScope,
                QStringLiteral("fake-candidate-credential"));
        }
    }

    // T034-B2-LC2-CF1: explicit saved-authority setup for controller-level
    // tests (saved config + provider-scoped credential, one call).
    void saveProvider(modbuslens::ui::AiProviderKind kind, const QString &model,
                      const QString &secret)
    {
        runtime_.saveConfiguration(kind, model);
        runtime_.saveCredential(kind, secret);
    }

    [[nodiscard]] modbuslens::ui::AiRuntimeConfiguration &runtime()
    {
        return runtime_;
    }

private:
    QTemporaryDir dir_;
    FakeCandidateCredentialStore store_;
    modbuslens::ui::AiRuntimeConfiguration runtime_;
};

class CandidateTransportTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Every test starts from a known configuration surface. The value is a
        // FAKE token; the Human's real token is never read or printed.
        qunsetenv("MODELSCOPE_API_KEY");
        qunsetenv("MODBUSLENS_MODELSCOPE_MODEL");
    }

    void cleanup() { init(); }

    // --------------------------------------------------------------- P01 --
    void p01_missingCredentialSendsNothing()
    {
        FakeHttpClient http;
        CandidateProviderRunner runner(http);

        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            1, nullptr);

        QCOMPARE(started, false);
        QCOMPARE(http.callCount(), 0);
        QCOMPARE(runner.dispatchCount(), 0);
        QCOMPARE(runner.beginCount(), 0);
    }

    // ---------------------------------------------------------- LIFE-02 --
    // T034-B2.1 SECURITY CORRECTION SLICE B2: a transport instance must not
    // retain the bearer token across requests. Request B (same transport, no
    // new credential supplied) must never inherit request A's token.
    // Pre-fix RED: bearerToken_ is a retained member, so B re-uses A's token.
    void life02_transportDoesNotRetainBearerToken()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{"token-A-not-a-real-secret"});

        const modbuslens::core::ExtractionRequest request =
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText});
        QVERIFY(runner.begin(request, 1, nullptr)); // request A: authenticated
        QCOMPARE(http.callCount(), 1);
        QCOMPARE(http.requests.front().bearerToken, std::string{"token-A-not-a-real-secret"});

        // Request B: the runner re-dispatches WITHOUT any new credential
        // input (the controller only re-resolves on a new beginAttempt; here
        // we call begin again directly to prove the transport layer alone
        // cannot resurrect the old token). Post-fix, the one-shot secret has
        // been consumed and B fails closed / dispatches unauthenticated —
        // but it must NEVER carry request A's token.
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string("Qwen/Qwen3.5-27B"),
                                   std::string()); // empty = no credential
        const bool startedB = runner.begin(request, 2, nullptr);
        if (startedB) {
            // If B dispatches at all, it must NOT carry A's token.
            QVERIFY(http.requests.back().bearerToken
                    != std::string{"token-A-not-a-real-secret"});
        }
        // Under the retained-token defect this fails: B carries A's token.
    }

    // --------------------------------------------------------------- P02 --
    void p02_modelConfigurationSemantics()
    {
        // T034-B2-LC2-T1: the runner has NO environment authority. Even with
        // EVERY legacy env token populated, begin() without an explicit
        // resolved configuration fails closed with zero dispatches.
        qputenv("MODELSCOPE_API_KEY", "legacy-sentinel-not-a-real-key");
        qputenv("MODBUSLENS_AI_PROVIDER", "modelscope");
        qputenv("MODBUSLENS_AI_MODEL", "Legacy/Env-Model");
        qputenv("MODBUSLENS_MODELSCOPE_MODEL", "Legacy/Legacy-Model");
        qputenv("DEEPSEEK_API_KEY", "legacy-sentinel-not-a-real-key");
        FakeHttpClient http;
        CandidateProviderRunner runner(http);

        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            1, nullptr);
        QCOMPARE(started, false);
        QCOMPARE(http.callCount(), 0);
        QCOMPARE(runner.dispatchCount(), 0);
        QCOMPARE(runner.beginCount(), 0);

        // And an EXPLICIT resolved configuration is what drives the model id.
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Vendor/Configured-Model"},
                                   std::string{kFakeToken});
        http.body = envelope(extractionContent(kManufacturerValue,
                                               kManufacturerExcerpt));
        const bool started2 = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            2, nullptr);
        QVERIFY(started2);
        QVERIFY2(http.requests.back().body.find("Vendor/Configured-Model")
                     != std::string::npos,
                 "the explicit configuration must drive the model id");
    }

    // --------------------------------------------------------------- P03 --
    void p03_requestTargetsOfficialHttpsEndpoint()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});

        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            7, nullptr);
        QVERIFY(started);

        QVERIFY(http.callCount() == 1);
        const std::string url = http.requests.front().url;
        QCOMPARE(url, CandidateProviderRunner::officialEndpoint());
        QVERIFY(url.rfind("https://api-inference.modelscope.cn/v1/chat/completions", 0)
                == 0);
        // The seam's only operation is a POST and the production client issues
        // QNetworkAccessManager::post — there is no way to reach the endpoint
        // through this application with any other method.
        QCOMPARE(CandidateProviderRunner::productionTimeoutMs(), 90000);
    }

    // --------------------------------------------------------------- P04 --
    void p04_tokenOnlyInAuthorizationHeader()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});

        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(started);

        QVERIFY(http.callCount() == 1);
        QCOMPARE(http.requests.front().bearerToken, std::string{kFakeToken});
        QVERIFY(http.requests.front().body.find(kFakeToken) == std::string::npos);
        QVERIFY(http.requests.front().url.find(kFakeToken) == std::string::npos);
    }

    // --------------------------------------------------------------- P05 --
    void p05_contentTypeIsJson()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});

        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(started);

        QCOMPARE(http.requests.front().contentType, std::string{"application/json"});
        QCOMPARE(std::string{ChatCompletionsTransport::kJsonContentType},
                 std::string{"application/json"});
    }

    // --------------------------------------------------------------- P06 --
    void p06_payloadIsMinimized()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});

        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(started);

        QVERIFY(http.callCount() == 1);
        const std::string body = http.requests.front().body;
        // The intended extraction payload IS sent ...
        QVERIFY(body.find(kManufacturerExcerpt) != std::string::npos);
        QVERIFY(body.find("manufacturer") != std::string::npos);
        QVERIFY(body.find("Inverter Manual") != std::string::npos);
        // ... and prohibited application truth is NOT.
        QVERIFY(body.find(kDocumentId) == std::string::npos);
        QVERIFY(body.find(kContentHash) == std::string::npos);
        QVERIFY(body.find(kForbiddenSentinel) == std::string::npos);
        QVERIFY(body.find("inverter-manual.txt") == std::string::npos);
        QVERIFY(body.find("register") == std::string::npos);
        QVERIFY(body.find("transaction") == std::string::npos);
        QVERIFY(body.find(kFakeToken) == std::string::npos);
    }

    // ----------------------------------------------------------- P07/P08 --
    void p07_p08_consentGovernsTransportAttempts()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument();

        controller.requestExtractionFor(document, std::string{kManualText});
        QCOMPARE(controller.stateToken(), QStringLiteral("consent_required"));
        QCOMPARE(http.callCount(), 0); // P07: reject path never dispatches

        controller.rejectConsent();
        QCOMPARE(http.callCount(), 0);

        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        QVERIFY(http.callCount() == 1); // P08: exactly one attempt per trigger
        QVERIFY(controller.candidateCount() == 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("succeeded"));
    }

    // --------------------------------------------------------------- P09 --
    void p09_validEnvelopeYieldsPendingReviewCandidate()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument();
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();

        QVERIFY(controller.candidateCount() == 1);
        QVERIFY(controller.candidateCount() >= 1);
        const QVariantMap candidate = controller.candidates().front().toMap();
        QCOMPARE(candidate.value(QStringLiteral("targetField")).toString(),
                 QStringLiteral("manufacturer"));
        QCOMPARE(candidate.value(QStringLiteral("proposedValue")).toString(),
                 QString::fromUtf8(kManufacturerValue));
        QCOMPARE(candidate.value(QStringLiteral("lifecycle")).toString(),
                 QStringLiteral("pending_review"));
        QCOMPARE(candidate.value(QStringLiteral("documentId")).toString(),
                 QString::fromUtf8(kDocumentId));
    }

    // --------------------------------------------------------------- P10 --
    void p10_httpSuccessWithFalseEvidencePreservesOldSet()
    {
        FakeHttpClient http;
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument();

        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        QVERIFY(controller.candidateCount() == 1);
        QVERIFY(controller.candidateCount() >= 1);
        const QString accepted = controller.candidates().front().toMap()
                                     .value(QStringLiteral("proposedValue"))
                                     .toString();

        // HTTP 200 with a struct valid payload whose excerpt is not in the text.
        http.body = envelope(extractionContent("FABRICATED", "Manufacturer: FABRICATED"));
        controller.requestExtractionFor(document, std::string{kManualText});
        QVERIFY(controller.candidateCount() == 1);
        QVERIFY(controller.candidateCount() >= 1);
        QCOMPARE(controller.candidates().front().toMap()
                     .value(QStringLiteral("proposedValue")).toString(),
                 accepted);
        QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
    }

    // --------------------------------------------------------------- P11 --
    void p11_malformedEnvelopeFailsClosed()
    {
        FakeHttpClient http;
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        CandidateExtractionController controller(runner);
        const ManualDocument document = makeDocument();

        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        QVERIFY(controller.candidateCount() == 1);

        const std::vector<std::string> malformed{
            "not json at all",
            "{\"choices\":[]}",                       // empty choices
            "{\"choices\":[{\"message\":{}}]}",       // missing content
            "{\"choices\":\"nope\"}",                 // wrong type
            "{}",                                     // missing choices
            envelope(""),                             // empty assistant content
            envelope("{not json}"),                   // unusable content
        };
        for (const std::string &body : malformed) {
            http.body = body;
            controller.requestExtractionFor(document, std::string{kManualText});
            QVERIFY(controller.candidateCount() == 1); // old set preserved
            QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
        }
    }

    // ------------------------------------------------------ P12/P13/P14 --
    void p12_p13_p14_nonSuccessStatusPreservesOldSetWithoutLeak()
    {
        for (const int status : {401, 403, 429, 500, 503}) {
            FakeHttpClient http;
            CandidateProviderRunner runner(http);
            SavedGlobalAiConfig globalAi;
            CandidateExtractionController controller(runner);
            controller.setRuntimeConfiguration(&globalAi.runtime());
            const ManualDocument document = makeDocument();

            http.status = 200;
            http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
            controller.requestExtractionFor(document, std::string{kManualText});
            controller.grantConsent();
            QVERIFY(controller.candidateCount() == 1);

            http.status = status;
            // A provider error envelope must never be able to smuggle the token
            // or the manual text into user-visible error text.
            http.body = std::string{"{\"error\":\"denied "} + kFakeToken + "\"}";
            controller.requestExtractionFor(document, std::string{kManualText});

            QVERIFY(controller.candidateCount() == 1);
            QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
            QCOMPARE(controller.failureToken(),
                     QStringLiteral("provider_rejected_status"));
            QVERIFY(!controller.failureText().contains(QString::fromUtf8(kFakeToken)));
            QVERIFY(!controller.failureText().contains(
                QString::fromUtf8(kManufacturerExcerpt)));
        }
    }

    // --------------------------------------------------------------- P15 --
    void p15_timeoutFailsClosedAndPreservesOldSet()
    {
        FakeHttpClient http;
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        SavedGlobalAiConfig globalAi;
        CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(&globalAi.runtime());
        const ManualDocument document = makeDocument();

        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        QVERIFY(controller.candidateCount() == 1);

        // A bounded timeout surfaces as "no HTTP status at all".
        http.transportOk = false;
        http.status = 0;
        http.body.clear();
        controller.requestExtractionFor(document, std::string{kManualText});

        QVERIFY(controller.candidateCount() == 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
        QCOMPARE(controller.failureToken(), QStringLiteral("transport_error"));
    }

    // --------------------------------------------------------------- P16 --
    void p16_networkFailureFailsClosedWithZeroTruthMutation()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ProfileStore::setManagedRootOverride(root.path());
        const QStringList before = snapshotTree(root.path());

        FakeHttpClient http;
        http.transportOk = false;
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();

        QVERIFY(controller.candidateCount() == 0);
        QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
        QCOMPARE(snapshotTree(root.path()), before);

        ProfileStore::setManagedRootOverride(QString());
    }

    // --------------------------------------------------------------- P17 --
    void p17_generationIsEchoedSoTheStaleGuardHasAValidInput()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});

        std::uint64_t seen = 0;
        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            4242, [&seen](const modbuslens::ui::ICandidateExtractionRunner::Completion &c) {
                seen = c.generation;
            });

        QVERIFY(started);
        // The runner echoes the generation verbatim, which is what makes the
        // accepted SESSION O stale-result guard authoritative. The DROP
        // behaviour itself is already accepted evidence (SESSION O O12,
        // `candidate_orchestration`) and is not re-derived here.
        QCOMPARE(seen, std::uint64_t{4242});
    }

    // --------------------------------------------------------------- P18 --
    void p18_onlyFullSuccessReplacesAtomically()
    {
        FakeHttpClient http;
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        SavedGlobalAiConfig globalAi;
        CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(&globalAi.runtime());
        const ManualDocument document = makeDocument();

        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        QVERIFY(controller.candidateCount() == 1);

        http.body = envelope(extractionContent("ACME", kManufacturerExcerpt));
        controller.requestExtractionFor(document, std::string{kManualText});
        QVERIFY(controller.candidateCount() == 1);
        QVERIFY(controller.candidateCount() >= 1);
        QCOMPARE(controller.candidates().front().toMap()
                     .value(QStringLiteral("proposedValue")).toString(),
                 QStringLiteral("ACME"));
        QCOMPARE(controller.stateToken(), QStringLiteral("succeeded"));
    }

    // --------------------------------------------------------------- P19 --
    void p19_rawResponseIsTransient()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();

        QVERIFY(controller.candidateCount() >= 1);
        const QVariantMap candidate = controller.candidates().front().toMap();
        QStringList keys = candidate.keys();
        keys.sort();
        QCOMPARE(keys, QStringList({QStringLiteral("contentHash"),
                                    QStringLiteral("documentId"),
                                    QStringLiteral("evidenceExcerpt"),
                                    QStringLiteral("lifecycle"),
                                    QStringLiteral("proposedValue"),
                                    QStringLiteral("targetField"),
                                    QStringLiteral("textEnd"),
                                    QStringLiteral("textStart")}));
        // The raw envelope marker must not have been retained anywhere.
        for (const QVariant &value : candidate) {
            QVERIFY(!value.toString().contains(QStringLiteral("cmpl-1")));
        }
    }

    // --------------------------------------------------------------- P20 --
    void p20_tokenNeverAppearsInUserVisibleState()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();

        const QStringList visible{
            controller.failureText(), controller.failureToken(),
            controller.stateToken(), controller.consentScopeText()};
        for (const QString &text : visible) {
            QVERIFY(!text.contains(QString::fromUtf8(kFakeToken)));
        }
        for (const QVariant &entry : controller.candidates()) {
            QVERIFY(!entry.toMap().value(QStringLiteral("proposedValue"))
                         .toString()
                         .contains(QString::fromUtf8(kFakeToken)));
        }
    }

    // --------------------------------------------------------------- P21 --
    void p21_zeroProfileDiffOnAllPaths()
    {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ProfileStore::setManagedRootOverride(root.path());
        const QStringList before = snapshotTree(root.path());

        for (const int status : {200, 401, 500}) {
            FakeHttpClient http;
            http.status = status;
            http.body = status == 200
                            ? envelope(extractionContent(kManufacturerValue,
                                                         kManufacturerExcerpt))
                            : std::string{"{\"error\":\"x\"}"};
            CandidateProviderRunner runner(http);
            CandidateExtractionController controller(runner);
            controller.requestExtractionFor(makeDocument(), std::string{kManualText});
            controller.grantConsent();
            QCOMPARE(snapshotTree(root.path()), before);
        }
        ProfileStore::setManagedRootOverride(QString());
    }

    // ------------------------------------------------------ P22 / P23 -----
    void p22_p23_noConfidenceAndNoOriginalBytes()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();

        QVERIFY(controller.candidateCount() >= 1);
        const QVariantMap candidate = controller.candidates().front().toMap();
        for (const QString &key : candidate.keys()) {
            const QString lowered = key.toLower();
            QVERIFY(!lowered.contains(QStringLiteral("confidence")));
            QVERIFY(!lowered.contains(QStringLiteral("score")));
            QVERIFY(!lowered.contains(QStringLiteral("probability")));
        }
        QVERIFY(http.callCount() == 1);
        const std::string body = http.requests.front().body;
        QVERIFY(body.find(kForbiddenSentinel) == std::string::npos);
        QVERIFY(body.find("%PDF") == std::string::npos);
        QVERIFY(body.find("PK\\x03\\x04") == std::string::npos);
    }

    // ------------------------------------------------- LC2-06 / LC2-07 --
    // T034-B2-LC2-CF1: controller-level authority oracles (real
    // CandidateExtractionController dispatch through the injected saved
    // global configuration). Defined inline at the end of this class.

    // --------------------------------------------------------------- P24 --
    void p24_productionRunnerReachesTheTransportLayer()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kFakeToken});
        CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();

        // With configuration present the runner must NOT report not_configured:
        // it must have reached the real transport layer, and the accepted chain
        // must have produced a Candidate.
        QVERIFY(controller.failureToken() != QStringLiteral("not_configured"));
        QVERIFY(http.callCount() == 1);
        QCOMPARE(runner.dispatchCount(), 1);
        QCOMPARE(runner.beginCount(), 1);
        QVERIFY(controller.candidateCount() == 1);
        QCOMPARE(controller.stateToken(), QStringLiteral("succeeded"));
        // And a fail-closed path still dispatches nothing.
        qunsetenv("MODELSCOPE_API_KEY");
        FakeHttpClient http2;
        CandidateProviderRunner runner2(http2);
        const bool startedWithoutCredential = runner2.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            1, nullptr);
        QCOMPARE(startedWithoutCredential, false);
        QCOMPARE(http2.callCount(), 0);
    }

    // ======================================================================
    // T034-B2.2-A-R2 — SHARED ASYNC CHAT-COMPLETIONS TRANSPORT
    //
    // The shared HTTP seam must expose its already-asynchronous
    // QNetworkAccessManager capability: MULTIPLE concurrent in-flight
    // exchanges, each with independent identity / reply / timeout ownership,
    // and exactly-once completion. These oracles drive the REAL
    // QtChatCompletionsHttpClient against the existing localhost fake server
    // (never a public provider).
    // ======================================================================

    // ---------------------------------------------------------- A01 --
    // Concurrency + identity oracle: request A is HELD while request B is
    // dispatched AND completed; A then completes late. Each completion fires
    // exactly once and neither sees the other's response.
    void a01_asyncConcurrencyIdentityOracle()
    {
        FakeChatCompletionsServer server;
        QVERIFY(server.start());
        // Script entry 1 (first arrival = request A): HELD 900 ms.
        server.enqueueResponse(
            200, "{\"choices\":[{\"message\":{\"content\":\"reply-A\"}}]}", 900);
        // Script entry 2 (request B): immediate.
        server.enqueueResponse(
            201, "{\"choices\":[{\"message\":{\"content\":\"reply-B\"}}]}", 0);

        QtChatCompletionsHttpClient http;
        ChatCompletionsTransport transport(
            http, server.chatCompletionsUrl().toStdString(), 20000);

        std::vector<std::pair<std::uint64_t, ChatCompletionsHttpResult>> done;
        std::uint64_t idA = 0;
        transport.setOneShotBearerToken("token-A-not-a-real-secret");
        const auto handleA = transport.sendAsync(
            std::string{"{\"req\":\"A\"}"}, nullptr,
            [&](ChatCompletionsHttpResult r) { done.emplace_back(idA, std::move(r)); });
        idA = handleA.id;
        QVERIFY(handleA.valid());
        // A goes ON THE WIRE first and its reply is HELD for 900 ms, so the
        // arrival order — and therefore the scripted-response pairing — is
        // deterministic instead of a race between two connections.
        QTRY_COMPARE(server.requestCount(), 1);
        QCOMPARE(server.requests().at(0).body, QByteArray("{\"req\":\"A\"}"));

        std::uint64_t idB = 0;
        transport.setOneShotBearerToken("token-B-not-a-real-secret");
        const auto handleB = transport.sendAsync(
            std::string{"{\"req\":\"B\"}"}, nullptr,
            [&](ChatCompletionsHttpResult r) { done.emplace_back(idB, std::move(r)); });
        idB = handleB.id;
        QVERIFY(handleB.valid());
        QVERIFY(idA != idB);
        QCOMPARE(transport.asyncDispatchCount(), 2);

        // B reaches the wire WHILE A is still pending (A's reply is held).
        QTRY_COMPARE(server.requestCount(), 2);

        // B (immediate) completes while A (held) is still in flight.
        QTRY_COMPARE(static_cast<int>(done.size()), 1);
        QCOMPARE(done.at(0).first, idB);
        QCOMPARE(done.at(0).second.status, 201);
        QVERIFY(done.at(0).second.body.find("reply-B") != std::string::npos);

        // A completes late: exactly one more callback carrying A's OWN reply.
        QTRY_COMPARE(static_cast<int>(done.size()), 2);
        QCOMPARE(done.at(1).first, idA);
        QCOMPARE(done.at(1).second.status, 200);
        QVERIFY(done.at(1).second.body.find("reply-A") != std::string::npos);
    }

    // ---------------------------------------------------------- A02 --
    // Timeout is exactly-once, and a LATE network completion after the timeout
    // produces NO second callback.
    void a02_asyncTimeoutExactlyOnceAndLateIgnored()
    {
        FakeChatCompletionsServer server;
        QVERIFY(server.start());
        server.enqueueResponse(200, "{\"late\":true}", 800);

        QtChatCompletionsHttpClient http;
        ChatCompletionsTransport transport(
            http, server.chatCompletionsUrl().toStdString(), 200);

        int calls = 0;
        bool sawTimeout = false;
        transport.setOneShotBearerToken("token-not-a-real-secret");
        const auto handle = transport.sendAsync(
            std::string{"{\"req\":\"T\"}"}, nullptr,
            [&](ChatCompletionsHttpResult r) { ++calls; sawTimeout = r.timedOut; });
        QVERIFY(handle.valid());

        QTRY_COMPARE(calls, 1);
        QVERIFY(sawTimeout);

        // The held reply arrives after the timeout: it must be ignored.
        QTest::qWait(900);
        QCOMPARE(calls, 1);
    }

    // ---------------------------------------------------------- A03 --
    // An async start that cannot dispatch (no one-shot credential) returns an
    // INVALID handle, dispatches NOTHING and never invokes the completion.
    void a03_asyncInvalidStartSendsNothing()
    {
        FakeChatCompletionsServer server;
        QVERIFY(server.start());

        QtChatCompletionsHttpClient http;
        ChatCompletionsTransport transport(
            http, server.chatCompletionsUrl().toStdString(), 5000);

        int calls = 0;
        const auto handle = transport.sendAsync(
            std::string{"{\"req\":\"X\"}"}, nullptr,
            [&](ChatCompletionsHttpResult) { ++calls; });

        QVERIFY(!handle.valid());
        QCOMPARE(transport.asyncDispatchCount(), 0);
        QTest::qWait(200);
        QCOMPARE(server.requestCount(), 0);
        QCOMPARE(calls, 0);
    }

    // ---------------------------------------------------------- A04 --
    // The shared interface DEFAULT async contract must NOT fall back to the
    // synchronous post(): an unsupported async start is invalid AND nothing is
    // dispatched synchronously (Candidate fakes stay behaviour-compatible).
    void a04_asyncDefaultNeverFallsBackToSync()
    {
        FakeHttpClient http; // overrides ONLY the synchronous path
        ChatCompletionsTransport transport(
            http, "https://example.invalid/v1/chat/completions", 5000);

        int calls = 0;
        transport.setOneShotBearerToken("token-not-a-real-secret");
        const auto handle = transport.sendAsync(
            std::string{"{\"req\":\"Y\"}"}, nullptr,
            [&](ChatCompletionsHttpResult) { ++calls; });

        QVERIFY(!handle.valid());
        QCOMPARE(http.callCount(), 0); // no hidden synchronous fallback
        QCOMPARE(transport.asyncDispatchCount(), 0);
        QCOMPARE(calls, 0);
    }

    // ---------------------------------------------------------- A05 --
    // Callback lifetime: if the bound context object dies before completion,
    // the completion is never delivered (no use-after-free).
    void a05_asyncLifetimeContextAbandonment()
    {
        FakeChatCompletionsServer server;
        QVERIFY(server.start());
        server.enqueueResponse(200, "{\"choices\":[]}", 500);

        QtChatCompletionsHttpClient http;
        ChatCompletionsTransport transport(
            http, server.chatCompletionsUrl().toStdString(), 20000);

        int calls = 0;
        {
            QObject context;
            transport.setOneShotBearerToken("token-not-a-real-secret");
            const auto handle = transport.sendAsync(
                std::string{"{\"req\":\"L\"}"}, &context,
                [&](ChatCompletionsHttpResult) { ++calls; });
            QVERIFY(handle.valid());
        } // context destroyed before the held reply arrives

        QTest::qWait(700);
        QCOMPARE(calls, 0);
    }

    // ------------------------------------------------------------- LC2-06 --
    // T034-B2-LC2-CF1: NO saved global configuration while ALL FIVE legacy
    // env tokens are populated — a REAL Candidate controller dispatch must
    // fail closed with ZERO provider requests.
    void lc2_06_controllerNoSavedConfigFailsClosedDespiteLegacyEnv()
    {
        SavedGlobalAiConfig globalAi{/*preloadModelScope=*/false};
        QVERIFY(!globalAi.runtime().hasSavedConfiguration());
        QVERIFY(!globalAi.runtime().resolveRequestConfig().has_value());

        qputenv("MODELSCOPE_API_KEY", "FAKE_LEGACY_MODELSCOPE_KEY");
        qputenv("MODBUSLENS_AI_PROVIDER", "modelscope");
        qputenv("MODBUSLENS_AI_MODEL", "FAKE_LEGACY_MODELSCOPE_MODEL");
        qputenv("MODBUSLENS_MODELSCOPE_MODEL", "FAKE_LEGACY_MODELSCOPE_MODEL");
        qputenv("DEEPSEEK_API_KEY", "FAKE_LEGACY_DEEPSEEK_KEY");

        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(&globalAi.runtime());

        const ManualDocument document = makeDocument();
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();

        // Resolver-level: the injected authority stays NOT CONFIGURED.
        QVERIFY(!globalAi.runtime().resolveRequestConfig().has_value());
        // Controller-level: fail closed, zero provider requests.
        QVERIFY(!controller.candidateCount());
        QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
        QCOMPARE(controller.failureToken(), QStringLiteral("not_configured"));
        QCOMPARE(http.callCount(), 0);
        QCOMPARE(runner.dispatchCount(), 0);
        QCOMPARE(runner.beginCount(), 0);

        for (const char *name :
             {"MODELSCOPE_API_KEY", "MODBUSLENS_AI_PROVIDER",
              "MODBUSLENS_AI_MODEL", "MODBUSLENS_MODELSCOPE_MODEL",
              "DEEPSEEK_API_KEY"}) {
            qunsetenv(name);
        }
    }

    // ------------------------------------------------------------- LC2-07 --
    // T034-B2-LC2-CF1: saved DeepSeek authority + CONFLICTING legacy
    // ModelScope env sentinels — a REAL controller dispatch must go to the
    // SAVED DeepSeek endpoint/model/credential and never to ModelScope.
    void lc2_07_controllerSavedDeepSeekIgnoresLegacyModelScopeEnv()
    {
        SavedGlobalAiConfig globalAi{/*preloadModelScope=*/false};
        globalAi.saveProvider(modbuslens::ui::AiProviderKind::DeepSeek,
                              QStringLiteral("ds-lc2-model"),
                              QStringLiteral("fake-deepseek-lc2-key"));
        QVERIFY(globalAi.runtime().hasSavedConfiguration());

        qputenv("MODELSCOPE_API_KEY", "FAKE_LEGACY_MODELSCOPE_KEY");
        qputenv("MODBUSLENS_AI_PROVIDER", "modelscope");
        qputenv("MODBUSLENS_AI_MODEL", "FAKE_LEGACY_MODELSCOPE_MODEL");
        qputenv("MODBUSLENS_MODELSCOPE_MODEL", "FAKE_LEGACY_MODELSCOPE_MODEL");

        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        CandidateProviderRunner runner(http);
        CandidateExtractionController controller(runner);
        controller.setRuntimeConfiguration(&globalAi.runtime());

        const ManualDocument document = makeDocument();
        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();

        // Provider authority = saved DeepSeek: exactly one dispatch, to the
        // DeepSeek endpoint, with the saved model and the saved credential.
        QCOMPARE(controller.candidateCount(), 1);
        QCOMPARE(http.callCount(), 1);
        QVERIFY(http.requests.front().url.find("api.deepseek.com")
                != std::string::npos);
        QVERIFY(http.requests.front().url.find("modelscope.cn")
                == std::string::npos);
        QCOMPARE(http.requests.front().bearerToken,
                 std::string{"fake-deepseek-lc2-key"});
        QVERIFY(http.requests.front().bearerToken
                != std::string{"FAKE_LEGACY_MODELSCOPE_KEY"});
        const QJsonObject body = QJsonDocument::fromJson(
            QByteArray::fromStdString(http.requests.front().body)).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(),
                 QStringLiteral("ds-lc2-model"));
        QVERIFY(!body.contains(QStringLiteral("chat_template_kwargs")));

        for (const char *name :
             {"MODELSCOPE_API_KEY", "MODBUSLENS_AI_PROVIDER",
              "MODBUSLENS_AI_MODEL", "MODBUSLENS_MODELSCOPE_MODEL"}) {
            qunsetenv(name);
        }
    }
};

QTEST_MAIN(CandidateTransportTest)
#include "test_candidate_transport.moc"
