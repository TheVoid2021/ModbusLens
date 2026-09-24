#include "ui/TransactionListModel.h"

#include <QStringList>

#include <iterator>
#include <utility>

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
    case modbuslens::core::TransactionStatus::ExpectedNoResponse:
        // T015 Gate C: truthful broadcast wording — never 成功 / 超时.
        return QStringLiteral("预期无响应");
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
    case TransactionIssueCode::WriteSingleRegisterEchoMismatch:
        if (issue.expectedRegisterAddress.has_value() && issue.actualRegisterAddress.has_value()
            && issue.expectedRegisterValue.has_value() && issue.actualRegisterValue.has_value()) {
            const auto hex4 = [](std::uint16_t value) {
                return QStringLiteral("0x")
                    + QString::number(value, 16).toUpper().rightJustified(4, QLatin1Char('0'));
            };
            return QStringLiteral("写入回显不匹配（请求 %1=%2 / 响应 %3=%4）")
                .arg(hex4(*issue.expectedRegisterAddress),
                     hex4(*issue.expectedRegisterValue),
                     hex4(*issue.actualRegisterAddress),
                     hex4(*issue.actualRegisterValue));
        }
        return QStringLiteral("写入回显不匹配");
    case TransactionIssueCode::UnexpectedResponseForBroadcast:
        return QStringLiteral("广播请求不应答却收到响应");
    case TransactionIssueCode::WriteMultipleRegistersEchoMismatch:
        if (issue.expectedRegisterAddress.has_value() && issue.actualRegisterAddress.has_value()
            && issue.expectedQuantity.has_value() && issue.actualQuantity.has_value()) {
            const auto hex4 = [](std::uint16_t value) {
                return QStringLiteral("0x")
                    + QString::number(value, 16).toUpper().rightJustified(4, QLatin1Char('0'));
            };
            return QStringLiteral("写多个寄存器响应不匹配（起始地址 请求 %1 / 响应 %2；数量 请求 %3 / 响应 %4）")
                .arg(hex4(*issue.expectedRegisterAddress),
                     hex4(*issue.actualRegisterAddress))
                .arg(*issue.expectedQuantity)
                .arg(*issue.actualQuantity);
        }
        return QStringLiteral("写多个寄存器响应不匹配");
    }
    return QString();
}

QString readResultClassMachineToken(ReadResultClass resultClass)
{
    switch (resultClass) {
    case ReadResultClass::LocalRejected:
        return QStringLiteral("local_rejected");
    case ReadResultClass::TransportFailed:
        return QStringLiteral("transport_failed");
    case ReadResultClass::TimeoutNoData:
        return QStringLiteral("timeout_no_data");
    case ReadResultClass::IncompleteResponse:
        return QStringLiteral("incomplete_response");
    case ReadResultClass::DeviceException:
        return QStringLiteral("device_exception");
    case ReadResultClass::CrcFailure:
        return QStringLiteral("crc_failure");
    case ReadResultClass::ResponseMismatch:
        return QStringLiteral("response_mismatch");
    case ReadResultClass::MalformedResponse:
        return QStringLiteral("malformed_response");
    case ReadResultClass::UnknownResponse:
        return QStringLiteral("unknown_response");
    case ReadResultClass::ReadSuccess:
        return QStringLiteral("read_success");
    }
    return QStringLiteral("unknown_response");
}

// FROZEN user-visible titles (T023 DECISION 3). Verbatim contract.
QString readResultClassTitle(ReadResultClass resultClass)
{
    switch (resultClass) {
    case ReadResultClass::LocalRejected:
        return QStringLiteral("请求未发送");
    case ReadResultClass::TransportFailed:
        return QStringLiteral("传输失败");
    case ReadResultClass::TimeoutNoData:
        return QStringLiteral("响应超时");
    case ReadResultClass::IncompleteResponse:
        return QStringLiteral("响应不完整");
    case ReadResultClass::DeviceException:
        return QStringLiteral("从站异常");
    case ReadResultClass::CrcFailure:
        return QStringLiteral("CRC 校验失败");
    case ReadResultClass::ResponseMismatch:
        return QStringLiteral("响应不匹配");
    case ReadResultClass::MalformedResponse:
        return QStringLiteral("响应格式错误");
    case ReadResultClass::UnknownResponse:
        return QStringLiteral("无法识别的响应");
    case ReadResultClass::ReadSuccess:
        return QStringLiteral("读取成功");
    }
    return QStringLiteral("无法识别的响应");
}

