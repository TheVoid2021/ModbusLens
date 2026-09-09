#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqml.h>

#include <chrono>
#include <cstdint>
#include <optional>

#include "core/analysis/TransactionStatistics.h"
#include "core/diagnosis/DiagnosisContext.h"
#include "core/diagnosis/RuleBasedDiagnosis.h"
#include "ui/TransactionListModel.h"
#include "ui/ai/ModelScopeDiagnosisClient.h"
#include "ui/serial/SerialPortAdapter.h"

// Application-layer adapter between modbuslens_core and QML (ADR001).
// NOT part of modbuslens_core — Qt types are allowed only in this layer.
// Optional semantics: hasX carries the meaning, value getters return a
// safe 0.0 placeholder when absent (QML must check hasX first).
class AnalysisController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int observedCount READ observedCount NOTIFY statisticsChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY statisticsChanged)
    Q_PROPERTY(int completedCount READ completedCount NOTIFY statisticsChanged)
    Q_PROPERTY(int successCount READ successCount NOTIFY statisticsChanged)
    Q_PROPERTY(int exceptionCount READ exceptionCount NOTIFY statisticsChanged)
    Q_PROPERTY(int crcErrorCount READ crcErrorCount NOTIFY statisticsChanged)
    Q_PROPERTY(int timeoutCount READ timeoutCount NOTIFY statisticsChanged)
    Q_PROPERTY(int protocolErrorCount READ protocolErrorCount NOTIFY statisticsChanged)
    Q_PROPERTY(bool hasSuccessRate READ hasSuccessRate NOTIFY statisticsChanged)
    Q_PROPERTY(double successRate READ successRate NOTIFY statisticsChanged)
    Q_PROPERTY(bool hasAverageSuccessLatency READ hasAverageSuccessLatency NOTIFY statisticsChanged)
    Q_PROPERTY(double averageSuccessLatencyMs READ averageSuccessLatencyMs NOTIFY statisticsChanged)
    Q_PROPERTY(QAbstractItemModel* transactionModel READ transactionModel CONSTANT)
    Q_PROPERTY(bool hasReplayError READ hasReplayError NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayErrorMessage READ replayErrorMessage NOTIFY replayStateChanged)
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY sourceChanged)
    Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY sourceChanged)
    Q_PROPERTY(bool serialConnected READ serialConnected NOTIFY serialConnChanged)
    Q_PROPERTY(bool serialBusy READ serialBusy NOTIFY serialStatusChanged)
    Q_PROPERTY(bool hasSerialError READ hasSerialError NOTIFY serialErrorChanged)
    Q_PROPERTY(QString serialErrorMessage READ serialErrorMessage NOTIFY serialErrorChanged)
    Q_PROPERTY(QStringList serialPortNames READ serialPortNames NOTIFY serialPortsChanged)
    Q_PROPERTY(bool hasBaselineDiagnosis READ hasBaselineDiagnosis NOTIFY diagnosisChanged)
    Q_PROPERTY(QString baselineDiagnosisText READ baselineDiagnosisText NOTIFY diagnosisChanged)
    Q_PROPERTY(bool aiConfigured READ aiConfigured NOTIFY aiStateChanged)
    Q_PROPERTY(bool aiDiagnosisBusy READ aiDiagnosisBusy NOTIFY aiStateChanged)
    Q_PROPERTY(bool hasAiDiagnosis READ hasAiDiagnosis NOTIFY aiStateChanged)
    Q_PROPERTY(QString aiDiagnosisText READ aiDiagnosisText NOTIFY aiStateChanged)
    Q_PROPERTY(QString aiDiagnosisErrorMessage READ aiDiagnosisErrorMessage NOTIFY aiStateChanged)
    Q_PROPERTY(QString aiModelName READ aiModelName NOTIFY aiStateChanged)

