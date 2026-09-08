#include <QtTest>

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/diagnosis/DiagnosisContext.h"
#include "core/diagnosis/RuleBasedDiagnosis.h"
#include "ui/ai/DiagnosisPromptBuilder.h"
#include "ui/ai/ModelScopeDiagnosisClient.h"

#include "fake_chat_completions_server.h"

using modbuslens::core::DiagnosisContext;
using modbuslens::core::DiagnosisReport;
using modbuslens::core::DiagnosisTransaction;
using modbuslens::core::TransactionAnalysis;
using modbuslens::core::TransactionStatus;
using modbuslens::core::buildDiagnosisContext;
using modbuslens::core::diagnoseTransactions;

namespace {

using ms = std::chrono::milliseconds;

DiagnosisTransaction tx(std::uint8_t address, TransactionStatus status,
                        long long elapsedMs,
                        std::optional<std::uint8_t> exceptionCode = std::nullopt)
{
    return DiagnosisTransaction{
        .deviceAddress = address,
        .functionCode = 0x03,
        .analysis = TransactionAnalysis{
            .status = status,
            .elapsed = ms{elapsedMs},
            .exceptionCode = exceptionCode,
        },
    };
}

DiagnosisContext goldenContext()
{
    const std::vector<DiagnosisTransaction> golden = {
        tx(0x01, TransactionStatus::Success, 25),
        tx(0x01, TransactionStatus::Exception, 18, std::uint8_t{0x02}),
        tx(0x01, TransactionStatus::CrcError, 17),
        tx(0x01, TransactionStatus::Timeout, 1000),
    };
    return buildDiagnosisContext(golden);
}

QByteArray chatCompletionBody(const QByteArray& content,
                              const QByteArray& extraMessageFields = {})
{
    QJsonObject message{{"role", "assistant"}, {"content", QString::fromUtf8(content)}};
    if (!extraMessageFields.isEmpty()) {
        const QJsonObject extra =
            QJsonDocument::fromJson(extraMessageFields).object();
        for (auto it = extra.begin(); it != extra.end(); ++it) {
            message.insert(it.key(), it.value());
        }
    }
    QJsonObject choice{{"message", message}};
    QJsonObject root{{"choices", QJsonArray{QJsonObject{choice}}}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

} // namespace

class AiClientTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- prompt builder ----
    void b01_promptAuthority();
    void b02_promptBounded();

    // ---- client + fake server ----
    void b03_requestShape();
    void b04_parseSuccess();
    void b05_choiceNotFirst();
    void b06_invalidResponse();
    void b07_unauthorized();
    void b08_rateLimited();
    void b09_serverError();
    void b10_reasoningIgnored();
    void b11_cancel();
    void b12_timeout();
    void b13_requestIdentityGuard();
};

void AiClientTest::b01_promptAuthority()
{
    const DiagnosisContext context = goldenContext();
    const DiagnosisReport report = diagnoseTransactions(context);
    const DiagnosisPrompt prompt = buildDiagnosisPrompt(context, report);

    // Authority instructions present.
    const QString system = prompt.systemInstructions;
    QVERIFY(system.contains(QStringLiteral("authoritative")));
    QVERIFY(system.contains(QStringLiteral("Do not recalculate")));
    QVERIFY(system.contains(QStringLiteral("contradict")));
    QVERIFY(system.contains(QStringLiteral("root cause")));
    QVERIFY(system.contains(QStringLiteral("plain-text")));

    // Deterministic facts present, incl. the exception code.
    const QString user = prompt.userPrompt;
    QVERIFY(user.contains(QStringLiteral("success_rate")));
    QVERIFY(user.contains(QStringLiteral("0.25")));
    QVERIFY(user.contains(QStringLiteral("exception_code=0x02")));
    QVERIFY(user.contains(QStringLiteral("crc_error")));
    QVERIFY(user.contains(QStringLiteral("timeout")));

    // NOT present: raw wire, filenames, per-byte hex of the golden frames,
    // or anything presentation-shaped.
    QVERIFY(!user.contains(QStringLiteral("C4 0B")));
    QVERIFY(!user.contains(QStringLiteral("demo_v1")));
    QVERIFY(!user.contains(QStringLiteral("QML")));
}

void AiClientTest::b02_promptBounded()
{
    // 30 transactions: 5 Success first, then 25 CrcError. Non-Success are
    // selected first, so the detail must contain exactly 20 CRC entries and
    // NO Success entry despite successes existing in the batch.
    std::vector<DiagnosisTransaction> batch;
    for (int i = 0; i < 5; ++i) {
        batch.push_back(tx(0x01, TransactionStatus::Success, 20 + i));
    }
    for (int i = 0; i < 25; ++i) {
        batch.push_back(tx(0x01, TransactionStatus::CrcError, 10 + i));
    }
    const DiagnosisContext context = buildDiagnosisContext(batch);
    const DiagnosisPrompt prompt = buildDiagnosisPrompt(context, diagnoseTransactions(context));
    const QString user = prompt.userPrompt;

    QVERIFY(user.contains(QStringLiteral("total_transactions=30")));
    QVERIFY(user.contains(QStringLiteral("detailed_transactions=20")));
    QVERIFY(user.contains(QStringLiteral("details_truncated=true")));
    QCOMPARE(user.count(QStringLiteral("status=CrcError")), 20);
    QCOMPARE(user.count(QStringLiteral("status=Success")), 0);
    // Statistics still describe the WHOLE batch.
    QVERIFY(user.contains(QStringLiteral("crc_error=25")));
    QVERIFY(user.contains(QStringLiteral("observed=30")));

    // Second shape: non-Success under the cap is padded with Success by
    // original order.
    std::vector<DiagnosisTransaction> small;
    for (int i = 0; i < 3; ++i) {
        small.push_back(tx(0x01, TransactionStatus::Timeout, 900 + i));
    }
    for (int i = 0; i < 30; ++i) {
        small.push_back(tx(0x01, TransactionStatus::Success, 5 + i));
    }
    const DiagnosisPrompt prompt2 = buildDiagnosisPrompt(
        buildDiagnosisContext(small),
        diagnoseTransactions(buildDiagnosisContext(small)));
    QVERIFY(prompt2.userPrompt.contains(QStringLiteral("details_truncated=true")));
    QCOMPARE(prompt2.userPrompt.count(QStringLiteral("status=Timeout")), 3);
    QCOMPARE(prompt2.userPrompt.count(QStringLiteral("status=Success")), 17);
}

void AiClientTest::b03_requestShape()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, chatCompletionBody("ok"));

    ModelScopeDiagnosisClient client;
    ModelScopeClientConfig config;
    config.endpoint = QUrl(server.chatCompletionsUrl());
    config.apiKey = QStringLiteral("fake-test-token");
    config.modelId = QStringLiteral("fake-model");
    config.timeout = ms{5000};
    client.configure(config);

    QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisSucceeded);
    client.requestDiagnosis(QStringLiteral("system-message"),
                            QStringLiteral("user-message"), 1);
    QVERIFY(spy.wait(3000));

    const FakeChatCompletionsServer::CapturedRequest request = server.lastRequest();
    QCOMPARE(request.method, QByteArray("POST"));
    QCOMPARE(request.path, QStringLiteral("/chat/completions"));
    QCOMPARE(request.authorization, QByteArray("Bearer fake-test-token"));
    QVERIFY(request.contentType.startsWith("application/json"));

    const QJsonObject body = QJsonDocument::fromJson(request.body).object();
    QCOMPARE(body.value("model").toString(), QStringLiteral("fake-model"));
    QCOMPARE(body.value("stream").toBool(), false);
    QCOMPARE(body.value("max_tokens").toInt(), 768);
    const QJsonArray messages = body.value("messages").toArray();
    QCOMPARE(messages.size(), 2);
    QCOMPARE(messages.at(0).toObject().value("role").toString(), QStringLiteral("system"));
    QCOMPARE(messages.at(0).toObject().value("content").toString(), QStringLiteral("system-message"));
    QCOMPARE(messages.at(1).toObject().value("role").toString(), QStringLiteral("user"));
    // Forbidden fields absent.
    for (const char* forbidden : {"tools", "tool_choice", "functions", "history", "web_search"}) {
        QVERIFY2(!body.contains(QLatin1String(forbidden)), forbidden);
    }
}

