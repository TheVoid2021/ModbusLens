#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstdint>

#include "ui/ai/ModelScopeDiagnosisClient.h" // config + shared error taxonomy

// T012 Part B Phase 1 — native tool-calling provider client (app layer).
//
// Deliberately NOT a generalization of ModelScopeDiagnosisClient: the T011
// client is a verified, shipped one-shot pipeline and stays untouched. This
// client shares ONLY the stable value types (ModelScopeClientConfig,
// AiAbortReason, AiDiagnosisErrorCode) and implements the SAME safety
// contract: single QTimer timeout owner (ISSUE-005), same-origin redirects,
// TLS verification ON, abort-reason classification, no token logging.
//
// One `requestRound` call = one Chat Completions POST carrying a full
// message history (system/user/assistant tool_calls/tool) plus the tool
// schemas. The assistant message is returned WHOLE (QJsonObject) so the
// runtime can parse tool_calls; reasoning_content is carried but never
// treated as the answer (T011 contract).
class ModelScopeAgentClient : public QObject
{
    Q_OBJECT

public:
    explicit ModelScopeAgentClient(QObject* parent = nullptr);

    void configure(const ModelScopeClientConfig& config);
    [[nodiscard]] bool isConfigured() const;
    [[nodiscard]] QString modelName() const;

    void requestRound(std::uint64_t runGeneration, const QJsonArray& messages,
                      const QJsonArray& tools);
    void cancel(AiAbortReason reason = AiAbortReason::UserCancel);
    [[nodiscard]] bool isBusy() const;

signals:
    void roundSucceeded(std::uint64_t runGeneration, QJsonObject assistantMessage);
    void roundFailed(std::uint64_t runGeneration, AiDiagnosisErrorCode code,
                     QString sanitizedMessage);

private slots:
    void handleTimeout();
    void handleFinished();

private:
    void fail(std::uint64_t runGeneration, AiDiagnosisErrorCode code,
              const QString& message);

    ModelScopeClientConfig config_;

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QTimer* timeoutTimer_ = nullptr;

    std::uint64_t currentRunGeneration_ = 0;
    bool busy_ = false;
    AiAbortReason abortReason_ = AiAbortReason::None;
};