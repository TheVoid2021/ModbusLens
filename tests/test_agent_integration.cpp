#include <QtTest>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QTimer>

#include <memory>
#include <QUrl>

#include <chrono>
#include <optional>

#include "core/analysis/TransactionAnalysis.h"
#include "fake_chat_completions_server.h"
#include "ui/candidate/ChatCompletionsHttpClient.h"
#include "ui/AnalysisController.h"
#include "ui/ai/AiRuntimeConfiguration.h"
#include "ui/ai/IAiCredentialStore.h"

#include <map>
#include <string>
#include <string_view>

// T012 Part B Phase 2 — Controller + AgentRuntime integration tests.
// All cloud traffic goes to the localhost fake server (zero internet, zero
// quota, fake token). The controller is constructed with the production
// env config path, so EVERY test clears the config first and re-injects the
// fake endpoint explicitly (same discipline as T011 ai01).
namespace {

// T034-B2.2-A-R2: the Diagnosis chain now takes its provider/model/credential
// authority from the injected GLOBAL configuration, so the integration tests
// inject a provider-scoped in-memory credential store + saved provider/model.
class AgentFakeCredentialStore final : public modbuslens::ui::IAiCredentialStore
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

// Process-lifetime fixture (deliberately leaked: no static-destruction hazard).
modbuslens::ui::AiRuntimeConfiguration& fakeAgentAiRuntime()
{
    static AgentFakeCredentialStore* store = new AgentFakeCredentialStore();
    static modbuslens::ui::AiRuntimeConfiguration* runtime = [store] {
        store->saveCredential(modbuslens::ui::AiProviderKind::ModelScope,
                              std::string_view{"fake-test-token"});
        const QString path =
            QDir::tempPath() + QStringLiteral("/modbuslens_agentint_settings.ini");
        QFile::remove(path);
        auto* config = new modbuslens::ui::AiRuntimeConfiguration(*store, path);
        config->saveConfiguration(modbuslens::ui::AiProviderKind::ModelScope,
                                  QStringLiteral("fake-model"));
        return config;
    }();
    return *runtime;
}

// ---------------------------------------------------------------------------
// T034-B2.2-C-R1: test-only fake of the SHARED async HTTP seam.
//
// It replaces ONLY the HTTP execution end. The request it captures is exactly
// what production would have sent — including the REAL AiProviderProfile
// endpoint — while responses are scripted locally, so ZERO network I/O happens
// and no public provider is ever contacted. It carries NO provider, model,
// credential or revision authority: the AgentRunConfig still comes from the
// controller's global-snapshot resolution.
// ---------------------------------------------------------------------------
class FakeAgentHttpClient final : public QObject,
                                 public modbuslens::ui::IChatCompletionsHttpClient
{
public:
    using QObject::QObject;
    struct Captured {
        QByteArray url;
        QByteArray authorization;
        QByteArray body;
    };
    struct Scripted {
        int statusCode{200};
        QByteArray body;
    };

    [[nodiscard]] modbuslens::ui::ChatCompletionsHttpResult post(
        const modbuslens::ui::ChatCompletionsHttpRequest&) override
    {
        return {}; // the async path is the only one under test
    }

    [[nodiscard]] modbuslens::ui::ChatCompletionsHttpAsyncHandle startAsync(
        const modbuslens::ui::ChatCompletionsHttpRequest& request, QObject*,
        modbuslens::ui::ChatCompletionsHttpCompletion completion) override
    {
        Captured captured;
        captured.url = QByteArray::fromStdString(request.url);
        captured.authorization = QByteArray::fromStdString(request.bearerToken);
        captured.body = QByteArray::fromStdString(request.body);
        captured_.push_back(captured);

        const std::uint64_t id = ++nextId_;
        if (holdAll_) {
            Held held;
            held.id = id;
            held.completion = std::move(completion);
            held_.push_back(std::move(held));
            return modbuslens::ui::ChatCompletionsHttpAsyncHandle{id};
        }
        // Deliver on the NEXT event-loop iteration: exactly like the retired
        // loopback server, so a run is observably in flight right after
        // askAgent() returns and QTRY_* observes the completion.
        auto pending = std::make_shared<
            modbuslens::ui::ChatCompletionsHttpCompletion>(
            std::move(completion));
        QTimer::singleShot(0, this, [this, id, pending]() {
            deliver(id, std::move(*pending));
        });
        return modbuslens::ui::ChatCompletionsHttpAsyncHandle{id};
    }

    // ---- scripting (mirrors the retired loopback server's enqueue API) -----
    void enqueueResponse(int statusCode, const QByteArray& body,
                         int /*delayMs*/ = 0)
    {
        scripted_.push_back(Scripted{statusCode, body});
    }
    void setNextResponse(int statusCode, const QByteArray& body,
                         int /*delayMs*/ = 0)
    {
        scripted_.insert(scripted_.begin(), Scripted{statusCode, body});
    }
    [[nodiscard]] int requestCount() const
    {
        return static_cast<int>(captured_.size());
    }
    [[nodiscard]] const std::vector<Captured>& requests() const
    {
        return captured_;
    }
    [[nodiscard]] const Captured& at(std::size_t index) const
    {
        return captured_.at(index);
    }

    // ---- held-exchange control (B22G-10 overlap oracle) --------------------
    void setHoldAll(bool hold) { holdAll_ = hold; }
    [[nodiscard]] std::size_t pendingCount() const { return held_.size(); }
    [[nodiscard]] std::vector<std::uint64_t> pendingIds() const
    {
        std::vector<std::uint64_t> ids;
        for (const Held& held : held_) {
            ids.push_back(held.id);
        }
        return ids;
    }
    void completeHeld(std::uint64_t id, int statusCode, const QByteArray& body)
    {
        for (auto it = held_.begin(); it != held_.end(); ++it) {
            if (it->id != id) {
                continue;
            }
            auto completion = std::move(it->completion);
            held_.erase(it);
            modbuslens::ui::ChatCompletionsHttpResult result;
            result.transportOk = true;
            result.status = statusCode;
            result.body = body.toStdString();
            completion(std::move(result));
            return;
        }
    }

private:
    struct Held {
        std::uint64_t id{0};
        modbuslens::ui::ChatCompletionsHttpCompletion completion;
    };

    void deliver(std::uint64_t id,
                 modbuslens::ui::ChatCompletionsHttpCompletion completion)
    {
        modbuslens::ui::ChatCompletionsHttpResult result;
        if (scripted_.empty()) {
            // Nothing scripted: a deterministic provider failure (a test that
            // expects success always scripts its responses).
            result.transportOk = true;
            result.status = 500;
            result.body = "{}";
        } else {
            const Scripted next = scripted_.front();
            scripted_.erase(scripted_.begin());
            result.transportOk = true;
            result.status = next.statusCode;
            result.body = next.body.toStdString();
        }
        Q_UNUSED(id);
        completion(std::move(result));
    }

    std::vector<Captured> captured_;
    std::vector<Scripted> scripted_;
    std::vector<Held> held_;
    bool holdAll_{false};
    std::uint64_t nextId_{0};

};

using ms = std::chrono::milliseconds;

QByteArray completionBody(const QJsonObject& message, const QString& finishReason)
{
    const QJsonObject root{{QStringLiteral("choices"),
                            QJsonArray{QJsonObject{
                                {QStringLiteral("index"), 0},
                                {QStringLiteral("message"), message},
                                {QStringLiteral("finish_reason"), finishReason},
                            }}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QJsonObject toolCall(const QString& id, const QString& name, const QString& args)
{
    return QJsonObject{{QStringLiteral("id"), id},
                       {QStringLiteral("type"), QStringLiteral("function")},
                       {QStringLiteral("function"),
                        QJsonObject{{QStringLiteral("name"), name},
                                    {QStringLiteral("arguments"), args}}}};
}

QJsonObject assistantToolCalls(const QJsonArray& calls)
{
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                       {QStringLiteral("content"), QStringLiteral("")},
                       {QStringLiteral("tool_calls"), calls}};
}

QJsonObject finalMessage(const QString& content)
{
    return QJsonObject{{QStringLiteral("role"), QStringLiteral("assistant")},
                       {QStringLiteral("content"), content}};
}

QJsonObject bodyOf(const FakeChatCompletionsServer& server, int index)
{
    return QJsonDocument::fromJson(server.requests().at(index).body).object();
}

QJsonObject bodyOf(const FakeAgentHttpClient& http, int index)
{
    return QJsonDocument::fromJson(http.requests().at(index).body).object();
}

const QString kBadHexLog = QStringLiteral(
    "MODBUSLENS_MLOG|1|timeout_ms=1000\n"
    "TXN|1|GG|01 03 04 00 64 00 C8 BA 7A\n");

QString writeTempMlog(std::optional<QTemporaryFile>& holder)
{
    holder.emplace(QDir::tempPath() + QStringLiteral("/modbuslens_ag_XXXXXX.mlog"));
    if (!holder->open()) {
        return {};
    }
    holder->write(kBadHexLog.toUtf8());
    holder->flush();
    return holder->fileName();
}

} // namespace

// Per-test global authority: an in-memory provider-scoped credential store
// plus a PRIVATE temporary INI (never the developer's real QSettings).
class ScopedGlobalAi
{
public:
    ScopedGlobalAi()
        : runtime_(store_, QDir(tempDir_.path()).filePath(QStringLiteral("ai.ini")))
    {
    }

    [[nodiscard]] modbuslens::ui::AiRuntimeConfiguration& runtime()
    {
        return runtime_;
    }
    void save(modbuslens::ui::AiProviderKind kind, const QString& model,
              const QString& secret)
    {
        if (!secret.isEmpty()) {
            store_.saveCredential(kind, secret.toStdString());
        }
        runtime_.saveConfiguration(kind, model);
    }
    // Hot credential replacement through the runtime (bumps the revision).
    void replaceCredential(modbuslens::ui::AiProviderKind kind,
                           const QString& secret)
    {
        runtime_.saveCredential(kind, secret);
    }

private:
    QTemporaryDir tempDir_;
    AgentFakeCredentialStore store_;
    modbuslens::ui::AiRuntimeConfiguration runtime_;
};

[[nodiscard]] QByteArray profileEndpoint(modbuslens::ui::AiProviderKind kind)
{
    return QByteArray(modbuslens::ui::aiProviderProfile(kind)
                          .chatCompletionsEndpoint);
}

class AgentIntegrationTest : public QObject
{
    Q_OBJECT

private slots:
    void ag01_initialAgentProperties();
    void ag02_snapshotSelfConsistentToolResult();
    void ag03_revisionSyncAllowsRun();
    void ag04_agentSuccessPublishesAnswerFactsUnchanged();
    void ag05_providerFailureErrorShownFactsUnchanged();
    void ag06_askAiBusyRejectsAgent();
    void ag07_agentBusyRejectsAskAi();
    void ag08_cancelAgentSilent();
    void ag09_batchChangeDuringFirstRequest();
    void ag10_batchChangeAfterToolResult();
    void ag11_repeatAskWhileBusyRejected();
    void ag12_revisionSyncPreventsStaleStart();
    void ag13_noDataZeroRequests();
    void ag14_invalidQuestionZeroRequests();
    void ag15_agentNeverChangesFacts();
    void ag16_newBatchClearsOldAnswer();
    void ag17_failedReloadKeepsAnswer();
    void ag18_qmlSmokeCoveredByCtest(); // qml_smoke carries AG18 (no-op marker)
    void ag19_providerConfigSeam();
    void ag20_batchInvalidationOrderingSafety();
    void ag21_http429ProviderFailureVisible();
    void ag22_malformedHttp200Visible();

    // T034-B2.2-C B22G-01..11 — Agent global-AI authority acceptance.
    void b22g01_deepSeekGlobalAuthority();
    void b22g02_modelScopeGlobalAuthorityAndPayload();
    void b22g03_runtimeProviderSwitch();
    void b22g04_runtimeModelSwitch();
    void b22g05_runtimeCredentialReplacement();
    void b22g06_missingCredentialFailsClosed();
    void b22g07_noSavedConfigFailsClosedDespiteLegacyEnv();
    void b22g08_legacyEnvNegativeControl();
    void b22g09_sameRunSnapshotCoherence();
    void b22g10_staleOwnerOverlapAndNoStaleToolEffect();
    void b22g11_startupNoNetwork();

private:
    struct H {
        FakeChatCompletionsServer server; // DIAGNOSIS loopback only
        FakeAgentHttpClient http;         // AGENT shared-HTTP seam
        AnalysisController controller;
        explicit H() {}
        void clearConfig()
        {
            controller.configureAiClient({}, {}, {}, {});
        }
        // T034-B2.2-C-R1: the AGENT's provider authority is the injected GLOBAL
        // configuration (saved provider/model + provider-scoped credential).
        // The fake HTTP seam replaces ONLY the HTTP execution end, so the
        // captured request is exactly what production would have sent — while
        // provider / model / credential / revision / fail-closed authority all
        // keep running through the real controller path.
        void wireFake()
        {
            QVERIFY2(server.start(), "fake loopback listen");
            // The DIAGNOSIS chain keeps its test-only loopback endpoint
            // override (Diagnosis authority is unchanged by B2.2-C), so no
            // public provider is ever contacted by either chain.
            controller.configureAiClient(QUrl(server.chatCompletionsUrl()),
                                         QStringLiteral("fake-test-token"),
                                         QStringLiteral("fake-model"), ms{5000});
            // The AGENT takes its provider/model/credential/revision authority
            // from the injected GLOBAL configuration; the fake seam replaces
            // ONLY the HTTP execution end.
            controller.setAiRuntimeConfiguration(&fakeAgentAiRuntime());
            controller.setAgentHttpClientForAutomation(&http);
        }
    };
};

void AgentIntegrationTest::ag01_initialAgentProperties()
{
    H h;
    h.clearConfig(); // environment token must never leak into the tests
    QVERIFY(!h.controller.agentBusy());
    QVERIFY(!h.controller.hasAgentAnswer());
    QVERIFY(h.controller.agentAnswerText().isEmpty());
    QVERIFY(h.controller.agentErrorText().isEmpty());
    QVERIFY(!h.controller.agentAvailable());
    QVERIFY(!h.controller.cloudAiBusy());
}

void AgentIntegrationTest::ag02_snapshotSelfConsistentToolResult()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.enqueueResponse(
        200, completionBody(assistantToolCalls(QJsonArray{
                                toolCall(QStringLiteral("call-1"),
                                         QStringLiteral("get_session_summary"),
                                         QStringLiteral("{}"))}),
                            QStringLiteral("tool_calls")));
    h.http.enqueueResponse(
        200, completionBody(finalMessage(QStringLiteral("共 4 条事务。")),
                            QStringLiteral("stop")));

    h.controller.askAgent(QStringLiteral("本批次有多少条事务？"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(h.controller.agentAnswerText(), QStringLiteral("共 4 条事务。"));
    QCOMPARE(h.http.requestCount(), 2);

    // Request 1: fixed three-tool schema (capability surface).
    const QJsonArray tools = bodyOf(h.http, 0).value("tools").toArray();
    QCOMPARE(tools.size(), 3);
    QCOMPARE(tools.at(0).toObject().value("function").toObject()
                 .value("name").toString(), QStringLiteral("get_session_summary"));
    QCOMPARE(tools.at(1).toObject().value("function").toObject()
                 .value("name").toString(), QStringLiteral("get_recent_anomalies"));
    QCOMPARE(tools.at(2).toObject().value("function").toObject()
                 .value("name").toString(), QStringLiteral("get_transaction_detail"));

    // Request 2: role=tool message with the SELF-CONSISTENT summary derived
    // by makeAgentToolContext from the copied batch (not presentation state).
    const QJsonArray messages = bodyOf(h.http, 1).value("messages").toArray();
    QCOMPARE(messages.size(), 4);
    const QJsonObject toolMsg = messages.at(3).toObject();
    QCOMPARE(toolMsg.value("role").toString(), QStringLiteral("tool"));
    QCOMPARE(toolMsg.value("tool_call_id").toString(), QStringLiteral("call-1"));
    const QJsonObject summary =
        QJsonDocument::fromJson(toolMsg.value("content").toString().toUtf8()).object();
    QCOMPARE(summary.value("observed_count").toInt(), 4);
    QCOMPARE(summary.value("timeout_count").toInt(), 1);
    QCOMPARE(summary.value("crc_error_count").toInt(), 1);
    QCOMPARE(summary.value("exception_count").toInt(), 1);
}

void AgentIntegrationTest::ag03_revisionSyncAllowsRun()
{
    // controller-level proof that the runtime live revision is synced from
    // activeBatchRevision_: if the seam ever lagged, the runtime preflight
    // would silently refuse (zero requests). A run firing at all is the
    // revision-sync contract at work.
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.setNextResponse(
        200, completionBody(finalMessage(QStringLiteral("OK")), QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("本批次情况如何？"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(h.http.requestCount(), 1);
}

void AgentIntegrationTest::ag04_agentSuccessPublishesAnswerFactsUnchanged()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.setNextResponse(
        200, completionBody(finalMessage(QStringLiteral("回答 A")), QStringLiteral("stop")));
    const int observedBefore = h.controller.observedCount();
    const int rowsBefore = h.controller.transactionModel()->rowCount();

    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(h.controller.agentAnswerText(), QStringLiteral("回答 A"));
    QVERIFY(h.controller.agentErrorText().isEmpty());
    QCOMPARE(h.controller.observedCount(), observedBefore);
    QCOMPARE(h.controller.transactionModel()->rowCount(), rowsBefore);
}

void AgentIntegrationTest::ag05_providerFailureErrorShownFactsUnchanged()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.controller.runBaselineDiagnosis();
    const QString baselineBefore = h.controller.baselineDiagnosisText();

    h.http.setNextResponse(500, QByteArray());
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(!h.controller.agentErrorText().isEmpty());
    QVERIFY(h.controller.agentErrorText().contains(QStringLiteral("ModelScope 服务暂时不可用")));
    QVERIFY(!h.controller.hasAgentAnswer());
    QCOMPARE(h.controller.baselineDiagnosisText(), baselineBefore);
    QCOMPARE(h.controller.observedCount(), 4);
}

void AgentIntegrationTest::ag06_askAiBusyRejectsAgent()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.controller.runBaselineDiagnosis();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("AI 答")),
                                                 QStringLiteral("stop")), 350);
    h.controller.askAiDiagnosis();
    QVERIFY(h.controller.aiDiagnosisBusy());

