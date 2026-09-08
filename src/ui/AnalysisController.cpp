#include "ui/AnalysisController.h"

#include "core/analysis/TransactionStatistics.h"
#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/replay/ReplayAnalysis.h"
#include "core/replay/ReplayLog.h"
#include "core/diagnosis/DiagnosisContext.h"
#include "core/diagnosis/RuleBasedDiagnosis.h"
#include "core/simulator/SimulatedSlave.h"
#include "core/simulator/SimulationFault.h"
#include "ui/ai/DiagnosisPromptBuilder.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QSerialPortInfo>
#include <QUrl>

#include <algorithm>
#include <array>
#include <string_view>
#include <variant>
#include <vector>

namespace {

// Bounded display contract: the UI adapter narrows core size_t counters to
// int for QML. Demo/history batches are far below INT_MAX; core stays size_t.
int toInt(std::size_t value)
{
    return static_cast<int>(value);
}

// Build a Function 0x03 read request (deterministic demo fixture, not a
// public protocol API — see T008 Part B rule B).
modbuslens::core::ModbusRtuFrame makeFc03Read(
    std::uint8_t address, std::uint16_t start, std::uint16_t quantity)
{
    return modbuslens::core::ModbusRtuFrame{
        .address = address,
        .functionCode = 0x03,
        .data = {
            static_cast<std::uint8_t>(start >> 8),
            static_cast<std::uint8_t>(start & 0xFF),
            static_cast<std::uint8_t>(quantity >> 8),
            static_cast<std::uint8_t>(quantity & 0xFF),
        },
    };
}

// ---- Replay error presentation adapters (Core -> user-readable QString).
// Core stays enum + line/index; this file owns the human phrasing. ----

QString parseErrorMessage(const modbuslens::core::ReplayParseError& error)
{
    const char* phrase = "invalid replay log";
    switch (error.code) {
    case modbuslens::core::ReplayParseErrorCode::MissingHeader:
        phrase = "log header is missing";
        break;
    case modbuslens::core::ReplayParseErrorCode::UnsupportedVersion:
        phrase = "unsupported log version";
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidHeader:
        phrase = "invalid log header";
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidRecord:
        phrase = "invalid record";
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidElapsed:
        phrase = "invalid elapsed value";
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidHex:
        phrase = "invalid hex data";
        break;
    case modbuslens::core::ReplayParseErrorCode::MissingRequest:
        phrase = "request field is empty";
        break;
    case modbuslens::core::ReplayParseErrorCode::InvalidResponseField:
        phrase = "invalid response field";
        break;
    }
    // lineNumber == 0 means "no specific offending line" (e.g. a log with no
    // header at all) — never render a meaningless "line 0".
    if (error.lineNumber == 0) {
        return QStringLiteral("Replay parse error: %1")
            .arg(QLatin1String(phrase));
    }
    return QStringLiteral("Replay parse error at line %1: %2")
        .arg(error.lineNumber)
        .arg(QLatin1String(phrase));
}

QString executionErrorMessage(
    const modbuslens::core::ReplayExecutionError& error)
{
    const char* phrase = "replay analysis failed";
    switch (error.code) {
    case modbuslens::core::ReplayExecutionErrorCode::InvalidRequestWire:
        phrase = "invalid request wire data";
        break;
    case modbuslens::core::ReplayExecutionErrorCode::InvalidRequestFunction:
        phrase = "unsupported function code in request";
        break;
    case modbuslens::core::ReplayExecutionErrorCode::InvalidRequestData:
        phrase = "invalid request data";
        break;
    }
    // Core indexing is 0-based; humans count transactions from 1.
    return QStringLiteral("Replay analysis error at transaction %1: %2")
        .arg(error.transactionIndex + 1)
        .arg(QLatin1String(phrase));
}

} // namespace

AnalysisController::AnalysisController(QObject* parent)
    : QObject(parent)
{
    // Same source of truth as T007: an empty batch produces the zeroed
    // snapshot with undefined successRate/latency (hasX == false).
    applySnapshot(summarizeTransactions(
        std::span<const modbuslens::core::TransactionAnalysis>{}));

    // The adapter is the ONLY QSerialPort owner in the app layer; QML never
    // sees it. Its signals are the only entry doors for serial results/errors.
    connect(&serialAdapter_, &SerialTransactionAdapter::transactionCompleted,
            this, &AnalysisController::handleSerialTransactionCompleted);
    connect(&serialAdapter_, &SerialTransactionAdapter::transportError,
            this, &AnalysisController::handleSerialTransportError);

    // AI (T011 Part B): production config comes from the process environment
    // ONLY (BYOK). QML never sees the token — just aiConfigured.
    ModelScopeClientConfig productionConfig;
    if (buildModelScopeProductionConfig(productionConfig)) {
        aiClient_.configure(productionConfig);
        aiConfigured_ = true;
        aiModelName_ = productionConfig.modelId;
    }
    connect(&aiClient_, &ModelScopeDiagnosisClient::diagnosisSucceeded,
            this, &AnalysisController::handleAiSucceeded);
    connect(&aiClient_, &ModelScopeDiagnosisClient::diagnosisFailed,
            this, &AnalysisController::handleAiFailed);
}

