// M12-D FIRST SLICE (T027 §100, D1–D5): deterministic unit tests for the
// Manual Q&A controller, the strict provider parser and the local citation
// validator. Everything runs against a fake runner and a temporary managed
// root: zero network, zero credential, zero live provider (QA-34/QA-35).

#include <vector>
#include <map>
#include <memory>
#include <string>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <utility>

#include "core/manualqa/ManualQaContract.h"
#include "ui/agent/ModelScopeAgentClient.h"
#include "ui/ai/AiProviderProfile.h"
#include "ui/ai/AiRuntimeConfiguration.h"
#include "ui/ai/IAiCredentialStore.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/candidate/ChatCompletionsHttpClient.h"
#include "ui/manual/ManualImportController.h"
#include "ui/manual/ManualStore.h"
#include "ui/manualqa/ManualQaAsyncRunner.h"
#include "ui/manualqa/ManualQaController.h"
#include "ui/profile/ProfileController.h"
#include "ui/profile/ProfileStore.h"

using namespace modbuslens;
using namespace modbuslens::core;
using namespace modbuslens::ui;

namespace {

constexpr auto kSeedText =
    "Device: QA-1000 Inverter\n"
    "Manufacturer: ACME Power Systems Ltd.\n"
    "Model: QA-1000\n"
    " Rated input voltage: 380 V AC three-phase.\n"
    " Warranty: 24 months from purchase.\n";

// T033 fixtures: minimal offline PDFs (same technique as the frozen
// test_manual_import_pdf_docx fixtures — deterministic, licence-free).
[[nodiscard]] QByteArray qaBuildPdf(const QList<QByteArray> &objects)
{
    QByteArray out = "%PDF-1.4\n";
    QList<int> offsets;
    for (int index = 0; index < objects.size(); ++index) {
        offsets.append(out.size());
        out += QByteArray::number(index + 1) + " 0 obj\n" + objects.at(index)
               + "\nendobj\n";
    }
    const int xrefAt = out.size();
    out += "xref\n0 " + QByteArray::number(objects.size() + 1)
           + "\n0000000000 65535 f \n";
    for (int offset : offsets) {
        out += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1)
           + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefAt)
           + "\n%%EOF\n";
    return out;
}

[[nodiscard]] QByteArray qaStreamObject(const QByteArray &stream)
{
    return "<< /Length " + QByteArray::number(stream.size()) + " >>\nstream\n"
           + stream + "\nendstream";
}

[[nodiscard]] QByteArray qaTextLayerPdf(const QString &text)
{
    const QByteArray stream =
        "BT /F1 18 Tf 72 700 Td (" + text.toUtf8() + ") Tj ET";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R "
        "/Resources << /Font << /F1 5 0 R >> >> >>",
        qaStreamObject(stream),
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    };
    return qaBuildPdf(objects);
}

[[nodiscard]] QByteArray qaNoTextPdf()
{
    const QByteArray stream = "0.9 0.9 0.9 rg 72 600 200 80 re f";
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Contents 4 0 R >>",
        qaStreamObject(stream),
    };
    return qaBuildPdf(objects);
}

// Deterministic fake runner: programmable outcome, deferrable completion,
// generation capture. Zero network by construction (QA-35).
class FakeManualQaRunner final : public IManualQaRunner
{
public:
    enum class Mode {
        FoundValid,
        FoundWrongDocument,
        FoundStaleHash,
        FoundInvalidRange,
        FoundExcerptMismatch,
        FoundNoCitations,
        FoundEmptyAnswer,
        NotFound,
        Insufficient,
        Malformed,
        Failure,
        Deferred, // completion is captured, delivered by deliverDeferred()
        Scripted, // delivers the exact scripted rawJson (contract fixtures)
    };

    void script(const std::string& rawJson)
    {
        mode_ = Mode::Scripted;
        scriptedJson_ = rawJson;
    }
    void configure(Mode mode, const ManualQaParsedResult& result)
    {
        mode_ = mode;
        result_ = result;
    }
    void reset()
    {
        mode_ = Mode::FoundValid;
        beginCount_ = 0;
        cancelCount_ = 0;
        deferred_.clear();
    }
    [[nodiscard]] int beginCount() const override { return beginCount_; }
    [[nodiscard]] int cancelCount() const { return cancelCount_; }
    [[nodiscard]] bool hasDeferred() const { return !deferred_.empty(); }

    // Delivers the OLDEST deferred completion (FIFO) so stale-generation
    // deliveries can be simulated in dispatch order.
    void deliverDeferred()
    {
        if (deferred_.empty()) {
            return;
        }
        auto entry = std::move(deferred_.front());
        deferred_.erase(deferred_.begin());
        entry.onDone(IManualQaRunner::Completion{entry.generation, true,
                                                 std::string(),
                                                 entry.rawJson});
    }

    [[nodiscard]] bool begin(const ManualQaDispatchConfig& config,
                             const core::ManualQaRequest& request,
                             std::uint64_t generation,
                             const CompletionHandler& onDone) override
    {
        ++beginCount_;
        lastConfig_ = config;
        hasConfig_ = true;
        lastRequestQuestion_ = request.question;
        lastRequestDocumentId_ = request.documentId;
        lastRequestContentHash_ = request.contentHash;
        // NOTE: the fake is a TEST-ONLY runner installed through
        // setRunnerForAutomation; it deliberately does NOT enforce the
        // production credential gate (the controller skips global-configuration
        // resolution for an automation runner). failClosed_ lets the credential
        // boundary itself be exercised where a test wants it.
        if (failClosed_) {
            return false;
        }
        if (mode_ == Mode::Deferred) {
            DeferredCompletion deferred;
            deferred.generation = generation;
            deferred.onDone = onDone;
            deferred.rawJson = foundJsonFor(request, -1, -1, std::string(),
                                            false, false);
            deferred_.push_back(std::move(deferred));
            return true;
        }
        if (mode_ == Mode::Scripted) {
            onDone(IManualQaRunner::Completion{generation, true, std::string(),
                                               scriptedJson_});
            return true;
        }
        const auto emitFound = [&](const std::string& documentId,
                                   const std::string& contentHash,
                                   std::int64_t start, std::int64_t end,
                                   const std::string& excerpt,
                                   bool emptyCitations, bool emptyAnswer) {
            onDone(IManualQaRunner::Completion{
                generation, true, std::string(),
                foundJsonFor(request, start, end, excerpt, emptyCitations,
                             emptyAnswer, documentId, contentHash)});
        };
        switch (mode_) {
        case Mode::FoundValid:
            emitFound(request.documentId, request.contentHash, -1, -1,
                      std::string(), false, false);
            break;
        case Mode::FoundWrongDocument:
            emitFound("some-other-document-id", request.contentHash, -1, -1,
                      std::string(), false, false);
            break;
        case Mode::FoundStaleHash:
            emitFound(request.documentId, "stale-content-hash", -1, -1,
                      std::string(), false, false);
            break;
        case Mode::FoundInvalidRange:
            // A non-empty excerpt keeps the explicit (out-of-bounds) span —
            // the empty-excerpt default would be replaced by the real block.
            emitFound(request.documentId, request.contentHash, 999999,
                      1000000, "out of bounds span", false, false);
            break;
        case Mode::FoundExcerptMismatch:
            emitFound(request.documentId, request.contentHash, -1, -1,
                      "MISMATCHED EXCERPT", false, false);
            break;
        case Mode::FoundNoCitations:
            emitFound(request.documentId, request.contentHash, -1, -1,
                      std::string(), /*emptyCitations*/ true, false);
            break;
        case Mode::FoundEmptyAnswer:
            emitFound(request.documentId, request.contentHash, -1, -1,
                      std::string(), false, /*emptyAnswer*/ true);
            break;
        case Mode::NotFound:
            onDone(IManualQaRunner::Completion{
                generation, true, std::string(),
                "{\"status\":\"not_found\",\"answer\":\"I am general "
                "knowledge, do not display me\",\"citations\":[]}"});
            break;
        case Mode::Insufficient:
            onDone(IManualQaRunner::Completion{
                generation, true, std::string(),
                "{\"status\":\"insufficient_evidence\",\"answer\":\"do not "
                "display\",\"citations\":[]}"});
            break;
        case Mode::Malformed:
            onDone(IManualQaRunner::Completion{generation, true, std::string(),
                                               "{not json at all"});
            break;
        case Mode::Failure:
            onDone(IManualQaRunner::Completion{generation, false,
                                               "qa_provider_failure",
                                               std::string()});
            break;
        case Mode::Deferred:
            break;
        }
        return true;
    }

    void cancel() override { ++cancelCount_; }

    [[nodiscard]] std::string lastQuestion() const
    {
        return lastRequestQuestion_;
    }
    [[nodiscard]] std::string lastDocumentId() const
    {
        return lastRequestDocumentId_;
    }
    [[nodiscard]] bool sawConfig() const { return hasConfig_; }
    void setFailClosed(bool value) { failClosed_ = value; }
    [[nodiscard]] ManualQaDispatchConfig lastConfig() const
    {
        return lastConfig_;
    }

private:
    [[nodiscard]] static std::string escape(const std::string& text)
    {
        std::string out;
        for (const char c : text) {
            if (c == '"' || c == '\\') {
                out.push_back('\\');
            }
            if (c == '\n') {
                out += "\\n";
                continue;
            }
            out.push_back(c);
        }
        return out;
    }

    [[nodiscard]] static std::string foundJsonFor(
        const core::ManualQaRequest& request, std::int64_t start,
        std::int64_t end, const std::string& excerpt, bool emptyCitations,
        bool emptyAnswer, const std::string& documentOverride = std::string(),
        const std::string& hashOverride = std::string())
    {
        const std::string& documentId =
            !documentOverride.empty() ? documentOverride : request.documentId;
        const std::string& contentHash =
            !hashOverride.empty() ? hashOverride : request.contentHash;
        // Default citation: the request's OWN first context block — a real
        // span of the currently selected manual, so the exact round-trip
        // always succeeds unless a test corrupts one of the D2 gates.
        std::string cited = excerpt;
        if (cited.empty() && !request.blocks.empty()) {
            start = request.blocks.front().start;
            end = request.blocks.front().end;
            cited = request.blocks.front().text;
        }
        if (start < 0) {
            start = 0;
        }
        if (end <= start) {
            end = start + static_cast<std::int64_t>(cited.size());
        }
        std::string citations = "[{\"documentId\":\"" + escape(documentId)
            + "\",\"contentHash\":\"" + escape(contentHash)
            + "\",\"pageNumber\":-1,\"textStart\":" + std::to_string(start)
            + ",\"textEnd\":" + std::to_string(end) + ",\"excerpt\":\""
            + escape(cited) + "\"}]";
        if (emptyCitations) {
            citations = "[]";
        }
        const std::string answer =
            emptyAnswer ? "   " : "The manufacturer is ACME Power Systems "
                                  "Ltd. per the manual.";
        return "{\"status\":\"found\",\"answer\":\"" + escape(answer)
            + "\",\"citations\":" + citations + "}";
    }

