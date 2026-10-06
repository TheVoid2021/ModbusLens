#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>

#include <optional>
#include <string>

#include "core/manualqa/ManualQaContract.h"
#include "ui/agent/ModelScopeAgentClient.h"
#include "ui/manualqa/IManualQaRunner.h"

namespace modbuslens::ui {

// ---------------------------------------------------------------------------
// M12-D FIRST SLICE — the production Q&A runner (§100.4/D4): reuses the
// EXISTING ModelScope infrastructure (the verified ModelScopeAgentClient
// async model: single timeout owner, same-origin redirects, TLS on, no token
// logging, generation-guarded rounds). It owns NO consent and NO generation
// authority — ManualQaController grants consent and owns generations; this
// runner only runs an already-consented dispatch.
//
// Credential boundary: fail-closed. Without an ambient MODELSCOPE_API_KEY the
// attempt cannot even start (zero network); the controller records the local
// failure itself.
//
// The provider payload is parsed with a strict, fail-closed reader into the
// core ManualQaParsedResult (§100.6). Malformed output is an error — it is
// never coerced into a semantic state. The raw payload lives in memory only
// (D5 session-only) and is never logged or persisted.
// ---------------------------------------------------------------------------

class ModelScopeManualQaRunner : public QObject, public IManualQaRunner
{
    Q_OBJECT

public:
    explicit ModelScopeManualQaRunner(QObject* parent = nullptr);

    // Strict, fail-closed parse of the provider payload into the core result.
    // nullopt on ANY deviation (malformed JSON, wrong shape, wrong types,
    // unknown status, broken citation shape). Since M12-D-R2D, a parse
    // failure ALSO carries a SAFE deterministic category token through
    // failureCategory (category name only — never question/answer/excerpt/
    // reasoning/raw content) so the intermittent live structured-output
    // defect can be RCA'd without weakening the parser acceptance set.
    [[nodiscard]] static std::optional<core::ManualQaParsedResult>
    parseProviderResult(const std::string& json,
                        QString* failureCategory = nullptr);

    // M12-D-R2E: parse + resolve pass. Provider responses now use the
    // APP-RESOLVED citation shape ({"citationId": "cN"}) — the provider only
    // SELECTS opaque ids the app attached to its own deterministic context
    // blocks; this pass maps the selected ids back to the canonical D2
    // citation fields (documentId/contentHash from the request binding,
    // offsets/excerpt from the referenced block) so the unchanged local
    // citation validator remains the final authority. Unknown ids /
    // wrong-generation ids are fail-closed (ERROR, never coerced).
    [[nodiscard]] static std::optional<core::ManualQaParsedResult>
    resolveProviderResult(const std::string& json,
                          const core::ManualQaRequest& request,
                          QString* failureCategory = nullptr);

    [[nodiscard]] bool begin(const core::ManualQaRequest& request,
                             std::uint64_t generation,
                             const CompletionHandler& onDone) override;

    void cancel() override;

    [[nodiscard]] int beginCount() const override;

    // Pure request-body builder (M12-D-R2B): exposed for deterministic
    // tests of the provider contract — the Q&A task requests NON-THINKING
    // behavior (chat_template_kwargs.enable_thinking=false) because the
    // thinking pass can starve/truncate the final structured JSON.
    [[nodiscard]] static QJsonObject buildRequestBody(
        const core::ManualQaRequest& request);

    // Pure assistant-content extractor (M12-D-R2B root-cause fix):
    // ModelScopeAgentClient emits the assistant MESSAGE object
    // (choices[0].message), so the answer content lives at its TOP LEVEL.
    // The pre-fix code re-applied choices[0].message extraction and always
    // produced empty content (every live Q&A parse failed). Exposed for
    // deterministic regression tests of that exact contract.
    [[nodiscard]] static QString extractAssistantContent(
        const QJsonObject& assistantMessage);

private:
    void handleRoundSucceeded(std::uint64_t generation,
                              const QJsonObject& assistantMessage);
    void handleRoundFailed(std::uint64_t generation,
                           AiDiagnosisErrorCode errorCode,
                           const QString& sanitizedMessage);

    ModelScopeAgentClient client_;
    int beginCount_{0};
    std::uint64_t pendingGeneration_{0};
    CompletionHandler pendingOnDone_;
};

} // namespace modbuslens::ui
