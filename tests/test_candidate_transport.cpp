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
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>

#include <string>
#include <vector>

#include "core/candidate/CandidateExtraction.h"
#include "core/candidate/ProviderExtractionContract.h"
#include "ui/ai/ModelScopeCandidateAdapter.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/candidate/ModelScopeCandidateRunner.h"
#include "ui/candidate/ModelScopeExtractionTransport.h"
#include "ui/candidate/ModelScopeHttpClient.h"
#include "ui/profile/ProfileStore.h"

using modbuslens::core::CandidateProposal;
using modbuslens::core::ManualDocument;
using modbuslens::core::ProfileFieldTarget;
using modbuslens::ui::CandidateExtractionController;
using modbuslens::ui::IModelScopeHttpClient;
using modbuslens::ui::ModelScopeCandidateRunner;
using modbuslens::ui::ModelScopeExtractionTransport;
using modbuslens::ui::ModelScopeHttpRequest;
using modbuslens::ui::ModelScopeHttpResult;
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
class FakeHttpClient : public IModelScopeHttpClient
{
public:
    std::vector<ModelScopeHttpRequest> requests;
    int status{200};
    std::string body;
    bool transportOk{true};

    ModelScopeHttpResult post(const ModelScopeHttpRequest &request) override
    {
        requests.push_back(request);
        ModelScopeHttpResult result;
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
        ModelScopeCandidateRunner runner(http);

        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            1, nullptr);

        QCOMPARE(started, false);
        QCOMPARE(http.callCount(), 0);
        QCOMPARE(runner.dispatchCount(), 0);
        QCOMPARE(runner.beginCount(), 0);
    }

    // --------------------------------------------------------------- P02 --
    void p02_modelConfigurationSemantics()
    {
        // The repository already carries an accepted canonical default model
        // (M6 kCandidateModel = Qwen/Qwen3.5-27B), so a model id can never be
        // "missing": an empty override falls back to that default. P02's
        // "missing model" branch is therefore NOT APPLICABLE; what IS tested is
        // that the override is honoured and that the default is not silently
        // replaced by an example model.
        QCOMPARE(QString::fromStdString(ModelScopeCandidateRunner::configuredModelId()),
                 QStringLiteral("Qwen/Qwen3.5-27B"));

        qputenv("MODBUSLENS_MODELSCOPE_MODEL", "Vendor/Configured-Model");
        QCOMPARE(QString::fromStdString(ModelScopeCandidateRunner::configuredModelId()),
                 QStringLiteral("Vendor/Configured-Model"));

        qputenv("MODBUSLENS_MODELSCOPE_MODEL", "   ");
        QCOMPARE(QString::fromStdString(ModelScopeCandidateRunner::configuredModelId()),
                 QStringLiteral("Qwen/Qwen3.5-27B"));
    }

    // --------------------------------------------------------------- P03 --
    void p03_requestTargetsOfficialHttpsEndpoint()
    {
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);

        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            7, nullptr);
        QVERIFY(started);

        QVERIFY(http.callCount() == 1);
        const std::string url = http.requests.front().url;
        QCOMPARE(url, ModelScopeCandidateRunner::officialEndpoint());
        QVERIFY(url.rfind("https://api-inference.modelscope.cn/v1/chat/completions", 0)
                == 0);
        // The seam's only operation is a POST and the production client issues
        // QNetworkAccessManager::post — there is no way to reach the endpoint
        // through this application with any other method.
        QCOMPARE(ModelScopeCandidateRunner::productionTimeoutMs(), 90000);
    }

    // --------------------------------------------------------------- P04 --
    void p04_tokenOnlyInAuthorizationHeader()
    {
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);

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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);

        const bool started = runner.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(started);

        QCOMPARE(http.requests.front().contentType, std::string{"application/json"});
        QCOMPARE(std::string{ModelScopeExtractionTransport::kJsonContentType},
                 std::string{"application/json"});
    }

    // --------------------------------------------------------------- P06 --
    void p06_payloadIsMinimized()
    {
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);

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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        ModelScopeCandidateRunner runner(http);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        ModelScopeCandidateRunner runner(http);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        for (const int status : {401, 403, 429, 500, 503}) {
            FakeHttpClient http;
            ModelScopeCandidateRunner runner(http);
            CandidateExtractionController controller(runner);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        ModelScopeCandidateRunner runner(http);
        CandidateExtractionController controller(runner);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        ProfileStore::setManagedRootOverride(root.path());
        const QStringList before = snapshotTree(root.path());

        FakeHttpClient http;
        http.transportOk = false;
        ModelScopeCandidateRunner runner(http);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);

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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        ModelScopeCandidateRunner runner(http);
        CandidateExtractionController controller(runner);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
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
            ModelScopeCandidateRunner runner(http);
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
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);
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

    // --------------------------------------------------------------- P24 --
    void p24_productionRunnerReachesTheTransportLayer()
    {
        qputenv("MODELSCOPE_API_KEY", kFakeToken);
        FakeHttpClient http;
        http.body = envelope(extractionContent(kManufacturerValue, kManufacturerExcerpt));
        ModelScopeCandidateRunner runner(http);
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
        ModelScopeCandidateRunner runner2(http2);
        const bool startedWithoutCredential = runner2.begin(
            modbuslens::core::buildC2FirstSliceExtractionRequest(
                makeDocument(), std::string_view{kManualText}),
            1, nullptr);
        QCOMPARE(startedWithoutCredential, false);
        QCOMPARE(http2.callCount(), 0);
    }
};

QTEST_MAIN(CandidateTransportTest)
#include "test_candidate_transport.moc"