int AnalysisController::observedCount() const
{
    return toInt(statistics_.observedCount);
}

int AnalysisController::pendingCount() const
{
    return toInt(statistics_.pendingCount);
}

int AnalysisController::completedCount() const
{
    return toInt(statistics_.completedCount);
}

int AnalysisController::successCount() const
{
    return toInt(statistics_.successCount);
}

int AnalysisController::exceptionCount() const
{
    return toInt(statistics_.exceptionCount);
}

int AnalysisController::crcErrorCount() const
{
    return toInt(statistics_.crcErrorCount);
}

int AnalysisController::timeoutCount() const
{
    return toInt(statistics_.timeoutCount);
}

int AnalysisController::protocolErrorCount() const
{
    return toInt(statistics_.protocolErrorCount);
}

bool AnalysisController::hasSuccessRate() const
{
    return statistics_.successRate.has_value();
}

double AnalysisController::successRate() const
{
    // 0.0 is only a safe placeholder when hasSuccessRate() is false — QML
    // must branch on hasSuccessRate first (never show "0%" for no data).
    return statistics_.successRate.value_or(0.0);
}

bool AnalysisController::hasAverageSuccessLatency() const
{
    return statistics_.averageSuccessLatencyMs.has_value();
}

double AnalysisController::averageSuccessLatencyMs() const
{
    return statistics_.averageSuccessLatencyMs.value_or(0.0);
}

QAbstractItemModel* AnalysisController::transactionModel()
{
    return &transactionModel_;
}

bool AnalysisController::hasReplayError() const
{
    return hasReplayError_;
}

QString AnalysisController::replayErrorMessage() const
{
    return replayErrorMessage_;
}

QString AnalysisController::modeLabel() const
{
    return modeLabel_;
}

QString AnalysisController::sourceLabel() const
{
    return sourceLabel_;
}

void AnalysisController::setReplayError(const QString& message)
{
    hasReplayError_ = true;
    replayErrorMessage_ = message;
    emit replayStateChanged();
}

void AnalysisController::clearReplayError()
{
    hasReplayError_ = false;
    replayErrorMessage_.clear();
    emit replayStateChanged();
}

// ---- T010 Part B: serial source ----