    h.controller.askAgent(QStringLiteral("情况如何？"));
    QVERIFY(h.controller.agentErrorText().contains(QStringLiteral("AI 解释请求进行中")));
    QTest::qWait(600); // let the AI request finish
    // The fake seam carries the AGENT path only: zero captures proves the
    // rejected Agent ask never fired a provider request. (The Diagnosis request
    // runs on its own loopback endpoint and is not part of this oracle.)
    QCOMPARE(h.http.requestCount(), 0);
    QVERIFY(!h.controller.hasAgentAnswer());
}

void AgentIntegrationTest::ag07_agentBusyRejectsAskAi()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.controller.runBaselineDiagnosis();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("Agent 答")),
                                                 QStringLiteral("stop")), 350);
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QVERIFY(h.controller.agentBusy());

    h.controller.askAiDiagnosis();
    QVERIFY(h.controller.aiDiagnosisErrorMessage().contains(QStringLiteral("Agent 问答进行中")));
    QTest::qWait(600);
    QCOMPARE(h.http.requestCount(), 1); // only the Agent request ever fired
}

void AgentIntegrationTest::ag08_cancelAgentSilent()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("LATE")),
                                                 QStringLiteral("stop")), 400);
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QVERIFY(h.controller.agentBusy());
    h.controller.cancelAgent();
    QVERIFY(!h.controller.agentBusy());       // promptly not busy
    QVERIFY(h.controller.agentErrorText().isEmpty()); // no red Cancelled error
    QVERIFY(!h.controller.hasAgentAnswer());
    QTest::qWait(700);
    QVERIFY(!h.controller.hasAgentAnswer());  // late response never publishes
    QVERIFY(h.controller.agentErrorText().isEmpty());
}

