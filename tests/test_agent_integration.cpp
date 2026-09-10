#include <QtTest>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QUrl>

#include <chrono>
#include <optional>

#include "core/analysis/TransactionAnalysis.h"
#include "fake_chat_completions_server.h"
#include "ui/AnalysisController.h"

// T012 Part B Phase 2 — Controller + AgentRuntime integration tests.
// All cloud traffic goes to the localhost fake server (zero internet, zero
// quota, fake token). The controller is constructed with the production
// env config path, so EVERY test clears the config first and re-injects the
// fake endpoint explicitly (same discipline as T011 ai01).
namespace {

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

private:
    struct H {
        FakeChatCompletionsServer server;
        AnalysisController controller;
        explicit H() {}
        void clearConfig()
        {
            controller.configureAiClient({}, {}, {}, {});
        }
        void wireFake()
        {
            QVERIFY2(server.start(), "fake server listen");
            controller.configureAiClient(QUrl(server.chatCompletionsUrl()),
                                         QStringLiteral("fake-test-token"),
                                         QStringLiteral("fake-model"), ms{5000});
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
    h.server.enqueueResponse(
        200, completionBody(assistantToolCalls(QJsonArray{
                                toolCall(QStringLiteral("call-1"),
                                         QStringLiteral("get_session_summary"),
                                         QStringLiteral("{}"))}),
                            QStringLiteral("tool_calls")));
    h.server.enqueueResponse(
        200, completionBody(finalMessage(QStringLiteral("共 4 条事务。")),
                            QStringLiteral("stop")));

    h.controller.askAgent(QStringLiteral("本批次有多少条事务？"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(h.controller.agentAnswerText(), QStringLiteral("共 4 条事务。"));
    QCOMPARE(h.server.requestCount(), 2);

    // Request 1: fixed three-tool schema (capability surface).
    const QJsonArray tools = bodyOf(h.server, 0).value("tools").toArray();
    QCOMPARE(tools.size(), 3);
    QCOMPARE(tools.at(0).toObject().value("function").toObject()
                 .value("name").toString(), QStringLiteral("get_session_summary"));
    QCOMPARE(tools.at(1).toObject().value("function").toObject()
                 .value("name").toString(), QStringLiteral("get_recent_anomalies"));
    QCOMPARE(tools.at(2).toObject().value("function").toObject()
                 .value("name").toString(), QStringLiteral("get_transaction_detail"));

    // Request 2: role=tool message with the SELF-CONSISTENT summary derived
    // by makeAgentToolContext from the copied batch (not presentation state).
    const QJsonArray messages = bodyOf(h.server, 1).value("messages").toArray();
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
    h.server.setNextResponse(
        200, completionBody(finalMessage(QStringLiteral("OK")), QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("本批次情况如何？"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(h.server.requestCount(), 1);
}

void AgentIntegrationTest::ag04_agentSuccessPublishesAnswerFactsUnchanged()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.server.setNextResponse(
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

    h.server.setNextResponse(500, QByteArray());
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(!h.controller.agentErrorText().isEmpty());
    QVERIFY(h.controller.agentErrorText().contains(QStringLiteral("服务端错误")));
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
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("AI 答")),
                                                 QStringLiteral("stop")), 350);
    h.controller.askAiDiagnosis();
    QVERIFY(h.controller.aiDiagnosisBusy());

    h.controller.askAgent(QStringLiteral("情况如何？"));
    QVERIFY(h.controller.agentErrorText().contains(QStringLiteral("AI 解释请求进行中")));
    QTest::qWait(600); // let the AI request finish
    QCOMPARE(h.server.requestCount(), 1); // only the AI request ever fired
    QVERIFY(!h.controller.hasAgentAnswer());
}

void AgentIntegrationTest::ag07_agentBusyRejectsAskAi()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.controller.runBaselineDiagnosis();
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("Agent 答")),
                                                 QStringLiteral("stop")), 350);
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QVERIFY(h.controller.agentBusy());

    h.controller.askAiDiagnosis();
    QVERIFY(h.controller.aiDiagnosisErrorMessage().contains(QStringLiteral("Agent 问答进行中")));
    QTest::qWait(600);
    QCOMPARE(h.server.requestCount(), 1); // only the Agent request ever fired
}

