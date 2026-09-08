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
    // returned without touching a single piece of the old state (rule A) —
    // including an open serial transport, which stays intact on a failed
    // replay load.
    //
    // Serial teardown is part of the successful switch ONLY (SB-13): the
    // port is closed after the replay data is fully validated, never before.
    teardownSerialTransport();
    transactionModel_.setEntries(std::move(entries));
    statistics_ = batch.statistics;
    modeLabel_ = QStringLiteral("Replay Mode");
    // Presentation keeps the basename only; the full path never enters the UI.
    sourceLabel_ = QFileInfo(filePath).fileName();
    clearReplayError();
    clearSerialError();
    emit statisticsChanged();
    emit sourceChanged();
}