void AgentIntegrationTest::ag09_batchChangeDuringFirstRequest()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("OLD 批次答案")),
                                                 QStringLiteral("stop")), 400);
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QVERIFY(h.controller.agentBusy());

    h.controller.loadReplayFile(QUrl::fromLocalFile(
        QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH))); // batch switches
    QVERIFY(!h.controller.agentBusy());        // promptly cleared
    QVERIFY(!h.controller.hasAgentAnswer());   // old-run answer invalidated
    QVERIFY(h.controller.agentErrorText().isEmpty());
    QTest::qWait(700);                          // late delivery window
    QVERIFY(!h.controller.hasAgentAnswer());
    QVERIFY(h.controller.agentErrorText().isEmpty());
}

void AgentIntegrationTest::ag10_batchChangeAfterToolResult()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.enqueueResponse(
        200, completionBody(assistantToolCalls(QJsonArray{
                                toolCall(QStringLiteral("call-1"),
                                         QStringLiteral("get_session_summary"),
                                         QStringLiteral("{}"))}),
                            QStringLiteral("tool_calls")));
    h.http.enqueueResponse(
        200, completionBody(finalMessage(QStringLiteral("STALE FINAL")),
                            QStringLiteral("stop")), 350);
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(h.http.requestCount() == 2); // tool executed, final pending

    h.controller.loadReplayFile(QUrl::fromLocalFile(
        QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    QVERIFY(!h.controller.agentBusy());
    QVERIFY(!h.controller.hasAgentAnswer());
    QTest::qWait(700);
    QVERIFY(!h.controller.hasAgentAnswer());   // final never publishes
}

void AgentIntegrationTest::ag11_repeatAskWhileBusyRejected()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("第一个答案")),
                                                 QStringLiteral("stop")), 350);
    h.controller.askAgent(QStringLiteral("问题一"));
    QTRY_VERIFY(h.http.requestCount() == 1); // first request really on wire
    h.controller.askAgent(QStringLiteral("问题二"));
    QVERIFY(h.controller.agentErrorText().contains(QStringLiteral("已有 Agent 请求进行中")));
    QCOMPARE(h.http.requestCount(), 1); // the second ask never became a request
    QTest::qWait(600);
    QCOMPARE(h.controller.agentAnswerText(), QStringLiteral("第一个答案"));
}