void AiClientTest::b04_parseSuccess()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    const QByteArray payload = chatCompletionBody("Final explanation");
    server.setNextResponse(200, payload);

    ModelScopeDiagnosisClient client;
    ModelScopeClientConfig config{QUrl(server.chatCompletionsUrl()),
                                  QStringLiteral("fake-test-token"),
                                  QStringLiteral("fake-model"), ms{5000}};
    client.configure(config);

    QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisSucceeded);
    bool failed = false;
    connect(&client, &ModelScopeDiagnosisClient::diagnosisFailed, this,
            [&](std::uint64_t, AiDiagnosisErrorCode, const QString&) { failed = true; });

    client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 7);
    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toULongLong(), static_cast<unsigned long long>(7));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Final explanation"));
    QVERIFY(!failed);
    QVERIFY(!client.isBusy());
}

void AiClientTest::b05_choiceNotFirst()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    // choices[0]: a reason-only entry without usable content; choices[1]:
    // the real answer. The parser must walk to the first USABLE choice.
    QJsonObject emptyMessage{{"role", "assistant"}, {"reasoning_content", "hidden"}};
    QJsonObject goodMessage{{"role", "assistant"}, {"content", "The real answer"}};
    QJsonObject root{{"choices",
                      QJsonArray{QJsonObject{{"message", emptyMessage}},
                                 QJsonObject{{"message", goodMessage}}}}};
    server.setNextResponse(200, QJsonDocument(root).toJson(QJsonDocument::Compact));

    ModelScopeDiagnosisClient client;
    client.configure({QUrl(server.chatCompletionsUrl()),
                      QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{5000}});
    QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisSucceeded);
    client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 1);
    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("The real answer"));
}

