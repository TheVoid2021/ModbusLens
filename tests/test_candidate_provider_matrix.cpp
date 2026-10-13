// T034 B1 — provider selection / secret isolation / request-body matrix for the
// Candidate chain (tests written FIRST, against the CURRENT ModelScope-only
// candidate API: the provider is selected purely through process-environment
// inputs, so every case compiles and runs pre-implementation and the provider
// cases fail BEHAVIORALLY — REAL RED, never a compile failure).
//
// Determinism: zero network — the HTTP seam is the injected capture fake.
#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <string>
#include <vector>

#include "core/candidate/ProviderExtractionContract.h"
#include "ui/candidate/CandidateExtractionController.h"
#include "ui/candidate/CandidateExtractionRunner.h"
#include "ui/candidate/CandidateProviderRunner.h"

using modbuslens::core::buildC2FirstSliceExtractionRequest;
using modbuslens::ui::AiProviderKind;

namespace {

constexpr char kModelScopeKey[] = "fake-modelscope-key";
constexpr char kDeepSeekKey[] = "fake-deepseek-key";
constexpr char kDeepSeekModel[] = "deepseek-chat-test-model";
constexpr char kManualText[] =
    "ACME Power Systems Ltd. - Inverter Manual\n"
    "Manufacturer: ACME Power Systems Ltd.\n";
constexpr char kManufacturerValue[] = "ACME Power Systems Ltd.";
constexpr char kManufacturerExcerpt[] = "Manufacturer: ACME Power Systems Ltd.";
constexpr char kDocumentId[] = "aaaaaaaa-1111-2222-3333-444444444444";
constexpr char kContentHash[] =
    "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

// The frozen provider response envelope (same contract the transport tests use).
[[nodiscard]] std::string extractionContent(const char *value, const char *excerpt)
{
    std::string content = "{\"proposals\":[{\"target\":\"manufacturer\",";
    content += "\"value\":\"" + std::string(value) + "\",";
    content += "\"evidence\":\"" + std::string(excerpt) + "\"}]}";
    return content;
}

[[nodiscard]] std::string envelope(const std::string &content)
{
    // The inner extraction JSON travels as the JSON string literal of
    // message.content; QJsonDocument owns the escaping (authoritative).
    const QJsonObject message{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), QString::fromStdString(content)}};
    const QJsonObject choice{{QStringLiteral("message"), message}};
    const QJsonObject root{{QStringLiteral("choices"), QJsonArray{choice}},
                           {QStringLiteral("object"),
                            QStringLiteral("chat.completion")}};
    return std::string(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

[[nodiscard]] modbuslens::core::ManualDocument makeDocument()
{
    modbuslens::core::ManualDocument document;
    document.documentId = kDocumentId;
    document.originalFileName = "inverter-manual.txt";
    document.documentType = modbuslens::core::ManualDocumentType::Txt;
    document.originalPath = "Z:/provenance-only/never-read.txt";
    document.contentHash = kContentHash;
    document.status = modbuslens::core::ManualDocumentStatus::Ready;
    return document;
}

// Capture-only HTTP seam (same shape the frozen transport tests use).
class FakeHttpClient : public modbuslens::ui::IChatCompletionsHttpClient
{
public:
    struct Call {
        std::string url;
        std::string bearerToken;
        std::string contentType;
        std::string body;
    };

    modbuslens::ui::ChatCompletionsHttpResult post(
        const modbuslens::ui::ChatCompletionsHttpRequest &request) override
    {
        ++callCount;
        requests.push_back(Call{request.url, request.bearerToken,
                                request.contentType, request.body});
        modbuslens::ui::ChatCompletionsHttpResult result;
        result.status = 200;
        result.body = body;
        result.transportOk = true;
        return result;
    }

    int callCount{0};
    std::string body;
    std::vector<Call> requests;
};

} // namespace

class CandidateProviderMatrixTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Deterministic environment: clear every provider-related variable the
        // matrix controls, then each test sets exactly what it declares.
        for (const char *name :
             {"MODELSCOPE_API_KEY", "DEEPSEEK_API_KEY",
              "MODBUSLENS_AI_PROVIDER", "MODBUSLENS_AI_MODEL",
              "MODBUSLENS_MODELSCOPE_MODEL"}) {
            qunsetenv(name);
        }
    }

    // =========== A. existing ModelScope behavior unchanged (control) ==========
    void a_modelScopeExplicitUnchanged()
    {
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kModelScopeKey});

        const bool started = runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(started);
        QCOMPARE(http.callCount, 1);
        QCOMPARE(http.requests.front().bearerToken, std::string{kModelScopeKey});
        QVERIFY(http.requests.front().url.find("api-inference.modelscope.cn")
                != std::string::npos);
        // The strict parser still yields the same pending candidate.
        QVERIFY(runner.beginCount() == 1);
    }

    // =========== B. DeepSeek selected → reaches fake server correctly ==========
    void b_deepSeekReachesFakeServer()
    {
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});

        const bool started = runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(started); // PRE-FIX RED: provider env ignored, key missing
        QCOMPARE(http.callCount, 1);
        QCOMPARE(http.requests.front().bearerToken, std::string{kDeepSeekKey});
        QVERIFY(http.requests.front().url.find("api.deepseek.com")
                != std::string::npos);
        const QJsonObject body = QJsonDocument::fromJson(
            QByteArray::fromStdString(http.requests.front().body)).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(),
                 QString::fromUtf8(kDeepSeekModel));
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), false);
        QCOMPARE(body.value(QStringLiteral("thinking"))
                     .toObject()
                     .value(QStringLiteral("type"))
                     .toString(),
                 QStringLiteral("disabled"));
        // Provider extras must NOT leak across profiles.
        QVERIFY(!body.contains(QStringLiteral("chat_template_kwargs")));
    }

    // =========== C. DeepSeek must not send ModelScope thinking kwarg ==========
    // (dispatch-dependent assertion; shares the B path — separate slot so the
    // failure names the exact rule)
    void c_deepSeekBodyHasNoChatTemplateKwargs()
    {
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});

        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr));
        const QJsonObject body = QJsonDocument::fromJson(
            QByteArray::fromStdString(http.requests.front().body)).object();
        QVERIFY(!body.contains(QStringLiteral("chat_template_kwargs")));
        QVERIFY(!QString::fromStdString(http.requests.front().body)
                     .contains(QStringLiteral("enable_thinking")));
    }

    // =========== D. ModelScope must NOT read the DeepSeek key =================
    void d_modelScopeIgnoresDeepSeekKey()
    {
        qputenv("DEEPSEEK_API_KEY", kDeepSeekKey);
        // provider unset → default = modelscope; MODELSCOPE_API_KEY absent.
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);

        const bool started = runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(!started);
        QCOMPARE(http.callCount, 0); // fail-closed, zero dispatch
    }

    // =========== E. DeepSeek must NOT read the ModelScope key =================
    void e_deepSeekIgnoresModelScopeKey()
    {
        qputenv("MODELSCOPE_API_KEY", kModelScopeKey);
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});

        // T034-B2-LC2-T1: the DeepSeek run is authorized ONLY by its own
        // provider-scoped credential — the ambient ModelScope key must never
        // surface on the wire.
        const bool started = runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(started);
        QCOMPARE(http.callCount, 1);
        QVERIFY(http.requests.front().bearerToken != std::string{kModelScopeKey});
        QCOMPARE(http.requests.front().bearerToken, std::string{kDeepSeekKey});
    }

    // =========== F. missing provider-specific key → not_configured ============
    void f_deepSeekWithoutKeyFailsClosed()
    {
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string());

        const bool started = runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(!started);
        QCOMPARE(http.callCount, 0);
    }

    // =========== G. unknown provider token → fail closed ======================
    void g_unknownProviderFailsClosed()
    {
        qputenv("MODBUSLENS_AI_PROVIDER", "not-a-provider");
        qputenv("MODELSCOPE_API_KEY", kModelScopeKey);
        qputenv("DEEPSEEK_API_KEY", kDeepSeekKey);
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);

        const bool started = runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr);
        QVERIFY(!started); // PRE-FIX RED: unknown token is (wrongly) ignored
        QCOMPARE(http.callCount, 0);
    }

    // =========== H. JSON escaping regression ==================================
    void h_jsonEscapingRoundTrip()
    {
        const char *modelWithNasties = "mo\\del \"quoted\" \n x 中文 ✓";
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{modelWithNasties},
                                   std::string{kDeepSeekKey});

        // The canonical text itself carries quotes / backslashes / newlines /
        // unicode — the wire body must survive every one of them.
        const std::string nastyText =
            "Line1 \"quoted\" \\\\ backslash \n\t tab 中文 ✓ end";
        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{ nastyText }),
            1, nullptr));
        QCOMPARE(http.callCount, 1);
        const QJsonDocument parsed = QJsonDocument::fromJson(
            QByteArray::fromStdString(http.requests.front().body));
        QVERIFY(parsed.isObject()); // valid JSON despite every nasty character
        const QJsonObject body = parsed.object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(),
                 QString::fromUtf8(modelWithNasties));
        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        const QString userContent =
            messages.at(1).toObject().value(QStringLiteral("content")).toString();
        QVERIFY(userContent.contains(QStringLiteral("中文 ✓")));
        QVERIFY(userContent.contains(QStringLiteral("Line1 \"quoted\"")));
        QVERIFY(userContent.contains(QStringLiteral("\\\\ backslash")));
        QVERIFY(userContent.contains(QStringLiteral("Line1")));
    }

    // =========================================================================
    // T034-B1 JSON OUTPUT ROUND (Human fix decision: DeepSeek native
    // response_format=json_object + JSON-only prompt instruction; strict
    // parser untouched).
    // =========================================================================

    // DS-J01 (Human RED): the DeepSeek request MUST carry
    // response_format.type == "json_object".
    void ds_j01_deepSeekRequiresResponseFormat()
    {
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});

        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr));
        const QJsonObject body = QJsonDocument::fromJson(
            QByteArray::fromStdString(http.requests.front().body)).object();
        QCOMPARE(body.value(QStringLiteral("response_format"))
                     .toObject()
                     .value(QStringLiteral("type"))
                     .toString(),
                 QStringLiteral("json_object"));
    }

    // DS-J02 (Human RED): the DeepSeek user prompt must contain the explicit
    // JSON-only contract (semantic assertions, NO golden prompt string):
    // a JSON requirement, a no-markdown-fence/no-prose intent, and the real
    // strict schema example fields.
    void ds_j02_deepSeekPromptRequiresJsonOnly()
    {
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});

        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr));
        const QJsonArray messages = QJsonDocument::fromJson(
            QByteArray::fromStdString(http.requests.front().body))
                                           .object()
                                           .value(QStringLiteral("messages"))
                                           .toArray();
        QVERIFY(messages.size() == 2);
        const QString systemContent =
            messages.at(0).toObject().value(QStringLiteral("content")).toString();
        const QString userContent =
            messages.at(1).toObject().value(QStringLiteral("content")).toString();
        const QString prompt = systemContent + QStringLiteral("\n") + userContent;
        // JSON requirement exists (semantic: the prompt must demand JSON).
        QVERIFY(prompt.contains(QStringLiteral("JSON")));
        // No-markdown-fence / no-prose intent exists.
        QVERIFY(prompt.contains(QStringLiteral("Markdown"),
                                Qt::CaseInsensitive)
                || prompt.contains(QStringLiteral("code fence"),
                                   Qt::CaseInsensitive));
        QVERIFY(prompt.contains(QStringLiteral("prose"), Qt::CaseInsensitive)
                || prompt.contains(QStringLiteral("only"), Qt::CaseInsensitive));
        // The REAL strict schema example fields exist (target/value/evidence).
        QVERIFY(prompt.contains(QStringLiteral("proposals")));
        QVERIFY(prompt.contains(QStringLiteral("target")));
        QVERIFY(prompt.contains(QStringLiteral("value")));
        QVERIFY(prompt.contains(QStringLiteral("evidence")));
    }

    // DS-J03 (control): the ModelScope request must NOT gain
    // response_format=json_object from this fix.
    void ds_j03_modelScopeHasNoResponseFormat()
    {
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kModelScopeKey});

        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr));
        const QJsonObject body = QJsonDocument::fromJson(
            QByteArray::fromStdString(http.requests.front().body)).object();
        QVERIFY(!body.contains(QStringLiteral("response_format")));
    }

    // DS-J04 (GREEN): DeepSeek response_format and thinking.disabled coexist.
    void ds_j04_responseFormatAndThinkingCoexist()
    {
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});

        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr));
        const QJsonObject body = QJsonDocument::fromJson(
            QByteArray::fromStdString(http.requests.front().body)).object();
        QCOMPARE(body.value(QStringLiteral("response_format"))
                     .toObject()
                     .value(QStringLiteral("type"))
                     .toString(),
                 QStringLiteral("json_object"));
        QCOMPARE(body.value(QStringLiteral("thinking"))
                     .toObject()
                     .value(QStringLiteral("type"))
                     .toString(),
                 QStringLiteral("disabled"));
    }

    // LIFE-01 (T034-B2.1 SECURITY CORRECTION SLICE B2): the runner must NOT
    // retain the plaintext credential as long-lived member state. After one
    // authenticated request, a second begin() WITHOUT a fresh applyExplicit
    // credential must fail closed before authenticated dispatch — it must
    // never reuse request A's secret.
    void life01_runnerDoesNotReuseCredentialAcrossRequests()
    {
        // No provider credential env inputs at all.
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);

        runner.applyExplicitConfig(AiProviderKind::DeepSeek,
                                   std::string("fake-model"),
                                   std::string(kDeepSeekKey));
        const modbuslens::core::ExtractionRequest request =
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText});

        // Request A: authenticated dispatch with the explicit secret.
        QVERIFY(runner.begin(request, 1, nullptr));
        QCOMPARE(http.callCount, 1);
        QCOMPARE(http.requests.front().bearerToken, std::string(kDeepSeekKey));

        // Request B: NO new credential supplied. The runner consumed the
        // one-shot secret in request A and must fail closed before any
        // authenticated dispatch. Pre-fix RED: explicitSecret_ persists and
        // request B dispatches with request A's credential.
        const bool startedB = runner.begin(request, 2, nullptr);
        QVERIFY(!startedB);
        QCOMPARE(http.callCount, 1); // zero additional dispatches
        QCOMPARE(http.requests.size(), std::size_t(1));
    }

    // DS-J05 (fail-closed chain preserved, G): DeepSeek + non-JSON content =>
    // strict parser still FAILS CLOSED (malformed_response), zero candidates.
    void ds_j05_deepSeekInvalidContentFailsClosed()
    {
        FakeHttpClient http;
        // Markdown-fenced / prosed output: valid HTTP 200, unusable content.
        http.body = envelope("```json\n{\"proposals\":[]}\n```");
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});
        modbuslens::ui::CandidateExtractionController controller(runner);
        const modbuslens::core::ManualDocument document = makeDocument();

        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
        QCOMPARE(controller.failureToken(), QStringLiteral("malformed_response"));
        QCOMPARE(controller.candidateCount(), 0);
    }

    // DS-J06 (fail-closed chain preserved, H): DeepSeek + schema-invalid JSON
    // => schema_violation (unknown field stays a refusal).
    void ds_j06_deepSeekSchemaViolationToken()
    {
        FakeHttpClient http;
        // Schema violation: an invented confidence field (H5) inside the item.
        std::string content =
            "{\"proposals\":[{\"target\":\"manufacturer\","
            "\"value\":\"ACME\",\"evidence\":\"Manufacturer: ACME\","
            "\"confidence\":0.9}]}";
        http.body = envelope(content);
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});
        modbuslens::ui::CandidateExtractionController controller(runner);
        const modbuslens::core::ManualDocument document = makeDocument();

        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
        QCOMPARE(controller.failureToken(), QStringLiteral("schema_violation"));
        QCOMPARE(controller.candidateCount(), 0);
    }

    // DS-J07 (fail-closed chain preserved, I): DeepSeek + schema-valid JSON
    // with FALSE evidence => no Candidate (local validation is the authority).
    void ds_j07_deepSeekFalseEvidenceNoCandidate()
    {
        FakeHttpClient http;
        http.body = envelope(extractionContent(
            kManufacturerValue, "Manufacturer: FABRICATED NOT IN TEXT"));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});
        modbuslens::ui::CandidateExtractionController controller(runner);
        const modbuslens::core::ManualDocument document = makeDocument();

        controller.requestExtractionFor(document, std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(controller.stateToken(), QStringLiteral("failed"));
        QCOMPARE(controller.failureToken(), QStringLiteral("no_candidate"));
        QCOMPARE(controller.candidateCount(), 0);
    }

    // FN-01 (forensic convergence, default-OFF control): with
    // MODBUSLENS_PROVIDER_FORENSICS unset, a real dispatch produces ZERO
    // forensic file output.
    void fn01_forensicsDefaultOffProducesNothing()
    {
        qunsetenv("MODBUSLENS_PROVIDER_FORENSICS");
        const QString logPath =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/modbuslens_provider_forensics.log");
        QFile::remove(logPath);
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kModelScopeKey});

        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr));
        QCOMPARE(http.callCount, 1);
        QVERIFY(!QFile::exists(logPath)); // default-off: zero forensic output
    }

    // FN-02 (forensic convergence, gated-ON control): with the variable set,
    // the gated path DOES write the sanitized record.
    void fn02_forensicsGatedOnWrites()
    {
        qputenv("MODBUSLENS_PROVIDER_FORENSICS", "1");
        const QString logPath =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/modbuslens_provider_forensics.log");
        QFile::remove(logPath);
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kModelScopeKey});

        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr));
        QCOMPARE(http.callCount, 1);
        QVERIFY(QFile::exists(logPath));
        QFile::remove(logPath); // keep the host clean for later runs
    }

    // ---------------------------------------------------------------------
    // CAND-R2 (T034-B2-HV2-D-CAND-R2): FORENSICS CONTENT CONTRACT.
    // fn02 only asserted "a file was written"; the CONTENT that makes case A
    // (provider returned ZERO proposals) decidable from case B (proposals
    // returned, ALL rejected by local evidence) was NOT covered. These tests
    // assert the content contract on synthetic data only.
    // ---------------------------------------------------------------------
    void fn03_forensicsDistinguishesEmptyFromAllRejected()
    {
        qputenv("MODBUSLENS_PROVIDER_FORENSICS", "1");
        const QString logPath =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/modbuslens_provider_forensics.log");

        const auto runExtraction = [&](const std::string &body) -> QString {
            QFile::remove(logPath); // isolate THIS run's record
            FakeHttpClient http;
            http.body = body;
            modbuslens::ui::CandidateProviderRunner runner(http);
            runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                       std::string{kDeepSeekModel},
                                       std::string{kDeepSeekKey});
            modbuslens::ui::CandidateExtractionController controller(runner);
            controller.requestExtractionFor(makeDocument(),
                                            std::string{kManualText});
            controller.grantConsent();
            return controller.failureToken();
        };
        const auto readLog = [&]() -> QString {
            QFile file(logPath);
            if (!file.open(QIODevice::ReadOnly)) {
                return QString();
            }
            return QString::fromUtf8(file.readAll());
        };

        // CASE A: provider returned an EMPTY proposals array.
        QCOMPARE(runExtraction(envelope("{\"proposals\":[]}")),
                 QStringLiteral("no_candidate"));
        const QString logA = readLog();
        QVERIFY2(logA.contains(QStringLiteral("proposals_count=0")),
                 qPrintable(logA));

        // CASE B: provider returned ONE proposal, rejected by evidence.
        QCOMPARE(
            runExtraction(envelope(extractionContent(
                kManufacturerValue, "Manufacturer: FABRICATED NOT IN TEXT"))),
            QStringLiteral("no_candidate"));
        const QString logB = readLog();
        QVERIFY2(logB.contains(QStringLiteral("proposals_count=1")),
                 qPrintable(logB));
        QVERIFY2(logB.contains(QStringLiteral("refused_total=1")),
                 qPrintable(logB));
        QVERIFY2(logB.contains(QStringLiteral("candidates=0")),
                 qPrintable(logB));
        QVERIFY2(logB.contains(QStringLiteral("stage=evidence:")),
                 qPrintable(logB));
        // The TWO runs are decidable from the metadata alone.
        QVERIFY2(!logB.contains(QStringLiteral("proposals_count=0")),
                 qPrintable(logB));

        qunsetenv("MODBUSLENS_PROVIDER_FORENSICS");
        QFile::remove(logPath);
    }

    void fn04_forensicsRecordsPartialAcceptance()
    {
        qputenv("MODBUSLENS_PROVIDER_FORENSICS", "1");
        const QString logPath =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/modbuslens_provider_forensics.log");
        QFile::remove(logPath);

        // Two proposals: one backed by the REAL excerpt, one fabricated.
        std::string twoProposals = "{\"proposals\":[";
        twoProposals += "{\"target\":\"manufacturer\",\"value\":\"";
        twoProposals += kManufacturerValue;
        twoProposals += "\",\"evidence\":\"";
        twoProposals += kManufacturerExcerpt;
        twoProposals += "\"},";
        twoProposals +=
            "{\"target\":\"manufacturer\",\"value\":\"FAKE\","
            "\"evidence\":\"FABRICATED NOT IN TEXT\"}]}";

        FakeHttpClient http;
        http.body = envelope(twoProposals);
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();

        QCOMPARE(controller.stateToken(), QStringLiteral("succeeded"));
        QCOMPARE(controller.candidateCount(), 1);

        QFile file(logPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString log = QString::fromUtf8(file.readAll());
        file.close();
        QVERIFY2(log.contains(QStringLiteral("proposals_count=2")),
                 qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("candidates=1")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("refused_total=1")),
                 qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("stage=accepted")), qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("stage=evidence:")), qPrintable(log));

        qunsetenv("MODBUSLENS_PROVIDER_FORENSICS");
        QFile::remove(logPath);
    }

    void fn05_forensicsNeverLeaksValueOrEvidenceText()
    {
        qputenv("MODBUSLENS_PROVIDER_FORENSICS", "1");
        const QString logPath =
            QStandardPaths::writableLocation(QStandardPaths::TempLocation)
            + QStringLiteral("/modbuslens_provider_forensics.log");
        QFile::remove(logPath);

        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});
        modbuslens::ui::CandidateExtractionController controller(runner);
        controller.requestExtractionFor(makeDocument(), std::string{kManualText});
        controller.grantConsent();
        QCOMPARE(controller.stateToken(), QStringLiteral("succeeded"));

        QFile file(logPath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString log = QString::fromUtf8(file.readAll());
        file.close();
        // Counts/lengths ARE recorded (that is the point of the forensics)...
        QVERIFY2(log.contains(QStringLiteral("proposals_count=1")),
                 qPrintable(log));
        QVERIFY2(log.contains(QStringLiteral("value_len=")), qPrintable(log));
        // ...but the VALUE and the EVIDENCE TEXT never are.
        QVERIFY2(!log.contains(QString::fromUtf8(kManufacturerValue)),
                 qPrintable(log));
        QVERIFY2(!log.contains(QString::fromUtf8(kManufacturerExcerpt)),
                 qPrintable(log));
        // Nor the credential sentinel.
        QVERIFY2(!log.contains(QString::fromUtf8(kDeepSeekKey)),
                 qPrintable(log));

        qunsetenv("MODBUSLENS_PROVIDER_FORENSICS");
        QFile::remove(logPath);
    }

    // ---------------------------------------------------------------------
    // CAND-F1 (T034-B2-HV2-D-CAND-F1): EVIDENCE ANCHORING prompt contract.
    // The local validator locates an excerpt by EXACT substring match with NO
    // normalization, so the prompt must STATE the properties the validator
    // enforces. These tests assert the SEMANTIC contract (keyword level, never
    // a full-string match — the exact wording stays an implementation detail).
    // ---------------------------------------------------------------------
    void cf01_promptRequiresVerbatimContiguousUniqueEvidence()
    {
        FakeHttpClient http;
        http.body = envelope(
            extractionContent(kManufacturerValue, kManufacturerExcerpt));
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                   std::string{kDeepSeekModel},
                                   std::string{kDeepSeekKey});

        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr));
        QCOMPARE(http.callCount, 1);

        const QJsonObject body =
            QJsonDocument::fromJson(
                QByteArray::fromStdString(http.requests.front().body))
                .object();
        const QJsonArray messages =
            body.value(QStringLiteral("messages")).toArray();
        QVERIFY(!messages.isEmpty());
        const QString system =
            messages.at(0).toObject().value(QStringLiteral("content")).toString();
        const QString lowered = system.toLower();
        QVERIFY2(!system.isEmpty(), "system instruction must not be empty");

        // Verbatim / character-level fidelity.
        QVERIFY2(lowered.contains(QStringLiteral("verbatim"))
                     || lowered.contains(QStringLiteral("character for character")),
                 qPrintable(system));
        // Contiguity (no joining of separate fragments across lines).
        QVERIFY2(lowered.contains(QStringLiteral("contiguous")),
                 qPrintable(system));
        // Single-line preference.
        QVERIFY2(lowered.contains(QStringLiteral("single-line")),
                 qPrintable(system));
        // Whole-document uniqueness.
        QVERIFY2(lowered.contains(QStringLiteral("exactly once")),
                 qPrintable(system));
        // Repeated headers/footers are called out as unusable evidence.
        QVERIFY2(lowered.contains(QStringLiteral("header"))
                     && lowered.contains(QStringLiteral("footer")),
                 qPrintable(system));
    }

    void cf02_promptForbidsFabricationAndAllowsLegalEmptyResult()
    {
        FakeHttpClient http;
        http.body = envelope("{\"proposals\":[]}");
        modbuslens::ui::CandidateProviderRunner runner(http);
        runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::ModelScope,
                                   std::string{"Qwen/Qwen3.5-27B"},
                                   std::string{kModelScopeKey});

        QVERIFY(runner.begin(
            buildC2FirstSliceExtractionRequest(makeDocument(),
                                               std::string_view{kManualText}),
            1, nullptr));
        QCOMPARE(http.callCount, 1);

        const QJsonObject body =
            QJsonDocument::fromJson(
                QByteArray::fromStdString(http.requests.front().body))
                .object();
        const QJsonArray messages =
            body.value(QStringLiteral("messages")).toArray();
        const QString system =
            messages.at(0).toObject().value(QStringLiteral("content")).toString();
        const QString user =
            messages.at(1).toObject().value(QStringLiteral("content")).toString();
        const QString lowered = system.toLower();

        // Never rewrite / paraphrase / summarize / translate the source.
        QVERIFY2(lowered.contains(QStringLiteral("paraphrase"))
                     && lowered.contains(QStringLiteral("summarize")),
                 qPrintable(system));
        // An empty result is the LEGAL outcome when no snippet qualifies.
        QVERIFY2(lowered.contains(QStringLiteral("empty")),
                 qPrintable(system));
        // The schema still lives in the USER prompt and is NOT duplicated.
        QVERIFY2(user.contains(QStringLiteral("\"proposals\"")),
                 qPrintable(user));
        QVERIFY2(user.contains(QStringLiteral("manufacturer")),
                 qPrintable(user));
        QVERIFY2(!system.contains(QStringLiteral("\"proposals\"")),
                 "the system instruction must not repeat the JSON schema");
    }

    void cf03_existingProviderRequestContractUnchanged()
    {
        // ModelScope: no provider extras at all.
        {
            FakeHttpClient http;
            http.body = envelope(
                extractionContent(kManufacturerValue, kManufacturerExcerpt));
            modbuslens::ui::CandidateProviderRunner runner(http);
            runner.applyExplicitConfig(
                modbuslens::ui::AiProviderKind::ModelScope,
                std::string{"Qwen/Qwen3.5-27B"}, std::string{kModelScopeKey});
            QVERIFY(runner.begin(
                buildC2FirstSliceExtractionRequest(
                    makeDocument(), std::string_view{kManualText}),
                1, nullptr));
            const QJsonObject body =
                QJsonDocument::fromJson(
                    QByteArray::fromStdString(http.requests.front().body))
                    .object();
            QCOMPARE(body.value(QStringLiteral("model")).toString(),
                     QStringLiteral("Qwen/Qwen3.5-27B"));
            QCOMPARE(body.value(QStringLiteral("stream")).toBool(), false);
            QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 768);
            QVERIFY(!body.contains(QStringLiteral("thinking")));
            QVERIFY(!body.contains(QStringLiteral("response_format")));
        }
        // DeepSeek: thinking disabled + JSON output contract preserved.
        {
            FakeHttpClient http;
            http.body = envelope(
                extractionContent(kManufacturerValue, kManufacturerExcerpt));
            modbuslens::ui::CandidateProviderRunner runner(http);
            runner.applyExplicitConfig(modbuslens::ui::AiProviderKind::DeepSeek,
                                       std::string{kDeepSeekModel},
                                       std::string{kDeepSeekKey});
            QVERIFY(runner.begin(
                buildC2FirstSliceExtractionRequest(
                    makeDocument(), std::string_view{kManualText}),
                1, nullptr));
            const QJsonObject body =
                QJsonDocument::fromJson(
                    QByteArray::fromStdString(http.requests.front().body))
                    .object();
            QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 768);
            QCOMPARE(body.value(QStringLiteral("thinking"))
                         .toObject()
                         .value(QStringLiteral("type"))
                         .toString(),
                     QStringLiteral("disabled"));
            QCOMPARE(body.value(QStringLiteral("response_format"))
                         .toObject()
                         .value(QStringLiteral("type"))
                         .toString(),
                     QStringLiteral("json_object"));
            // The user prompt still carries the JSON-only instruction for
            // DeepSeek (profile-driven), and the schema is present exactly once.
            const QJsonArray messages =
                body.value(QStringLiteral("messages")).toArray();
            const QString user =
                messages.at(1).toObject().value(QStringLiteral("content")).toString();
            QVERIFY(user.contains(QStringLiteral("\"proposals\"")));
        }
    }
};

QTEST_GUILESS_MAIN(CandidateProviderMatrixTest)
#include "test_candidate_provider_matrix.moc"