namespace {

constexpr std::array<int, 5> kSupportedSerialBauds = {9600, 19200, 38400, 57600, 115200};

// ---- T011 Part A: deterministic baseline presentation formatter ----
// Translates a DiagnosisReport (facts + structured codes) into readable
// text. ONLY translates — never re-judges statuses, never re-counts, never
// re-runs rules, never claims a root cause, never labels itself "AI".

QString findingPhrase(const modbuslens::core::DiagnosisFinding& finding)
{
    using namespace modbuslens::core;
    switch (finding.code) {
    case DiagnosisFindingCode::Healthy:
        return QStringLiteral("All %1 observed transactions completed successfully")
            .arg(finding.affectedCount);
    case DiagnosisFindingCode::PendingObserved:
        return QStringLiteral("Pending transactions: %1").arg(finding.affectedCount);
    case DiagnosisFindingCode::ExceptionObserved:
        return QStringLiteral("Device exception 0x%1: %2")
            .arg(QString::number(finding.exceptionCode.value_or(0), 16)
                     .toUpper()
                     .rightJustified(2, QLatin1Char('0')))
            .arg(finding.affectedCount);
    case DiagnosisFindingCode::CrcErrorObserved:
        return QStringLiteral("CRC integrity errors: %1").arg(finding.affectedCount);
    case DiagnosisFindingCode::TimeoutObserved:
        return QStringLiteral("No-response timeouts: %1").arg(finding.affectedCount);
    case DiagnosisFindingCode::ProtocolErrorObserved:
        return QStringLiteral("Protocol inconsistencies: %1").arg(finding.affectedCount);
    case DiagnosisFindingCode::NoData:
        return QStringLiteral("No analysis data available");
    }
    return {};
}

QString actionPhrase(modbuslens::core::DiagnosisActionCode action)
{
    using namespace modbuslens::core;
    switch (action) {
    case DiagnosisActionCode::WaitForCompletion:
        return QStringLiteral("Wait for in-flight transactions to complete");
    case DiagnosisActionCode::CheckDevicePower:
        return QStringLiteral("Verify device power");
    case DiagnosisActionCode::CheckSlaveAddress:
        return QStringLiteral("Verify slave address");
    case DiagnosisActionCode::CheckSerialSettings:
        return QStringLiteral("Check serial settings");
    case DiagnosisActionCode::CheckWiring:
        return QStringLiteral("Inspect wiring");
    case DiagnosisActionCode::CheckNoiseAndGrounding:
        return QStringLiteral("Inspect wiring and grounding/noise");
    case DiagnosisActionCode::CheckFunctionSupport:
        return QStringLiteral("Verify the requested function is supported");
    case DiagnosisActionCode::CheckRegisterMap:
        return QStringLiteral("Verify register map");
    case DiagnosisActionCode::CheckRequestParameters:
        return QStringLiteral("Check request parameters");
    case DiagnosisActionCode::CheckDeviceHealth:
        return QStringLiteral("Check device health");
    case DiagnosisActionCode::CheckDeviceDocumentation:
        return QStringLiteral("Consult device documentation");
    case DiagnosisActionCode::InspectProtocolConsistency:
        return QStringLiteral("Inspect protocol and response consistency");
    }
    return {};
}

QString formatDiagnosisReport(const modbuslens::core::DiagnosisReport& report)
{
    // A lone NoData report = "diagnosis was RUN on an empty batch" — a
    // different state from "no diagnosis has been run yet".
    if (report.findings.size() == 1
        && report.findings.front().code
            == modbuslens::core::DiagnosisFindingCode::NoData) {
        return QStringLiteral("No analysis data available");
    }

    QString text;
    text += QStringLiteral("Deterministic findings:\n");
    for (const auto& finding : report.findings) {
        text += QStringLiteral("- ") + findingPhrase(finding) + QLatin1Char('\n');
    }

    // Suggested checks: union of all findings' actions, deduplicated in the
    // fixed enum declaration order (deterministic — never unordered).
    constexpr std::array<modbuslens::core::DiagnosisActionCode, 12> kActionOrder = {
        modbuslens::core::DiagnosisActionCode::WaitForCompletion,
        modbuslens::core::DiagnosisActionCode::CheckDevicePower,
        modbuslens::core::DiagnosisActionCode::CheckSlaveAddress,
        modbuslens::core::DiagnosisActionCode::CheckSerialSettings,
        modbuslens::core::DiagnosisActionCode::CheckWiring,
        modbuslens::core::DiagnosisActionCode::CheckNoiseAndGrounding,
        modbuslens::core::DiagnosisActionCode::CheckFunctionSupport,
        modbuslens::core::DiagnosisActionCode::CheckRegisterMap,
        modbuslens::core::DiagnosisActionCode::CheckRequestParameters,
        modbuslens::core::DiagnosisActionCode::CheckDeviceHealth,
        modbuslens::core::DiagnosisActionCode::CheckDeviceDocumentation,
        modbuslens::core::DiagnosisActionCode::InspectProtocolConsistency,
    };
    const auto hasAction = [&report](modbuslens::core::DiagnosisActionCode action) {
        for (const auto& finding : report.findings) {
            for (const auto candidate : finding.recommendedActions) {
                if (candidate == action) {
                    return true;
                }
            }
        }
        return false;
    };
    QStringList checkLines;
    for (const auto action : kActionOrder) {
        if (hasAction(action)) {
            checkLines << actionPhrase(action);
        }
    }
    if (!checkLines.isEmpty()) {
        text += QStringLiteral("\nSuggested checks:\n");
        for (const auto& line : checkLines) {
            text += QStringLiteral("- ") + line + QLatin1Char('\n');
        }
    }
    return text;
}

} // namespace

bool AnalysisController::serialConnected() const
{
    return serialConnected_;
}

bool AnalysisController::serialBusy() const
{
    return serialBusy_;
}

bool AnalysisController::hasSerialError() const
{
    return hasSerialError_;
}

QString AnalysisController::serialErrorMessage() const
{
    return serialErrorMessage_;
}

QStringList AnalysisController::serialPortNames() const
{
    return serialPortNames_;
}

bool AnalysisController::hasBaselineDiagnosis() const
{
    return hasBaselineDiagnosis_;
}

QString AnalysisController::baselineDiagnosisText() const
{
    return baselineDiagnosisText_;
}

bool AnalysisController::aiConfigured() const
{
    return aiConfigured_;
}

bool AnalysisController::aiDiagnosisBusy() const
{
    return aiDiagnosisBusy_;
}

bool AnalysisController::hasAiDiagnosis() const
{
    return hasAiDiagnosis_;
}

QString AnalysisController::aiDiagnosisText() const
{
    return aiDiagnosisText_;
}

QString AnalysisController::aiDiagnosisErrorMessage() const
{
    return aiDiagnosisErrorMessage_;
}

QString AnalysisController::aiModelName() const
{
    return aiModelName_;
}