void AiClientTest::b06_invalidResponse()
{
    // 200 + malformed JSON, and 200 + empty choices: both InvalidResponse.
    for (const QByteArray& payload : {QByteArray("this is not json"),
                                      QByteArray("{\"choices\":[]}"),
                                      QByteArray("{\"choices\":[{\"message\":{}}]}")}) {
        FakeChatCompletionsServer server;
        QVERIFY(server.start());
        server.setNextResponse(200, payload);

        ModelScopeDiagnosisClient client;
        client.configure({QUrl(server.chatCompletionsUrl()),
                          QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{5000}});
        QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisFailed);
        client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 1);
        QVERIFY(spy.wait(3000));
        bool sawInvalid = false;
        for (int i = 0; i < spy.count(); ++i) {
            const auto code = spy.at(i).at(1).toInt();
            if (code == static_cast<int>(AiDiagnosisErrorCode::InvalidResponse)) {
                sawInvalid = true;
            }
        }
        QVERIFY(sawInvalid);
    }
}

void AiClientTest::b07_unauthorized()
{
    for (const int status : {401, 403}) {
        FakeChatCompletionsServer server;
        QVERIFY(server.start());
        server.setNextResponse(status, QByteArray("{\"error\":{\"message\":\"denied\"}}"));

        ModelScopeDiagnosisClient client;
        client.configure({QUrl(server.chatCompletionsUrl()),
                          QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{5000}});
        QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisFailed);
        client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 1);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.at(0).at(1).toInt(),
                 static_cast<int>(AiDiagnosisErrorCode::Unauthorized));
    }
}

void AiClientTest::b08_rateLimited()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(429, QByteArray("{\"error\":{\"message\":\"slow down\"}}"));

    ModelScopeDiagnosisClient client;
    client.configure({QUrl(server.chatCompletionsUrl()),
                      QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{5000}});
    QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisFailed);
    client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 1);
    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.at(0).at(1).toInt(),
             static_cast<int>(AiDiagnosisErrorCode::RateLimited));
    // Zero automatic retries: exactly one physical request.
    QCOMPARE(server.requestCount(), 1);
}

void AiClientTest::b09_serverError()
{
    for (const int status : {500, 503}) {
        FakeChatCompletionsServer server;
        QVERIFY(server.start());
        server.setNextResponse(status, QByteArray());

        ModelScopeDiagnosisClient client;
        client.configure({QUrl(server.chatCompletionsUrl()),
                          QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{5000}});
        QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisFailed);
        client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 1);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.at(0).at(1).toInt(),
                 static_cast<int>(AiDiagnosisErrorCode::ServerError));
        QCOMPARE(server.requestCount(), 1);
    }
}