void AgentIntegrationTest::ag12_revisionSyncPreventsStaleStart()
{
    // Controller-side ST-A: the snapshot is built synchronously from the
    // CURRENT batch + CURRENT revision, so a "stale context start" is not
    // expressible through the production API (the preflight always matches
    // and a new batch asks cleanly). The stale-start contract itself is
    // proven at the runtime layer by Phase 1 B22; here we lock the seam
    // invariant: after a batch switch, a new ask is accepted (not silently
    // eaten), proving the live revision moved with the batch.
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("第一答")),
                                                 QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());

    h.controller.loadReplayFile(QUrl::fromLocalFile(
        QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("第二答")),
                                                 QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("二"));
    QTRY_VERIFY(h.controller.agentAnswerText() == QStringLiteral("第二答"));
    QCOMPARE(h.http.requestCount(), 2);
}

void AgentIntegrationTest::ag13_noDataZeroRequests()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.clearResults(); // empty batch
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QVERIFY(h.controller.agentErrorText().contains(QStringLiteral("当前没有可分析的事务数据")));
    QCOMPARE(h.http.requestCount(), 0);
    QVERIFY(!h.controller.hasAgentAnswer());
}

void AgentIntegrationTest::ag14_invalidQuestionZeroRequests()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.controller.askAgent(QStringLiteral("   "));
    QTRY_VERIFY(h.controller.agentErrorText().contains(QStringLiteral("问题不能为空")));
    QCOMPARE(h.http.requestCount(), 0);
    QVERIFY(!h.controller.hasAgentAnswer());
}

