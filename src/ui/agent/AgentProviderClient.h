#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <cstdint>
#include <string>

#include "ui/ai/AiProviderProfile.h"
#include "ui/ai/ModelScopeDiagnosisClient.h" // AiAbortReason + AiDiagnosisErrorCode

namespace modbuslens::agent {

// ---------------------------------------------------------------------------
// T034-B2.2-C — the IMMUTABLE per-run provider snapshot.
//
// One Agent run resolves this EXACTLY ONCE at start and every provider step of
// that run reuses the SAME copy. A mid-run global configuration change can
// therefore never make step 2 speak to a different provider/model/credential
// than step 1: the run either keeps its original snapshot or is invalidated —
// it never switches provider half-way.
// ---------------------------------------------------------------------------
struct AgentRunConfig {
    modbuslens::ui::AiProviderKind kind{modbuslens::ui::AiProviderKind::ModelScope};
    std::string endpoint; // from AiProviderProfile
    std::string model;    // from the resolved global snapshot
    std::string secret;   // provider-scoped credential for THIS run
    int timeoutMs{90000};

    [[nodiscard]] bool usable() const
    {
        return !endpoint.empty() && !model.empty() && !secret.empty();
    }
};

} // namespace modbuslens::agent

// ---------------------------------------------------------------------------
// T034-B2.2-C — provider-neutral Agent network seam.
//
// The Agent runtime talks ONLY to this interface, so it owns no provider,
// model, credential or transport knowledge. The response contract is
// unchanged from the accepted Agent design: the assistant MESSAGE OBJECT is
// delivered WHOLE (tool_calls live inside it) and the shared T011 error
// taxonomy is reused — no second HTTP error hierarchy.
//
// There is deliberately NO global single-flight gate on this seam: a
// superseded run and its successor may both have an exchange in flight, and
// staleness is decided by the OWNER (AgentRuntime) via run generation, not by
// a client-side busy flag.
// ---------------------------------------------------------------------------
class AgentProviderClient : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~AgentProviderClient() override = default;

    AgentProviderClient(const AgentProviderClient&) = delete;
    AgentProviderClient& operator=(const AgentProviderClient&) = delete;

    // Starts ONE asynchronous provider round for this run. `config` is the
    // run's immutable snapshot and MUST be reused verbatim by every round of
    // the same run.
    virtual void requestRound(const modbuslens::agent::AgentRunConfig& config,
                              std::uint64_t runGeneration,
                              const QJsonArray& messages,
                              const QJsonArray& tools) = 0;

    // Best-effort abort of any in-flight exchanges this client owns. Network
    // cancellation is NOT part of the contract: the owner's generation guard
    // is what makes a late delivery inert.
    virtual void cancel(AiAbortReason reason = AiAbortReason::UserCancel) = 0;

signals:
    void roundSucceeded(std::uint64_t runGeneration, QJsonObject assistantMessage);
    void roundFailed(std::uint64_t runGeneration, AiDiagnosisErrorCode code,
                     QString sanitizedMessage);
};