void AnalysisController::configureAiClient(const QUrl& endpoint,
                                           const QString& apiKey,
                                           const QString& modelId,
                                           std::chrono::milliseconds timeout)
{
    // C++-side test seam (never QML): explicit adoption of a client config.
    // An empty endpoint clears the configured state ("not configured" path).
    if (endpoint.isEmpty()) {
        aiConfigured_ = false;
        aiModelName_.clear();
        emit aiStateChanged();
        return;
    }
    aiClient_.configure(ModelScopeClientConfig{
        .endpoint = endpoint,
        .apiKey = apiKey,
        .modelId = modelId,
        .timeout = timeout,
    });
    aiConfigured_ = true;
    aiModelName_ = modelId;
    emit aiStateChanged();
}

void AnalysisController::setAiError(const QString& message)
{
    aiDiagnosisErrorMessage_ =
        QStringLiteral("Latest AI request failed: %1").arg(message);
    emit aiStateChanged();
}

void AnalysisController::handleAiSucceeded(std::uint64_t requestId,
                                           const QString& text)
{
    // Two-dimensional stale guard: the delivery is applied only when BOTH
    // dimensions still match — same analysis batch AND latest AI request.
    if (!activeAiRequestId_.has_value() || requestId != *activeAiRequestId_) {
        return;
    }
    if (!requestBatchRevision_.has_value()
        || *requestBatchRevision_ != activeBatchRevision_) {
        return;
    }
    activeAiRequestId_.reset();
    requestBatchRevision_.reset();
    aiDiagnosisBusy_ = false;
    hasAiDiagnosis_ = true;
    aiDiagnosisText_ = text;
    aiDiagnosisErrorMessage_.clear();
    emit aiStateChanged();
}

void AnalysisController::handleAiFailed(std::uint64_t requestId,
                                        AiDiagnosisErrorCode code,
                                        const QString& sanitizedMessage)
{
    if (!activeAiRequestId_.has_value() || requestId != *activeAiRequestId_) {
        return;
    }
    if (!requestBatchRevision_.has_value()
        || *requestBatchRevision_ != activeBatchRevision_) {
        return;
    }
    activeAiRequestId_.reset();
    requestBatchRevision_.reset();
    aiDiagnosisBusy_ = false;
    // A failed attempt keeps a previous same-batch explanation (if any):
    // error and result may legitimately coexist.
    Q_UNUSED(code);
    setAiError(sanitizedMessage);
}

void AnalysisController::cancelAiDiagnosis()
{
    if (!aiDiagnosisBusy_ && !activeAiRequestId_.has_value()) {
        return;
    }
    // Invalidate identity FIRST, then abort — a late finished() delivery can
    // never write UI state again (UI-AI11).
    ++aiRequestGeneration_;
    activeAiRequestId_.reset();
    requestBatchRevision_.reset();
    aiClient_.cancel();
    aiDiagnosisBusy_ = false;
    emit aiStateChanged();
}

void AnalysisController::askAiDiagnosis()
{
    // C++ re-validates every precondition (the QML button is only UX).
    if (!aiConfigured_) {
        setAiError(QStringLiteral(
            "ModelScope API token is not configured. "
            "Set MODELSCOPE_API_KEY and restart the app."));
        return;
    }
    if (activeDiagnosisTransactions_.empty()) {
        setAiError(QStringLiteral("No analysis data available."));
        return;
    }
    if (!hasBaselineDiagnosis_) {
        setAiError(QStringLiteral("Run Baseline Diagnosis first."));
        return;
    }
    if (aiDiagnosisBusy_) {
        return;
    }

    // Prompt is derived ONLY from structured deterministic facts.
    const auto context = modbuslens::core::buildDiagnosisContext(
        activeDiagnosisTransactions_);
    const auto report = modbuslens::core::diagnoseTransactions(context);
    const auto prompt = buildDiagnosisPrompt(context, report);

    ++aiRequestGeneration_;
    activeAiRequestId_ = aiRequestGeneration_;
    requestBatchRevision_ = activeBatchRevision_;
    aiDiagnosisBusy_ = true;
    aiDiagnosisErrorMessage_.clear(); // clear the LATEST error, keep old text
    emit aiStateChanged();
    aiClient_.requestDiagnosis(prompt.systemInstructions, prompt.userPrompt,
                               aiRequestGeneration_);
}

void AnalysisController::invalidateAiForBatchChange()
{
    // Backing state goes consistent BEFORE any notification (§36): bump the
    // batch revision, abort any in-flight AI request and invalidate its
    // identity, then clear every derived view (AI result/error + baseline).
    ++activeBatchRevision_;
    if (aiDiagnosisBusy_ || activeAiRequestId_.has_value()) {
        ++aiRequestGeneration_;
        activeAiRequestId_.reset();
        requestBatchRevision_.reset();
        aiClient_.cancel();
    }
    aiDiagnosisBusy_ = false;
    hasAiDiagnosis_ = false;
    aiDiagnosisText_.clear();
    aiDiagnosisErrorMessage_.clear();
    clearDiagnosisState();
    emit aiStateChanged();
}