ReadResultClass classifyFc03ReadResult(
    const modbuslens::core::TransactionStatus status,
    const std::optional<modbuslens::core::TransactionIssue>& issue)
{
    using modbuslens::core::TransactionIssueCode;
    using modbuslens::core::TransactionStatus;

    switch (status) {
    case TransactionStatus::Success:
        return ReadResultClass::ReadSuccess;
    case TransactionStatus::Exception:
        return ReadResultClass::DeviceException;
    case TransactionStatus::CrcError:
        return ReadResultClass::CrcFailure;
    case TransactionStatus::Timeout:
        // Pure timeout: the analyzer only reaches Timeout through NoResponse,
        // i.e. no byte was ever observed. Bytes-below-minimum-frame arrive as
        // ProtocolError + ResponseFrameTooShort, which classifies as CLASS-04
        // below — so the two are never conflated.
        return ReadResultClass::TimeoutNoData;
    case TransactionStatus::Pending:
        // A transition state, never a terminal result (T023 MAT-4). Mapped to
        // CLASS-03 for totality only; the caller must not present it as a
        // finished result.
        return ReadResultClass::TimeoutNoData;
    case TransactionStatus::ExpectedNoResponse:
        // Unreachable for an active FC03 read (a broadcast is rejected before
        // submission). Mapped to CLASS-09 rather than inventing a class.
        return ReadResultClass::UnknownResponse;
    case TransactionStatus::ProtocolError:
        break;
    }

    // ProtocolError is the ONLY status carrying an issue. An issue-less
    // ProtocolError is the core's documented defensive case ("render by
    // omitting the detail — never by guessing a reason"), and the
    // UnknownProtocolError code is the explicit fallback branch: both are
    // CLASS-09.
    if (!issue.has_value()
        || issue->code == TransactionIssueCode::UnknownProtocolError) {
        return ReadResultClass::UnknownResponse;
    }
    switch (issue->code) {
    case TransactionIssueCode::ResponseFrameTooShort:
        return ReadResultClass::IncompleteResponse;
    case TransactionIssueCode::ResponseAddressMismatch:
    case TransactionIssueCode::UnexpectedResponseFunction:
        return ReadResultClass::ResponseMismatch;
    case TransactionIssueCode::MalformedExceptionResponse:
    case TransactionIssueCode::MalformedNormalResponse:
        return ReadResultClass::MalformedResponse;
    case TransactionIssueCode::QuantityMismatch:
        // A well-formed reply that answers with a different register count is
        // a pairing mismatch of this request — CLASS-07, never a malformed
        // frame (the frame itself decoded cleanly).
        return ReadResultClass::ResponseMismatch;
    case TransactionIssueCode::WriteSingleRegisterEchoMismatch:
    case TransactionIssueCode::WriteMultipleRegistersEchoMismatch:
    case TransactionIssueCode::UnexpectedResponseForBroadcast:
        // Write-side codes can never appear on an FC03 read path; mapped to
        // CLASS-09 instead of inventing a read class for them.
        return ReadResultClass::UnknownResponse;
    case TransactionIssueCode::UnknownProtocolError:
        return ReadResultClass::UnknownResponse;
    }
    return ReadResultClass::UnknownResponse;
}

// Safe 「可能原因」 phrasing. Every string is a CHECK ITEM, never a root-cause
// claim (no 接线不良 / 信号干扰 / 地址配错 / PLC 程序错误 / 设备老化 ...).
QString readResultPossibleCausesFor(ReadResultClass resultClass)
{
    switch (resultClass) {
    case ReadResultClass::LocalRejected:
        return QStringLiteral("请求参数不满足本功能的合法范围。按提示修正参数后可重试。");
    case ReadResultClass::TransportFailed:
        return QStringLiteral("串口设备不可用或已被占用。确认串口设备状态后可重试。");
    case ReadResultClass::TimeoutNoData:
        return QStringLiteral("从站地址、串口参数（波特率/数据位/校验/停止位）"
                              "与请求数量是否与设备一致。");
    case ReadResultClass::IncompleteResponse:
        return QStringLiteral("响应字节数少于最小帧长。核对串口参数与请求数量。");
    case ReadResultClass::DeviceException:
        return QStringLiteral("设备按异常码拒绝本次请求。按异常码含义核对请求内容。");
    case ReadResultClass::CrcFailure:
        return QStringLiteral("接收字节的 CRC 与帧内容不一致。核对串口参数。");
    case ReadResultClass::ResponseMismatch:
        return QStringLiteral("响应的从站地址/功能码/寄存器数量与本次请求不一致。"
                              "核对该地址是否正确，以及总线上是否有其它主站。");
    case ReadResultClass::MalformedResponse:
        return QStringLiteral("响应帧结构不符合该功能码的格式。核对串口参数。");
    case ReadResultClass::UnknownResponse:
        return QString();
    case ReadResultClass::ReadSuccess:
        return QString();
    }
    return QString();
}