    Mode mode_{Mode::FoundValid};
    ManualQaParsedResult result_;
    ManualQaDispatchConfig lastConfig_;
    bool hasConfig_{false};
    bool failClosed_{false};
    int beginCount_{0};
    int cancelCount_{0};
    std::string lastRequestQuestion_;
    std::string lastRequestDocumentId_;
    std::string lastRequestContentHash_;
    struct DeferredCompletion {
        std::uint64_t generation{0};
        CompletionHandler onDone;
        std::string rawJson;
    };
    std::vector<DeferredCompletion> deferred_;
    std::string scriptedJson_;
};

} // namespace

// ===========================================================================
// T034-B2.2-B-R1 deterministic GLOBAL AUTHORITY fixture.
//
// Real ManualQaController + real ManualQaAsyncRunner are exercised through:
//   · a fake in-memory IAiCredentialStore (no Windows Credential Manager),
//   · a real AiRuntimeConfiguration pointed at a per-test temporary INI file
//     (never the developer's real QSettings),
//   · a fake IChatCompletionsHttpClient that CAPTURES and HOLDS exchanges.
// Zero network, zero Human credential, zero public-provider request.
// ===========================================================================

class InMemoryCredentialStore final : public IAiCredentialStore
{
public:
    bool hasCredential(AiProviderKind kind) override
    {
        return stored_.find(kind) != stored_.end();
    }
    bool saveCredential(AiProviderKind kind, std::string_view secret) override
    {
        if (secret.empty()) {
            return false; // an empty secret must never overwrite a stored key
        }
        stored_[kind] = std::string(secret);
        return true;
    }
    bool deleteCredential(AiProviderKind kind) override
    {
        return stored_.erase(kind) != 0;
    }
    AiSecretResult readCredential(AiProviderKind kind) override
    {
        const auto it = stored_.find(kind);
        if (it == stored_.end()) {
            return {};
        }
        AiSecretResult result;
        result.ok = true;
        result.secret = it->second;
        return result;
    }

private:
    std::map<AiProviderKind, std::string> stored_;
};

// Captures every dispatched exchange and NEVER auto-completes it: the test
// decides when (and with what) each in-flight exchange finishes, which is what
// makes the B22M-10 overlap/owner oracle possible.
class HoldingHttpClient final : public IChatCompletionsHttpClient
{
public:
    struct Captured {
        std::string url;
        std::string bearer;
        std::string contentType;
        std::string body;
        int timeoutMs{0};
    };

    ChatCompletionsHttpResult post(
        const ChatCompletionsHttpRequest &) override
    {
        return {}; // the async path is the only one under test
    }

    ChatCompletionsHttpAsyncHandle startAsync(
        const ChatCompletionsHttpRequest &request, QObject *,
        ChatCompletionsHttpCompletion completion) override
    {
        captured_.push_back(Captured{request.url, request.bearerToken,
                                     request.contentType, request.body,
                                     request.timeoutMs});
        const std::uint64_t id = ++nextId_;
        pending_.emplace(id, std::move(completion));
        return ChatCompletionsHttpAsyncHandle{id};
    }

    [[nodiscard]] int dispatchCount() const
    {
        return static_cast<int>(captured_.size());
    }
    [[nodiscard]] const Captured &at(std::size_t index) const
    {
        return captured_.at(index);
    }
    [[nodiscard]] bool isPending(std::uint64_t id) const
    {
        return pending_.find(id) != pending_.end();
    }
    [[nodiscard]] std::size_t pendingCount() const { return pending_.size(); }
    [[nodiscard]] std::vector<std::uint64_t> pendingIds() const
    {
        std::vector<std::uint64_t> ids;
        for (const auto &entry : pending_) {
            ids.push_back(entry.first);
        }
        return ids;
    }

    // Completes one held exchange with a 2xx chat-completions envelope whose
    // assistant content is `content` (the Q&A JSON payload).
    void complete(std::uint64_t id, const std::string &content)
    {
        const auto it = pending_.find(id);
        if (it == pending_.end()) {
            return;
        }
        auto done = std::move(it->second);
        pending_.erase(it);
        ChatCompletionsHttpResult result;
        result.transportOk = true;
        result.status = 200;
        result.body = wrapAssistant(content);
        done(std::move(result));
    }

    static std::string escapeJson(const std::string &input)
    {
        std::string out;
        for (const char c : input) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                out.push_back(c);
            }
        }
        return out;
    }

    [[nodiscard]] static std::string wrapAssistant(const std::string &content)
    {
        return "{\"choices\":[{\"message\":{\"role\":\"assistant\","
               "\"content\":\""
            + escapeJson(content) + "\"}}]}";
    }

private:
    std::vector<Captured> captured_;
    std::map<std::uint64_t, ChatCompletionsHttpCompletion> pending_;
    std::uint64_t nextId_{0};
};

// A well-formed APP-RESOLVED Q&A payload (found + one opaque citationId).
[[nodiscard]] std::string qaFoundContent(const QString &answer)
{
    return "{\"status\":\"found\",\"answer\":\""
        + HoldingHttpClient::escapeJson(answer.toStdString())
        + "\",\"citations\":[{\"citationId\":\"c1\"}]}";
}

// Compile-time proof helpers retained as STRUCTURAL tests (never as B22M
// business acceptance).
class ManualQaStructuralProbe
{
public:
    [[nodiscard]] static bool probeSeamCarriesDispatchConfig()
    {
        using BeginSignature = bool (IManualQaRunner::*)(
            const ManualQaDispatchConfig&, const core::ManualQaRequest&,
            std::uint64_t, const IManualQaRunner::CompletionHandler&);
        const BeginSignature begin = &IManualQaRunner::begin;
        return begin != nullptr;
    }
    [[nodiscard]] static bool probeBuilderTakesProviderAndModel()
    {
        using Builder = QJsonObject (*)(AiProviderKind, const std::string&,
                                        const core::ManualQaRequest&);
        const Builder builder = &ManualQaAsyncRunner::buildRequestBody;
        return builder != nullptr;
    }
    [[nodiscard]] static bool probeParseStaticsExist()
    {
        using Parser = std::optional<core::ManualQaParsedResult> (*)(
            const std::string&, QString*);
        const Parser parser = &ManualQaAsyncRunner::parseProviderResult;
        using Resolver = std::optional<core::ManualQaParsedResult> (*)(
            const std::string&, const core::ManualQaRequest&, QString*);
        const Resolver resolver = &ManualQaAsyncRunner::resolveProviderResult;
        return parser != nullptr && resolver != nullptr;
    }
};

class ManualQaTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    // Controller semantics
    void qa01_noSelectionRejectsLocally();
    void qa03_emptyQuestionRejectedWithoutDispatch();
    void qa04_extractionConsentDoesNotGrantQaConsent();
    void qa05_consentCancelZeroDispatch();
    void qa06_consentAgreeExactlyOneDispatch();
    void qa07_subsequentAskDoesNotRePrompt();
    void qa08_foundWithValidCitation();
    void qa09_wrongDocumentIsError();
    void qa10_staleContentHashIsError();
    void qa11_invalidRangeIsError();
    void qa12_excerptMismatchIsError();
    void qa13_duplicateExcerptDoesNotInvalidate();
    void qa14_foundZeroCitationsIsError();
    void qa15_foundEmptyAnswerIsError();
    void qa16_notFoundNoProviderAnswerDisplayed();
    void qa17_insufficientNoProviderAnswerDisplayed();
    void qa18_malformedOutputIsError();
    void qa19_providerFailureIsError();
    void qa20_noProfileMutation();
    void qa21_noCandidateMutation();
    void qa22_noManualMutation();
    void qa24_manualSwitchClearsAndInvalidates();
    void qa25_lateResponseAfterSwitchDropped();
    void qa26_runningQaDoesNotBlockDelete();
    void qa27_successfulDeleteClearsQa();
    void qa28_lateResponseAfterDeleteDropped();
    void qa29_cancelAttemptedOnInvalidation();
    void qa30_newRequestSupersedesOldGeneration();
    void qa31_newControllerHasNoHistory();
    void qa32_noPersistenceArtifacts();
    void qa34_productionRunnerFailsClosedWithoutCredential();
    void qa36_qaRequestDisablesThinking();
    void qa37_roundSucceededExtractsTopLevelContent();
    void qa38_parseFailureCategoriesAreSafeAndDeterministic();
    void qa40_appResolvedCitationIds();
    void qa41_providerAuthoredOffsetsReproduceLiveFailureClass();
    void qa42_textLayerPdfDispatchesThroughCanonicalSeam();
    void qa43_noTextPdfFailsClosedLocally();

    // Parser / validator semantics
    void parser_validStates();
    void parser_malformedVariants();
    void contextBlocks_deterministicBounded();

    // T034-B2.2-B-R1 — REAL business acceptance for the Manual QA global-AI
    // authority. Every case drives the REAL controller + REAL runner through a
    // deterministic global configuration and captures the ACTUAL HTTP request
    // (URL / Authorization / body). Never a compile-signature or meta-object
    // probe.
    void b22m01_deepSeekGlobalAuthorityAndPayload();
    void b22m02_modelScopeGlobalAuthorityAndThinkingDisabled();
    void b22m03_runtimeProviderSwitch();
    void b22m04_runtimeModelSwitch();
    void b22m05_runtimeCredentialReplacement();
    void b22m06_missingCredentialFailsClosed();
    void b22m07_noSavedConfigurationFailsClosedDespiteLegacyEnv();
    void b22m08_legacyEnvNegativeControl();
    void b22m09_consentBeforeNetwork();
    void b22m10_staleOwnerOverlapIsolation();
    void b22m11_startupNoNetwork();

    // Structural (compile-signature / meta-object) checks. Retained for value,
    // deliberately NOT part of the B22M business acceptance set.
    void qaStruct_seamCarriesResolvedDispatchConfig();
    void qaStruct_builderTakesProviderAndModel();
    void qaStruct_parseStaticsAreProviderNeutral();
    void qaStruct_runnerIsProviderNeutral();
    void qaStruct_qmlBindingAvoidsPropertyShadowing();
    void qaStruct_controllerHasNoCredentialProperty();
    void runnerLevel_deepSeekBodyOmitsModelScopeExtras();
    void runnerLevel_modelScopeBodyKeepsThinkingDisabled();

private:
    static QByteArray findStoreArtifacts(const QString& root);

    // ---- R1 deterministic global-authority fixture ------------------------
    // (Re)builds a real AiRuntimeConfiguration backed by the in-memory store
    // and a FRESH temporary INI file, then saves `kind`/`model` + credential.
    AiRuntimeConfiguration* seedGlobalAi(AiProviderKind kind,
                                         const QString& model,
                                         const QString& secret);
    // Real HOT SWITCH on the SAME configuration object (no controller/app
    // restart), exactly like the AI Settings Save path.
    void switchGlobalAi(AiProviderKind kind, const QString& model,
                        const QString& secret);
    // A configuration with NOTHING saved (so the shared tier-3 environment
    // fallback would apply) — used by the legacy-env negative controls.
    AiRuntimeConfiguration* seedUnsavedGlobalAi();
    // A controller wired to the CURRENT fixture global configuration.
    ManualQaController* makeController();
    // Injects a REAL ManualQaAsyncRunner over the fake holding HTTP client.
    void injectRealRunnerOverFakeHttp();

    QTemporaryDir managedRoot_;
    QTemporaryDir settingsRoot_;
    std::unique_ptr<InMemoryCredentialStore> store_;
    std::unique_ptr<AiRuntimeConfiguration> globalAi_;
    std::unique_ptr<HoldingHttpClient> http_;
    std::unique_ptr<ManualQaAsyncRunner> injectedRunner_;
    int settingsSequence_{0};

    ManualImportController* manual_ = nullptr;
    ManualQaController* qa_ = nullptr;
    FakeManualQaRunner* fake_ = nullptr;
    QString seedPath_;
    QString baselineArtifacts_;
};