void AgentIntegrationTest::ag08_cancelAgentSilent()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("LATE")),
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
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("OLD 批次答案")),
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
    h.server.enqueueResponse(
        200, completionBody(assistantToolCalls(QJsonArray{
                                toolCall(QStringLiteral("call-1"),
                                         QStringLiteral("get_session_summary"),
                                         QStringLiteral("{}"))}),
                            QStringLiteral("tool_calls")));
    h.server.enqueueResponse(
        200, completionBody(finalMessage(QStringLiteral("STALE FINAL")),
                            QStringLiteral("stop")), 350);
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(h.server.requestCount() == 2); // tool executed, final pending

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
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("第一个答案")),
                                                 QStringLiteral("stop")), 350);
    h.controller.askAgent(QStringLiteral("问题一"));
    QTRY_VERIFY(h.server.requestCount() == 1); // first request really on wire
    h.controller.askAgent(QStringLiteral("问题二"));
    QVERIFY(h.controller.agentErrorText().contains(QStringLiteral("已有 Agent 请求进行中")));
    QCOMPARE(h.server.requestCount(), 1); // the second ask never became a request
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
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("第一答")),
                                                 QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());

    h.controller.loadReplayFile(QUrl::fromLocalFile(
        QString::fromUtf8(MODBUSLENS_DEMO_MLOG_PATH)));
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("第二答")),
                                                 QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("二"));
    QTRY_VERIFY(h.controller.agentAnswerText() == QStringLiteral("第二答"));
    QCOMPARE(h.server.requestCount(), 2);
}

void AgentIntegrationTest::ag13_noDataZeroRequests()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.clearResults(); // empty batch
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QVERIFY(h.controller.agentErrorText().contains(QStringLiteral("当前没有可分析的事务数据")));
    QCOMPARE(h.server.requestCount(), 0);
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
    QCOMPARE(h.server.requestCount(), 0);
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
    h.server.enqueueResponse(
        200, completionBody(assistantToolCalls(QJsonArray{
                                toolCall(QStringLiteral("call-1"),
                                         QStringLiteral("get_recent_anomalies"),
                                         QStringLiteral("{}"))}),
                            QStringLiteral("tool_calls")));
    h.server.enqueueResponse(
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
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("旧答案")),
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
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("保留我")),
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
    H h;
    h.clearConfig();
    QVERIFY(!h.controller.agentAvailable());
    h.server.start();
    h.controller.configureAiClient(QUrl(h.server.chatCompletionsUrl()),
                                   QStringLiteral("fake-test-token"),
                                   QStringLiteral("fake-model"), ms{5000});
    QVERIFY(h.controller.agentAvailable());
    h.controller.runDemoBatch();
    h.server.setNextResponse(200, completionBody(finalMessage(QStringLiteral("配置正常")),
                                                 QStringLiteral("stop")));
    h.controller.askAgent(QStringLiteral("一"));
    QTRY_VERIFY(h.controller.hasAgentAnswer());
    QCOMPARE(h.server.requests().at(0).authorization,
             QByteArray("Bearer fake-test-token"));
    QCOMPARE(h.server.requests().at(0).path, QStringLiteral("/chat/completions"));
}

void AgentIntegrationTest::ag20_batchInvalidationOrderingSafety()
{
    H h;
    h.clearConfig();
    h.wireFake();
    h.controller.runDemoBatch();
    h.server.enqueueResponse(
        200, completionBody(assistantToolCalls(QJsonArray{
                                toolCall(QStringLiteral("call-1"),
                                         QStringLiteral("get_session_summary"),
                                         QStringLiteral("{}"))}),
                            QStringLiteral("tool_calls")));
    h.server.enqueueResponse(
        200, completionBody(finalMessage(QStringLiteral("LATE FINAL")),
                            QStringLiteral("stop")), 350);
    h.controller.askAgent(QStringLiteral("情况如何？"));
    QTRY_VERIFY(h.server.requestCount() == 2);

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

QTEST_GUILESS_MAIN(AgentIntegrationTest)
#include "test_agent_integration.moc"