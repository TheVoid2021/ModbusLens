#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

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
    // reads. No append/remove/paging — no requirement exists in Part A.
    void setEntries(std::vector<TransactionListEntry> entries);

private:
    std::vector<TransactionListEntry> entries_;
};