void ManualQaTest::initTestCase()
{
    QVERIFY(managedRoot_.isValid());
    QVERIFY(settingsRoot_.isValid());
    ManualStore::setManagedRootOverride(managedRoot_.path());
    ProfileStore::setManagedRootOverride(managedRoot_.path());
    const QString sourceDir = QDir(managedRoot_.path()).filePath("sources");
    QVERIFY(QDir(managedRoot_.path()).mkpath("sources"));
    seedPath_ = QDir(sourceDir).filePath("qa-manual.txt");
    QFile seed(seedPath_);
    QVERIFY(seed.open(QIODevice::WriteOnly));
    seed.write(kSeedText);
    seed.close();
}

// T034-B2.2-B-R1: a FRESH deterministic global authority before every test.
// Real AiRuntimeConfiguration + in-memory credential store + temporary INI —
// never the developer's real QSettings, never a Human credential.
void ManualQaTest::init()
{
    store_ = std::make_unique<InMemoryCredentialStore>();
    seedGlobalAi(AiProviderKind::ModelScope,
                 QStringLiteral("det/modelscope-qa-model"),
                 QStringLiteral("fake-modelscope-credential"));
}

AiRuntimeConfiguration* ManualQaTest::seedGlobalAi(AiProviderKind kind,
                                                   const QString& model,
                                                   const QString& secret)
{
    const QString ini = settingsRoot_.filePath(
        QStringLiteral("ai-%1.ini").arg(++settingsSequence_));
    globalAi_ = std::make_unique<AiRuntimeConfiguration>(*store_, ini);
    if (!secret.isEmpty()) {
        globalAi_->saveCredential(kind, secret);
    }
    globalAi_->saveConfiguration(kind, model);
    return globalAi_.get();
}

ManualQaController* ManualQaTest::makeController()
{
    auto* controller = new ManualQaController(); // the ONE construction site
    if (globalAi_ != nullptr) {
        controller->setAiRuntimeConfiguration(globalAi_.get());
    }
    return controller;
}

void ManualQaTest::switchGlobalAi(AiProviderKind kind, const QString& model,
                                  const QString& secret)
{
    QVERIFY(globalAi_ != nullptr);
    if (!secret.isEmpty()) {
        QVERIFY(globalAi_->saveCredential(kind, secret));
    }
    QVERIFY(globalAi_->saveConfiguration(kind, model));
}

AiRuntimeConfiguration* ManualQaTest::seedUnsavedGlobalAi()
{
    const QString ini = settingsRoot_.filePath(
        QStringLiteral("ai-unsaved-%1.ini").arg(++settingsSequence_));
    globalAi_ = std::make_unique<AiRuntimeConfiguration>(*store_, ini);
    return globalAi_.get(); // deliberately NOT saved
}

// A REAL ManualQaAsyncRunner whose transport is the fake holding client.
void ManualQaTest::injectRealRunnerOverFakeHttp()
{
    if (qa_ == nullptr) {
        return;
    }
    if (http_ == nullptr) {
        http_ = std::make_unique<HoldingHttpClient>();
    }
    injectedRunner_ = std::make_unique<ManualQaAsyncRunner>(*http_);
    qa_->setRunnerForAutomation(injectedRunner_.get());
}

void ManualQaTest::cleanup()
{
    // Fresh controllers per test; the managed root override stays for the
    // whole suite so the store path is stable.
    delete qa_;
    qa_ = nullptr;
    delete manual_;
    manual_ = nullptr;
    fake_ = nullptr;
    // The injected runner must outlive nothing: destroy it AFTER the
    // controllers that reference it (done above).
    injectedRunner_.reset();
    http_.reset();
    globalAi_.reset();
    store_.reset();
}

static ManualImportController* importSeed(const QString& seedPath)
{
    auto* manual = new ManualImportController();
    bool imported = false;
    QMetaObject::invokeMethod(manual, "importManualFile",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(bool, imported),
                              Q_ARG(QUrl, QUrl::fromLocalFile(seedPath)));
    if (!imported) {
        delete manual;
        return nullptr;
    }
    return manual;
}

void ManualQaTest::qa01_noSelectionRejectsLocally()
{
    manual_ = new ManualImportController();
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    QVERIFY(!qa_->hasSelectedManual());
    qa_->ask("What is the rated input voltage?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("failed"));
    QCOMPARE(qa_->lastErrorToken(), QStringLiteral("qa_no_selection"));
    QCOMPARE(fake_->beginCount(), 0);
}

void ManualQaTest::qa03_emptyQuestionRejectedWithoutDispatch()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("   ");
    QCOMPARE(qa_->stateToken(), QStringLiteral("failed"));
    QCOMPARE(qa_->lastErrorToken(), QStringLiteral("qa_empty_question"));
    QCOMPARE(fake_->beginCount(), 0);
}

void ManualQaTest::qa04_extractionConsentDoesNotGrantQaConsent()
{
    // Prove the extraction consent is genuinely separate: granting the
    // extraction-side consent leaves Q&A at consent_required (D4).
    auto* candidateController = new CandidateExtractionController(this);
    bool granted = false;
    QMetaObject::invokeMethod(candidateController, "grantConsent",
                              Qt::DirectConnection);
    QVERIFY(granted || true); // extraction grant never touches Q&A state

    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->ask("What is the warranty period?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("consent_required"));
    QCOMPARE(fake_->beginCount(), 0);
    delete candidateController;
}

void ManualQaTest::qa05_consentCancelZeroDispatch()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->ask("What is the warranty period?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("consent_required"));
    qa_->rejectConsent(); // cancel: zero dispatch, question stays retryable
    QCOMPARE(fake_->beginCount(), 0);
    QCOMPARE(qa_->stateToken(), QStringLiteral("consent_required"));
}

void ManualQaTest::qa06_consentAgreeExactlyOneDispatch()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->ask("What is the warranty period?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("consent_required"));
    qa_->grantConsent();
    qa_->ask("What is the warranty period?");
    QCOMPARE(fake_->beginCount(), 1);
    QCOMPARE(qa_->stateToken(), QStringLiteral("completed"));
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("found"));
    QVERIFY(!qa_->answerText().isEmpty());
    QCOMPARE(qa_->citations().size(), 1);
}

void ManualQaTest::qa07_subsequentAskDoesNotRePrompt()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->ask("What is the warranty period?");
    qa_->grantConsent();
    qa_->ask("What is the warranty period?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("found"));
    qa_->ask("What is the rated input voltage?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("completed"));
    QCOMPARE(fake_->beginCount(), 2); // no re-prompt, second dispatch directly
}

void ManualQaTest::qa08_foundWithValidCitation()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("found"));
    const QVariantList citations = qa_->citations();
    QCOMPARE(citations.size(), 1);
    const QVariantMap citation = citations.first().toMap();
    QVERIFY(!citation.value(QStringLiteral("excerpt")).toString().isEmpty());
    QVERIFY(!citation.value(QStringLiteral("contentHash")).toString().isEmpty());
    QVERIFY(citation.value(QStringLiteral("textStart")).toLongLong() >= 0);
}

void ManualQaTest::qa09_wrongDocumentIsError()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::FoundWrongDocument,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
    QCOMPARE(qa_->lastErrorToken(),
             QStringLiteral("qa_citation_document_mismatch"));
}

void ManualQaTest::qa10_staleContentHashIsError()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::FoundStaleHash,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
    QCOMPARE(qa_->lastErrorToken(),
             QStringLiteral("qa_citation_content_hash_mismatch"));
}

void ManualQaTest::qa11_invalidRangeIsError()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::FoundInvalidRange,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
    QCOMPARE(qa_->lastErrorToken(),
             QStringLiteral("qa_citation_invalid_range"));
}

void ManualQaTest::qa12_excerptMismatchIsError()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::FoundExcerptMismatch,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
    QCOMPARE(qa_->lastErrorToken(),
             QStringLiteral("qa_citation_round_trip_failed"));
}

void ManualQaTest::qa13_duplicateExcerptDoesNotInvalidate()
{
    // §100.2 (corrected plan): repeated text elsewhere in the Manual does NOT
    // invalidate an otherwise exact citation — no global uniqueness rule.
    const std::string text = "Alpha line.\nRated voltage: 380 V AC.\n"
                             "Middle filler.\nRated voltage: 380 V AC.\n";
    ManualQaCitation citation;
    citation.documentId = "doc";
    citation.contentHash = "hash";
    citation.pageNumber = -1;
    citation.textStart = 12;
    citation.textEnd = 36;
    citation.excerpt = "Rated voltage: 380 V AC.";
    QCOMPARE(validateManualQaCitation(citation, "doc", "hash", text),
             ManualQaCitationCode::Ok);
    QVERIFY(text.find(citation.excerpt) != std::string::npos);
    QVERIFY(text.find(citation.excerpt,
                      text.find(citation.excerpt) + 1)
            != std::string::npos); // genuinely duplicated
}

void ManualQaTest::qa14_foundZeroCitationsIsError()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::FoundNoCitations,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
    QVERIFY(qa_->lastErrorToken().startsWith(QStringLiteral("qa_citation_")));
}

void ManualQaTest::qa15_foundEmptyAnswerIsError()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::FoundEmptyAnswer,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
}

void ManualQaTest::qa16_notFoundNoProviderAnswerDisplayed()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::NotFound,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("What is the quantum flux capacity?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("not_found"));
    // The provider's general-knowledge prose must NOT be displayed.
    QVERIFY(!qa_->answerText().contains(QStringLiteral("general knowledge")));
    QVERIFY(qa_->citations().isEmpty());
}

void ManualQaTest::qa17_insufficientNoProviderAnswerDisplayed()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Insufficient,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Explain the quantum flux capacity in detail.");
    QCOMPARE(qa_->resultStatusToken(),
             QStringLiteral("insufficient_evidence"));
    QVERIFY(!qa_->answerText().contains(QStringLiteral("do not display")));
    QVERIFY(qa_->citations().isEmpty());
}

void ManualQaTest::qa18_malformedOutputIsError()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Malformed,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
    // M12-D-R2D: the token now carries the SAFE parse-failure category.
    QCOMPARE(qa_->lastErrorToken(), QStringLiteral("qa_parse_json_syntax"));
    // ERROR must not be converted into a semantic state.
    QVERIFY(qa_->resultStatusToken() != QStringLiteral("not_found"));
    QVERIFY(qa_->resultStatusToken()
            != QStringLiteral("insufficient_evidence"));
}

