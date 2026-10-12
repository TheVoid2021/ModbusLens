#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "core/manualqa/ManualQaContract.h"
#include "ui/candidate/ChatCompletionsHttpClient.h"
#include "ui/candidate/ChatCompletionsTransport.h"
#include "ui/manualqa/IManualQaRunner.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// T034-B2.2-B — PROVIDER-NEUTRAL Manual Q&A runner.
//
// It replaces ModelScopeManualQaRunner in the production path. Two things
// changed and nothing else:
//
//  1. CREDENTIAL / PROVIDER AUTHORITY moved OUT. The old runner read
//     MODELSCOPE_API_KEY and a ModelScope model override straight from the
//     process environment and posted to a compiled ModelScope endpoint. This
//     runner receives an ALREADY-RESOLVED ManualQaDispatchConfig from the
//     controller (which resolves it from the single global AI configuration
//     owner) and reads NO environment variable at all.
//
//  2. TRANSPORT unified. The old runner drove ModelScopeAgentClient (a
//     ModelScope-specific client with its own QNetworkAccessManager and
//     single-flight state). This runner uses the SAME shared
//     ChatCompletionsTransport::sendAsync over IChatCompletionsHttpClient
//     that the Diagnosis path uses (T034-B2.2-A-R2), so there is no private
//     network stack and multiple dispatches can overlap.
//
// It owns NO consent and NO generation authority — ManualQaController grants
// consent and owns generations; this runner only runs an already-consented
// dispatch. Fail-closed: a missing endpoint / model / credential means the
// attempt cannot start and nothing is sent.
//
// The provider payload is parsed with a strict, fail-closed reader into the
// core ManualQaParsedResult. Malformed output is an error — it is never
// coerced into a semantic state. The raw payload lives in memory only (D5
// session-only) and is never logged or persisted.
// ---------------------------------------------------------------------------

class ManualQaAsyncRunner : public QObject, public IManualQaRunner
{
    Q_OBJECT

public:
    explicit ManualQaAsyncRunner(QObject* parent = nullptr);
    // Injection seam: deterministic tests supply a fake HTTP client. The
    // production default owns a shared QtChatCompletionsHttpClient.
    explicit ManualQaAsyncRunner(IChatCompletionsHttpClient& http,
                                 QObject* parent = nullptr);

    // Strict, fail-closed parse of the provider payload into the core result.
    // nullopt on ANY deviation (malformed JSON, wrong shape, wrong types,
    // unknown status, broken citation shape). A parse failure ALSO carries a
    // SAFE deterministic category token through failureCategory (category name
    // only — never question/answer/excerpt/reasoning/raw content).
    [[nodiscard]] static std::optional<core::ManualQaParsedResult>
    parseProviderResult(const std::string& json,
                        QString* failureCategory = nullptr);

    // Parse + resolve pass. Provider responses use the APP-RESOLVED citation
    // shape ({"citationId": "cN"}) — the provider only SELECTS opaque ids the
    // app attached to its own deterministic context blocks; this pass maps the
    // selected ids back to the canonical D2 citation fields so the unchanged
    // local citation validator remains the final authority. Unknown ids /
    // wrong-generation ids are fail-closed (ERROR, never coerced).
    [[nodiscard]] static std::optional<core::ManualQaParsedResult>
    resolveProviderResult(const std::string& json,
                          const core::ManualQaRequest& request,
                          QString* failureCategory = nullptr);

    // Pure request-body builder. Provider-neutral: the RESOLVED provider kind
    // and model id are parameters (the model was a compiled ModelScope default
    // before the migration). R1: `kind` selects provider-specific wire extras.
    [[nodiscard]] static QJsonObject
    buildRequestBody(AiProviderKind kind, const std::string& model,
                     const core::ManualQaRequest& request);

    // Pure assistant-content extractor: the transport emits the assistant
    // MESSAGE object (choices[0].message), so the answer content lives at its
    // TOP LEVEL. reasoning_content is a separate field and is never treated as
    // answer content.
    [[nodiscard]] static QString
    extractAssistantContent(const QJsonObject& assistantMessage);

    [[nodiscard]] bool begin(const ManualQaDispatchConfig& config,
                             const core::ManualQaRequest& request,
                             std::uint64_t generation,
                             const CompletionHandler& onDone) override;

    void cancel() override;

    [[nodiscard]] int beginCount() const override;

    // Diagnostic: async exchanges actually started (0 proves a fail-closed
    // path dispatched nothing).
    [[nodiscard]] int dispatchCount() const { return dispatchCount_; }

private:
    void handleCompletion(std::uint64_t generation,
                          ChatCompletionsHttpResult result);

    std::unique_ptr<QtChatCompletionsHttpClient> ownedHttp_;
    IChatCompletionsHttpClient* http_{nullptr};
    // ONE transport per in-flight dispatch, erased on completion, so no
    // credential-bearing object is retained between requests.
    std::map<std::uint64_t, std::unique_ptr<ChatCompletionsTransport>> transports_;
    int beginCount_{0};
    int dispatchCount_{0};
    std::uint64_t pendingGeneration_{0};
    CompletionHandler pendingOnDone_;
    core::ManualQaRequest pendingRequest_;
};

} // namespace modbuslens::ui