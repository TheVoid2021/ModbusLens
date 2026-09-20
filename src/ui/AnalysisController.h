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

#include "core/active/ActiveTransactionEvidence.h"
#include "core/active/PreparedWriteSnapshot.h"
#include "core/analysis/TransactionProvenance.h"
#include "core/analysis/TransactionStatistics.h"
#include "core/diagnosis/DiagnosisContext.h"
#include "core/diagnosis/RuleBasedDiagnosis.h"
#include "ui/TransactionListModel.h"
#include "ui/agent/AgentRuntime.h"
#include "ui/agent/AgentToolContext.h"
#include "ui/agent/ModelScopeAgentClient.h"
#include "ui/ai/ModelScopeDiagnosisClient.h"
#include "ui/serial/SerialPortAdapter.h"
#include "ui/serial/SerialTransport.h"

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
    Q_PROPERTY(int expectedNoResponseCount READ expectedNoResponseCount NOTIFY statisticsChanged)
    Q_PROPERTY(bool hasSuccessRate READ hasSuccessRate NOTIFY statisticsChanged)
    Q_PROPERTY(double successRate READ successRate NOTIFY statisticsChanged)
    Q_PROPERTY(bool hasAverageSuccessLatency READ hasAverageSuccessLatency NOTIFY statisticsChanged)
    Q_PROPERTY(double averageSuccessLatencyMs READ averageSuccessLatencyMs NOTIFY statisticsChanged)
    Q_PROPERTY(QAbstractItemModel* transactionModel READ transactionModel CONSTANT)
    Q_PROPERTY(bool hasReplayError READ hasReplayError NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayErrorMessage READ replayErrorMessage NOTIFY replayStateChanged)
    Q_PROPERTY(bool hasReplayNotice READ hasReplayNotice NOTIFY replayStateChanged)
    Q_PROPERTY(QString replayNoticeText READ replayNoticeText NOTIFY replayStateChanged)
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
    Q_PROPERTY(bool agentBusy READ agentBusy NOTIFY agentStateChanged)
    Q_PROPERTY(bool hasAgentAnswer READ hasAgentAnswer NOTIFY agentStateChanged)
    Q_PROPERTY(QString agentAnswerText READ agentAnswerText NOTIFY agentStateChanged)
    Q_PROPERTY(QString agentErrorText READ agentErrorText NOTIFY agentStateChanged)
    Q_PROPERTY(bool agentAvailable READ agentAvailable NOTIFY aiStateChanged)
    Q_PROPERTY(bool cloudAiBusy READ cloudAiBusy NOTIFY cloudAiChanged)
    // ---- M10-C2: read-only prepared-write projection (QML surface) ----
    // Machine-readable state + the facts the confirmation summary must show.
    // There is deliberately NO setter and NO QML-side authority: the snapshot
    // lives in the controller, and QML only reads this projection.
    Q_PROPERTY(bool hasPreparedWrite READ hasPreparedWrite NOTIFY preparedWriteChanged)
    Q_PROPERTY(QString preparedWriteState READ preparedWriteStateToken NOTIFY preparedWriteChanged)
    Q_PROPERTY(qulonglong preparedWriteToken READ preparedWriteTokenValue NOTIFY preparedWriteChanged)
    Q_PROPERTY(int preparedWriteFunction READ preparedWriteFunction NOTIFY preparedWriteChanged)
    Q_PROPERTY(int preparedWriteUnitId READ preparedWriteUnitId NOTIFY preparedWriteChanged)
    Q_PROPERTY(int preparedWriteAddress READ preparedWriteAddress NOTIFY preparedWriteChanged)
    Q_PROPERTY(int preparedWriteValue READ preparedWriteValue NOTIFY preparedWriteChanged)
    Q_PROPERTY(QVariantList preparedWriteValues READ preparedWriteValues NOTIFY preparedWriteChanged)
    Q_PROPERTY(int preparedWriteQuantity READ preparedWriteQuantity NOTIFY preparedWriteChanged)
    Q_PROPERTY(int preparedWriteTimeoutMs READ preparedWriteTimeoutMs NOTIFY preparedWriteChanged)
    Q_PROPERTY(QString preparedWriteConnectionLabel READ preparedWriteConnectionLabel NOTIFY preparedWriteChanged)
    Q_PROPERTY(QString preparedWriteInvalidReason READ preparedWriteInvalidReasonToken NOTIFY preparedWriteChanged)
    Q_PROPERTY(bool hasWriteDraftError READ hasWriteDraftError NOTIFY writeDraftErrorChanged)
    Q_PROPERTY(QString writeDraftError READ writeDraftError NOTIFY writeDraftErrorChanged)

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

    // ---- T012 Part B Phase 2: read-only Agent (Controller integration) ----
    // Single-flight: accepted only while no cloud LLM workflow is busy.
    // The question validator stays in the Runtime (backend authority); this
    // layer only decides batch/config/busy preconditions and snapshots.
    Q_INVOKABLE void askAgent(const QString& question);
    Q_INVOKABLE void cancelAgent();

    [[nodiscard]] int observedCount() const;
    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] int completedCount() const;
    [[nodiscard]] int successCount() const;
    [[nodiscard]] int exceptionCount() const;
    [[nodiscard]] int crcErrorCount() const;
    [[nodiscard]] int timeoutCount() const;
    [[nodiscard]] int protocolErrorCount() const;
    [[nodiscard]] int expectedNoResponseCount() const;
    [[nodiscard]] bool hasSuccessRate() const;
    [[nodiscard]] double successRate() const;
    [[nodiscard]] bool hasAverageSuccessLatency() const;
    [[nodiscard]] double averageSuccessLatencyMs() const;
    [[nodiscard]] QAbstractItemModel* transactionModel();

    [[nodiscard]] bool hasReplayError() const;
    [[nodiscard]] QString replayErrorMessage() const;
    // T015 Gate F disclosure: non-fatal note when a loaded replay batch
    // contained records that are valid Modbus but unsupported for analysis.
    [[nodiscard]] bool hasReplayNotice() const;
    [[nodiscard]] QString replayNoticeText() const;
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

    [[nodiscard]] bool agentBusy() const;
    [[nodiscard]] bool hasAgentAnswer() const;
    [[nodiscard]] QString agentAnswerText() const;
    [[nodiscard]] QString agentErrorText() const;
    [[nodiscard]] bool agentAvailable() const;
    [[nodiscard]] bool cloudAiBusy() const;

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

    // ---- M10-A: Active Master contract foundation (C++ seams only) ----
    // Production completion entry: guards against stale completions (a
    // result arriving with no pending serial metadata must NEVER override the
    // current Simulator/Replay batch — UI-S10) and retains the transaction as
    // an authoritative session record (send-time intent snapshot + wire
    // evidence), which is never overwritten by a later transaction.
    void handleSerialTransactionCompleted(
        const modbuslens::core::ActiveTransactionResult& result);

    // Production termination entry (M10-A correction): a SUBMITTED request
    // that ended without a trusted Modbus response (port error / explicit
    // disconnect / source teardown). Retains the attempt's evidence in the
    // same Active Serial session — no Modbus outcome is fabricated, and the
    // evidence is never dropped together with the pending snapshot.
    void handleSerialTransactionTerminated(
        const modbuslens::core::ActiveTransportTerminal& terminal);

    // Transport seam (never reaches QML): point the runtime at an alternative
    // transport implementation — deterministic tests inject a recording
    // transport here. nullptr restores the built-in production adapter. The
    // injected transport is NOT owned by the controller.
    void setSerialTransport(SerialTransport* transport);

    // M10-B: the ONE presentation path for Active Serial session history.
    // Appends the authoritative record and projects it into the visible
    // history: the row is APPENDED (never a latest-only replacement), and the
    // statistics/diagnosis inputs are re-derived from the WHOLE session batch.
    // Presentation only — the record itself stays the authority (wire
    // evidence, provenance, disposition), and transport terminals are never
    // published here as Modbus rows.
    void appendActiveSerialTransaction(
        const modbuslens::core::ActiveTransactionRecord& record);

    // ---- M10-C1: prepared write foundation (C++ seams; NO send, NO UI) ----
    // Prepares an immutable write snapshot after authoritative validation.
    // Rejects when the source is not Active Serial / not connected / busy /
    // the draft fails validation; an existing Prepared snapshot is kept and
    // reported instead of being replaced. Nothing here encodes, starts or
    // sends anything: 0x06 / 0x10 encoders do not exist yet.
    [[nodiscard]] modbuslens::core::WritePrepareOutcome prepareWriteSingleRegister(
        std::int64_t unitId, std::int64_t registerAddress, std::int64_t value,
        std::int64_t timeoutMs);
    [[nodiscard]] modbuslens::core::WritePrepareOutcome prepareWriteMultipleRegisters(
        std::int64_t unitId, std::int64_t startAddress, std::string_view valuesText,
        std::int64_t timeoutMs);
    // Confirmation consumes the snapshot (Prepared -> Consumed) after the
    // runtime re-checks source / session / connection / busy. It does NOT
    // dispatch: transport start/send counts stay untouched (dispatch is
    // M10-D/E work).
    [[nodiscard]] modbuslens::core::ConfirmWriteOutcome confirmPreparedWrite(
        std::uint64_t token);
    // Explicit cancel: Prepared -> Invalidated(UserCancelled). Never a serial
    // error, never a transaction, never a send.
    bool cancelPreparedWrite(std::uint64_t token);

    // ---- M10-C2: QML-facing seams (still NO dispatch) ----
    // Write activation: read the draft, run the SAME authoritative C1
    // validation, and on success create the prepared snapshot (which is what
    // opens the confirmation dialog). A failure creates NO snapshot and the
    // dialog must not open; the typed error is mapped to presentation text in
    // writeDraftError (never a raw enum token in the UI).
    Q_INVOKABLE bool prepareWrite06(int unitId, int registerAddress, int value,
                                    int timeoutMs);
    Q_INVOKABLE bool prepareWrite10(int unitId, int startAddress,
                                    const QString& valuesText, int timeoutMs);
    // Confirmation / cancellation by OPAQUE TOKEN only: QML hands back exactly
    // the value it read from the projection, never the draft fields. Neither
    // call encodes, dispatches or sends anything (dispatch is M10-D/E).
    Q_INVOKABLE bool confirmPreparedWriteToken(qulonglong token);
    Q_INVOKABLE bool cancelPreparedWriteToken(qulonglong token);

    // QML projection getters (read-only; typed accessors above stay for C++).
    [[nodiscard]] bool hasPreparedWrite() const;
    [[nodiscard]] QString preparedWriteStateToken() const;
    [[nodiscard]] qulonglong preparedWriteTokenValue() const;
    [[nodiscard]] int preparedWriteFunction() const;
    [[nodiscard]] int preparedWriteUnitId() const;
    [[nodiscard]] int preparedWriteAddress() const;
    [[nodiscard]] int preparedWriteValue() const;
    [[nodiscard]] QVariantList preparedWriteValues() const;
    [[nodiscard]] int preparedWriteQuantity() const;
    [[nodiscard]] int preparedWriteTimeoutMs() const;
    [[nodiscard]] QString preparedWriteConnectionLabel() const;
    [[nodiscard]] QString preparedWriteInvalidReasonToken() const;
    [[nodiscard]] bool hasWriteDraftError() const;
    [[nodiscard]] QString writeDraftError() const;

    // Read-only projection of the prepared snapshot (C1 C++ accessors).
    [[nodiscard]] modbuslens::core::PreparedWriteState preparedWriteState() const;
    [[nodiscard]] std::optional<std::uint64_t> preparedWriteToken() const;
    [[nodiscard]] std::optional<modbuslens::core::PreparedWriteSnapshot>
    preparedWriteSnapshot() const;
    [[nodiscard]] std::optional<modbuslens::core::PreparedWriteInvalidReason>
    preparedWriteInvalidReason() const;

    // Source identity (typed, never inferred from modeLabel text, a workspace
    // index or a filename) and the Active Serial session it belongs to.
    [[nodiscard]] modbuslens::core::TransactionSourceKind sourceKind() const;
    [[nodiscard]] std::uint64_t activeSerialSessionId() const;
    [[nodiscard]] int activeSerialRecordCount() const;
    [[nodiscard]] const std::vector<modbuslens::core::ActiveTransactionRecord>&
    activeSerialRecords() const;
    // Post-submission transport terminations of the same session (parallel
    // authoritative evidence — deliberately NOT faked into completed
    // transactions).
    [[nodiscard]] int activeSerialTerminalCount() const;
    [[nodiscard]] const std::vector<modbuslens::core::ActiveTransportTerminal>&
    activeSerialTerminations() const;

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
    void agentStateChanged();
    void cloudAiChanged();
    // One notification for the whole prepared-write projection: QML never
    // polls and never observes a half-updated frame.
    void preparedWriteChanged();
    void writeDraftErrorChanged();

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
    void handleAgentCompleted(std::uint64_t runGeneration, const QString& answer);
    void handleAgentFailed(std::uint64_t runGeneration,
                           const modbuslens::agent::AgentRunFailure& failure);
    void handleAgentCancelled(std::uint64_t runGeneration);