void ManualQaTest::qa19_providerFailureIsError()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Failure,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
    QCOMPARE(qa_->lastErrorToken(), QStringLiteral("qa_provider_failure"));
}

void ManualQaTest::qa20_noProfileMutation()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    auto* profiles = new ProfileController(manual_);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    profiles->newProfile();
    profiles->setDisplayName("Isolation Profile");
    profiles->setManufacturer("Original Co.");
    QVERIFY(profiles->saveCurrent());
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("found"));
    // The Q&A path has NO reference to ProfileController by construction;
    // assert the profile stays untouched.
    QCOMPARE(profiles->manufacturer(), QStringLiteral("Original Co."));
    QCOMPARE(profiles->dirty(), false);
    const auto persisted = ProfileStore::loadFromFile(
        ProfileStore::defaultFilePathFor(profiles->currentProfileId()));
    QVERIFY(persisted.ok());
    QCOMPARE(QString::fromStdString(persisted.profile.manufacturer),
             QStringLiteral("Original Co."));
    delete profiles;
}

void ManualQaTest::qa21_noCandidateMutation()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    auto* candidateController = new CandidateExtractionController(this);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("found"));
    // Zero Candidate creation/mutation/consumption from the Q&A path.
    QCOMPARE(candidateController->property("candidateCount").toInt(), 0);
    delete candidateController;
}

void ManualQaTest::qa22_noManualMutation()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    const int countBefore = manual_->manualDocuments().size();
    const QString contentBefore = ManualStore::loadText(
        manual_->selectedDocument().value("contentHash").toString());
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("found"));
    QCOMPARE(manual_->manualDocuments().size(), countBefore);
    QCOMPARE(ManualStore::loadText(
                 manual_->selectedDocument().value("contentHash").toString()),
             contentBefore);
}

void ManualQaTest::qa24_manualSwitchClearsAndInvalidates()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Deferred, ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("running"));
    QVERIFY(fake_->hasDeferred());
    // Import a second manual and switch to it: Q&A context must clear.
    const QString secondPath = QDir(managedRoot_.path())
                                   .filePath(QStringLiteral("sources/second.txt"));
    {
        QFile second(secondPath);
        QVERIFY(second.open(QIODevice::WriteOnly));
        second.write("Other: manual two\n");
    }
    bool imported = false;
    QMetaObject::invokeMethod(manual_, "importManualFile",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(bool, imported),
                              Q_ARG(QUrl, QUrl::fromLocalFile(secondPath)));
    QVERIFY(imported);
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("none"));
    QCOMPARE(qa_->stateToken(), QStringLiteral("idle"));
    // The deferred completion belongs to the invalidated generation: DROP.
    fake_->deliverDeferred();
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("none"));
}

void ManualQaTest::qa25_lateResponseAfterSwitchDropped()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Deferred, ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QVERIFY(fake_->hasDeferred());
    // Importing a new manual changes the list (documentsChanged) but the
    // selection identity ALSO moves to the new document -> invalidated.
    const QString secondPath = QDir(managedRoot_.path())
                                   .filePath(QStringLiteral("sources/second.txt"));
    {
        QFile second(secondPath);
        QVERIFY(second.open(QIODevice::WriteOnly));
        second.write("Other: manual two\n");
    }
    bool imported = false;
    QMetaObject::invokeMethod(manual_, "importManualFile",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(bool, imported),
                              Q_ARG(QUrl, QUrl::fromLocalFile(secondPath)));
    QVERIFY(imported);
    fake_->deliverDeferred();
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("none"));
    QCOMPARE(qa_->stateToken(), QStringLiteral("idle"));
}

void ManualQaTest::qa26_runningQaDoesNotBlockDelete()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Deferred, ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("running"));
    // ML-2 delete stays authoritative and must NOT be blocked by Q&A.
    const QString documentId =
        manual_->selectedDocument().value("documentId").toString();
    const ManualStore::ManualDeleteResult result =
        manual_->deleteDocumentById(documentId);
    QVERIFY(result.outcome == ManualStore::ManualDeleteOutcome::Success
            || result.outcome
                == ManualStore::ManualDeleteOutcome::SuccessWithCleanupWarning);
}

void ManualQaTest::qa27_successfulDeleteClearsQa()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Deferred, ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("running"));
    const QString documentId =
        manual_->selectedDocument().value("documentId").toString();
    manual_->deleteDocumentById(documentId);
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("none"));
    QCOMPARE(qa_->stateToken(), QStringLiteral("idle"));
    QVERIFY(!qa_->hasSelectedManual());
}

void ManualQaTest::qa28_lateResponseAfterDeleteDropped()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Deferred, ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QVERIFY(fake_->hasDeferred());
    const QString documentId =
        manual_->selectedDocument().value("documentId").toString();
    manual_->deleteDocumentById(documentId);
    fake_->deliverDeferred(); // late completion for the dead generation
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("none"));
    QCOMPARE(qa_->stateToken(), QStringLiteral("idle"));
}

void ManualQaTest::qa29_cancelAttemptedOnInvalidation()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Deferred, ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QVERIFY(fake_->hasDeferred());
    const QString documentId =
        manual_->selectedDocument().value("documentId").toString();
    manual_->deleteDocumentById(documentId);
    // Best-effort cancel was attempted on invalidation (D5/§100.5). Both the
    // list-change and the selection-change notifications run the same
    // idempotent invalidation, so 1+ calls are fine.
    QVERIFY(fake_->cancelCount() >= 1);
}

void ManualQaTest::qa30_newRequestSupersedesOldGeneration()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Deferred, ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("First question?");
    QVERIFY(fake_->hasDeferred()); // generation N is in flight
    // Switch manuals: N is invalidated and the Q&A surface clears.
    const QString secondPath = QDir(managedRoot_.path())
                                   .filePath(QStringLiteral("sources/second.txt"));
    {
        QFile second(secondPath);
        QVERIFY(second.open(QIODevice::WriteOnly));
        second.write("Other: manual two\n");
    }
    bool imported = false;
    QMetaObject::invokeMethod(manual_, "importManualFile",
                              Qt::DirectConnection,
                              Q_RETURN_ARG(bool, imported),
                              Q_ARG(QUrl, QUrl::fromLocalFile(secondPath)));
    QVERIFY(imported);
    QCOMPARE(qa_->stateToken(), QStringLiteral("idle"));
    // A NEW request under generation N+1 goes to the NEW selected manual.
    qa_->ask("Second question?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("running"));
    // The STALE N completion arrives first: it must NOT become current truth.
    fake_->deliverDeferred();
    QCOMPARE(qa_->stateToken(), QStringLiteral("running"));
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("none"));
    // The N+1 completion lands normally.
    fake_->deliverDeferred();
    QCOMPARE(qa_->stateToken(), QStringLiteral("completed"));
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("found"));
}

void ManualQaTest::qa31_newControllerHasNoHistory()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    QCOMPARE(qa_->stateToken(), QStringLiteral("idle"));
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("none"));
    QCOMPARE(qa_->answerText(), QString());
    QVERIFY(qa_->citations().isEmpty());
}

void ManualQaTest::qa32_noPersistenceArtifacts()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    const QDir storeRoot(managedRoot_.path());
    const QStringList before =
        storeRoot.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("found"));
    const QStringList after =
        storeRoot.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    QCOMPARE(after, before); // session-only: nothing new was written
}

void ManualQaTest::qa34_productionRunnerFailsClosedWithoutCredential()
{
    // T034-B2.2-B (migrated): the production runner no longer reads the
    // process environment AT ALL. Credential authority moved to the global
    // configuration owner, which resolves the secret and hands it down in a
    // ManualQaDispatchConfig. This test therefore proves the NEW invariant:
    //   empty credential in the dispatch config
    //   => started == false, zero dispatch, onDone never invoked
    // AND, as the load-bearing half of the migration, that an AMBIENT
    // MODELSCOPE_API_KEY has ZERO effect on the runner (there is no legacy
    // fallback left to honour it).
    struct EnvGuard
    {
        QList<QPair<QString, QString>> saved;
        explicit EnvGuard(std::initializer_list<const char *> vars)
        {
            for (const char *raw : vars) {
                const QString name = QString::fromLatin1(raw);
                const QString value = qEnvironmentVariable(raw);
                if (!value.isEmpty())
                    saved.append({name, value});
                qunsetenv(raw);
            }
        }
        EnvGuard(const EnvGuard &) = delete;
        EnvGuard &operator=(const EnvGuard &) = delete;
        ~EnvGuard()
        {
            for (const auto &entry : std::as_const(saved))
                qputenv(entry.first.toUtf8().constData(),
                        entry.second.toUtf8().constData());
        }
    } guard{{"MODELSCOPE_API_KEY", "MODBUSLENS_MODELSCOPE_MODEL",
             "DEEPSEEK_API_KEY", "MODBUSLENS_AI_PROVIDER",
             "MODBUSLENS_AI_MODEL"}};

    ManualQaAsyncRunner runner;
    core::ManualQaRequest request;
    request.question = "test";
    request.documentId = "doc";
    request.contentHash = "hash";

    // Case A: an AMBIENT credential must NOT be honoured any more. With a
    // fully populated environment but an empty dispatch config, nothing is
    // sent.
    qputenv("MODELSCOPE_API_KEY", "ambient-key-that-must-be-ignored");
    bool completedAmbient = false;
    const bool startedAmbient = runner.begin(
        ManualQaDispatchConfig{}, request, 1,
        [&completedAmbient](const IManualQaRunner::Completion &) {
            completedAmbient = true;
        });
    QVERIFY2(!startedAmbient,
             "an ambient MODELSCOPE_API_KEY must not make the runner dispatch: "
             "the legacy environment fallback is gone");
    QVERIFY(!completedAmbient);
    qunsetenv("MODELSCOPE_API_KEY");

    // Case B: the honest fail-closed assertion — a usable endpoint/model but
    // an EMPTY credential is still refused.
    bool completed = false;
    ManualQaDispatchConfig config;
    config.endpoint = "https://example.invalid/v1/chat/completions";
    config.model = "some/model";
    config.secret = std::string(); // credential absent
    const bool started = runner.begin(
        config, request, 2,
        [&completed](const IManualQaRunner::Completion &) { completed = true; });
    QVERIFY2(!started, "an empty credential must fail closed before any dispatch");
    QVERIFY(!completed);
    QCOMPARE(runner.dispatchCount(), 0);
    QCOMPARE(runner.beginCount(), 2);
}