void AnalysisController::clearDiagnosisState()
{
    hasBaselineDiagnosis_ = false;
    baselineDiagnosisText_.clear();
    emit diagnosisChanged();
}

void AnalysisController::clearDiagnosis()
{
    // Diagnosis-only clear: the batch, rows, statistics and source all stay.
    // Part B: this also drops the AI explanation and aborts an in-flight AI
    // request (identity invalidation first). The batch revision does NOT
    // change — the deterministic facts did not change.
    if (aiDiagnosisBusy_ || activeAiRequestId_.has_value()) {
        ++aiRequestGeneration_;
        activeAiRequestId_.reset();
        requestBatchRevision_.reset();
        aiClient_.cancel();
    }
    aiDiagnosisBusy_ = false;
    hasAiDiagnosis_ = false;
    aiDiagnosisText_.clear();
    aiDiagnosisErrorMessage_.clear();
    clearDiagnosisState();
    emit aiStateChanged();
}

void AnalysisController::runBaselineDiagnosis()
{
    // Presentation is generated ONLY from the rule core's report — the
    // controller never re-judges statuses or hand-counts here.
    const auto context = modbuslens::core::buildDiagnosisContext(
        activeDiagnosisTransactions_);
    const auto report = modbuslens::core::diagnoseTransactions(context);
    baselineDiagnosisText_ = formatDiagnosisReport(report);
    hasBaselineDiagnosis_ = true;
    emit diagnosisChanged();
}

void AnalysisController::setSerialError(const QString& message)
{
    hasSerialError_ = true;
    serialErrorMessage_ = message;
    emit serialErrorChanged();
}

void AnalysisController::clearSerialError()
{
    hasSerialError_ = false;
    serialErrorMessage_.clear();
    emit serialErrorChanged();
}

void AnalysisController::teardownSerialTransport()
{
    // Silent local close (adapter guarantees no transportError on intent):
    // cancels pending transaction, closes port, resets every serial flag.
    serialAdapter_.closePort();
    serialConnected_ = false;
    serialBusy_ = false;
    pendingSerialAddress_.reset();
    emit serialConnChanged();
    emit serialStatusChanged();
}

void AnalysisController::handleSerialTransportError(const QString& message)
{
    // A transport failure is NOT a Modbus diagnosis: sync state from the
    // adapter and surface the message — statistics/rows/mode/source stay
    // exactly as they are (old completed batch remains visible).
    serialBusy_ = false;
    pendingSerialAddress_.reset();
    serialConnected_ = serialAdapter_.isPortOpen();
    setSerialError(QStringLiteral("Serial transport error: %1").arg(message));
    emit serialConnChanged();
    emit serialStatusChanged();
}

void AnalysisController::refreshSerialPorts()
{
    // Discovery only: enumerate. NEVER open/write/probe any detected port.
    QStringList names;
    const auto ports = QSerialPortInfo::availablePorts();
    names.reserve(ports.size());
    for (const auto& info : ports) {
        if (!info.portName().isEmpty()) {
            names.append(info.portName());
        }
    }
    serialPortNames_ = std::move(names);
    emit serialPortsChanged();
}

void AnalysisController::connectSerial(const QString& portName, int baudRate)
{
    if (portName.isEmpty()) {
        setSerialError(QStringLiteral("Serial error: empty port name"));
        return;
    }
    if (std::find(kSupportedSerialBauds.begin(), kSupportedSerialBauds.end(), baudRate)
        == kSupportedSerialBauds.end()) {
        setSerialError(QStringLiteral("Serial error: unsupported baud rate"));
        return;
    }

    // Open FIRST; only a fully successful transport open may transition the
    // source. On failure the adapter's bounded transportError reaches
    // handleSerialTransportError — the old batch/mode/source stay untouched
    // (UI-S02).
    if (!serialAdapter_.openPort(portName, baudRate)) {
        return;
    }

    // Connect success = explicit source transition: clear the old active
    // batch so a Serial header can never sit over Replay/Demo rows.
    applySnapshot(summarizeTransactions(
        std::span<const modbuslens::core::TransactionAnalysis>{}));
    transactionModel_.setEntries({});
    activeDiagnosisTransactions_.clear();
    invalidateAiForBatchChange();

    serialSourceLabel_ = QStringLiteral("%1 @ %2").arg(portName).arg(baudRate);
    modeLabel_ = QStringLiteral("Serial Mode");
    sourceLabel_ = serialSourceLabel_;
    serialConnected_ = true;
    serialBusy_ = false;
    clearSerialError();
    emit serialConnChanged();
    emit serialStatusChanged();
    emit sourceChanged();
}