private:
    void setReplayError(const QString& message);
    void clearReplayError();
    // T015 Gate F: non-fatal disclosure helpers (never a load failure).
    void setReplayNotice(const QString& message);
    void clearReplayNotice();
    void setSerialError(const QString& message);
    void clearSerialError();
    void teardownSerialTransport(); // adapter close + state reset (silent)
    void clearDiagnosisState();     // baseline diagnosis only (not the batch)
    void setAiError(const QString& message);
    // All derived views of a changed batch die together: baseline, AI result
    // and AI error are cleared, an in-flight AI request is aborted and its
    // identity invalidated, and activeBatchRevision_ is bumped.
    void invalidateAiForBatchChange();

    // Presentation projection of ONE session record (row fields + issue text).
    [[nodiscard]] TransactionListEntry makeSessionRow(
        const modbuslens::core::ActiveTransactionRecord& record) const;
    // Derived views over the WHOLE Active Serial session: statistics snapshot
    // and the deterministic-diagnosis batch. Rows are NOT rebuilt here — they
    // grow by append, so existing rows/selection stay untouched.
    void refreshActiveSessionDerivedViews();
    // (Re)connects the completion/error doors of the active transport.
    void connectSerialTransportSignals(SerialTransport& transport);
    // Shared prepare plumbing: context guards, generation, store.
    [[nodiscard]] modbuslens::core::WritePrepareOutcome prepareWriteIntent(
        modbuslens::core::WriteIntentResult intentResult);
    // Validation presentation mapping (typed code -> human text, one-based
    // line numbers). Presentation belongs to this Qt adapter layer; the typed
    // authority stays in core.
    void setWriteDraftErrorFrom(const modbuslens::core::PrepareRejected& rejected);
    void setWriteDraftError(const QString& message);
    void clearWriteDraftError();
    // Emitted after every prepared-state transition (prepare/confirm/cancel/
    // invalidate) so the projection stays consistent in one step.
    void announcePreparedWriteChanged();

    modbuslens::core::TransactionStatisticsSnapshot statistics_;
    TransactionListModel transactionModel_;

    bool hasReplayError_ = false;
    QString replayErrorMessage_;
    // T015: non-fatal unsupported-record disclosure for the loaded batch.
    bool hasReplayNotice_ = false;
    QString replayNoticeText_;
    QString modeLabel_ = QStringLiteral("模拟器模式");
    QString sourceLabel_ = QStringLiteral("确定性演示");
    // M10-A: TYPED source identity (Simulator / Replay / ActiveSerial) — the
    // label strings above stay presentation only.
    modbuslens::core::TransactionSourceKind sourceKind_ =
        modbuslens::core::TransactionSourceKind::Simulator;

    // T010 Part B: the SINGLE serial adapter owned by the app layer (never a
    // second QSerialPort anywhere; QML never sees this object).
    SerialTransactionAdapter serialAdapter_;
    // M10-A seam: production default points at serialAdapter_; tests may point
    // it at a recording transport instead (never owned here).
    SerialTransport* serialTransport_ = nullptr;

    QStringList serialPortNames_;
    bool serialConnected_ = false;
    bool serialBusy_ = false;
    bool hasSerialError_ = false;
    QString serialErrorMessage_;
    QString serialSourceLabel_;                       // "COM3 @ 9600"
    // Active Serial session identity: bumped on every successful connect, so
    // records of two sessions can never be confused for one history.
    std::uint64_t activeSerialSessionId_ = 0;
    // Send-time snapshot of the in-flight request (its presence is also the
    // stale-completion guard). NEVER re-read from QML at completion time.
    std::optional<modbuslens::core::ActiveRequestDescriptor> pendingRequest_;
    // Authoritative Active Serial session history (append-only per completed
    // transaction; the wire evidence lives here, not in a QML row).
    std::vector<modbuslens::core::ActiveTransactionRecord> activeSerialRecords_;
    // Post-submission transport terminations of the same session: parallel
    // append-only evidence, never mixed into the completed-transaction
    // history (a transport abort is not a Modbus transaction).
    std::vector<modbuslens::core::ActiveTransportTerminal> activeSerialTerminations_;
    // M10-C1: the ONE prepared write snapshot of this runtime (one-shot state
    // machine) plus its monotonic generation counter. Draft data never enters
    // this class — only an already-validated intent does.
    modbuslens::core::PreparedWriteStore preparedWriteStore_;
    std::uint64_t preparedWriteGeneration_ = 0;
    bool hasWriteDraftError_ = false;
    QString writeDraftError_;

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

    // ---- T012 Part B Phase 2: read-only Agent ----
    // Same provider config source as T011 (configured together); the
    // runtime owns all Agent async validity. NO second agentBusy_ bool —
    // agentBusy() derives straight from the runtime. hasAgentAnswer_ holds
    // answer presentation only (facts live in the structured batch).
    ModelScopeAgentClient agentClient_;
    AgentRuntime agentRuntime_;
    std::uint64_t agentRequestGeneration_ = 0;
    bool hasAgentAnswer_ = false;
    QString agentAnswerText_;
    QString agentErrorText_;
};