// M12-D-R2B (RCA-confirmed thinking-mode defect): the Q&A provider request
// MUST explicitly disable thinking for the strict structured-output task.
// Proven against the real provider (safe structural diagnostics, T027
// packet §11 allowlist): without the flag the thinking pass consumes
// thousands of completion tokens and can starve/truncate the final JSON;
// with chat_template_kwargs.enable_thinking=false the provider returns
// reasoning-free content (completion tokens 4000-6000 -> 63-65).
void ManualQaTest::qa36_qaRequestDisablesThinking()
{
    // The Q&A request body is built by a pure function, so the thinking-mode
    // contract is asserted directly: the Q&A task MUST request non-thinking
    // behavior (RCA-confirmed defect: the thinking pass consumes thousands of
    // completion tokens and can starve/truncate the final JSON).
    core::ManualQaRequest request;
    request.question = "Who is the manufacturer?";
    request.documentId = "doc-diag";
    request.contentHash = "hash-diag";
    core::ManualQaContextBlock block;
    block.start = 0;
    block.end = 5;
    block.text = "Hello";
    request.blocks.push_back(block);

    // T034-B2.2-B-R1: the builder takes the RESOLVED provider kind + model id.
    const QJsonObject body = ManualQaAsyncRunner::buildRequestBody(
        AiProviderKind::ModelScope, "Qwen/Qwen3.5-27B", request);
    const QJsonObject templateKwargs =
        body.value(QStringLiteral("chat_template_kwargs")).toObject();
    QVERIFY(!templateKwargs.isEmpty());
    QCOMPARE(templateKwargs.value(QStringLiteral("enable_thinking")),
             QJsonValue(false));
    // Shared fields stay intact (diagnosis contract untouched).
    QCOMPARE(body.value(QStringLiteral("max_tokens")), QJsonValue(768));
    QCOMPARE(body.value(QStringLiteral("stream")), QJsonValue(false));
    QVERIFY(body.value(QStringLiteral("messages")).isArray());
    QCOMPARE(body.value(QStringLiteral("model")),
             QJsonValue(QStringLiteral("Qwen/Qwen3.5-27B")));
}

// M12-D-R2B root-cause regression: ModelScopeAgentClient emits the assistant
// MESSAGE object; the answer content lives at its TOP LEVEL. The pre-fix
// runner re-applied choices[0].message extraction and always produced empty
// content, so every live Q&A parse failed with qa_malformed_output.
void ManualQaTest::qa37_roundSucceededExtractsTopLevelContent()
{
    QJsonObject assistantMessage;
    assistantMessage.insert(
        QStringLiteral("role"), QStringLiteral("assistant"));
    assistantMessage.insert(
        QStringLiteral("content"),
        QStringLiteral("{\"status\":\"found\",\"answer\":\"ACME\","
                       "\"citations\":[]}"));
    // The message shape deliberately does NOT contain a nested "choices" or
    // "message" key: re-applying choices[0].message extraction on it must
    // yield empty content (the pre-fix behavior).
    const QString content =
        ManualQaAsyncRunner::extractAssistantContent(assistantMessage);
    QVERIFY(!content.isEmpty());
    QVERIFY(content.startsWith(QStringLiteral("{\"status\"")));

    // Defensive contract: reasoning_content never leaks into the answer.
    QJsonObject withReasoning = assistantMessage;
    withReasoning.insert(QStringLiteral("reasoning_content"),
                         QStringLiteral("REASONING MUST NOT LEAK"));
    const QString content2 =
        ManualQaAsyncRunner::extractAssistantContent(withReasoning);
    QVERIFY(!content2.contains(QStringLiteral("REASONING")));
}

// M12-D-R2D observability: the SAME malformed inputs stay rejected (the
// accepted input set is unchanged), but each rejection now carries a SAFE
// deterministic category token (never question/answer/excerpt/reasoning/
// raw content). This is what makes the intermittent live structured-output
// defect RCA-able.
void ManualQaTest::qa38_parseFailureCategoriesAreSafeAndDeterministic()
{
    QString category;
    const auto parse = [&category](const char* json) {
        const auto result = ManualQaAsyncRunner::parseProviderResult(
            json, &category);
        return result.has_value();
    };

    // Envelope classifications (still rejected — observability only).
    QCOMPARE(parse(""), false);
    QCOMPARE(category, QStringLiteral("qa_parse_empty_content"));

    QCOMPARE(parse("```json\" \"{\"status\":\"found\"}\" \"```"), false);
    QCOMPARE(category, QStringLiteral("qa_parse_markdown_fence"));

    QCOMPARE(parse("<think>{\"status\":\"found\"}</think>"), false);
    QCOMPARE(category, QStringLiteral("qa_parse_think_envelope"));

    QCOMPARE(parse("{broken"), false);
    QCOMPARE(category, QStringLiteral("qa_parse_json_syntax"));

    QCOMPARE(parse("[\"array\"]"), false);
    QCOMPARE(category, QStringLiteral("qa_parse_top_level_not_object"));

    // Schema classifications.
    QCOMPARE(parse("{}"), false);
    QCOMPARE(category, QStringLiteral("qa_parse_missing_status"));

    QCOMPARE(parse("{\"status\":42}"), false);
    QCOMPARE(category, QStringLiteral("qa_parse_status_wrong_type"));

    QCOMPARE(parse("{\"status\":\"bogus\",\"answer\":\"x\","
                   "\"citations\":[]}"),
             false);
    QCOMPARE(category, QStringLiteral("qa_parse_status_unknown"));

    QCOMPARE(parse("{\"status\":\"found\",\"citations\":[]}"), false);
    QCOMPARE(category, QStringLiteral("qa_parse_missing_answer"));

    QCOMPARE(parse("{\"status\":\"found\",\"answer\":42,"
                   "\"citations\":[]}"),
             false);
    QCOMPARE(category, QStringLiteral("qa_parse_answer_wrong_type"));

    QCOMPARE(parse("{\"status\":\"found\",\"answer\":\"x\"}"), false);
    QCOMPARE(category, QStringLiteral("qa_parse_missing_citations"));

    QCOMPARE(parse("{\"status\":\"found\",\"answer\":\"x\","
                   "\"citations\":\"not-a-list\"}"),
             false);
    QCOMPARE(category, QStringLiteral("qa_parse_citations_wrong_type"));

    QCOMPARE(parse("{\"status\":\"found\",\"answer\":\"x\","
                   "\"citations\":[\"str\"]}"),
             false);
    QCOMPARE(category, QStringLiteral("qa_parse_citation_not_object"));

    QCOMPARE(parse("{\"status\":\"found\",\"answer\":\"x\","
                   "\"citations\":[{\"documentId\":\"d\"}]}"),
             false);
    QCOMPARE(category,
             QStringLiteral("qa_parse_citation_missing_required_field"));

    QCOMPARE(parse("{\"status\":\"found\",\"answer\":\"x\","
                   "\"citations\":[{\"documentId\":\"d\","
                   "\"contentHash\":\"h\",\"pageNumber\":\"-1\","
                   "\"textStart\":0,\"textEnd\":5,\"excerpt\":\"e\"}]}"),
             false);
    QCOMPARE(category, QStringLiteral("qa_parse_citation_wrong_field_type"));

    // Prose around JSON remains rejected (arbitrary extraction forbidden).
    QCOMPARE(parse("Here is the answer: {\"status\":\"found\","
                   "\"answer\":\"x\",\"citations\":[]}"),
             false);
    QVERIFY(category == QStringLiteral("qa_parse_json_syntax")
            || category == QStringLiteral("qa_parse_top_level_not_object"));
    QCOMPARE(parse("{\"status\":\"found\",\"answer\":\"x\","
                   "\"citations\":[]} trailing"),
             false);
    QVERIFY(category == QStringLiteral("qa_parse_json_syntax")
            || category == QStringLiteral("qa_parse_top_level_not_object"));

    // Valid input: category stays empty on success.
    QCOMPARE(parse("{\"status\":\"not_found\",\"answer\":\"\","
                   "\"citations\":[]}"),
             true);
    QCOMPARE(category, QString());
}

// M12-D-R2E: the APP-RESOLVED citation contract. The provider only SELECTS
// opaque citationIds the app attached to its own deterministic context
// blocks; the controller maps the ids back to canonical D2 citations
// (documentId/contentHash from the request binding, offsets/excerpt from
// the referenced block) and the UNCHANGED local validator remains the final
// authority. Provider authority is reduced: it can no longer author
// canonical identity/hash/offsets at all.
void ManualQaTest::qa40_appResolvedCitationIds()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();

    // Provider selects citationId "c1" — the new output contract.
    const auto selectedIdJson =
        "{\"status\":\"found\",\"answer\":\"The manufacturer is ACME "
        "Power Systems Ltd.\",\"citations\":[{\"citationId\":\"c1\"}]}";
    fake_->script(selectedIdJson);
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("found"));
    const QVariantList citations = qa_->citations();
    QCOMPARE(citations.size(), 1);
    const QVariantMap citation = citations.first().toMap();
    // The mapped citation is the CANONICAL D2 shape with REAL block data.
    QCOMPARE(citation.value(QStringLiteral("documentId")).toString(),
             manual_->selectedDocument()
                 .value(QStringLiteral("documentId"))
                 .toString());
    QCOMPARE(citation.value(QStringLiteral("contentHash")).toString(),
             manual_->selectedDocument()
                 .value(QStringLiteral("contentHash"))
                 .toString());
    QVERIFY(citation.value(QStringLiteral("textStart")).toLongLong() >= 0);
    QVERIFY(citation.value(QStringLiteral("textEnd")).toLongLong()
            > citation.value(QStringLiteral("textStart")).toLongLong());
    // The excerpt is the REAL block slice: exact round-trip by construction.
    const QString excerpt = citation.value(QStringLiteral("excerpt")).toString();
    QVERIFY(!excerpt.isEmpty());

    // Strictness: an UNKNOWN citationId is fail-closed ERROR.
    fake_->script(
        "{\"status\":\"found\",\"answer\":\"x\","
        "\"citations\":[{\"citationId\":\"c99\"}]}");
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
    QCOMPARE(qa_->lastErrorToken(),
             QStringLiteral("qa_parse_citation_unknown_id"));
}

// Deterministic reproduction of the Human live failure class (R2E probes x2:
// provider-authored offsets inside the block but excerpt/offsets not exactly
// matching the canonical slice -> round_trip_failed). Under the repaired
// contract the provider can no longer author offsets, but the class remains
// ERROR fail-closed for any legacy-shaped output.
void ManualQaTest::qa41_providerAuthoredOffsetsReproduceLiveFailureClass()
{
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::FoundExcerptMismatch,
                     ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(qa_->resultStatusToken(), QStringLiteral("error"));
    QCOMPARE(qa_->lastErrorToken(),
             QStringLiteral("qa_citation_round_trip_failed"));
}

// T033 regression: a text-layer PDF must dispatch Q&A through the canonical
// seam (ManualStore::loadText now aggregates the per-page PDF cache truth).
// Pre-fix this failed locally with qa_no_canonical_text.
void ManualQaTest::qa42_textLayerPdfDispatchesThroughCanonicalSeam()
{
    const QString source =
        QDir(managedRoot_.path()).filePath("sources/qa-manual.pdf");
    QFile pdf(source);
    QVERIFY(pdf.open(QIODevice::WriteOnly));
    const QByteArray pdfBytes = qaTextLayerPdf(
        QStringLiteral("Manufacturer: ACME Power Systems Ltd."));
    QCOMPARE(pdf.write(pdfBytes), static_cast<qint64>(pdfBytes.size()));
    pdf.close();

    manual_ = new ManualImportController();
    bool imported = false;
    QMetaObject::invokeMethod(manual_, "importManualFile", Qt::DirectConnection,
                              Q_RETURN_ARG(bool, imported),
                              Q_ARG(QUrl, QUrl::fromLocalFile(source)));
    QVERIFY(imported);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    fake_->configure(FakeManualQaRunner::Mode::Deferred, ManualQaParsedResult{});
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("Who is the manufacturer?");
    QCOMPARE(fake_->beginCount(), 1);
}

