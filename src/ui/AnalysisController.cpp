#include "ui/AnalysisController.h"

#include "core/analysis/TransactionStatistics.h"
#include "core/protocol/Function03.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "core/replay/ReplayAnalysis.h"
#include "core/replay/ReplayLog.h"
#include "core/simulator/SimulatedSlave.h"
#include "core/simulator/SimulationFault.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

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

    // Deterministic slave (rebuilt each invocation for reproducibility).
    modbuslens::core::SimulatedSlave slave{0x01};
    slave.setHoldingRegister(0, 100);
    slave.setHoldingRegister(1, 200);
    slave.setHoldingRegister(2, 1500);

    std::vector<modbuslens::core::TransactionAnalysis> analyses;
    std::vector<TransactionListEntry> entries;
    analyses.reserve(4);
    entries.reserve(4);

    auto makeEntry = [&](const modbuslens::core::ModbusRtuFrame& request,
                         const modbuslens::core::TransactionAnalysis& analysis) {
        entries.push_back(TransactionListEntry{
            .deviceAddress = request.address,
            .functionCode = static_cast<int>(request.functionCode),
            .status = analysis.status,
            .elapsedMs = analysis.elapsed.count(),
            .exceptionCode = analysis.exceptionCode,
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

    // Atomic batch publish: model + snapshot from same source.
    auto snapshot = modbuslens::core::summarizeTransactions(analyses);
    transactionModel_.setEntries(std::move(entries));
    statistics_ = std::move(snapshot);
    modeLabel_ = QStringLiteral("Simulator Mode");
    sourceLabel_ = QStringLiteral("Deterministic Demo");
    clearReplayError();
    emit statisticsChanged();
    emit sourceChanged();
}

void AnalysisController::clearResults()
{
    // Clears the analysis results and any pending replay error, but NEVER
    // switches the source: the user still sees which mode/source they are in
    // (only runDemoBatch explicitly switches to Simulator).
    applySnapshot(summarizeTransactions(
        std::span<const modbuslens::core::TransactionAnalysis>{}));
    transactionModel_.setEntries({});
    clearReplayError();
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
    entries.reserve(batch.transactions.size());
    for (const auto& outcome : batch.transactions) {
        entries.push_back(TransactionListEntry{
            .deviceAddress = static_cast<int>(outcome.deviceAddress),
            .functionCode = static_cast<int>(outcome.functionCode),
            .status = outcome.analysis.status,
            .elapsedMs = outcome.analysis.elapsed.count(),
            .exceptionCode = outcome.analysis.exceptionCode,
        });
    }

    // Atomic publish (rule B): everything below runs only after the whole
    // read/parse/analyze/adapt pipeline succeeded. Any earlier failure
    // returned without touching a single piece of the old state (rule A).
    transactionModel_.setEntries(std::move(entries));
    statistics_ = batch.statistics;
    modeLabel_ = QStringLiteral("Replay Mode");
    // Presentation keeps the basename only; the full path never enters the UI.
    sourceLabel_ = QFileInfo(filePath).fileName();
    clearReplayError();
    emit statisticsChanged();
    emit sourceChanged();
}