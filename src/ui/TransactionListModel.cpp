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

// T014: issue -> conservative deterministic presentation text. Observed
// facts only — deliberately NEVER root-cause prose like "地址配置错误" /
// "设备返回了错误寄存器" / "线路坏了". Empty string when no issue
// exists (including the defensive issue-less ProtocolError, refinement A).
QString issueDetailText(const modbuslens::core::TransactionAnalysis& analysis)
{
    if (!analysis.issue.has_value()) {
        return QString();
    }
    using modbuslens::core::TransactionIssueCode;
    const auto& issue = *analysis.issue;
    const auto hex2 = [](std::uint8_t value) {
        return QString::number(value, 16).toUpper().rightJustified(2, QLatin1Char('0'));
    };
    switch (issue.code) {
    case TransactionIssueCode::ResponseFrameTooShort:
        return QStringLiteral("响应帧过短（不足最小帧长）");
    case TransactionIssueCode::ResponseAddressMismatch:
        if (issue.expectedAddress.has_value() && issue.actualAddress.has_value()) {
            return QStringLiteral("响应地址不匹配（请求 0x%1 / 响应 0x%2）")
                .arg(hex2(*issue.expectedAddress), hex2(*issue.actualAddress));
        }
        return QStringLiteral("响应地址不匹配");
    case TransactionIssueCode::MalformedExceptionResponse:
        return QStringLiteral("异常响应格式非法");
    case TransactionIssueCode::MalformedNormalResponse:
        return QStringLiteral("正常响应格式非法");
    case TransactionIssueCode::QuantityMismatch:
        if (issue.expectedQuantity.has_value() && issue.actualQuantity.has_value()) {
            return QStringLiteral("寄存器数量不匹配（请求 %1 / 响应 %2）")
                .arg(*issue.expectedQuantity)
                .arg(*issue.actualQuantity);
        }
        return QStringLiteral("寄存器数量不匹配");
    case TransactionIssueCode::UnexpectedResponseFunction:
        if (issue.actualFunctionCode.has_value()) {
            return QStringLiteral("响应功能码不符（实际 0x%1）")
                .arg(hex2(*issue.actualFunctionCode));
        }
        return QStringLiteral("响应功能码不符");
    case TransactionIssueCode::UnknownProtocolError:
        return QStringLiteral("协议错误（未记录细节）");
    }
    return QString();
}

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
    case IssueTextRole:
        return entry.issueText;
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
        {IssueTextRole, QByteArrayLiteral("issueText")},
    };
}

void TransactionListModel::setEntries(std::vector<TransactionListEntry> entries)
{
    beginResetModel();
    entries_ = std::move(entries);
    endResetModel();
}