public:
    explicit AnalysisController(QObject* parent = nullptr);

    // Part B: deterministic demo orchestration (QML invokable commands)
    Q_INVOKABLE void runDemoBatch();
    // T009 Part B: generic result clear. Replaces the demo-only clearDemo()
    // (renamed, no forwarding alias — no external stable consumers yet):
    // clears statistics + rows + replay error, but NEVER switches the
    // source (only runDemoBatch does that).
    Q_INVOKABLE void clearResults();
    // T009 Part B: loads a .mlog file, replays it through the Part A core
    // (parse + analyze) and atomically publishes the batch. On ANY failure
    // the previous successful batch, mode and source are left untouched;
    // only the replay error state is set.
    Q_INVOKABLE void loadReplayFile(const QUrl& fileUrl);

    // ---- T010 Part B: serial source commands ----
    // Discovery only: enumerate available ports. NEVER opens/writes/probes
    // any detected COM — real opens happen solely via connectSerial.
    Q_INVOKABLE void refreshSerialPorts();
    // Connect is an explicit source transition: on SUCCESS the old batch is
    // cleared and mode/source switch to Serial; on FAILURE the entire old
    // statistics/rows/mode/source stay untouched (only the serial error
    // state changes).
    Q_INVOKABLE void connectSerial(const QString& portName, int baudRate);
    // Intentional disconnect: silent transport close; pending transaction is
    // cancelled without a Modbus diagnosis; the last result and the Serial
    // source identity remain on the dashboard (Clear is the only clearer).
    Q_INVOKABLE void disconnectSerial();
    // One FC03 read. Re-validates every range BEFORE any narrowing cast;
    // requires serialConnected && !serialBusy, otherwise serial error only.
    Q_INVOKABLE void readHoldingRegistersOnce(int slaveAddress, int startAddress,
                                              int quantity, int timeoutMs);

    // ---- T011 Part A: deterministic baseline diagnosis ----
    // Diagnoses the CURRENT structured active batch through the rule core.
    // An empty batch is a VALID input and yields a NoData report (the state
    // "diagnosis says no data" is distinct from "no diagnosis run yet").
    Q_INVOKABLE void runBaselineDiagnosis();
    // Clears ONLY the diagnosis result — statistics, rows, the active
    // diagnosis batch and the source stay untouched (Clear Diagnosis !=
    // Clear Results). The NEXT run re-derives the same report from the
    // still-present batch. With Part B this also clears the AI explanation
    // and aborts any in-flight AI request.
    Q_INVOKABLE void clearDiagnosis();

    // ---- T011 Part B: LLM explanation (ModelScope) ----
    // Explicit user action ONLY: no code path ever calls this implicitly.
    // C++ re-validates every precondition (configured + baseline present +
    // non-empty batch + not busy) regardless of the QML button state.
    Q_INVOKABLE void askAiDiagnosis();
    // User control, not a diagnostic failure: aborts the request and keeps
    // any previous same-batch explanation visible.
    Q_INVOKABLE void cancelAiDiagnosis();

    [[nodiscard]] int observedCount() const;
    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] int completedCount() const;
    [[nodiscard]] int successCount() const;
    [[nodiscard]] int exceptionCount() const;
    [[nodiscard]] int crcErrorCount() const;
    [[nodiscard]] int timeoutCount() const;
    [[nodiscard]] int protocolErrorCount() const;
    [[nodiscard]] bool hasSuccessRate() const;
    [[nodiscard]] double successRate() const;
    [[nodiscard]] bool hasAverageSuccessLatency() const;
    [[nodiscard]] double averageSuccessLatencyMs() const;
    [[nodiscard]] QAbstractItemModel* transactionModel();

    [[nodiscard]] bool hasReplayError() const;
    [[nodiscard]] QString replayErrorMessage() const;
    [[nodiscard]] QString modeLabel() const;
    [[nodiscard]] QString sourceLabel() const;

    [[nodiscard]] bool serialConnected() const;
    [[nodiscard]] bool serialBusy() const;
    [[nodiscard]] bool hasSerialError() const;
    [[nodiscard]] QString serialErrorMessage() const;
    [[nodiscard]] QStringList serialPortNames() const;

    [[nodiscard]] bool hasBaselineDiagnosis() const;
    [[nodiscard]] QString baselineDiagnosisText() const;

    [[nodiscard]] bool aiConfigured() const;
    [[nodiscard]] bool aiDiagnosisBusy() const;
    [[nodiscard]] bool hasAiDiagnosis() const;
    [[nodiscard]] QString aiDiagnosisText() const;
    [[nodiscard]] QString aiDiagnosisErrorMessage() const;
    [[nodiscard]] QString aiModelName() const;

    // C++-side test seam (never reaches QML): adopts an explicit client
    // configuration. Tests point it at a localhost fake endpoint with a
    // fake token; production wiring (constructor) uses the official
    // endpoint + process-environment token. An empty endpoint clears the
    // configured state (UI-AI01: "not configured" path).
    void configureAiClient(const QUrl& endpoint, const QString& apiKey,
                           const QString& modelId,
                           std::chrono::milliseconds timeout);

    // C++-side data entry points (not Q_INVOKABLE): Part B's demo flow and
    // tests call these; QML only reads.
    void applySnapshot(const modbuslens::core::TransactionStatisticsSnapshot& snapshot);
    void setTransactionEntries(std::vector<TransactionListEntry> entries);

    // T010 Part B hardware-free seam: maps an already-produced Core analysis
    // into the shared dashboard (one row + one-element statistics batch,
    // replace semantics). Production path: the adapter's transactionCompleted
    // reaches handleSerialTransactionCompleted, which validates pending
    // metadata and delegates here; tests call this helper directly with
    // TransactionAnalysis fixtures — mapping correctness is what it proves.
    void publishSerialResult(const QString& sourceLabel, int deviceAddress,
                             const modbuslens::core::TransactionAnalysis& analysis);

    // Production completion entry: guards against stale completions (an
    // analysis arriving with no pending serial metadata must NEVER override
    // the current Simulator/Replay batch — UI-S10).
    void handleSerialTransactionCompleted(
        const modbuslens::core::TransactionAnalysis& analysis);