void AgentIntegrationTest::ag15_agentNeverChangesFacts()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.controller.runBaselineDiagnosis();
    const QString baselineBefore = h.controller.baselineDiagnosisText();
    const int observedBefore = h.controller.observedCount();
    const int successBefore = h.controller.successCount();
    const int protocolBefore = h.controller.protocolErrorCount();
    const int rowsBefore = h.controller.transactionModel()->rowCount();
    h.http.enqueueResponse(
        200, completionBody(assistantToolCalls(QJsonArray{
                                toolCall(QStringLiteral("call-1"),
                                         QStringLiteral("get_recent_anomalies"),
                                         QStringLiteral("{}"))}),
                            QStringLiteral("tool_calls")));
    h.http.enqueueResponse(
        200, completionBody(finalMessage(QStringLiteral("我会胡说八道改变事实")),
                            QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());

    QCOMPARE(h.controller.baselineDiagnosisText(), baselineBefore);
    QCOMPARE(h.controller.observedCount(), observedBefore);
    QCOMPARE(h.controller.successCount(), successBefore);
    QCOMPARE(h.controller.protocolErrorCount(), protocolBefore);
    QCOMPARE(h.controller.transactionModel()->rowCount(), rowsBefore);
}

void AgentIntegrationTest::ag16_newBatchClearsOldAnswer()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("旧答案")),
                                                 QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());

    h.controller.loadReplayFile(QUrl::fromLocalFile(
        QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH))); // new batch
    QVERIFY(!h.controller.hasAgentAnswer());
    QVERIFY(h.controller.agentAnswerText().isEmpty());
    QVERIFY(h.controller.agentErrorText().isEmpty());
}

void AgentIntegrationTest::ag17_failedReloadKeepsAnswer()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("保留我")),
                                                 QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());

    std::optional<QTemporaryFile> holder;
    const QString badPath = writeTempMlog(holder);
    QVERIFY(!badPath.isEmpty());
    h.controller.loadReplayFile(QUrl::fromLocalFile(badPath)); // FAILS: batch unchanged
    QVERIFY(h.controller.hasReplayError());
    QVERIFY(h.controller.hasAgentAnswer());          // existing Agent result kept
    QCOMPARE(h.controller.agentAnswerText(), QStringLiteral("保留我"));
}

void AgentIntegrationTest::ag18_qmlSmokeCoveredByCtest()
{
    // UI-AG18 is carried by the existing `qml_smoke` ctest (real app loads
    // the shipped QML module headlessly; any ReferenceError/binding-loop
    // would fail the smoke). This function only documents the mapping.
    QVERIFY(true);
}

void AgentIntegrationTest::ag19_providerConfigSeam()
{
    // T034-B2.2-C-R1: the retired explicit seam no longer carries Agent
    // authority. Availability AND dispatch now both come from the injected
    // GLOBAL configuration (saved provider/model + provider-scoped credential).
    H h;
    h.clearConfig();
    QVERIFY(!h.controller.agentAvailable()); // no global config wired yet
    h.wireFake();
    QVERIFY(h.controller.agentAvailable());  // saved config + credential ready
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("配置正常")),
                                                 QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(h.http.requestCount(), 1);
    // The shared transport carries the RAW one-shot token; the production HTTP
    // implementation is what prepends "Bearer ". Compared with QVERIFY2 so a
    // failure never dumps the token value.
    QVERIFY2(h.http.at(0).authorization == QByteArray("fake-test-token"),
             "the provider-scoped credential must ride the request");
    // The endpoint is the RESOLVED provider profile endpoint — not a test URL.
    QCOMPARE(h.http.at(0).url,
             QByteArray(modbuslens::ui::aiProviderProfile(
                            modbuslens::ui::AiProviderKind::ModelScope)
                            .chatCompletionsEndpoint));
}

