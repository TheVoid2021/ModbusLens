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
    // unknown status, broken citation shape).
    [[nodiscard]] static std::optional<core::ManualQaParsedResult>
    parseProviderResult(const std::string& json);

    [[nodiscard]] bool begin(const core::ManualQaRequest& request,
                             std::uint64_t generation,
                             const CompletionHandler& onDone) override;

    void cancel() override;

    [[nodiscard]] int beginCount() const override;

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