signals:
    void statisticsChanged();
    void replayStateChanged();
    void sourceChanged();
    void serialConnChanged();
    void serialStatusChanged();
    void serialErrorChanged();
    void serialPortsChanged();
    void diagnosisChanged();
    void aiStateChanged();

private slots:
    // Serial transport errors are NOT Modbus diagnoses: sync state from the
    // adapter, drop pending metadata, surface the message — and touch
    // NOTHING in statistics/rows/mode/source.
    void handleSerialTransportError(const QString& message);
    // AI reply handlers enforce the two-dimensional stale guard: a delivery
    // is applied only when BOTH its request generation and its captured
    // batch revision still match the controller's current state.
    void handleAiSucceeded(std::uint64_t requestId, const QString& text);
    void handleAiFailed(std::uint64_t requestId, AiDiagnosisErrorCode code,
                        const QString& sanitizedMessage);

private:
    void setReplayError(const QString& message);
    void clearReplayError();
    void setSerialError(const QString& message);
    void clearSerialError();
    void teardownSerialTransport(); // adapter close + state reset (silent)
    void clearDiagnosisState();     // baseline diagnosis only (not the batch)
    void setAiError(const QString& message);
    // All derived views of a changed batch die together: baseline, AI result
    // and AI error are cleared, an in-flight AI request is aborted and its
    // identity invalidated, and activeBatchRevision_ is bumped.
    void invalidateAiForBatchChange();

    modbuslens::core::TransactionStatisticsSnapshot statistics_;
    TransactionListModel transactionModel_;

    bool hasReplayError_ = false;
    QString replayErrorMessage_;
    QString modeLabel_ = QStringLiteral("模拟器模式");
    QString sourceLabel_ = QStringLiteral("确定性演示");

    // T010 Part B: the SINGLE serial adapter owned by the app layer (never a
    // second QSerialPort anywhere; QML never sees this object).
    SerialTransactionAdapter serialAdapter_;

    QStringList serialPortNames_;
    bool serialConnected_ = false;
    bool serialBusy_ = false;
    bool hasSerialError_ = false;
    QString serialErrorMessage_;
    QString serialSourceLabel_;                       // "COM3 @ 9600"
    std::optional<std::uint8_t> pendingSerialAddress_; // only while reading

    // T011 Part A: the STRUCTURED active batch for diagnosis — same source
    // as rows + statistics on every successful publish (never reconstructed
    // from the presentation model).
    std::vector<modbuslens::core::DiagnosisTransaction> activeDiagnosisTransactions_;
    bool hasBaselineDiagnosis_ = false;
    QString baselineDiagnosisText_;

    // ---- T011 Part B ----
    // Two-dimensional async validity (see T011 archive PB-S):
    //   batchRevision — "still the same analysis batch?"
    //   requestGeneration — "still the latest AI request?"
    std::uint64_t activeBatchRevision_ = 0;
    std::uint64_t aiRequestGeneration_ = 0;
    std::optional<std::uint64_t> activeAiRequestId_;
    std::optional<std::uint64_t> requestBatchRevision_;

    ModelScopeDiagnosisClient aiClient_;

    bool aiConfigured_ = false;
    bool aiDiagnosisBusy_ = false;
    bool hasAiDiagnosis_ = false;
    QString aiDiagnosisText_;
    QString aiDiagnosisErrorMessage_;
    QString aiModelName_;
};