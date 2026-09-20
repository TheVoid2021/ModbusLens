#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

#include "core/active/ActiveTransactionEvidence.h"
#include "core/analysis/TransactionAnalysis.h"

// App/Presentation adapter layer: UI-facing list of recent transactions.
// NOT part of modbuslens_core — Qt types are allowed only in this layer.
struct TransactionListEntry {
    int deviceAddress{};
    int functionCode{};
    modbuslens::core::TransactionStatus status{};
    qint64 elapsedMs{};
    std::optional<std::uint8_t> exceptionCode;
    // T014: deterministic secondary text for ProtocolError rows formatted in
    // this Qt adapter (never in Core). Empty for every other row.
    QString issueText;
    // M10-A: Active Serial provenance + wire evidence, OPTIONAL and
    // provenance-scoped on purpose — Simulator/Replay/passive rows and every
    // pre-M10 fixture construct the entry without it (no fake transport facts
    // are ever invented for sources that have none).
    std::optional<modbuslens::core::ActiveSerialProvenance> activeSerialProvenance;
};

// Adapter formatting: TransactionIssue -> conservative deterministic Chinese
// presentation text (observed facts only; explicitly NOT root-cause prose).
QString issueDetailText(const modbuslens::core::TransactionAnalysis& analysis);

// T015: request-side facts -> conservative Chinese text (adapter only; Core
// never carries presentation strings). Multiple issues are joined in the
// CORE-determined order — the adapter re-sorts nothing.
QString requestIssueDetailText(
    const std::vector<modbuslens::core::TransactionRequestIssue>& requestIssues);

class TransactionListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        DeviceAddressRole = Qt::UserRole + 1,
        FunctionCodeRole,
        StatusCodeRole,
        StatusTextRole,
        ElapsedMsRole,
        HasExceptionCodeRole,
        ExceptionCodeRole,
        IssueTextRole
    };

    explicit TransactionListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Whole-batch replacement from the C++ side (not Q_INVOKABLE); QML only
    // reads. Used for SOURCE TRANSITION only: Simulator/Replay batches and
    // Clear Results replace the visible set wholesale, and the reset
    // invalidates page-local presentation state (M9-D selection contract).
    void setEntries(std::vector<TransactionListEntry> entries);

    // M10-B: TRUE APPEND — the Active Serial session history grows one
    // transaction at a time. Insertion (never a reset) is the whole point:
    // rows already on screen keep their content, their order and their
    // identity, and a page-local selection pointing at an existing row stays
    // valid. append != source replacement; never make setEntries() append.
    void appendEntries(std::vector<TransactionListEntry> entries);

private:
    std::vector<TransactionListEntry> entries_;
};