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

// T023: lowercase, space-separated uppercase HEX projection of a byte vector
// ("01 03 04 00 70 00 C8 3B 2E"). Empty input yields an empty string — the
// caller must render "未观测到任何字节" itself; this helper never invents a
// placeholder byte. Shared by the read-result projection so the TX/RX columns
// cannot drift into two formats.
QString evidenceHexText(const std::vector<std::uint8_t>& bytes);

// ---------------------------------------------------------------------------
// T023 / M10 correction: FC03 READ-RESULT classification (CLASS-01…10).
//
// Adapter-only, and deliberately a PURE MAPPING of facts the core already
// produced (TransactionStatus + TransactionIssue + the transport disposition /
// terminal reason). It is NOT a second outcome authority: no core semantic is
// widened, no new status is invented, and every class is reachable only
// through an existing deterministic core fact.
//
// The numbering is the Human-frozen DECISION 3 ordering (it intentionally
// differs from the earlier T023 draft ordering — the frozen user-visible
// titles below are the contract):
//   CLASS-01 请求未发送          local validation rejection, nothing sent
//   CLASS-02 传输失败            transport failure / short submission
//   CLASS-03 响应超时            timeout with NO bytes observed
//   CLASS-04 响应不完整          bytes observed but below the minimum frame
//   CLASS-05 从站异常            legal Modbus exception response
//   CLASS-06 CRC 校验失败        wire arrived, CRC mismatch
//   CLASS-07 响应不匹配          reply belongs to another request/device
//   CLASS-08 响应格式错误        reply shape illegal for its function
//   CLASS-09 无法识别的响应      deterministic defensive/fallback branch
//   CLASS-10 读取成功            fully paired normal response
// ---------------------------------------------------------------------------
enum class ReadResultClass {
    LocalRejected,      // CLASS-01
    TransportFailed,    // CLASS-02
    TimeoutNoData,      // CLASS-03
    IncompleteResponse, // CLASS-04
    DeviceException,    // CLASS-05
    CrcFailure,         // CLASS-06
    ResponseMismatch,   // CLASS-07
    MalformedResponse,  // CLASS-08
    UnknownResponse,    // CLASS-09
    ReadSuccess,        // CLASS-10
};

// Stable machine token (e.g. "local_rejected") — adapter/tool serialization
// only, never human UI prose (T023 READ-MSG-2).
QString readResultClassMachineToken(ReadResultClass resultClass);

// The FROZEN primary user-visible title (T023 DECISION 3 / READ-MSG-4). These
// strings are a contract: changing one requires a contract revision.
QString readResultClassTitle(ReadResultClass resultClass);

// Map a core analysis verdict to its class. Total over the existing core
// facts: an issue-less ProtocolError (the core's documented defensive case)
// and the UnknownProtocolError fallback both land in CLASS-09 rather than
// being guessed into a more specific class (T023 UNK-1/UNK-2).
ReadResultClass classifyFc03ReadResult(
    const modbuslens::core::TransactionStatus status,
    const std::optional<modbuslens::core::TransactionIssue>& issue);

// The explicitly-labelled 「可能原因」 line for a class (empty when no
// speculation is warranted). Safe phrasing only — never the forbidden
// root-cause words (T023 READ-UI-4). The caller renders the label itself so
// the word 「可能原因」 can never be dropped by accident.
QString readResultPossibleCausesFor(ReadResultClass resultClass);

// Standard Chinese name of a Modbus exception code (0x01…0x04); empty when the
// code is not one of the standard four — an unknown code is never guessed
// (the same discipline as the existing standardExceptionName helper).
QString standardExceptionNameZh(std::uint8_t exceptionCode);

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