void AgentIntegrationTest::ag20_batchInvalidationOrderingSafety()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.enqueueResponse(
        200, completionBody(assistantToolCalls(QJsonArray{
                                toolCall(QStringLiteral("call-1"),
                                         QStringLiteral("get_session_summary"),
                                         QStringLiteral("{}"))}),
                            QStringLiteral("tool_calls")));
    h.http.enqueueResponse(
        200, completionBody(finalMessage(QStringLiteral("LATE FINAL")),
                            QStringLiteral("stop")), 350);
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(h.http.requestCount() == 2);

    // Immediate assertions after the batch switch — NO sleeps: the seam must
    // have switched the live revision and silently killed the run already.
    h.controller.loadReplayFile(QUrl::fromLocalFile(
        QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    QVERIFY(!h.controller.agentBusy());
    QVERIFY(!h.controller.hasAgentAnswer());
    QVERIFY(h.controller.agentErrorText().isEmpty());
    QTest::qWait(700); // abort-neighborhood callbacks land in this window
    QVERIFY(!h.controller.hasAgentAnswer());
    QVERIFY(h.controller.agentErrorText().isEmpty());
}

void AgentIntegrationTest::ag21_http429ProviderFailureVisible()
{
    // Full propagation: fake 429 -> client RateLimited -> runtime runFailed
    // -> controller agentErrorText non-empty PERSISTENT text, busy false.
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.http.setNextResponse(429,
        QByteArray("{\"error\":{\"message\":\"rate limited\"}}"));
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(!h.controller.agentBusy());
    QCOMPARE(h.controller.agentErrorText(),
             QStringLiteral("ModelScope 请求受限或额度不足，请检查账户状态后重试。"));
    QVERIFY(!h.controller.hasAgentAnswer());
    // Lifecycle: the error persists after busy ends; ONLY a new accepted
    // run clears it.
    QTest::qWait(120);
    QVERIFY(!h.controller.agentErrorText().isEmpty());
    h.http.setNextResponse(200,
        completionBody(finalMessage(QStringLiteral("新回答")), QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("再问一次"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QVERIFY(h.controller.agentErrorText().isEmpty()); // cleared by accepted run
}

void AgentIntegrationTest::ag22_malformedHttp200Visible()
{
    // HTTP 200 but NO usable content and NO tool calls -> InvalidResponse
    // must reach the user as a persistent visible error, never silence.
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    const QJsonObject unusable{
        {QStringLiteral("role"), QStringLiteral("assistant")},
        {QStringLiteral("content"), QStringLiteral("")},
        {QStringLiteral("reasoning_content"),
         QStringLiteral("must never surface")},
    };
    h.http.setNextResponse(200, completionBody(unusable, QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(!h.controller.agentBusy());
    QVERIFY(!h.controller.agentErrorText().isEmpty());
    QVERIFY(h.controller.agentErrorText().contains(
        QStringLiteral("模型返回了无法处理的响应格式")));
    QVERIFY(!h.controller.hasAgentAnswer()); // reasoning never promoted
}

// ===========================================================================
// T034-B2.2-C — B22G-01..11 Agent global-AI authority acceptance.
//
// Every case drives the REAL AnalysisController::askAgent() through the REAL
// AgentRuntime and the REAL provider-neutral AgentAsyncClient, with a per-test
// global AiRuntimeConfiguration + in-memory credential store. The fake HTTP
// seam captures the ACTUAL request (URL / Authorization / body) while doing
// zero network I/O. Credential comparisons use QVERIFY2 so a failing assertion
// never prints a token value.
// ===========================================================================

namespace {
using modbuslens::ui::AiProviderKind;

// A minimal helper: wire a controller to a scoped global authority + fake HTTP.
// T034-B2.2-C-D2: the fixture owns NO HTTP fake of its own. It takes the
// caller's FakeAgentHttpClient BY REFERENCE, so the instance injected into the
// production Agent HTTP seam is guaranteed to be the SAME instance that the
// test scripts, observes (requestCount / pendingCount / captured request) and
// drives (hold / deliver). A second, similarly named fake is structurally
// impossible here.
struct Wire {
    ScopedGlobalAi ai;

    void attach(AnalysisController& controller, FakeAgentHttpClient& http)
    {
        controller.setAiRuntimeConfiguration(&ai.runtime());
        controller.setAgentHttpClientForAutomation(&http);
    }
};
}

void AgentIntegrationTest::b22g01_deepSeekGlobalAuthority()
{
    Wire w;
    w.ai.save(AiProviderKind::DeepSeek, QStringLiteral("det/deepseek-agent-model"),
              QStringLiteral("fake-deepseek-credential"));

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("答案")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());

    QCOMPARE(h.http.requestCount(), 1);
    QCOMPARE(h.http.at(0).url, profileEndpoint(AiProviderKind::DeepSeek));
    QVERIFY(h.http.at(0).url.contains("deepseek"));
    QVERIFY(!h.http.at(0).url.contains("modelscope"));
    QVERIFY2(h.http.at(0).authorization == QByteArray("fake-deepseek-credential"),
             "the DeepSeek provider-scoped credential must authorize the run");
    const QJsonObject body = bodyOf(h.http, 0);
    QCOMPARE(body.value(QStringLiteral("model")).toString(),
             QStringLiteral("det/deepseek-agent-model"));
}

void AgentIntegrationTest::b22g02_modelScopeGlobalAuthorityAndPayload()
{
    Wire w;
    w.ai.save(AiProviderKind::ModelScope, QStringLiteral("det/modelscope-agent-model"),
              QStringLiteral("fake-modelscope-credential"));

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("答案")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());

    QCOMPARE(h.http.requestCount(), 1);
    QCOMPARE(h.http.at(0).url, profileEndpoint(AiProviderKind::ModelScope));
    QVERIFY2(h.http.at(0).authorization == QByteArray("fake-modelscope-credential"),
             "the ModelScope provider-scoped credential must authorize the run");
    const QJsonObject body = bodyOf(h.http, 0);
    QCOMPARE(body.value(QStringLiteral("model")).toString(),
             QStringLiteral("det/modelscope-agent-model"));
    // The existing Agent payload semantics are locked, with NO provider extras.
    QCOMPARE(body.value(QStringLiteral("tools")).toArray().size(), 3);
    QCOMPARE(body.value(QStringLiteral("stream")), QJsonValue(false));
    QCOMPARE(body.value(QStringLiteral("max_tokens")), QJsonValue(768));
    QVERIFY(body.value(QStringLiteral("messages")).isArray());
    QVERIFY2(!body.contains(QStringLiteral("chat_template_kwargs")),
             "the Agent payload carries no provider-specific extras");
}

void AgentIntegrationTest::b22g03_runtimeProviderSwitch()
{
    Wire w;
    w.ai.save(AiProviderKind::ModelScope, QStringLiteral("det/modelscope-agent-model"),
              QStringLiteral("fake-modelscope-credential"));

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    h.controller.runDemoBatch();

    // run #1 (ModelScope) completes.
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("一")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(h.http.requestCount(), 1);

    // Live Save switches the provider — same controller, no restart.
    w.ai.save(AiProviderKind::DeepSeek, QStringLiteral("det/deepseek-agent-model"),
              QStringLiteral("fake-deepseek-credential"));

    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("二")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("二"));
    QTRY_COMPARE(h.http.requestCount(), 2);
    QCOMPARE(h.http.at(0).url, profileEndpoint(AiProviderKind::ModelScope));
    QCOMPARE(h.http.at(1).url, profileEndpoint(AiProviderKind::DeepSeek));
    QVERIFY2(h.http.at(1).authorization == QByteArray("fake-deepseek-credential"),
             "run #2 must use the DeepSeek provider-scoped credential");
    QCOMPARE(bodyOf(h.http, 1).value(QStringLiteral("model")).toString(),
             QStringLiteral("det/deepseek-agent-model"));
}

void AgentIntegrationTest::b22g04_runtimeModelSwitch()
{
    Wire w;
    w.ai.save(AiProviderKind::ModelScope, QStringLiteral("det/model-A"),
              QStringLiteral("fake-modelscope-credential"));

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    h.controller.runDemoBatch();

    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("一")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(bodyOf(h.http, 0).value(QStringLiteral("model")).toString(),
             QStringLiteral("det/model-A"));

    w.ai.save(AiProviderKind::ModelScope, QStringLiteral("det/model-B"),
              QStringLiteral("fake-modelscope-credential"));

    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("二")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("二"));
    QTRY_COMPARE(h.http.requestCount(), 2);
    QCOMPARE(bodyOf(h.http, 1).value(QStringLiteral("model")).toString(),
             QStringLiteral("det/model-B"));
}

void AgentIntegrationTest::b22g05_runtimeCredentialReplacement()
{
    Wire w;
    w.ai.save(AiProviderKind::ModelScope, QStringLiteral("det/modelscope-agent-model"),
              QStringLiteral("fake-credential-A"));

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    h.controller.runDemoBatch();

    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("一")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QVERIFY2(h.http.at(0).authorization == QByteArray("fake-credential-A"),
             "run #1 must use credential A");

    w.ai.replaceCredential(AiProviderKind::ModelScope,
                           QStringLiteral("fake-credential-B"));

    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("二")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("二"));
    QTRY_COMPARE(h.http.requestCount(), 2);
    QVERIFY2(h.http.at(1).authorization == QByteArray("fake-credential-B"),
             "run #2 must use the REPLACEMENT credential");
    QVERIFY2(h.http.at(1).authorization != h.http.at(0).authorization,
             "run #2 must NOT reuse credential A");
}

void AgentIntegrationTest::b22g06_missingCredentialFailsClosed()
{
    // Valid saved provider/model but the provider-scoped credential is ABSENT.
    Wire w;
    w.ai.save(AiProviderKind::DeepSeek, QStringLiteral("det/deepseek-agent-model"),
              QString()); // no credential stored

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    h.controller.runDemoBatch();
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTest::qWait(150);

    QCOMPARE(h.http.requestCount(), 0); // zero network, fail closed
    QVERIFY(!h.controller.hasAgentAnswer());
    QVERIFY(!h.controller.agentBusy());
}

void AgentIntegrationTest::b22g07_noSavedConfigFailsClosedDespiteLegacyEnv()
{
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

    // A configuration with NOTHING saved, while the legacy environment
    // sentinels above are present in this process.
    ScopedGlobalAi ai;
    H h;
    h.clearConfig();
    h.controller.setAiRuntimeConfiguration(&ai.runtime());
    h.controller.setAgentHttpClientForAutomation(&h.http);
    h.controller.runDemoBatch();
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTest::qWait(150);

    QCOMPARE(h.http.requestCount(), 0); // the legacy env is NOT Agent authority
    QVERIFY(!h.controller.hasAgentAnswer());
}

void AgentIntegrationTest::b22g08_legacyEnvNegativeControl()
{
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

    Wire w;
    w.ai.save(AiProviderKind::DeepSeek, QStringLiteral("det/deepseek-agent-model"),
              QStringLiteral("fake-deepseek-credential"));

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    h.controller.runDemoBatch();
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("答案")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());

    QCOMPARE(h.http.requestCount(), 1);
    QCOMPARE(h.http.at(0).url, profileEndpoint(AiProviderKind::DeepSeek));
    QVERIFY2(h.http.at(0).authorization == QByteArray("fake-deepseek-credential"),
             "the legacy environment sentinel must NOT authorize the run");
    QCOMPARE(bodyOf(h.http, 0).value(QStringLiteral("model")).toString(),
             QStringLiteral("det/deepseek-agent-model"));
}