void AnalysisController::disconnectSerial()
{
    // Intentional disconnect: transport-only. The last completed result and
    // the Serial source identity stay on the dashboard — Clear is the only
    // clearer, and no transport error is shown for a user close.
    teardownSerialTransport();
}

void AnalysisController::readHoldingRegistersOnce(
    int slaveAddress, int startAddress, int quantity, int timeoutMs)
{
    // Range validation BEFORE any narrowing cast: QML numbers arrive as int,
    // and a silent uint8_t/uint16_t wrap here would be undefined-behavior
    // territory the Core must never be handed.
    if (slaveAddress < 1 || slaveAddress > 247) {
        setSerialError(QStringLiteral("Serial error: slave address must be 1..247"));
        return;
    }
    if (startAddress < 0 || startAddress > 65535) {
        setSerialError(QStringLiteral("Serial error: start address must be 0..65535"));
        return;
    }
    if (quantity < 1 || quantity > 125) {
        setSerialError(QStringLiteral("Serial error: quantity must be 1..125"));
        return;
    }
    if (timeoutMs <= 0) {
        setSerialError(QStringLiteral("Serial error: timeout must be > 0 ms"));
        return;
    }
    if (!serialConnected_) {
        setSerialError(QStringLiteral("Serial error: port not connected"));
        return;
    }
    if (serialBusy_) {
        setSerialError(QStringLiteral("Serial error: a transaction is already in progress"));
        return;
    }

    // Accept only writes metadata AFTER the adapter really accepted: a
    // failure drops us back with no pending state at all.
    if (!serialAdapter_.startTransaction(
            static_cast<std::uint8_t>(slaveAddress),
            static_cast<std::uint16_t>(startAddress),
            static_cast<std::uint16_t>(quantity),
            std::chrono::milliseconds{timeoutMs})) {
        return; // adapter already reported the bounded transport error
    }

    pendingSerialAddress_ = static_cast<std::uint8_t>(slaveAddress);
    serialBusy_ = true;
    clearSerialError();
    emit serialStatusChanged();
    // The previous completed result stays visible until the new analysis
    // replaces it (Reading... state).
}

void AnalysisController::publishSerialResult(
    const QString& sourceLabel, int deviceAddress,
    const modbuslens::core::TransactionAnalysis& analysis)
{
    // Hardware-free mapping seam: one row + one-element statistics batch,
    // replace semantics (rowCount is always 1 after a serial read).
    std::vector<modbuslens::core::TransactionAnalysis> batch{analysis};
    auto snapshot = modbuslens::core::summarizeTransactions(batch);

    TransactionListEntry entry{
        .deviceAddress = deviceAddress,
        .functionCode = 0x03,
        .status = analysis.status,
        .elapsedMs = analysis.elapsed.count(),
        .exceptionCode = analysis.exceptionCode,
    };

    transactionModel_.setEntries({std::move(entry)});
    statistics_ = std::move(snapshot);
    activeDiagnosisTransactions_ = {
        modbuslens::core::DiagnosisTransaction{
            .deviceAddress = static_cast<std::uint8_t>(deviceAddress),
            .functionCode = 0x03,
            .analysis = analysis,
        },
    };
    invalidateAiForBatchChange();
    modeLabel_ = QStringLiteral("Serial Mode");
    sourceLabel_ = sourceLabel;
    serialBusy_ = false;
    clearSerialError();
    emit statisticsChanged();
    emit sourceChanged();
    emit serialStatusChanged();
}

void AnalysisController::handleSerialTransactionCompleted(
    const modbuslens::core::TransactionAnalysis& analysis)
{
    // Stale-completion guard (UI-S10): an analysis with no pending metadata
    // (e.g. after a source switch aged the completion out) must NEVER
    // overwrite the current Simulator/Replay batch.
    if (!pendingSerialAddress_.has_value()) {
        return;
    }
    publishSerialResult(serialSourceLabel_,
                        static_cast<int>(*pendingSerialAddress_), analysis);
    pendingSerialAddress_.reset();
}

void AnalysisController::applySnapshot(
    const modbuslens::core::TransactionStatisticsSnapshot& snapshot)
{
    statistics_ = snapshot;
    emit statisticsChanged();
}

void AnalysisController::setTransactionEntries(
    std::vector<TransactionListEntry> entries)
{
    transactionModel_.setEntries(std::move(entries));
}

// ---- Part B: deterministic demo orchestration ----

