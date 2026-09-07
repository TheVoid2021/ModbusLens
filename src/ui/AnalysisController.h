#pragma once

#include <QAbstractItemModel>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqml.h>

#include <chrono>
#include <optional>

#include "core/analysis/TransactionStatistics.h"
#include "ui/TransactionListModel.h"

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

    // C++-side data entry points (not Q_INVOKABLE): Part B's demo flow and
    // tests call these; QML only reads.
    void applySnapshot(const modbuslens::core::TransactionStatisticsSnapshot& snapshot);
    void setTransactionEntries(std::vector<TransactionListEntry> entries);

signals:
    void statisticsChanged();
    void replayStateChanged();
    void sourceChanged();

private:
    void setReplayError(const QString& message);
    void clearReplayError();

    modbuslens::core::TransactionStatisticsSnapshot statistics_;
    TransactionListModel transactionModel_;

    bool hasReplayError_ = false;
    QString replayErrorMessage_;
    QString modeLabel_ = QStringLiteral("Simulator Mode");
    QString sourceLabel_ = QStringLiteral("Deterministic Demo");
};