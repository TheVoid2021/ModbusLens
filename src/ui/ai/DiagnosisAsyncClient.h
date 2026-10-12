#pragma once

#include <QObject>
#include <QString>

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "ui/ai/ModelScopeDiagnosisClient.h" // AiDiagnosisErrorCode taxonomy only
#include "ui/candidate/ChatCompletionsHttpClient.h"
#include "ui/candidate/ChatCompletionsTransport.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// T034-B2.2-A-R2 — Diagnosis-specific async adapter over the SHARED transport.
//
// It retires ModelScopeDiagnosisClient from the PRODUCTION Diagnosis dispatch
// path (that class keeps its private QNetworkAccessManager and its global
// single-flight state, and is retained only for its own frozen tests).
//
// Contract:
//  * provider-NEUTRAL transport (ChatCompletionsTransport over
//    IChatCompletionsHttpClient) — no private QNetworkAccessManager;
//  * Diagnosis-SPECIFIC request builder, response parser and error mapping;
//  * the endpoint/model/credential for each dispatch arrive in
//    DiagnosisDispatchConfig and the credential is used ONLY for that
//    dispatch (one-shot bearer token); nothing is retained on this object
//    between requests;
//  * ONE transport instance per in-flight dispatch, so there is NO
//    single-flight state and multiple dispatches can overlap;
//  * exactly one of diagnosisSucceeded / diagnosisFailed per started request.
//
// NETWORK CANCELLATION is deliberately NOT implemented and must not be
// claimed: a superseded dispatch is simply abandoned by the owner.
// ---------------------------------------------------------------------------
struct DiagnosisDispatchConfig {
    std::string endpoint;         // from AiProviderProfile (or test override)
    std::string model;            // from the resolved global snapshot
    std::string secret;           // provider-scoped credential for THIS request
    int timeoutMs{90000};
};

class DiagnosisAsyncClient : public QObject
{
    Q_OBJECT

public:
    explicit DiagnosisAsyncClient(QObject* parent = nullptr);
    // Injection seam: the deterministic tests supply a fake HTTP client. The
    // production default owns a QtChatCompletionsHttpClient (shared).
    explicit DiagnosisAsyncClient(IChatCompletionsHttpClient& http,
                                  QObject* parent = nullptr);

    // Starts ONE asynchronous Diagnosis dispatch. Returns false when it cannot
    // start (missing endpoint/model/secret, or the transport refused to
    // dispatch): nothing is sent and neither signal is emitted. When it
    // returns true, EXACTLY ONE of the two signals is emitted later with
    // `requestId`.
    bool requestDiagnosis(const DiagnosisDispatchConfig& config,
                          const QString& systemPrompt, const QString& userPrompt,
                          std::uint64_t requestId);

    // Diagnostic: dispatches actually started (0 proves a fail-closed path
    // sent nothing).
    [[nodiscard]] int dispatchCount() const { return dispatchCount_; }

signals:
    void diagnosisSucceeded(std::uint64_t requestId, const QString& text);
    void diagnosisFailed(std::uint64_t requestId, AiDiagnosisErrorCode code,
                         const QString& sanitizedMessage);

private:
    void handleCompletion(std::uint64_t requestId,
                          ChatCompletionsHttpResult result);

    std::unique_ptr<QtChatCompletionsHttpClient> ownedHttp_;
    IChatCompletionsHttpClient* http_{nullptr};
    // One request-scoped transport per in-flight dispatch. Erased on
    // completion, so no credential-bearing object is retained between
    // requests.
    std::map<std::uint64_t, std::unique_ptr<ChatCompletionsTransport>> transports_;
    int dispatchCount_{0};
};

} // namespace modbuslens::ui