void AnalysisController::runDemoBatch()
{
    using ms = std::chrono::milliseconds;
    constexpr ms kThreshold{1000};

    // Source transition: leave the serial transport entirely BEFORE
    // producing Simulator data — no background COM while in Simulator Mode.
    teardownSerialTransport();

    // Deterministic slave (rebuilt each invocation for reproducibility).
    modbuslens::core::SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);
    slave.setHoldingRegister(2, 1500);

    std::vector<modbuslens::core::TransactionAnalysis> analyses;
    std::vector<TransactionListEntry> entries;
    std::vector<modbuslens::core::DiagnosisTransaction> diagnosisTransactions;
    analyses.reserve(4);
    entries.reserve(4);
    diagnosisTransactions.reserve(4);

    // One batch, three same-source views: rows, statistics and the
    // structured diagnosis input all come from the VERY same analyses.
    auto makeEntry = [&](const modbuslens::core::ModbusRtuFrame& request,
                         const modbuslens::core::TransactionAnalysis& analysis) {
        entries.push_back(TransactionListEntry{
            .deviceAddress = request.address,
            .functionCode = static_cast<int>(request.functionCode),
            .status = analysis.status,
            .elapsedMs = analysis.elapsed.count(),
            .exceptionCode = analysis.exceptionCode,
        });
        diagnosisTransactions.push_back(modbuslens::core::DiagnosisTransaction{
            .deviceAddress = request.address,
            .functionCode = request.functionCode,
            .analysis = analysis,
        });
    };

    // DEMO-1: Success (start=0, qty=2 → {100, 200})
    {
        const auto request = makeFc03Read(0x01, 0, 2);
        const auto slaveResult = slave.handleRequest(request);
        const auto* response = std::get_if<modbuslens::core::ModbusRtuFrame>(&slaveResult);
        if (!response) {
            qWarning("runDemoBatch: DEMO-1 slave returned non-frame result");
            return;
        }
        analyses.push_back(modbuslens::core::analyzeFunction03Transaction(
            request, modbuslens::core::ResponseObservation{*response}, ms{25}, kThreshold));
        makeEntry(request, analyses.back());
    }

    // DEMO-2: Exception (start=100, qty=1 → out of range → 0x83/{0x02})
    {
        const auto request = makeFc03Read(0x01, 100, 1);
        const auto slaveResult = slave.handleRequest(request);
        const auto* response = std::get_if<modbuslens::core::ModbusRtuFrame>(&slaveResult);
        if (!response) {
            qWarning("runDemoBatch: DEMO-2 slave returned non-frame result");
            return;
        }
        analyses.push_back(modbuslens::core::analyzeFunction03Transaction(
            request, modbuslens::core::ResponseObservation{*response}, ms{18}, kThreshold));
        makeEntry(request, analyses.back());
    }

    // DEMO-3: CRC Error (correct response → CorruptCrc → CrcMismatch)
    {
        const auto request = makeFc03Read(0x01, 0, 2);
        const auto slaveResult = slave.handleRequest(request);
        const auto* response = std::get_if<modbuslens::core::ModbusRtuFrame>(&slaveResult);
        if (!response) {
            qWarning("runDemoBatch: DEMO-3 slave returned non-frame result");
            return;
        }

        const auto wire = modbuslens::core::encodeRtuFrame(*response);
        const auto delivery = modbuslens::core::applySimulationFault(
            wire,
            modbuslens::core::SimulationFaultConfig{
                .mode = modbuslens::core::SimulationFaultMode::CorruptCrc});
        const auto* corrupted =
            std::get_if<modbuslens::core::DeliveredWire>(&delivery);
        if (!corrupted) {
            qWarning("runDemoBatch: DEMO-3 expected DeliveredWire");
            return;
        }

        const auto decodeResult =
            modbuslens::core::decodeRtuFrame(corrupted->bytes);
        const auto* decodeError =
            std::get_if<modbuslens::core::RtuDecodeError>(&decodeResult);
        if (!decodeError) {
            qWarning("runDemoBatch: DEMO-3 expected decode error");
            return;
        }

        analyses.push_back(modbuslens::core::analyzeFunction03Transaction(
            request, modbuslens::core::ResponseObservation{*decodeError}, ms{17}, kThreshold));
        makeEntry(request, analyses.back());
    }

    // DEMO-4: Timeout (DropResponse → NoResponse → elapsed >= threshold)
    {
        const auto request = makeFc03Read(0x01, 0, 2);
        const auto slaveResult = slave.handleRequest(request);
        const auto* response = std::get_if<modbuslens::core::ModbusRtuFrame>(&slaveResult);
        if (!response) {
            qWarning("runDemoBatch: DEMO-4 slave returned non-frame result");
            return;
        }

        const auto wire = modbuslens::core::encodeRtuFrame(*response);
        const auto delivery = modbuslens::core::applySimulationFault(
            wire,
            modbuslens::core::SimulationFaultConfig{
                .mode = modbuslens::core::SimulationFaultMode::DropResponse});
        if (!std::holds_alternative<modbuslens::core::DroppedResponse>(delivery)) {
            qWarning("runDemoBatch: DEMO-4 expected DroppedResponse");
            return;
        }

        // T006 delivers DropResponse (delivery fact); T007 judges Timeout
        // from NoResponse + elapsed >= threshold.
        analyses.push_back(modbuslens::core::analyzeFunction03Transaction(
            request, modbuslens::core::ResponseObservation{modbuslens::core::NoResponse{}}, ms{1000}, kThreshold));
        makeEntry(request, analyses.back());
    }

    // Atomic batch publish: model + snapshot from same source. Backing state
    // (diagnosis batch + invalidated diagnosis) is made consistent BEFORE
    // any notification so QML can never observe new-stats + old-diagnosis.
    auto snapshot = modbuslens::core::summarizeTransactions(analyses);
    transactionModel_.setEntries(std::move(entries));
    statistics_ = std::move(snapshot);
    activeDiagnosisTransactions_ = std::move(diagnosisTransactions);
    invalidateAiForBatchChange();
    modeLabel_ = QStringLiteral("Simulator Mode");
    sourceLabel_ = QStringLiteral("Deterministic Demo");
    clearReplayError();
    clearSerialError();
    emit statisticsChanged();
    emit sourceChanged();
}