void AgentIntegrationTest::b22g09_sameRunSnapshotCoherence()
{
    // The core Agent-specific invariant: ONE run, ONE immutable snapshot. A
    // mid-run configuration change must never make step 2 speak to a different
    // provider/model/credential than step 1.
    Wire w;
    w.ai.save(AiProviderKind::ModelScope, QStringLiteral("det/model-A"),
              QStringLiteral("fake-credential-A"));

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    h.controller.runDemoBatch();

    // Hold run N's first provider exchange so the config change can land while
    // the run is genuinely in flight.
    h.http.setHoldAll(true);
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_COMPARE(h.http.pendingCount(), std::size_t(1));
    const std::uint64_t idN = h.http.pendingIds().at(0);

    // A live Save switches everything to DeepSeek/B/keyB.
    w.ai.save(AiProviderKind::DeepSeek, QStringLiteral("det/model-B"),
              QStringLiteral("fake-credential-B"));

    // Run N lost its ownership: the stale reply (carrying a REAL tool_call)
    // must produce NO tool side effect and NO follow-up provider request.
    h.http.completeHeld(
        idN, 200,
        completionBody(assistantToolCalls(QJsonArray{toolCall(
                           QStringLiteral("call-stale"),
                           QStringLiteral("get_session_summary"),
                           QStringLiteral("{}"))}),
                       QStringLiteral("tool_calls")));
    QTest::qWait(150);

    QCOMPARE(h.http.requestCount(), 1); // no follow-up request from the old run
    QVERIFY(!h.controller.hasAgentAnswer());
    QVERIFY(!h.controller.agentBusy());
    // The single captured request used the RUN-START snapshot, never the new one.
    QCOMPARE(h.http.at(0).url, profileEndpoint(AiProviderKind::ModelScope));
    QVERIFY2(h.http.at(0).authorization == QByteArray("fake-credential-A"),
             "every step of a run uses its run-start snapshot");
    QCOMPARE(bodyOf(h.http, 0).value(QStringLiteral("model")).toString(),
             QStringLiteral("det/model-A"));

    // A brand-new explicit run does pick up the new snapshot.
    h.http.setHoldAll(false);
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("二")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("二"));
    QTRY_COMPARE(h.http.requestCount(), 2);
    QCOMPARE(h.http.at(1).url, profileEndpoint(AiProviderKind::DeepSeek));
}

