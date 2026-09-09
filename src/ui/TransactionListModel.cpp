#include "ui/TransactionListModel.h"

namespace {

// Adapter-only label mapping: enum -> UI text. Never placed in core, which
// stays language-neutral.
QString statusText(modbuslens::core::TransactionStatus status)
{
    switch (status) {
    case modbuslens::core::TransactionStatus::Pending:
        return QStringLiteral("进行中");
    case modbuslens::core::TransactionStatus::Success:
        return QStringLiteral("成功");
    case modbuslens::core::TransactionStatus::Exception:
        return QStringLiteral("异常");
    case modbuslens::core::TransactionStatus::CrcError:
        return QStringLiteral("CRC 错误");
    case modbuslens::core::TransactionStatus::Timeout:
        return QStringLiteral("超时");
    case modbuslens::core::TransactionStatus::ProtocolError:
        return QStringLiteral("协议错误");
    }
    return QStringLiteral("Unknown");
}

} // namespace

TransactionListModel::TransactionListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int TransactionListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(entries_.size());
}

QVariant TransactionListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(entries_.size())) {
        return QVariant{};
    }

    const TransactionListEntry& entry = entries_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case DeviceAddressRole:
        return entry.deviceAddress;
    case FunctionCodeRole:
        return entry.functionCode;
    case StatusCodeRole:
        return static_cast<int>(entry.status);
    case StatusTextRole:
        return statusText(entry.status);
    case ElapsedMsRole:
        return entry.elapsedMs;
    case HasExceptionCodeRole:
        return entry.exceptionCode.has_value();
    case ExceptionCodeRole:
        // Safe placeholder: 0 when absent — QML must check
        // HasExceptionCodeRole first (hasX/value pattern).
        return entry.exceptionCode.has_value()
            ? QVariant{static_cast<int>(*entry.exceptionCode)}
            : QVariant{0};
    default:
        return QVariant{};
    }
}

QHash<int, QByteArray> TransactionListModel::roleNames() const
{
    return {
        {DeviceAddressRole, QByteArrayLiteral("deviceAddress")},
        {FunctionCodeRole, QByteArrayLiteral("functionCode")},
        {StatusCodeRole, QByteArrayLiteral("statusCode")},
        {StatusTextRole, QByteArrayLiteral("statusText")},
        {ElapsedMsRole, QByteArrayLiteral("elapsedMs")},
        {HasExceptionCodeRole, QByteArrayLiteral("hasExceptionCode")},
        {ExceptionCodeRole, QByteArrayLiteral("exceptionCode")},
    };
}

void TransactionListModel::setEntries(std::vector<TransactionListEntry> entries)
{
    beginResetModel();
    entries_ = std::move(entries);
    endResetModel();
}