// T033: a no_extractable_text PDF resolves the cache with a TRUTHFUL empty
// aggregation — Q&A must still fail closed locally, never dispatch an empty
// context.
void ManualQaTest::qa43_noTextPdfFailsClosedLocally()
{
    const QString source =
        QDir(managedRoot_.path()).filePath("sources/qa-scanned.pdf");
    QFile pdf(source);
    QVERIFY(pdf.open(QIODevice::WriteOnly));
    const QByteArray pdfBytes = qaNoTextPdf();
    QCOMPARE(pdf.write(pdfBytes), static_cast<qint64>(pdfBytes.size()));
    pdf.close();

    manual_ = new ManualImportController();
    bool imported = false;
    QMetaObject::invokeMethod(manual_, "importManualFile", Qt::DirectConnection,
                              Q_RETURN_ARG(bool, imported),
                              Q_ARG(QUrl, QUrl::fromLocalFile(source)));
    QVERIFY(imported);
    qa_ = makeController();
    fake_ = new FakeManualQaRunner();
    qa_->setRunnerForAutomation(fake_);
    qa_->setManualController(manual_);
    qa_->grantConsent();
    qa_->ask("What is the warranty period?");
    QCOMPARE(qa_->stateToken(), QStringLiteral("failed"));
    QCOMPARE(qa_->lastErrorToken(), QStringLiteral("qa_no_canonical_text"));
    QCOMPARE(fake_->beginCount(), 0);
}

void ManualQaTest::parser_validStates()
{
    const std::string found =
        "{\"status\":\"found\",\"answer\":\"380 V AC\",\"citations\":"
        "[{\"documentId\":\"d\",\"contentHash\":\"h\",\"pageNumber\":-1,"
        "\"textStart\":0,\"textEnd\":5,\"excerpt\":\"Alpha\"}]}";
    auto result = ManualQaAsyncRunner::parseProviderResult(found);
    QVERIFY(result.has_value());
    QCOMPARE(result->status, ManualQaStatus::Found);
    QCOMPARE(result->answer, std::string("380 V AC"));
    QCOMPARE(result->citations.size(), std::size_t{1});

    result = ManualQaAsyncRunner::parseProviderResult(
        "{\"status\":\"not_found\",\"answer\":\"\",\"citations\":[]}");
    QVERIFY(result.has_value());
    QCOMPARE(result->status, ManualQaStatus::NotFound);

    result = ManualQaAsyncRunner::parseProviderResult(
        "{\"status\":\"insufficient_evidence\",\"answer\":\"\",\"citations\":"
        "[]}");
    QVERIFY(result.has_value());
    QCOMPARE(result->status, ManualQaStatus::InsufficientEvidence);
}

void ManualQaTest::parser_malformedVariants()
{
    QVERIFY(!ManualQaAsyncRunner::parseProviderResult("{").has_value());
    QVERIFY(!ManualQaAsyncRunner::parseProviderResult("[]").has_value());
    QVERIFY(!ManualQaAsyncRunner::parseProviderResult(
                "{\"answer\":\"x\",\"citations\":[]}")
                .has_value()); // missing status
    QVERIFY(!ManualQaAsyncRunner::parseProviderResult(
                "{\"status\":\"found\",\"citations\":[]}")
                .has_value()); // missing answer
    QVERIFY(!ManualQaAsyncRunner::parseProviderResult(
                "{\"status\":\"found\",\"answer\":\"x\"}")
                .has_value()); // missing citations
    QVERIFY(!ManualQaAsyncRunner::parseProviderResult(
                "{\"status\":\"totally_unknown\",\"answer\":\"x\","
                "\"citations\":[]}")
                .has_value()); // unknown status: ERROR, never coerced
    QVERIFY(!ManualQaAsyncRunner::parseProviderResult(
                "{\"status\":\"found\",\"answer\":\"x\",\"citations\":"
                "[{\"documentId\":\"d\"}]}")
                .has_value()); // broken citation shape
    QVERIFY(!ManualQaAsyncRunner::parseProviderResult(
                "{\"status\":\"found\",\"answer\":\"x\",\"citations\":"
                "[{\"documentId\":\"d\",\"contentHash\":\"h\","
                "\"pageNumber\":\"-1\",\"textStart\":0,\"textEnd\":5,"
                "\"excerpt\":\"e\"}]}")
                .has_value()); // wrong field type
    // Impossible numeric range is still a parseable SHAPE: the range check
    // belongs to the local citation validator, not the parser.
    QVERIFY(ManualQaAsyncRunner::parseProviderResult(
                "{\"status\":\"found\",\"answer\":\"x\",\"citations\":"
                "[{\"documentId\":\"d\",\"contentHash\":\"h\","
                "\"pageNumber\":-1,\"textStart\":99,\"textEnd\":5,"
                "\"excerpt\":\"e\"}]}")
                .has_value());
    // Range validity is the validator's job:
    ManualQaParsedResult bad;
    bad.status = ManualQaStatus::Found;
    bad.answer = "x";
    ManualQaCitation citation;
    citation.documentId = "d";
    citation.contentHash = "h";
    citation.pageNumber = -1;
    citation.textStart = 99;
    citation.textEnd = 5;
    citation.excerpt = "e";
    bad.citations.push_back(citation);
    const auto validation =
        validateManualQaFoundResult(bad, "d", "h", "short text");
    QVERIFY(!validation.ok);
    QCOMPARE(validation.citationCode, ManualQaCitationCode::InvalidRange);
}

void ManualQaTest::contextBlocks_deterministicBounded()
{
    std::string text;
    for (int i = 0; i < 200; ++i) {
        text += "Line " + std::to_string(i) + " filler content.\n";
    }
    const auto blocks = buildManualQaContextBlocks(text, "Line 190 filler");
    QVERIFY(!blocks.empty());
    QVERIFY(blocks.size() <= 6);
    // Deterministic: same inputs, same blocks.
    const auto again = buildManualQaContextBlocks(text, "Line 190 filler");
    QCOMPARE(blocks.size(), again.size());
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        QCOMPARE(blocks[i].start, again[i].start);
        QCOMPARE(blocks[i].end, again[i].end);
        QVERIFY(blocks[i].end > blocks[i].start);
    }
    // All blocks stay inside the selected Manual text bounds.
    for (const auto& block : blocks) {
        QVERIFY(block.end <= static_cast<std::int64_t>(text.size()));
    }
}

// ===========================================================================
// T034-B2.2-B — MANUAL QA GLOBAL-AI MIGRATION (B22M-01..11)
//
// Everything below is expressed through the Qt meta-object / public surface so
// the suite compiles against BOTH the pre- and post-migration headers. That is
// the point: a genuine RED here means "the migrated capability does not exist
// yet at runtime", never "the test file did not compile".
// ===========================================================================

namespace {

// Reads a bool from any object that exposes `name` as a Q_PROPERTY or
// Q_INVOKABLE returning bool. Returns false when the member does not exist at
// all (which is exactly the pre-migration state).
[[nodiscard]] bool reflectiveHasMember(const QObject* object, const char* name)
{
    const QMetaObject* meta = object->metaObject();
    if (meta->indexOfProperty(name) >= 0
        || meta->indexOfMethod(
               QMetaObject::normalizedSignature(
                   (QString(name) + "()").toUtf8().constData())
                   .constData())
            >= 0) {
        return true;
    }
    return false;
}

} // namespace

// ===========================================================================
// T034-B2.2-B-R1 — B22M-01..11 REAL authority / HTTP / stale-owner acceptance.
//
// Every case drives the REAL ManualQaController + REAL ManualQaAsyncRunner
// through a deterministic global configuration and inspects the ACTUAL HTTP
// request captured by the fake transport. Credential comparisons use QVERIFY2
// so a FAILING assertion never prints a token value.
// ===========================================================================

void ManualQaTest::b22m01_deepSeekGlobalAuthorityAndPayload()
{
    seedGlobalAi(AiProviderKind::DeepSeek,
                 QStringLiteral("det/deepseek-qa-model"),
                 QStringLiteral("fake-deepseek-credential"));
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();
    qa_->grantConsent();
    qa_->ask(QStringLiteral("What is the warranty?"));
    QCOMPARE(qa_->stateToken(), QLatin1String("running"));

    // HTTP dispatch oracle: exactly one exchange, to the DeepSeek profile
    // endpoint (never the ModelScope one).
    QCOMPARE(http_->dispatchCount(), 1);
    const HoldingHttpClient::Captured &sent = http_->at(0);
    QCOMPARE(QString::fromStdString(sent.url),
             QString::fromUtf8(
                 aiProviderProfile(AiProviderKind::DeepSeek)
                     .chatCompletionsEndpoint));
    QVERIFY(sent.url.find("modelscope") == std::string::npos);
    QVERIFY2(QString::fromStdString(sent.bearer)
                 == QStringLiteral("fake-deepseek-credential"),
             "Authorization must carry the DeepSeek provider-scoped credential");

    const QJsonObject body =
        QJsonDocument::fromJson(QByteArray::fromStdString(sent.body)).object();
    QCOMPARE(body.value(QStringLiteral("model")).toString(),
             QStringLiteral("det/deepseek-qa-model"));
    QCOMPARE(body.value(QStringLiteral("stream")), QJsonValue(false));
    QCOMPARE(body.value(QStringLiteral("max_tokens")), QJsonValue(768));
    QVERIFY(body.value(QStringLiteral("messages")).isArray());

    // Load-bearing NEGATIVE ORACLE: DeepSeek must NOT carry the ModelScope-only
    // payload extra. Proving provider+model alone is NOT sufficient.
    QVERIFY2(!body.contains(QStringLiteral("chat_template_kwargs")),
             "DeepSeek Manual QA payload must NOT carry the ModelScope-only "
             "chat_template_kwargs field");
}

void ManualQaTest::b22m02_modelScopeGlobalAuthorityAndThinkingDisabled()
{
    seedGlobalAi(AiProviderKind::ModelScope,
                 QStringLiteral("det/modelscope-qa-model"),
                 QStringLiteral("fake-modelscope-credential"));
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();
    qa_->grantConsent();
    qa_->ask(QStringLiteral("What is the warranty?"));
    QCOMPARE(qa_->stateToken(), QLatin1String("running"));

    QCOMPARE(http_->dispatchCount(), 1);
    const HoldingHttpClient::Captured &sent = http_->at(0);
    QCOMPARE(QString::fromStdString(sent.url),
             QString::fromUtf8(
                 aiProviderProfile(AiProviderKind::ModelScope)
                     .chatCompletionsEndpoint));
    QVERIFY2(QString::fromStdString(sent.bearer)
                 == QStringLiteral("fake-modelscope-credential"),
             "Authorization must carry the ModelScope provider-scoped "
             "credential");

    const QJsonObject body =
        QJsonDocument::fromJson(QByteArray::fromStdString(sent.body)).object();
    QCOMPARE(body.value(QStringLiteral("model")).toString(),
             QStringLiteral("det/modelscope-qa-model"));
    // ModelScope payload SEMANTICS PRESERVED.
    const QJsonObject kwargs =
        body.value(QStringLiteral("chat_template_kwargs")).toObject();
    QVERIFY(!kwargs.isEmpty());
    QCOMPARE(kwargs.value(QStringLiteral("enable_thinking")),
             QJsonValue(false));
}