void AnalysisController::clearResults()
{
    // Clears the analysis results and any pending replay/serial error, but
    // NEVER switches the source or closes the transport (Clear !=
    // Disconnect): the user still sees which mode/source they are in.
    applySnapshot(summarizeTransactions(
        std::span<const modbuslens::core::TransactionAnalysis>{}));
    transactionModel_.setEntries({});
    activeDiagnosisTransactions_.clear();
    invalidateAiForBatchChange();
    clearReplayError();
    clearSerialError();
}

void AnalysisController::loadReplayFile(const QUrl& fileUrl)
{
    using namespace modbuslens::core;

    if (!fileUrl.isLocalFile()) {
        setReplayError(QStringLiteral("Replay load failed: not a local file"));
        return;
    }

    const QString filePath = fileUrl.toLocalFile();
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setReplayError(QStringLiteral("Replay load failed: cannot open file"));
        return;
    }

    // QByteArray outlives the parse call; the resulting ReplayLog owns its
    // own bytes, so nothing view-like escapes this scope.
    const QByteArray contents = file.readAll();
    const std::string_view text{
        contents.constData(), static_cast<std::size_t>(contents.size())};

    const auto parseResult = parseReplayLog(text);
    if (const auto* parseError = std::get_if<ReplayParseError>(&parseResult)) {
        setReplayError(parseErrorMessage(*parseError));
        return;
    }
    const auto& replayLog = std::get<ReplayLog>(parseResult);

    const auto analysisResult = analyzeReplayLog(replayLog);
    if (const auto* executionError =
            std::get_if<ReplayExecutionError>(&analysisResult)) {
        setReplayError(executionErrorMessage(*executionError));
        return;
    }
    const auto& batch = std::get<ReplayBatchAnalysis>(analysisResult);

    // Adapter: ReplayTransactionOutcome -> existing presentation entries.
    // No second list model; the shared dashboard renders whatever batch is
    // current, no matter which source produced it.
    std::vector<TransactionListEntry> entries;
    std::vector<modbuslens::core::DiagnosisTransaction> diagnosisTransactions;
    entries.reserve(batch.transactions.size());
    diagnosisTransactions.reserve(batch.transactions.size());
    for (const auto& outcome : batch.transactions) {
        entries.push_back(TransactionListEntry{
            .deviceAddress = static_cast<int>(outcome.deviceAddress),
            .functionCode = static_cast<int>(outcome.functionCode),
            .status = outcome.analysis.status,
            .elapsedMs = outcome.analysis.elapsed.count(),
            .exceptionCode = outcome.analysis.exceptionCode,
        });
        diagnosisTransactions.push_back(modbuslens::core::DiagnosisTransaction{
            .deviceAddress = outcome.deviceAddress,
            .functionCode = outcome.functionCode,
            .analysis = outcome.analysis,
        });
    }

    // Atomic publish (rule B): everything below runs only after the whole
    // read/parse/analyze/adapt pipeline succeeded. Any earlier failure
    // returned without touching a single piece of the old state (rule A) —
    // including an open serial transport, which stays intact on a failed
    // replay load.
    //
    // Serial teardown is part of the successful switch ONLY (SB-13): the
    // port is closed after the replay data is fully validated, never before.
    teardownSerialTransport();
    transactionModel_.setEntries(std::move(entries));
    statistics_ = batch.statistics;
    activeDiagnosisTransactions_ = std::move(diagnosisTransactions);
    invalidateAiForBatchChange();
    modeLabel_ = QStringLiteral("Replay Mode");
    // Presentation keeps the basename only; the full path never enters the UI.
    sourceLabel_ = QFileInfo(filePath).fileName();
    clearReplayError();
    clearSerialError();
    emit statisticsChanged();
    emit sourceChanged();
}