void AiClientTest::b10_reasoningIgnored()
{
    // reasoning_content + content: only content surfaces.
    {
        FakeChatCompletionsServer server;
        QVERIFY(server.start());
        const QByteArray payload = chatCompletionBody(
            "Final explanation",
            QByteArray("{\"reasoning_content\":\"hidden reasoning\"}"));
        server.setNextResponse(200, payload);

        ModelScopeDiagnosisClient client;
        client.configure({QUrl(server.chatCompletionsUrl()),
                          QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{5000}});
        QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisSucceeded);
        client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 1);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("Final explanation"));
        QVERIFY(!spy.at(0).at(1).toString().contains(QStringLiteral("hidden reasoning")));
    }
    // only reasoning_content, no content: InvalidResponse.
    {
        FakeChatCompletionsServer server;
        QVERIFY(server.start());
        QJsonObject message{{"role", "assistant"}, {"reasoning_content", "hidden only"}};
        QJsonObject root{{"choices", QJsonArray{QJsonObject{{"message", message}}}}};
        server.setNextResponse(200, QJsonDocument(root).toJson(QJsonDocument::Compact));

        ModelScopeDiagnosisClient client;
        client.configure({QUrl(server.chatCompletionsUrl()),
                          QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{5000}});
        QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisFailed);
        client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 1);
        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.at(0).at(1).toInt(),
                 static_cast<int>(AiDiagnosisErrorCode::InvalidResponse));
    }
}

void AiClientTest::b11_cancel()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, chatCompletionBody("too late"), 300);

    ModelScopeDiagnosisClient client;
    client.configure({QUrl(server.chatCompletionsUrl()),
                      QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{5000}});
    QSignalSpy successSpy(&client, &ModelScopeDiagnosisClient::diagnosisSucceeded);
    QSignalSpy failSpy(&client, &ModelScopeDiagnosisClient::diagnosisFailed);

    client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 1);
    QVERIFY(client.isBusy());
    QTest::qWait(50);
    client.cancel();

    QVERIFY(!client.isBusy());
    QTest::qWait(500); // let any late delivery try (and fail) to arrive
    QCOMPARE(successSpy.count(), 0);
    QCOMPARE(failSpy.count(), 0); // cancel is silent — not a diagnosis failure
}

void AiClientTest::b12_timeout()
{
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, chatCompletionBody("late"), 300);

    ModelScopeDiagnosisClient client;
    client.configure({QUrl(server.chatCompletionsUrl()),
                      QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{80}});
    QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisFailed);
    client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 1);
    QVERIFY(spy.wait(3000));
    QCOMPARE(spy.at(0).at(1).toInt(),
             static_cast<int>(AiDiagnosisErrorCode::Timeout));
    // ISSUE-005: business wording — never Qt's localized operation-canceled
    // errorString.
    QCOMPARE(spy.at(0).at(2).toString(), QStringLiteral("AI request timed out"));
    QVERIFY(!client.isBusy());
}

void AiClientTest::b13_requestIdentityGuard()
{
    // Same batch: request #1 -> cancel -> request #2; #1's late response must
    // never be able to overwrite #2's outcome (client echoes request ids).
    FakeChatCompletionsServer server;
    QVERIFY(server.start());
    server.setNextResponse(200, chatCompletionBody("OLD RESPONSE"), 300);

    ModelScopeDiagnosisClient client;
    client.configure({QUrl(server.chatCompletionsUrl()),
                      QStringLiteral("fake-test-token"), QStringLiteral("fake-model"), ms{5000}});
    QSignalSpy spy(&client, &ModelScopeDiagnosisClient::diagnosisSucceeded);

    client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 10);
    QTest::qWait(50);
    client.cancel();

    server.setNextResponse(200, chatCompletionBody("NEW RESPONSE"));
    client.requestDiagnosis(QStringLiteral("s"), QStringLiteral("u"), 11);
    QVERIFY(spy.wait(3000));
    QTest::qWait(400); // allow the stale #1 delivery window to pass

    // Only request #2's response may surface.
    bool sawNew = false;
    for (int i = 0; i < spy.count(); ++i) {
        const auto id = spy.at(i).at(0).toULongLong();
        const QString text = spy.at(i).at(1).toString();
        if (id == 11 && text == QStringLiteral("NEW RESPONSE")) {
            sawNew = true;
        }
        QVERIFY2(id != 10, "stale request #1 surfaced");
        QVERIFY2(text != QStringLiteral("OLD RESPONSE"), "stale text surfaced");
    }
    QVERIFY(sawNew);
}

QTEST_GUILESS_MAIN(AiClientTest)
#include "test_ai_client.moc"