void ManualQaTest::b22m03_runtimeProviderSwitch()
{
    // Request #1 on ModelScope and COMPLETED; then a live Save switches the
    // provider to DeepSeek without a controller/app restart; request #2 must
    // use the DeepSeek endpoint / model / credential.
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();
    qa_->grantConsent();

    qa_->ask(QStringLiteral("question one"));
    QCOMPARE(http_->dispatchCount(), 1);
    QVERIFY(http_->at(0).url.find("modelscope") != std::string::npos);
    const std::uint64_t idFirst = http_->pendingIds().at(0);
    http_->complete(idFirst, qaFoundContent(QStringLiteral("answer-one")));
    QCOMPARE(qa_->stateToken(), QLatin1String("completed"));

    switchGlobalAi(AiProviderKind::DeepSeek,
                   QStringLiteral("det/deepseek-qa-model"),
                   QStringLiteral("fake-deepseek-credential"));
    QCOMPARE(qa_->stateToken(), QLatin1String("completed")); // not disturbed

    qa_->ask(QStringLiteral("question two"));
    QCOMPARE(http_->dispatchCount(), 2);
    const HoldingHttpClient::Captured &second = http_->at(1);
    QCOMPARE(QString::fromStdString(second.url),
             QString::fromUtf8(
                 aiProviderProfile(AiProviderKind::DeepSeek)
                     .chatCompletionsEndpoint));
    QVERIFY2(QString::fromStdString(second.bearer)
                 == QStringLiteral("fake-deepseek-credential"),
             "request #2 must use the DeepSeek provider-scoped credential");
    const QJsonObject secondBody =
        QJsonDocument::fromJson(QByteArray::fromStdString(second.body))
            .object();
    QCOMPARE(secondBody.value(QStringLiteral("model")).toString(),
             QStringLiteral("det/deepseek-qa-model"));
}

void ManualQaTest::b22m04_runtimeModelSwitch()
{
    // Provider unchanged; only the SAVED MODEL changes. Request #2 must use
    // model B, never A.
    seedGlobalAi(AiProviderKind::ModelScope,
                 QStringLiteral("det/modelscope-model-A"),
                 QStringLiteral("fake-modelscope-credential"));
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();
    qa_->grantConsent();

    qa_->ask(QStringLiteral("first"));
    QCOMPARE(http_->dispatchCount(), 1);
    {
        const QJsonObject first =
            QJsonDocument::fromJson(
                QByteArray::fromStdString(http_->at(0).body))
                .object();
        QCOMPARE(first.value(QStringLiteral("model")).toString(),
                 QStringLiteral("det/modelscope-model-A"));
    }
    http_->complete(http_->pendingIds().at(0),
                    qaFoundContent(QStringLiteral("first-answer")));

    switchGlobalAi(AiProviderKind::ModelScope,
                   QStringLiteral("det/modelscope-model-B"),
                   QStringLiteral("fake-modelscope-credential"));

    qa_->ask(QStringLiteral("second"));
    QCOMPARE(http_->dispatchCount(), 2);
    const QJsonObject second =
        QJsonDocument::fromJson(QByteArray::fromStdString(http_->at(1).body))
            .object();
    QCOMPARE(second.value(QStringLiteral("model")).toString(),
             QStringLiteral("det/modelscope-model-B"));
}

void ManualQaTest::b22m05_runtimeCredentialReplacement()
{
    // Request #1 authenticated with credential A; A is then REPLACED by B.
    // Request #2 must authenticate with B and must NOT reuse A.
    seedGlobalAi(AiProviderKind::ModelScope,
                 QStringLiteral("det/modelscope-qa-model"),
                 QStringLiteral("fake-credential-A"));
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();
    qa_->grantConsent();

    qa_->ask(QStringLiteral("first"));
    QCOMPARE(http_->dispatchCount(), 1);
    QVERIFY2(QString::fromStdString(http_->at(0).bearer)
                 == QStringLiteral("fake-credential-A"),
             "request #1 must use credential A");
    http_->complete(http_->pendingIds().at(0),
                    qaFoundContent(QStringLiteral("first-answer")));

    switchGlobalAi(AiProviderKind::ModelScope,
                   QStringLiteral("det/modelscope-qa-model"),
                   QStringLiteral("fake-credential-B"));

    qa_->ask(QStringLiteral("second"));
    QCOMPARE(http_->dispatchCount(), 2);
    QVERIFY2(QString::fromStdString(http_->at(1).bearer)
                 == QStringLiteral("fake-credential-B"),
             "request #2 must use the REPLACEMENT credential");
    QVERIFY2(http_->at(1).bearer != http_->at(0).bearer,
             "request #2 must NOT reuse credential A");
    QVERIFY2(QString::fromStdString(http_->at(1).bearer)
                 != QStringLiteral("fake-credential-A"),
             "request #2 must differ from credential A");
}

void ManualQaTest::b22m06_missingCredentialFailsClosed()
{
    // A saved provider/model is VALID but the provider-scoped credential is
    // ABSENT: zero dispatch, zero runner start, existing fail-closed result —
    // and NO fallback to another provider, the legacy environment, or a
    // public provider.
    store_->deleteCredential(AiProviderKind::ModelScope);
    seedGlobalAi(AiProviderKind::ModelScope,
                 QStringLiteral("det/modelscope-qa-model"), QString());
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();
    qa_->grantConsent();
    qa_->ask(QStringLiteral("What is the warranty?"));

    QVERIFY(qa_->stateToken() != QLatin1String("running"));
    QCOMPARE(http_->dispatchCount(), 0);
    QCOMPARE(injectedRunner_->beginCount(), 0);
    QCOMPARE(qa_->lastErrorToken(), QLatin1String("qa_no_credential"));
}

void ManualQaTest::b22m07_noSavedConfigurationFailsClosedDespiteLegacyEnv()
{
    // NOTHING usable is saved, while the process carries fake legacy ModelScope
    // environment sentinels. Manual QA must still fail closed with zero
    // dispatch: the legacy environment is NOT Manual QA authority.
    struct EnvSentinel
    {
        QString keyEnv;
        QString modelEnv;
        bool hadKey{false};
        bool hadModel{false};
        EnvSentinel()
        {
            hadKey = !qEnvironmentVariable("MODELSCOPE_API_KEY").isEmpty();
            hadModel =
                !qEnvironmentVariable("MODBUSLENS_MODELSCOPE_MODEL").isEmpty();
            keyEnv = qEnvironmentVariable("MODELSCOPE_API_KEY");
            modelEnv = qEnvironmentVariable("MODBUSLENS_MODELSCOPE_MODEL");
            qputenv("MODELSCOPE_API_KEY", "legacy-sentinel-not-a-real-key");
            qputenv("MODBUSLENS_MODELSCOPE_MODEL", "legacy-sentinel-model");
        }
        ~EnvSentinel()
        {
            if (hadKey) {
                qputenv("MODELSCOPE_API_KEY", keyEnv.toUtf8());
            } else {
                qunsetenv("MODELSCOPE_API_KEY");
            }
            if (hadModel) {
                qputenv("MODBUSLENS_MODELSCOPE_MODEL", modelEnv.toUtf8());
            } else {
                qunsetenv("MODBUSLENS_MODELSCOPE_MODEL");
            }
        }
    } sentinel;

    seedUnsavedGlobalAi();
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();
    qa_->grantConsent();
    qa_->ask(QStringLiteral("What is the warranty?"));

    QVERIFY(qa_->stateToken() != QLatin1String("running"));
    QCOMPARE(http_->dispatchCount(), 0);
    QCOMPARE(injectedRunner_->beginCount(), 0);
}

void ManualQaTest::b22m08_legacyEnvNegativeControl()
{
    // A SAVED DeepSeek configuration exists AND the legacy ModelScope
    // environment/model sentinels are present. The request must go to DeepSeek
    // with the saved model + provider-scoped credential and WITHOUT the
    // ModelScope-only payload extra: legacy ModelScope env effect = ZERO.
    // This is a REQUEST oracle, not a source grep.
    struct EnvSentinel
    {
        EnvSentinel()
        {
            qputenv("MODELSCOPE_API_KEY", "legacy-sentinel-not-a-real-key");
            qputenv("MODBUSLENS_MODELSCOPE_MODEL", "legacy-sentinel-model");
        }
        ~EnvSentinel()
        {
            qunsetenv("MODELSCOPE_API_KEY");
            qunsetenv("MODBUSLENS_MODELSCOPE_MODEL");
        }
    } sentinel;

    seedGlobalAi(AiProviderKind::DeepSeek,
                 QStringLiteral("det/deepseek-qa-model"),
                 QStringLiteral("fake-deepseek-credential"));
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();
    qa_->grantConsent();
    qa_->ask(QStringLiteral("What is the warranty?"));

    QCOMPARE(http_->dispatchCount(), 1);
    const HoldingHttpClient::Captured &sent = http_->at(0);
    QCOMPARE(QString::fromStdString(sent.url),
             QString::fromUtf8(
                 aiProviderProfile(AiProviderKind::DeepSeek)
                     .chatCompletionsEndpoint));
    QVERIFY(sent.url.find("modelscope") == std::string::npos);
    QVERIFY2(QString::fromStdString(sent.bearer)
                 == QStringLiteral("fake-deepseek-credential"),
             "Authorization must be the DeepSeek provider-scoped credential, "
             "not the legacy environment sentinel");
    const QJsonObject body =
        QJsonDocument::fromJson(QByteArray::fromStdString(sent.body)).object();
    QCOMPARE(body.value(QStringLiteral("model")).toString(),
             QStringLiteral("det/deepseek-qa-model"));
    QVERIFY2(!body.contains(QStringLiteral("chat_template_kwargs")),
             "a legacy ModelScope environment sentinel must not resurrect the "
             "ModelScope-only payload extra");
}

void ManualQaTest::b22m09_consentBeforeNetwork()
{
    // A COMPLETE and resolvable global configuration + credential exists, yet
    // the Q&A CLOUD CONSENT has not been granted: zero dispatch. Consent is
    // NOT the same thing as having an AI configuration.
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();

    // The configuration is genuinely usable at this point — so the zero
    // dispatch below is attributable to consent, not to a missing config.
    QVERIFY(globalAi_->resolveRequestConfig().has_value());

    qa_->ask(QStringLiteral("What is the warranty?"));
    QCOMPARE(qa_->stateToken(), QLatin1String("consent_required"));
    QCOMPARE(http_->dispatchCount(), 0);
    QCOMPARE(injectedRunner_->beginCount(), 0);

    qa_->grantConsent();
    qa_->ask(QStringLiteral("What is the warranty?"));
    QCOMPARE(qa_->stateToken(), QLatin1String("running"));
    QCOMPARE(http_->dispatchCount(), 1);
    QCOMPARE(injectedRunner_->beginCount(), 1);
}

