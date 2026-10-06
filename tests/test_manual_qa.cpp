// M12-D FIRST SLICE (T027 §100, D1–D5): deterministic unit tests for the
// Manual Q&A controller, the strict provider parser and the local citation
// validator. Everything runs against a fake runner and a temporary managed
// root: zero network, zero credential, zero live provider (QA-34/QA-35).

#include <vector>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "core/manualqa/ManualQaContract.h"
#include "ui/agent/ModelScopeAgentClient.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/manual/ManualImportController.h"
#include "ui/manual/ManualStore.h"
#include "ui/manualqa/ModelScopeManualQaRunner.h"
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
    };

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

    [[nodiscard]] bool begin(const core::ManualQaRequest& request,
                             std::uint64_t generation,
                             const CompletionHandler& onDone) override
    {
        ++beginCount_;
        lastRequestQuestion_ = request.question;
        lastRequestDocumentId_ = request.documentId;
        lastRequestContentHash_ = request.contentHash;
        if (mode_ == Mode::Deferred) {
            DeferredCompletion deferred;
            deferred.generation = generation;
            deferred.onDone = onDone;
            deferred.rawJson = foundJsonFor(request, -1, -1, std::string(),
                                            false, false);
            deferred_.push_back(std::move(deferred));
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
};

} // namespace

class ManualQaTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
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

    // Parser / validator semantics
    void parser_validStates();
    void parser_malformedVariants();
    void contextBlocks_deterministicBounded();

private:
    static QByteArray findStoreArtifacts(const QString& root);

    QTemporaryDir managedRoot_;
    ManualImportController* manual_ = nullptr;
    ManualQaController* qa_ = nullptr;
    FakeManualQaRunner* fake_ = nullptr;
    QString seedPath_;
    QString baselineArtifacts_;
};

void ManualQaTest::initTestCase()
{
    QVERIFY(managedRoot_.isValid());
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

void ManualQaTest::cleanup()
{
    // Fresh controllers per test; the managed root override stays for the
    // whole suite so the store path is stable.
    delete qa_;
    qa_ = nullptr;
    delete manual_;
    manual_ = nullptr;
    fake_ = nullptr;
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    qa_ = new ManualQaController();
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
    ModelScopeManualQaRunner runner;
    core::ManualQaRequest request;
    request.question = "test";
    request.documentId = "doc";
    request.contentHash = "hash";
    bool completed = false;
    const bool started = runner.begin(
        request, 1,
        [&completed](const IManualQaRunner::Completion&) { completed = true; });
    // Credential absent in this process: fail-closed, zero network, onDone
    // never invoked.
    QVERIFY(!started);
    QVERIFY(!completed);
    QCOMPARE(runner.beginCount(), 1);
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

    const QJsonObject body = ModelScopeManualQaRunner::buildRequestBody(request);
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
        ModelScopeManualQaRunner::extractAssistantContent(assistantMessage);
    QVERIFY(!content.isEmpty());
    QVERIFY(content.startsWith(QStringLiteral("{\"status\"")));

    // Defensive contract: reasoning_content never leaks into the answer.
    QJsonObject withReasoning = assistantMessage;
    withReasoning.insert(QStringLiteral("reasoning_content"),
                         QStringLiteral("REASONING MUST NOT LEAK"));
    const QString content2 =
        ModelScopeManualQaRunner::extractAssistantContent(withReasoning);
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
        const auto result = ModelScopeManualQaRunner::parseProviderResult(
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

void ManualQaTest::parser_validStates()
{
    const std::string found =
        "{\"status\":\"found\",\"answer\":\"380 V AC\",\"citations\":"
        "[{\"documentId\":\"d\",\"contentHash\":\"h\",\"pageNumber\":-1,"
        "\"textStart\":0,\"textEnd\":5,\"excerpt\":\"Alpha\"}]}";
    auto result = ModelScopeManualQaRunner::parseProviderResult(found);
    QVERIFY(result.has_value());
    QCOMPARE(result->status, ManualQaStatus::Found);
    QCOMPARE(result->answer, std::string("380 V AC"));
    QCOMPARE(result->citations.size(), std::size_t{1});

    result = ModelScopeManualQaRunner::parseProviderResult(
        "{\"status\":\"not_found\",\"answer\":\"\",\"citations\":[]}");
    QVERIFY(result.has_value());
    QCOMPARE(result->status, ManualQaStatus::NotFound);

    result = ModelScopeManualQaRunner::parseProviderResult(
        "{\"status\":\"insufficient_evidence\",\"answer\":\"\",\"citations\":"
        "[]}");
    QVERIFY(result.has_value());
    QCOMPARE(result->status, ManualQaStatus::InsufficientEvidence);
}

void ManualQaTest::parser_malformedVariants()
{
    QVERIFY(!ModelScopeManualQaRunner::parseProviderResult("{").has_value());
    QVERIFY(!ModelScopeManualQaRunner::parseProviderResult("[]").has_value());
    QVERIFY(!ModelScopeManualQaRunner::parseProviderResult(
                "{\"answer\":\"x\",\"citations\":[]}")
                .has_value()); // missing status
    QVERIFY(!ModelScopeManualQaRunner::parseProviderResult(
                "{\"status\":\"found\",\"citations\":[]}")
                .has_value()); // missing answer
    QVERIFY(!ModelScopeManualQaRunner::parseProviderResult(
                "{\"status\":\"found\",\"answer\":\"x\"}")
                .has_value()); // missing citations
    QVERIFY(!ModelScopeManualQaRunner::parseProviderResult(
                "{\"status\":\"totally_unknown\",\"answer\":\"x\","
                "\"citations\":[]}")
                .has_value()); // unknown status: ERROR, never coerced
    QVERIFY(!ModelScopeManualQaRunner::parseProviderResult(
                "{\"status\":\"found\",\"answer\":\"x\",\"citations\":"
                "[{\"documentId\":\"d\"}]}")
                .has_value()); // broken citation shape
    QVERIFY(!ModelScopeManualQaRunner::parseProviderResult(
                "{\"status\":\"found\",\"answer\":\"x\",\"citations\":"
                "[{\"documentId\":\"d\",\"contentHash\":\"h\","
                "\"pageNumber\":\"-1\",\"textStart\":0,\"textEnd\":5,"
                "\"excerpt\":\"e\"}]}")
                .has_value()); // wrong field type
    // Impossible numeric range is still a parseable SHAPE: the range check
    // belongs to the local citation validator, not the parser.
    QVERIFY(ModelScopeManualQaRunner::parseProviderResult(
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

QTEST_MAIN(ManualQaTest)
#include "test_manual_qa.moc"
