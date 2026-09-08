#pragma once

#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstdint>

// Why an abort happened (ISSUE-005): QNetworkReply::OperationCanceledError
// alone cannot tell WHO aborted — the transfer timeout, the QTimer, a user
// cancel or a stale-request teardown all surface as the same network error.
// The owner of every abort records the reason FIRST; the finished() mapping
// then classifies the cancellation instead of leaking Qt's localized
// "operation canceled" string into the UI.
enum class AiAbortReason {
    None,
    UserCancel,
    Timeout,
    BatchInvalidated,
    DiagnosisCleared,
    SupersededRequest,
};

// T011 Part B error taxonomy (app layer). Cancelled is deliberately NOT an
// error: it is a silent control path. Controller precondition rejections
// (NotConfigured / NoData / BaselineRequired / Busy) are raised by the
// CONTROLLER; the client reports transport/provider outcomes.
enum class AiDiagnosisErrorCode {
    NotConfigured,
    InvalidConfiguration,
    NoData,
    BaselineRequired,
    Busy,

    NetworkError,
    Timeout,
    Unauthorized,
    RateLimited,
    ProviderRequestError,
    ServerError,
    InvalidResponse,
};

// Production compile-time configuration: the official ModelScope
// API-Inference endpoint is NOT environment-overridable — mixing an
// env-supplied token with an env-supplied endpoint could exfiltrate the
// real token to an arbitrary host. Tests inject a localhost fake endpoint
// through the configure() seam instead.
struct ModelScopeClientConfig {
    QUrl endpoint{};                    // production: official chat/completions
    QString apiKey{};                   // production: MODELSCOPE_API_KEY
    QString modelId{};                  // production: env override or candidate
    std::chrono::milliseconds timeout{30000};
};

// Thin ModelScope Chat Completions client (app layer). Owns exactly one
// HTTPS POST lifecycle: request -> timeout -> cancel -> parse -> emit. It
// knows nothing about Modbus, statistics, diagnosis rules or the dashboard.
//
// Security contract (Part B design):
//  - Authorization is attached ONLY to the configured endpoint;
//  - TLS peer verification stays ON (no ignoreSslErrors anywhere);
//  - redirects are restricted to same-origin (a cross-origin redirect fails
//    as a provider error instead of carrying the Bearer token elsewhere);
//  - the api key / Authorization header / full request body are never
//    logged.
class ModelScopeDiagnosisClient : public QObject
{
    Q_OBJECT

public:
    explicit ModelScopeDiagnosisClient(QObject* parent = nullptr);

    // Adopts the given config (test seam entry). Endpoint/apiKey/model must
    // all be non-empty for the client to consider itself usable.
    void configure(const ModelScopeClientConfig& config);
    [[nodiscard]] bool isConfigured() const;

    // Fire one Chat Completions POST. `requestId` is the controller's
    // generation counter and is echoed verbatim on every signal so the
    // controller can drop stale deliveries.
    void requestDiagnosis(const QString& systemPrompt, const QString& userPrompt,
                          std::uint64_t requestId);

    // Aborts the in-flight request. UserCancel/BatchInvalidated/
    // DiagnosisCleared/SupersededRequest aborts stay SILENT (no signal);
    // the reason exists so the finished() callback of an internally aborted
    // reply can be classified correctly (ISSUE-005).
    void cancel(AiAbortReason reason = AiAbortReason::UserCancel);

    [[nodiscard]] bool isBusy() const;
    [[nodiscard]] QString modelName() const;

signals:
    void diagnosisSucceeded(std::uint64_t requestId, const QString& text);
    void diagnosisFailed(std::uint64_t requestId, AiDiagnosisErrorCode code,
                         const QString& sanitizedMessage);

private slots:
    void handleTimeout();
    void handleFinished();

private:
    void fail(std::uint64_t requestId, AiDiagnosisErrorCode code,
              const QString& message);

    ModelScopeClientConfig config_;

    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QTimer* timeoutTimer_ = nullptr;

    std::uint64_t currentRequestId_ = 0;
    bool busy_ = false;
    AiAbortReason abortReason_ = AiAbortReason::None;
};

// Production configuration assembly (app layer): official endpoint +
// process environment secret + model (env override with candidate default).
// Returns whether a usable configuration exists. The token never leaves
// this function scope beyond the config struct handed to configure().
bool buildModelScopeProductionConfig(ModelScopeClientConfig& out);