#pragma once

#include <QJsonArray>
#include <QObject>
#include <QString>

#include <cstdint>

#include "ui/agent/AgentPromptBuilder.h"
#include "ui/agent/AgentToolContext.h"
#include "ui/agent/ModelScopeAgentClient.h"
#include "ui/agent/AgentTools.h"
#include "ui/ai/ModelScopeDiagnosisClient.h" // AiDiagnosisErrorCode

// T012 Part B Phase 1 — bounded native tool-calling agent runtime (FSM).
//
// One AgentRun = one independent user question:
//   UserQuestion -> [system+user] -> model -> final | tool_calls
//   -> validate ALL calls -> execute on the IMMUTABLE context snapshot
//   -> role=tool results -> model -> ... until final content or a limit.
//
// Runtime owns NO network: the injected ModelScopeAgentClient performs the
// POSTs; the runtime owns the message history, the loop, limits, validation
// orchestration and the stale guards. No memory/conversation state survives
// a run.

namespace modbuslens::agent {

// Local (deterministic) run errors. Provider/network outcomes reuse the
// T011 AiDiagnosisErrorCode taxonomy — no second HTTP error hierarchy.
enum class AgentRunLocalError {
    InvalidQuestion,
    MalformedToolCall,
    UnknownTool,
    InvalidArguments,
    TransactionNotFound,
    ToolRoundLimitExceeded,
    ToolCallLimitExceeded,
};

struct AgentRunFailure {
    bool providerError{false};     // false -> localError, true -> providerCode
    AgentRunLocalError localError{AgentRunLocalError::InvalidQuestion};
    AiDiagnosisErrorCode providerCode{AiDiagnosisErrorCode::NetworkError};
    QString message;               // sanitized user-facing text

    static AgentRunFailure fromLocal(AgentRunLocalError code, QString message);
    static AgentRunFailure fromProvider(AiDiagnosisErrorCode code, QString message);
};

struct AgentRunRequest {
    QString userQuestion;
    AgentToolContext context;      // immutable per-run snapshot (self-consistent)
    // The context IS the full snapshot identity — including which batch it
    // belongs to. There is deliberately NO second revision field here (Phase
    // 1 review fix): a run can never be told a revision that disagrees with
    // its facts.
    std::uint64_t runGeneration{};
};

// Bounds (Learning TD-5 + Phase 1 spec §5/§11).
constexpr int kMaxAgentQuestionChars = 1000;
constexpr int kMaxAgentToolRounds = 3;
constexpr int kMaxAgentTotalToolCalls = 3;

} // namespace modbuslens::agent

class AgentRuntime : public QObject
{
    Q_OBJECT

public:
    explicit AgentRuntime(ModelScopeAgentClient* client,
                          QObject* parent = nullptr);

    // Starts a NEW independent run. If another run is in flight this
    // SUPERSEDES it (the old run is invalidated and aborted; only the new
    // run may ever publish) — the "same batch, newer run wins" contract.
    void start(const modbuslens::agent::AgentRunRequest& request);

    // User cancel: aborts the reply, invalidates the generation, clears
    // busy. Silent by design (Cancelled is not an error; a late response
    // can never write the answer afterwards).
    void cancel();

    // Batch invalidation seam (Phase 2): the live active batch changed, so
    // the in-flight run is already answering the WRONG batch. No-op when
    // idle; otherwise invalidate the generation BEFORE the abort, cancel
    // the client with BatchInvalidated and return to Idle. Deliberately NO
    // user-visible signal (runCancelled/runFailed are not emitted) — batch
    // invalidation is silent, unlike UserCancel.
    void invalidateForBatchChange();

    [[nodiscard]] bool isBusy() const;

    // Phase 2 controller seams. Tests drive them directly; the production
    // controller will mirror its activeBatchRevision / agent generation
    // here. Every provider delivery is consumed ONLY when it still matches
    // BOTH seams — otherwise it is discarded silently (stale).
    void setCurrentBatchRevision(std::uint64_t revision);
    void setCurrentAgentGeneration(std::uint64_t generation);

signals:
    void runCompleted(std::uint64_t runGeneration, QString finalAnswer);
    void runFailed(std::uint64_t runGeneration,
                   modbuslens::agent::AgentRunFailure failure);
    void runCancelled(std::uint64_t runGeneration);

private slots:
    void handleRoundSucceeded(std::uint64_t runGeneration,
                              QJsonObject assistantMessage);
    void handleRoundFailed(std::uint64_t runGeneration,
                           AiDiagnosisErrorCode code, QString sanitizedMessage);

private:
    enum class State {
        Idle,
        WaitingForModel,
        ExecutingTools,
        Completed,
        Failed,
    };

    bool isStale(std::uint64_t runGeneration) const;
    void failLocal(modbuslens::agent::AgentRunLocalError code, QString message);
    void failProvider(AiDiagnosisErrorCode code, QString message);
    void sendNextRequest();
    void finishCompleted(const QString& finalAnswer);

    ModelScopeAgentClient* client_ = nullptr;

    modbuslens::agent::AgentPrompt prompt_;
    State state_ = State::Idle;

    std::uint64_t runGeneration_ = 0;
    std::uint64_t capturedBatchRevision_ = 0;
    int toolRounds_ = 0;
    int totalToolCalls_ = 0;

    // Phase 2 seams (current truth):
    std::uint64_t currentBatchRevision_ = 0;
    std::uint64_t currentAgentGeneration_ = 0;

    QJsonArray messages_; // run-local conversation (no memory beyond run)
    modbuslens::agent::AgentToolContext context_;
};

Q_DECLARE_METATYPE(modbuslens::agent::AgentRunFailure)