QString standardExceptionNameZh(std::uint8_t exceptionCode)
{
    // Only the four standard codes get a name; an unknown code returns empty
    // and the caller shows the numeric code alone (never a guessed meaning).
    switch (exceptionCode) {
    case 0x01:
        return QStringLiteral("非法功能（Illegal Function）");
    case 0x02:
        return QStringLiteral("非法数据地址（Illegal Data Address）");
    case 0x03:
        return QStringLiteral("非法数据值（Illegal Data Value）");
    case 0x04:
        return QStringLiteral("从站设备故障（Slave Device Failure）");
    default:
        return QString();
    }
}

QString evidenceHexText(const std::vector<std::uint8_t>& bytes)
{
    // Uppercase byte pairs separated by a single space, e.g. "01 03 04 00 70".
    // The width is 2 per byte (rightJustified) so a sub-0x10 byte can never
    // print as a single digit and shift every following column.
    QString hex;
    hex.reserve(static_cast<int>(bytes.size()) * 3);
    bool first = true;
    for (const std::uint8_t byte : bytes) {
        if (!first) {
            hex += QLatin1Char(' ');
        }
        hex += QString::number(byte, 16).toUpper().rightJustified(2, QLatin1Char('0'));
        first = false;
    }
    return hex;
}

QString singleRequestIssueText(const modbuslens::core::TransactionRequestIssue& requestIssue)
{
    using modbuslens::core::TransactionRequestIssueCode;
    switch (requestIssue.code) {
    case TransactionRequestIssueCode::InvalidRequestQuantity:
        if (requestIssue.observedQuantity.has_value()
            && requestIssue.minAllowedQuantity.has_value()
            && requestIssue.maxAllowedQuantity.has_value()) {
            return QStringLiteral("请求数量不符合该功能码约束（%1，合法范围 %2–%3）")
                .arg(*requestIssue.observedQuantity)
                .arg(*requestIssue.minAllowedQuantity)
                .arg(*requestIssue.maxAllowedQuantity);
        }
        return QStringLiteral("请求数量不符合该功能码约束");
    case TransactionRequestIssueCode::InvalidRequestLength:
        if (requestIssue.observedLength.has_value()
            && requestIssue.expectedLength.has_value()) {
            return QStringLiteral("请求长度不匹配（实际 %1 / 期望 %2）")
                .arg(*requestIssue.observedLength)
                .arg(*requestIssue.expectedLength);
        }
        return QStringLiteral("请求长度不符合该功能码约束");
    case TransactionRequestIssueCode::InvalidRequestByteCount:
        if (requestIssue.observedByteCount.has_value()
            && requestIssue.expectedByteCount.has_value()) {
            return QStringLiteral("请求字节数不匹配（实际 %1 / 期望 %2）")
                .arg(*requestIssue.observedByteCount)
                .arg(*requestIssue.expectedByteCount);
        }
        return QStringLiteral("请求字节数不匹配");
    case TransactionRequestIssueCode::InvalidBroadcastFunction:
        return QStringLiteral("地址 0 不是该功能码的合法广播");
    }
    return QString();
}

QString requestIssueDetailText(
    const std::vector<modbuslens::core::TransactionRequestIssue>& requestIssues)
{
    // Deterministic join in CORE order — the adapter never re-sorts issues.
    QStringList parts;
    for (const auto& requestIssue : requestIssues) {
        const QString text = singleRequestIssueText(requestIssue);
        if (!text.isEmpty()) {
            parts << text;
        }
    }
    return parts.join(QStringLiteral("；"));
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

void TransactionListModel::appendEntries(std::vector<TransactionListEntry> entries)
{
    if (entries.empty()) {
        return;
    }
    // Rows are appended at the END (the list is oldest -> newest, matching
    // every existing batch publication order). beginInsertRows keeps the
    // existing rows untouched: no reset, no dataChanged, no reordering — so
    // the view's currentIndex and the page-local selection survive.
    const auto first = static_cast<int>(entries_.size());
    const auto last = first + static_cast<int>(entries.size()) - 1;
    beginInsertRows(QModelIndex{}, first, last);
    entries_.insert(entries_.end(),
                    std::make_move_iterator(entries.begin()),
                    std::make_move_iterator(entries.end()));
    endInsertRows();
}