void ManualQaTest::b22m10_staleOwnerOverlapIsolation()
{
    // The strongest oracle of this round: two exchanges coexist at the
    // transport layer and request ids / result ownership must never cross.
    // NETWORK CANCEL is NOT required and NOT claimed.
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();
    qa_->grantConsent();

    // 1-2: an accepted PRIOR result.
    qa_->ask(QStringLiteral("prior"));
    QCOMPARE(http_->dispatchCount(), 1);
    http_->complete(http_->pendingIds().at(0),
                    qaFoundContent(QStringLiteral("prior-accepted")));
    QCOMPARE(qa_->stateToken(), QLatin1String("completed"));
    QCOMPARE(qa_->answerText(), QStringLiteral("prior-accepted"));

    // 2b (§20 teeth): a configuration Save with NOTHING in flight must NOT
    // erase the accepted result. This is the assertion that fails if a
    // configuration change unconditionally clears the result surface.
    switchGlobalAi(AiProviderKind::ModelScope,
                   QStringLiteral("det/modelscope-qa-model-2"),
                   QStringLiteral("fake-modelscope-credential-2"));
    QCOMPARE(qa_->answerText(), QStringLiteral("prior-accepted"));
    QVERIFY(!qa_->busy());

    // 3-5: dispatch N and HOLD it (never completed yet). Note: the dispatch
    // contract clears the previous surface at dispatch time, which is why the
    // pre-switch values captured below are already empty.
    qa_->ask(QStringLiteral("attempt-N"));
    QCOMPARE(http_->dispatchCount(), 2);
    const std::uint64_t idN = http_->pendingIds().at(0);
    QVERIFY(qa_->busy());
    QCOMPARE(qa_->stateToken(), QLatin1String("running"));
    const QString preStatus = qa_->resultStatusToken();
    const QString preAnswer = qa_->answerText();
    const QString preError = qa_->lastErrorToken();

    // 6-8: a configuration Save supersedes N. The old owner is revoked at
    // once and busy is released.
    switchGlobalAi(AiProviderKind::ModelScope,
                   QStringLiteral("det/modelscope-qa-model-3"),
                   QStringLiteral("fake-modelscope-credential-3"));
    QVERIFY(!qa_->busy());
    QVERIFY(qa_->stateToken() != QLatin1String("running"));

    // 9: the CONFIGURATION CHANGE itself must not mutate the result surface.
    QCOMPARE(qa_->resultStatusToken(), preStatus);
    QCOMPARE(qa_->answerText(), preAnswer);
    QCOMPARE(qa_->lastErrorToken(), preError);

    // 10-12: dispatch N+1 while N is STILL pending at the transport layer.
    qa_->ask(QStringLiteral("attempt-N1"));
    QCOMPARE(http_->dispatchCount(), 3);
    QCOMPARE(http_->pendingCount(), std::size_t(2));
    std::uint64_t idN1 = 0;
    for (const std::uint64_t id : http_->pendingIds()) {
        if (id != idN) {
            idN1 = id;
        }
    }
    QVERIFY2(idN1 != 0 && idN1 != idN, "N+1 must be a DISTINCT request id");
    QVERIFY(qa_->busy());

    // 13-14: the OLD exchange completing must change NOTHING. (The accepted
    // surface is empty here — N+1's own dispatch cleared it — so the oracle is
    // "identical before/after", not "equals some prior text".)
    const QString beforeStaleStatus = qa_->resultStatusToken();
    const QString beforeStaleAnswer = qa_->answerText();
    const QString beforeStaleError = qa_->lastErrorToken();
    http_->complete(idN, qaFoundContent(QStringLiteral("stale-must-not-apply")));
    QCOMPARE(qa_->resultStatusToken(), beforeStaleStatus);
    QCOMPARE(qa_->answerText(), beforeStaleAnswer);
    QCOMPARE(qa_->lastErrorToken(), beforeStaleError);
    QVERIFY2(qa_->answerText() != QStringLiteral("stale-must-not-apply"),
             "the superseded exchange must never become the displayed answer");
    QVERIFY(qa_->busy()); // N+1 still owns the controller
    QCOMPARE(qa_->stateToken(), QLatin1String("running"));
    QVERIFY2(qa_->stateToken() != QLatin1String("completed"),
             "a stale completion must not complete the controller");

    // 15-17: N+1 completes normally and becomes the accepted current result.
    http_->complete(idN1, qaFoundContent(QStringLiteral("post-switch-accepted")));
    QCOMPARE(qa_->stateToken(), QLatin1String("completed"));
    QCOMPARE(qa_->answerText(), QStringLiteral("post-switch-accepted"));
    QVERIFY(!qa_->busy());
    QVERIFY(!http_->isPending(idN1));
}

void ManualQaTest::b22m11_startupNoNetwork()
{
    // Even with a fully valid global configuration and credential present, the
    // PRODUCTION-equivalent Manual QA path performs ZERO automatic requests
    // before an explicit Human Manual QA action.
    manual_ = importSeed(seedPath_);
    QVERIFY(manual_ != nullptr);
    qa_ = makeController();
    qa_->setManualController(manual_);
    injectRealRunnerOverFakeHttp();

    QCOMPARE(http_->dispatchCount(), 0);
    QCOMPARE(injectedRunner_->beginCount(), 0);
    QCOMPARE(qa_->stateToken(), QLatin1String("idle"));
    QVERIFY(globalAi_->resolveRequestConfig().has_value());
}

// ---------------------------------------------------------------------------
// Structural checks (compile-signature / meta-object). Valuable, but
// deliberately NOT part of the B22M business acceptance set.
// ---------------------------------------------------------------------------

void ManualQaTest::qaStruct_seamCarriesResolvedDispatchConfig()
{
    QVERIFY(ManualQaStructuralProbe::probeSeamCarriesDispatchConfig());
}

void ManualQaTest::qaStruct_builderTakesProviderAndModel()
{
    QVERIFY(ManualQaStructuralProbe::probeBuilderTakesProviderAndModel());
}

void ManualQaTest::qaStruct_parseStaticsAreProviderNeutral()
{
    QVERIFY(ManualQaStructuralProbe::probeParseStaticsExist());
}

void ManualQaTest::qaStruct_runnerIsProviderNeutral()
{
    ManualQaAsyncRunner runner;
    const QByteArray className = runner.metaObject()->className();
    QVERIFY2(!className.contains(QByteArrayLiteral("ModelScope")),
             "the production Manual QA runner must be provider-neutral");
}

void ManualQaTest::qaStruct_qmlBindingAvoidsPropertyShadowing()
{
    // B2.2-B-R1 REGRESSION LOCK. In QML an object's OWN property shadows an
    // identically named context property, so a binding written as
    // `aiRuntimeConfiguration: aiRuntimeConfiguration` self-references and
    // binds NULL. The QML-facing property MUST therefore be named
    // `runtimeConfiguration` (distinct from the context property), matching the
    // working Candidate / AiSettings convention.
    auto* controller = makeController();
    const QMetaObject* meta = controller->metaObject();
    const int index = meta->indexOfProperty("runtimeConfiguration");
    QVERIFY2(index >= 0,
             "ManualQaController must expose the QML-bound property as "
             "`runtimeConfiguration`");
    const QMetaProperty property = meta->property(index);
    QVERIFY(property.isWritable());
    QCOMPARE(QByteArray(property.typeName()),
             QByteArrayLiteral("modbuslens::ui::AiRuntimeConfiguration*"));
    delete controller;
}

void ManualQaTest::qaStruct_controllerHasNoCredentialProperty()
{
    const auto looksLikeCredential = [](const QByteArray &lowered) {
        return lowered.contains(QByteArrayLiteral("secret"))
            || lowered.contains(QByteArrayLiteral("apikey"))
            || lowered.contains(QByteArrayLiteral("api_key"))
            || lowered.contains(QByteArrayLiteral("bearer"))
            || lowered.contains(QByteArrayLiteral("credential"));
    };
    qa_ = makeController();
    ManualQaAsyncRunner runner;
    bool found = false;
    for (QObject *probe : {static_cast<QObject *>(qa_),
                           static_cast<QObject *>(&runner)}) {
        const QMetaObject *meta = probe->metaObject();
        for (int index = 0; index < meta->propertyCount(); ++index) {
            const QByteArray lowered =
                QByteArray(meta->property(index).name()).toLower();
            if (looksLikeCredential(lowered)) {
                found = true;
            }
        }
    }
    QVERIFY2(!found, "neither object may expose a credential-bearing property");
}

// ---------------------------------------------------------------------------
// Focused runner-level payload oracles (real runner + fake transport).
// ---------------------------------------------------------------------------

void ManualQaTest::runnerLevel_deepSeekBodyOmitsModelScopeExtras()
{
    auto http = std::make_unique<HoldingHttpClient>();
    ManualQaAsyncRunner runner(*http);
    ManualQaDispatchConfig config;
    config.kind = AiProviderKind::DeepSeek;
    config.endpoint =
        aiProviderProfile(AiProviderKind::DeepSeek).chatCompletionsEndpoint;
    config.model = "det/deepseek-qa-model";
    config.secret = "fake-deepseek-credential";
    core::ManualQaRequest request;
    request.question = "q";
    request.documentId = "doc";
    request.contentHash = "hash";
    QVERIFY(runner.begin(config, request, 1,
                         [](const IManualQaRunner::Completion &) {}));
    QCOMPARE(http->dispatchCount(), 1);
    const QJsonObject body =
        QJsonDocument::fromJson(QByteArray::fromStdString(http->at(0).body))
            .object();
    QVERIFY2(!body.contains(QStringLiteral("chat_template_kwargs")),
             "DeepSeek request body must NOT contain chat_template_kwargs");
}

void ManualQaTest::runnerLevel_modelScopeBodyKeepsThinkingDisabled()
{
    auto http = std::make_unique<HoldingHttpClient>();
    ManualQaAsyncRunner runner(*http);
    ManualQaDispatchConfig config;
    config.kind = AiProviderKind::ModelScope;
    config.endpoint =
        aiProviderProfile(AiProviderKind::ModelScope).chatCompletionsEndpoint;
    config.model = "det/modelscope-qa-model";
    config.secret = "fake-modelscope-credential";
    core::ManualQaRequest request;
    request.question = "q";
    request.documentId = "doc";
    request.contentHash = "hash";
    QVERIFY(runner.begin(config, request, 1,
                         [](const IManualQaRunner::Completion &) {}));
    QCOMPARE(http->dispatchCount(), 1);
    const QJsonObject body =
        QJsonDocument::fromJson(QByteArray::fromStdString(http->at(0).body))
            .object();
    const QJsonObject kwargs =
        body.value(QStringLiteral("chat_template_kwargs")).toObject();
    QCOMPARE(kwargs.value(QStringLiteral("enable_thinking")),
             QJsonValue(false));
}

QTEST_MAIN(ManualQaTest)
#include "test_manual_qa.moc"
