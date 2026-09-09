#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/analysis/TransactionAnalysis.h"
#include "core/analysis/TransactionStatistics.h"
#include "core/diagnosis/DiagnosisContext.h"
#include "ui/agent/AgentToolContext.h"
#include "ui/agent/AgentTools.h"

namespace {

using namespace modbuslens;
using ms = std::chrono::milliseconds;

core::TransactionAnalysis makeAnalysis(core::TransactionStatus status,
                                       long long elapsedMs,
                                       std::optional<std::uint8_t> exceptionCode = std::nullopt)
{
    return core::TransactionAnalysis{
        .status = status,
        .elapsed = ms{elapsedMs},
        .exceptionCode = exceptionCode,
    };
}

core::DiagnosisTransaction tx(std::uint8_t address, core::TransactionStatus status,
                              long long elapsedMs,
                              std::optional<std::uint8_t> exceptionCode = std::nullopt)
{
    return core::DiagnosisTransaction{
        .deviceAddress = address,
        .functionCode = 0x03,
        .analysis = makeAnalysis(status, elapsedMs, exceptionCode),
    };
}

// 1 Success + 1 CRC + 1 Timeout + 1 Exception 0x02 (the golden mixed batch).
agent::AgentToolContext goldenContext()
{
    const std::vector<core::DiagnosisTransaction> golden = {
        tx(0x01, core::TransactionStatus::Success, 25),
        tx(0x01, core::TransactionStatus::CrcError, 17),
        tx(0x01, core::TransactionStatus::Timeout, 1000),
        tx(0x01, core::TransactionStatus::Exception, 18, std::uint8_t{0x02}),
    };
    const auto context = core::buildDiagnosisContext(golden);
    return agent::AgentToolContext{
        .transactions = context.transactions,
        .statistics = context.statistics,
        .capturedBatchRevision = 7,
    };
}

QByteArray compactJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

class AgentToolsTest : public QObject
{
    Q_OBJECT

private slots:
    void a01_sessionSummaryGolden();
    void a02_recentAnomaliesOnlyFailuresAndLatest20();
    void a03_transactionDetailByNumber();
    void a04_transactionNotFound();
    void a05_unknownToolRejected();
    void a06_invalidArgumentsRejected();
    void a07_readOnlyWhitelistEnforced();
    void a08_deterministicRepeatedExecution();
    void a09_snapshotIsolation();
};

void AgentToolsTest::a01_sessionSummaryGolden()
{
    const agent::AgentToolContext context = goldenContext();
    const agent::AgentToolResult result = agent::dispatchAgentTool(
        context, "get_session_summary", QJsonObject{});

    const auto* summary = std::get_if<agent::SessionSummaryResult>(&result);
    QVERIFY(summary != nullptr);
    QCOMPARE(summary->observedCount, std::size_t{4});
    QCOMPARE(summary->completedCount, std::size_t{4});
    QCOMPARE(summary->pendingCount, std::size_t{0});
    QCOMPARE(summary->successCount, std::size_t{1});
    QCOMPARE(summary->crcErrorCount, std::size_t{1});
    QCOMPARE(summary->timeoutCount, std::size_t{1});
    QCOMPARE(summary->exceptionCount, std::size_t{1});
    QCOMPARE(summary->protocolErrorCount, std::size_t{0});
    QCOMPARE(summary->transactionCount, std::size_t{4});
    QVERIFY(summary->successRate.has_value());
    QVERIFY(qFuzzyCompare(*summary->successRate, 0.25));
    QVERIFY(summary->averageSuccessLatencyMs.has_value());
    QVERIFY(qFuzzyCompare(*summary->averageSuccessLatencyMs, 25.0));

    const QJsonObject json = agent::toJsonObject(*summary);
    QCOMPARE(json.value("observed_count").toInt(), 4);
    QCOMPARE(json.value("transaction_count").toInt(), 4);
    QVERIFY(json.contains("success_rate"));
    QCOMPARE(json.value("success_rate").toDouble(), 0.25);
    QVERIFY(json.contains("average_success_latency_ms"));
    QCOMPARE(json.value("evidence_scope").toString(),
             QStringLiteral("current_observed_batch"));
    QVERIFY(!json.contains("root_cause")); // tool outputs NO judgement

    // Empty batch: optional rate/latency must be ABSENT, never a fake 0.
    const auto emptyCore = core::buildDiagnosisContext({});
    const agent::AgentToolContext emptyCtx{
        .transactions = {}, .statistics = emptyCore.statistics, .capturedBatchRevision = 0};
    const auto emptyResult = agent::dispatchAgentTool(
        emptyCtx, "get_session_summary", QJsonObject{});
    const auto* empty = std::get_if<agent::SessionSummaryResult>(&emptyResult);
    QVERIFY(empty != nullptr);
    QVERIFY(!empty->successRate.has_value());
    QVERIFY(!empty->averageSuccessLatencyMs.has_value());
    const QJsonObject emptyJson = agent::toJsonObject(*empty);
    QVERIFY(!emptyJson.contains("success_rate"));
    QVERIFY(!emptyJson.contains("average_success_latency_ms"));
    QCOMPARE(emptyJson.value("transaction_count").toInt(), 0);
}

void AgentToolsTest::a02_recentAnomaliesOnlyFailuresAndLatest20()
{
    // Golden: 3 anomalies in ORIGINAL order, exception_code only where set.
    const agent::AgentToolContext context = goldenContext();
    const auto result = agent::dispatchAgentTool(
        context, "get_recent_anomalies", QJsonObject{});
    const auto* anomalies = std::get_if<agent::RecentAnomaliesResult>(&result);
    QVERIFY(anomalies != nullptr);
    QCOMPARE(anomalies->totalAnomalyCount, std::size_t{3});
    QVERIFY(!anomalies->truncated);
    QCOMPARE(anomalies->entries.size(), std::size_t{3});
    QCOMPARE(anomalies->entries[0].transactionNumber, std::size_t{2});
    QCOMPARE(anomalies->entries[0].status, core::TransactionStatus::CrcError);
    QCOMPARE(anomalies->entries[1].transactionNumber, std::size_t{3});
    QCOMPARE(anomalies->entries[1].status, core::TransactionStatus::Timeout);
    QVERIFY(!anomalies->entries[1].exceptionCode.has_value());
    QCOMPARE(anomalies->entries[2].transactionNumber, std::size_t{4});
    QCOMPARE(anomalies->entries[2].status, core::TransactionStatus::Exception);
    QVERIFY(anomalies->entries[2].exceptionCode.has_value());
    QCOMPARE(*anomalies->entries[2].exceptionCode, std::uint8_t{0x02});

    const QJsonObject json = agent::toJsonObject(*anomalies);
    QCOMPARE(json.value("total_anomaly_count").toInt(), 3);
    QCOMPARE(json.value("returned_count").toInt(), 3);
    QCOMPARE(json.value("truncated").toBool(), false);
    QCOMPARE(json.value("evidence_scope").toString(),
             QStringLiteral("current_observed_batch"));

    // > 20 anomalies: ONLY the latest 20, keeping ORIGINAL order.
    // Batch: 1 Success + 29 CrcError -> 29 anomalies -> latest 20 = #11..30.
    std::vector<core::DiagnosisTransaction> batch;
    batch.push_back(tx(0x01, core::TransactionStatus::Success, 10));
    for (int i = 0; i < 29; ++i) {
        batch.push_back(tx(0x01, core::TransactionStatus::CrcError, 20 + i));
    }
    const auto bigCore = core::buildDiagnosisContext(batch);
    const agent::AgentToolContext bigCtx{
        .transactions = bigCore.transactions,
        .statistics = bigCore.statistics,
        .capturedBatchRevision = 1};
    const auto bigResult = agent::dispatchAgentTool(
        bigCtx, "get_recent_anomalies", QJsonObject{});
    const auto* big = std::get_if<agent::RecentAnomaliesResult>(&bigResult);
    QVERIFY(big != nullptr);
    QCOMPARE(big->totalAnomalyCount, std::size_t{29});
    QVERIFY(big->truncated);
    QCOMPARE(big->entries.size(), std::size_t{20});
    QCOMPARE(big->entries.front().transactionNumber, std::size_t{11});
    QCOMPARE(big->entries.back().transactionNumber, std::size_t{30});
    for (std::size_t i = 1; i < big->entries.size(); ++i) {
        QVERIFY(big->entries[i].transactionNumber
                > big->entries[i - 1].transactionNumber); // original order
    }
    const QJsonObject bigJson = agent::toJsonObject(*big);
    QCOMPARE(bigJson.value("total_anomaly_count").toInt(), 29);
    QCOMPARE(bigJson.value("returned_count").toInt(), 20);
    QCOMPARE(bigJson.value("truncated").toBool(), true);
}

void AgentToolsTest::a03_transactionDetailByNumber()
{
    const agent::AgentToolContext context = goldenContext();

    // transaction #4 = Exception 0x02.
    const auto result = agent::dispatchAgentTool(
        context, "get_transaction_detail",
        QJsonObject{{QStringLiteral("transaction_number"), 4}});
    const auto* detail = std::get_if<agent::TransactionDetailResult>(&result);
    QVERIFY(detail != nullptr);
    QCOMPARE(detail->transactionNumber, std::size_t{4});
    QCOMPARE(detail->deviceAddress, std::uint8_t{1});
    QCOMPARE(detail->functionCode, std::uint8_t{3});
    QCOMPARE(detail->status, core::TransactionStatus::Exception);
    QCOMPARE(detail->elapsedMs, 18LL);
    QVERIFY(detail->exceptionCode.has_value());
    QCOMPARE(*detail->exceptionCode, std::uint8_t{0x02});

    const QJsonObject json = agent::toJsonObject(*detail);
    QCOMPARE(json.value("transaction_number").toInt(), 4);
    QCOMPARE(json.value("status").toString(), QStringLiteral("Exception"));
    QCOMPARE(json.value("elapsed_ms").toInt(), 18);
    QCOMPARE(json.value("exception_code").toInt(), 2);
    QCOMPARE(json.value("exception_name").toString(),
             QStringLiteral("Illegal Data Address")); // structured, no prose
    QCOMPARE(json.value("evidence_scope").toString(),
             QStringLiteral("current_observed_batch"));

    // transaction #1 = Success: no exception fields at all.
    const auto result1 = agent::dispatchAgentTool(
        context, "get_transaction_detail",
        QJsonObject{{QStringLiteral("transaction_number"), 1}});
    const auto* detail1 = std::get_if<agent::TransactionDetailResult>(&result1);
    QVERIFY(detail1 != nullptr);
    QCOMPARE(detail1->status, core::TransactionStatus::Success);
    QVERIFY(!detail1->exceptionCode.has_value());
    const QJsonObject json1 = agent::toJsonObject(*detail1);
    QVERIFY(!json1.contains("exception_code"));
    QVERIFY(!json1.contains("exception_name"));
}

void AgentToolsTest::a04_transactionNotFound()
{
    const agent::AgentToolContext context = goldenContext();
    for (const int number : {0, 5, 999}) {
        const auto result = agent::dispatchAgentTool(
            context, "get_transaction_detail",
            QJsonObject{{QStringLiteral("transaction_number"), number}});
        const auto* error = std::get_if<agent::AgentToolError>(&result);
        QVERIFY2(error != nullptr, qPrintable(QString::number(number)));
        QCOMPARE(error->code, agent::AgentToolErrorCode::TransactionNotFound);
    }
}

void AgentToolsTest::a05_unknownToolRejected()
{
    const agent::AgentToolContext context = goldenContext();
    for (const char* name : {"write_register", "send_modbus_request", "shell",
                             "get_transaction_detail_extra", "nope"}) {
        const auto result = agent::dispatchAgentTool(
            context, name, QJsonObject{});
        const auto* error = std::get_if<agent::AgentToolError>(&result);
        QVERIFY2(error != nullptr, name);
        QCOMPARE(error->code, agent::AgentToolErrorCode::UnknownTool);
    }
}

void AgentToolsTest::a06_invalidArgumentsRejected()
{
    const agent::AgentToolContext context = goldenContext();

    // Missing required argument.
    {
        const auto result = agent::dispatchAgentTool(
            context, "get_transaction_detail", QJsonObject{});
        const auto* error = std::get_if<agent::AgentToolError>(&result);
        QVERIFY(error != nullptr);
        QCOMPARE(error->code, agent::AgentToolErrorCode::InvalidArguments);
    }
    // Unknown fields on the parameterless tools.
    {
        const auto result = agent::dispatchAgentTool(
            context, "get_session_summary",
            QJsonObject{{QStringLiteral("limit"), 5}});
        const auto* error = std::get_if<agent::AgentToolError>(&result);
        QVERIFY(error != nullptr);
        QCOMPARE(error->code, agent::AgentToolErrorCode::InvalidArguments);
    }
    {
        const auto result = agent::dispatchAgentTool(
            context, "get_recent_anomalies",
            QJsonObject{{QStringLiteral("max"), 1}});
        const auto* error = std::get_if<agent::AgentToolError>(&result);
        QVERIFY(error != nullptr);
        QCOMPARE(error->code, agent::AgentToolErrorCode::InvalidArguments);
    }
    // Unknown field beside a valid one.
    {
        const auto result = agent::dispatchAgentTool(
            context, "get_transaction_detail",
            QJsonObject{{QStringLiteral("transaction_number"), 1},
                        {QStringLiteral("extra"), 2}});
        const auto* error = std::get_if<agent::AgentToolError>(&result);
        QVERIFY(error != nullptr);
        QCOMPARE(error->code, agent::AgentToolErrorCode::InvalidArguments);
    }
    // Wrong type (string instead of number).
    {
        const auto result = agent::dispatchAgentTool(
            context, "get_transaction_detail",
            QJsonObject{{QStringLiteral("transaction_number"),
                         QStringLiteral("4")}});
        const auto* error = std::get_if<agent::AgentToolError>(&result);
        QVERIFY(error != nullptr);
        QCOMPARE(error->code, agent::AgentToolErrorCode::InvalidArguments);
    }
    // Non-integer number.
    {
        const auto result = agent::dispatchAgentTool(
            context, "get_transaction_detail",
            QJsonObject{{QStringLiteral("transaction_number"), 1.5}});
        const auto* error = std::get_if<agent::AgentToolError>(&result);
        QVERIFY(error != nullptr);
        QCOMPARE(error->code, agent::AgentToolErrorCode::InvalidArguments);
    }
}

void AgentToolsTest::a07_readOnlyWhitelistEnforced()
{
    // Exact-name mapping: exactly the three whitelisted names, nothing else.
    QCOMPARE(agent::agentToolNameFromString("get_session_summary"),
             std::optional{agent::AgentToolName::GetSessionSummary});
    QCOMPARE(agent::agentToolNameFromString("get_recent_anomalies"),
             std::optional{agent::AgentToolName::GetRecentAnomalies});
    QCOMPARE(agent::agentToolNameFromString("get_transaction_detail"),
             std::optional{agent::AgentToolName::GetTransactionDetail});

    // Every write/control capability the product will ever NOT have: the
    // whitelist has no branch for them by construction.
    for (const char* name : {"write_register", "write_coil", "send_modbus_request",
                             "reconnect_serial", "change_serial_settings",
                             "open_serial_port", "close_serial_port", "delete_log",
                             "modify_file", "run_shell", "web_search"}) {
        QVERIFY2(!agent::agentToolNameFromString(name).has_value(), name);
    }

    const agent::AgentToolContext context = goldenContext();
    const auto result = agent::dispatchAgentTool(
        context, "reconnect_serial", QJsonObject{});
    const auto* error = std::get_if<agent::AgentToolError>(&result);
    QVERIFY(error != nullptr);
    QCOMPARE(error->code, agent::AgentToolErrorCode::UnknownTool);
}

void AgentToolsTest::a08_deterministicRepeatedExecution()
{
    const agent::AgentToolContext context = goldenContext();

    const auto jsonOf = [&context](const char* tool, const QJsonObject& args) {
        const auto result = agent::dispatchAgentTool(context, tool, args);
        return std::visit(
            [](const auto& value) -> QByteArray {
                return compactJson(agent::toJsonObject(value));
            },
            result);
    };

    const QByteArray s1 = jsonOf("get_session_summary", {});
    const QByteArray s2 = jsonOf("get_session_summary", {});
    QCOMPARE(s1, s2);

    const QByteArray a1 = jsonOf("get_recent_anomalies", {});
    const QByteArray a2 = jsonOf("get_recent_anomalies", {});
    QCOMPARE(a1, a2);

    const QJsonObject detailArgs{{QStringLiteral("transaction_number"), 4}};
    const QByteArray d1 = jsonOf("get_transaction_detail", detailArgs);
    const QByteArray d2 = jsonOf("get_transaction_detail", detailArgs);
    QCOMPARE(d1, d2);

    // Same facts -> byte-identical JSON (QJsonObject sorts keys, so this is
    // a strict serialization determinism check).
    QVERIFY(!s1.isEmpty());
    QVERIFY(s1 != d1);
}

void AgentToolsTest::a09_snapshotIsolation()
{
    const agent::AgentToolContext contextA = goldenContext(); // 4 tx, mixed
    const auto summaryA = agent::dispatchAgentTool(
        contextA, "get_session_summary", QJsonObject{});
    const auto detailA = agent::dispatchAgentTool(
        contextA, "get_transaction_detail",
        QJsonObject{{QStringLiteral("transaction_number"), 4}});
    const QByteArray summaryABytes = std::visit(
        [](const auto& value) { return compactJson(agent::toJsonObject(value)); },
        summaryA);
    const QByteArray detailABytes = std::visit(
        [](const auto& value) { return compactJson(agent::toJsonObject(value)); },
        detailA);

    // A completely different, newer batch exists "elsewhere" — but the
    // dispatcher was given a snapshot: it must NOT see or be affected by it.
    const std::vector<core::DiagnosisTransaction> otherBatch = {
        tx(0x05, core::TransactionStatus::Success, 9),
    };
    const auto otherCore = core::buildDiagnosisContext(otherBatch);
    const agent::AgentToolContext contextB{
        .transactions = otherCore.transactions,
        .statistics = otherCore.statistics,
        .capturedBatchRevision = 999};

    const auto summaryB = agent::dispatchAgentTool(
        contextB, "get_session_summary", QJsonObject{});
    const QByteArray summaryBBytes = std::visit(
        [](const auto& value) { return compactJson(agent::toJsonObject(value)); },
        summaryB);
    QVERIFY(summaryBBytes != summaryABytes); // B really is different

    // ... and a second dispatch on the SAME snapshot A still yields A's
    // facts, byte-identical — no hidden global/live state anywhere.
    const QByteArray summaryA2Bytes = std::visit(
        [](const auto& value) { return compactJson(agent::toJsonObject(value)); },
        agent::dispatchAgentTool(contextA, "get_session_summary", QJsonObject{}));
    const QByteArray detailA2Bytes = std::visit(
        [](const auto& value) { return compactJson(agent::toJsonObject(value)); },
        agent::dispatchAgentTool(
            contextA, "get_transaction_detail",
            QJsonObject{{QStringLiteral("transaction_number"), 4}}));
    QCOMPARE(summaryA2Bytes, summaryABytes);
    QCOMPARE(detailA2Bytes, detailABytes);
}

QTEST_GUILESS_MAIN(AgentToolsTest)
#include "test_agent_tools.moc"