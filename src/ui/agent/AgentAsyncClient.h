#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "ui/agent/AgentProviderClient.h"
#include "ui/candidate/ChatCompletionsHttpClient.h"
#include "ui/candidate/ChatCompletionsTransport.h"

// ---------------------------------------------------------------------------
// T034-B2.2-C — provider-neutral Agent async client.
//
// Replaces ModelScopeAgentClient as the PRODUCTION Agent network authority:
//   · the endpoint/model/credential arrive PER ROUND as the run's immutable
//     AgentRunConfig — this client stores NO provider, model or credential;
//   · the transport is the checkpointed shared ChatCompletionsTransport
//     asynchronously over the injected IChatCompletionsHttpClient seam (no
//     private generic QNetworkAccessManager, no third HTTP stack);
//   · the credential is supplied as a ONE-SHOT bearer token consumed by the
//     exchange, so no plaintext secret survives the request window.
//
// Deliberately NO global single-flight busy gate: a superseded run N and its
// successor N+1 may BOTH have an exchange in flight. Each exchange owns its
// own transport, so their completions can never interfere; staleness is the
// owner's (AgentRuntime's) decision, via the run generation carried through
// each completion.
// ---------------------------------------------------------------------------
class AgentAsyncClient : public AgentProviderClient
{
    Q_OBJECT

public:
    explicit AgentAsyncClient(QObject* parent = nullptr);
    // Injection seam: deterministic tests supply a fake HTTP client. The
    // production default owns a shared QtChatCompletionsHttpClient.
    explicit AgentAsyncClient(modbuslens::ui::IChatCompletionsHttpClient& http,
                              QObject* parent = nullptr);

    // C++ AUTOMATION SEAM ONLY (never a Q_PROPERTY, never Q_INVOKABLE, never
    // QML-visible). It swaps ONLY the execution/HTTP end; provider, model and
    // credential authority stay with the caller-supplied AgentRunConfig, and
    // the fail-closed rules are untouched.
    void setHttpClientForAutomation(modbuslens::ui::IChatCompletionsHttpClient& http);

    void requestRound(const modbuslens::agent::AgentRunConfig& config,
                      std::uint64_t runGeneration,
                      const QJsonArray& messages,
                      const QJsonArray& tools) override;

    void cancel(AiAbortReason reason = AiAbortReason::UserCancel) override;

    // Number of provider exchanges actually dispatched (0 proves a fail-closed
    // or rejected path sent nothing).
    [[nodiscard]] int dispatchCount() const { return dispatchCount_; }

private:
    void handleCompletion(std::uint64_t exchangeId,
                          std::uint64_t runGeneration,
                          modbuslens::ui::ChatCompletionsHttpResult result);

    std::unique_ptr<modbuslens::ui::QtChatCompletionsHttpClient> ownedHttp_;
    modbuslens::ui::IChatCompletionsHttpClient* http_{nullptr};
    // ONE transport per in-flight exchange, erased when that exchange
    // completes: no state is shared between concurrent runs.
    std::map<std::uint64_t,
             std::unique_ptr<modbuslens::ui::ChatCompletionsTransport>>
        transports_;
    std::uint64_t nextExchangeId_{0};
    int dispatchCount_{0};
};