void AgentIntegrationTest::b22g10_staleOwnerOverlapAndNoStaleToolEffect()
{
    Wire w;
    w.ai.save(AiProviderKind::ModelScope, QStringLiteral("det/model-A"),
              QStringLiteral("fake-credential-A"));

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    h.controller.runDemoBatch();

    // 1-2: a prior ACCEPTED Agent result.
    h.http.setNextResponse(200, completionBody(finalMessage(QStringLiteral("prior-accepted")),
                                               QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("prior"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(h.controller.agentAnswerText(), QStringLiteral("prior-accepted"));
    const int baselineRequests = h.http.requestCount();

    // 3-5: dispatch run N and HOLD its provider exchange.
    h.http.setHoldAll(true);
    h.controller.askAgent(QStringLiteral("attempt-N"));
    QTRY_COMPARE(h.http.pendingCount(), std::size_t(1));
    const std::uint64_t idN = h.http.pendingIds().at(0);
    QVERIFY(h.controller.agentBusy());

    // 6-7: a configuration Save supersedes N. Ownership is revoked, busy is
    // released, and the prior accepted result/error SURVIVES.
    w.ai.save(AiProviderKind::ModelScope, QStringLiteral("det/model-B"),
              QStringLiteral("fake-credential-B"));
    QVERIFY(!h.controller.agentBusy());
    QCOMPARE(h.controller.agentAnswerText(), QStringLiteral("prior-accepted"));


    // 8-11: run N+1 starts while N is STILL pending at the transport layer.
    h.controller.askAgent(QStringLiteral("attempt-N1"));
    QTRY_COMPARE(h.http.pendingCount(), std::size_t(2));
    const auto ids = h.http.pendingIds();
    QCOMPARE(ids.size(), std::size_t(2));
    QVERIFY2(ids.at(0) != ids.at(1), "run ids must be distinct");
    QVERIFY(h.controller.agentBusy());

    // 12-13: the STALE run N completes with a real tool_call. It must change
    // NOTHING: no result/error/tool-state mutation, no new request, and it must
    // not clear N+1's ownership.
    h.http.completeHeld(
        idN, 200,
        completionBody(assistantToolCalls(QJsonArray{toolCall(
                           QStringLiteral("call-stale"),
                           QStringLiteral("get_session_summary"),
                           QStringLiteral("{}"))}),
                       QStringLiteral("tool_calls")));
    QTest::qWait(150);
    QCOMPARE(h.controller.agentAnswerText(), QStringLiteral("prior-accepted"));
    QVERIFY(h.controller.agentBusy());                 // N+1 still owns the run
    QCOMPARE(h.http.requestCount(), baselineRequests + 2); // no stale follow-up
    QCOMPARE(h.http.pendingCount(), std::size_t(1));   // only N+1 remains

    // 14-16: N+1 completes normally and becomes the accepted result.
    const std::uint64_t idN1 =
        (h.http.pendingIds().at(0) == idN) ? 0 : h.http.pendingIds().at(0);
    QVERIFY(idN1 != 0);
    h.http.completeHeld(idN1, 200,
                        completionBody(finalMessage(QStringLiteral("post-switch-accepted")),
                                       QStringLiteral("stop")));
    QTRY_COMPARE(h.controller.agentAnswerText(),
                 QStringLiteral("post-switch-accepted"));
    QVERIFY(!h.controller.agentBusy());
    QCOMPARE(h.http.pendingCount(), std::size_t(0));
}

void AgentIntegrationTest::b22g11_startupNoNetwork()
{
    // A fully valid global configuration and credential exist, but no explicit
    // Agent action has been taken: ZERO provider requests.
    Wire w;
    w.ai.save(AiProviderKind::DeepSeek, QStringLiteral("det/deepseek-agent-model"),
              QStringLiteral("fake-deepseek-credential"));

    H h;
    h.clearConfig();
    w.attach(h.controller, h.http);
    QVERIFY(h.controller.agentAvailable());
    QTest::qWait(150);
    QCOMPARE(h.http.requestCount(), 0);
    QVERIFY(!h.controller.agentBusy());
}

QTEST_GUILESS_MAIN(AgentIntegrationTest)
#include "test_agent_integration.moc"