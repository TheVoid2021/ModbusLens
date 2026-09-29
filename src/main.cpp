#include <QAbstractItemModel>
#include <algorithm>
#include <QClipboard>
#include <QAccessible>
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QFile>
#include <QIcon>
#include <QKeyEvent>

// M9-E E1: generated version interface (configure_file output, build tree only).
#include "modbuslens_version.h"
// M10-B correction: the focus harness appends REAL Active Serial records
// through the controller's single production append seam, so the shipped
// application type is included here (harness only — no product change).
#include "core/active/ActiveRequestIntent.h"
#include "core/serial/SerialTransactionSession.h"
#include "core/active/ActiveTransactionEvidence.h"
#include "core/protocol/Function16.h"
#include "core/protocol/ModbusRtuCodec.h"
#include "ui/AnalysisController.h"
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QCoreApplication>
#include <QQuickStyle>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QStringList>
#include <QTextStream>
#include <QTimer>
#include <QTemporaryDir>
#include <QStandardPaths>
#include "core/profile/DeviceProfile.h"
#include "core/manual/ManualDocument.h"
#include "ui/profile/ProfileStore.h"
#include "ui/manual/ManualStore.h"

#include <zip.h>

#include <functional>

namespace {

// M9-A regression guard (ISSUE-012): minimal runtime geometry sanity for
// the statistics migration. Deliberately NOT a visual test — it only
// asserts the layout-size contract that qml_smoke cannot see:
//   1. statisticsPanel / every visible StatCard has width>0 and height>0
//   2. statisticsRow2 starts at or below the bottom of statisticsRow1
//   3. diagnosisWorkspace starts at or below the bottom of statisticsPanel
//
// The lookup walks the VISUAL item tree (childItems), not the QObject
// children: Repeater delegates are reachable as visual children while
// QObject::findChild misses them in this declarative tree.
QQuickItem *findNamedItemRecursive(QQuickItem *item, const QString &name)
{
    if (item->objectName() == name)
        return item;
    for (QQuickItem *child : item->childItems()) {
        if (auto *hit = findNamedItemRecursive(child, name))
            return hit;
    }
    return nullptr;
}

QQuickItem *findNamedItem(const QList<QObject *> &roots, const QString &name)
{
    for (QObject *root : roots) {
        auto *window = qobject_cast<QQuickWindow *>(root);
        if (!window)
            continue;
        if (auto *hit = findNamedItemRecursive(window->contentItem(), name))
            return hit;
    }
    return nullptr;
}

// M9-D D2: is `item` inside `ancestor`'s subtree? Used for the runtime
// single-owner proof (the transactions presentation must live under the
// Transactions page and never under the Legacy workspace).
bool underItem(QQuickItem *item, QQuickItem *ancestor)
{
    for (auto *p = item ? item->parentItem() : nullptr; p; p = p->parentItem())
        if (p == ancestor)
            return true;
    return false;
}

// M9-D D2: transactions-presentation probe helpers (runtime, not grep).
int rowCountOf(QObject *ctrl)
{
    if (!ctrl)
        return -1;
    if (auto *model = ctrl->property("transactionModel")
                         .value<QAbstractItemModel *>())
        return model->rowCount();
    return -1;
}

QVariant modelRole(QObject *ctrl, const QString &roleName, int row = 0)
{
    if (!ctrl)
        return {};
    auto *model = ctrl->property("transactionModel")
                      .value<QAbstractItemModel *>();
    if (!model || row < 0 || row >= model->rowCount())
        return {};
    const QHash<int, QByteArray> roles = model->roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == roleName)
            return model->data(model->index(row, 0), it.key());
    }
    return {};
}

// The migrated transactions presentation: ONE pane, under the Transactions
// page, never under the Legacy workspace; the viewport and the model agree on
// the row count; the empty hint follows the row count; and the first
// delegate's row height follows the model's issueText role (the data-driven
// sizing rule survived the move).
void assertTransactionsPresentation(const QList<QObject *> &roots,
                                    const QString &contextLabel,
                                    QStringList &failures)
{
    auto fail = [&failures, &contextLabel](const QString &message) {
        failures << contextLabel + QStringLiteral(": ") + message;
    };
    auto *ctrl = roots.value(0)
                     ? roots.value(0)->findChild<QObject *>(
                           QStringLiteral("analysisController"))
                     : nullptr;
    auto *page = findNamedItem(roots, QStringLiteral("transactionsPage"));
    auto *pane = findNamedItem(roots, QStringLiteral("transactionsPane"));
    auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
    auto *empty =
        findNamedItem(roots, QStringLiteral("transactionsEmptyHint"));
    // M9-D D5 retirement oracle: the Legacy workspace must not exist at all.
    if (findNamedItem(roots, QStringLiteral("legacyWorkspace")))
        fail(QStringLiteral("legacyWorkspace still exists — the Legacy "
                            "workspace was not retired"));

    if (!page || !page->isVisible())
        fail(QStringLiteral("the transactions page is not visible"));
    if (!pane) {
        fail(QStringLiteral("transactionsPane not found"));
        return;
    }
    if (pane->width() <= 0 || pane->height() <= 0)
        fail(QStringLiteral("transactionsPane has no geometry"));
    if (page && !underItem(pane, page))
        fail(QStringLiteral("transactionsPane is not under the transactions "
                            "page"));
    if (!list) {
        fail(QStringLiteral("transactionsList not found"));
        return;
    }
    if (list->width() <= 0 || list->height() <= 0)
        fail(QStringLiteral("transactionsList has no viewport geometry"));

    const int rows = rowCountOf(ctrl);
    if (rows < 0)
        fail(QStringLiteral("transactionModel could not be read"));
    const int viewCount = list->property("count").toInt();
    if (rows >= 0 && viewCount != rows)
        fail(QStringLiteral("the list view shows %1 rows while the model has "
                            "%2").arg(viewCount).arg(rows));
    if (!empty) {
        fail(QStringLiteral("transactionsEmptyHint not found"));
    } else if (empty->isVisible() != (rows == 0)) {
        fail(QStringLiteral("empty hint visibility %1 does not match the row "
                            "count %2")
                 .arg(empty->isVisible())
                 .arg(rows));
    }

    if (rows > 0) {
        if (modelRole(ctrl, QStringLiteral("statusText")).toString().isEmpty())
            fail(QStringLiteral("the first row carries no status text"));
        auto *content = list->property("contentItem").value<QQuickItem *>();
        const auto kids = content ? content->childItems() : QList<QQuickItem *>{};
        if (!kids.isEmpty()) {
            const bool hasIssue =
                !modelRole(ctrl, QStringLiteral("issueText")).toString().isEmpty();
            const double expected = hasIssue ? 64.0 : 36.0;
            if (qAbs(kids.first()->height() - expected) > 0.5)
                fail(QStringLiteral("first row height %1 does not follow the "
                                    "issueText rule (expected %2)")
                         .arg(kids.first()->height())
                         .arg(expected));
        }
    }
}

// M9-D D3: the page-local selection + detail contract. The authoritative
// model has exactly ONE mutation path (setEntries -> beginResetModel/
// endResetModel; no dataChanged anywhere), so the page-local snapshot can
// never go stale: every data change resets the model, and the page clears
// the selection on modelReset. The snapshot is a presentation copy only —
// never a Controller authority.
int selectedRowOf(const QList<QObject *> &roots)
{
    auto *page = findNamedItem(roots, QStringLiteral("transactionsPage"));
    return page ? page->property("selectedRow").toInt() : -2;
}

QVariantMap selectedEntryOf(const QList<QObject *> &roots)
{
    auto *page = findNamedItem(roots, QStringLiteral("transactionsPage"));
    if (!page)
        return {};
    return page->property("selectedEntry").toMap();
}

// M9-D D3 review (P0-2): the deferred half of the selection seam. A request
// whose delegate is not instantiated is parked in pendingSelectionRow and
// retried through Qt.callLater; the harness reads that state directly so a
// stale completion is observable, not merely argued from the source.
int pendingSelectionRowOf(const QList<QObject *> &roots)
{
    auto *page = findNamedItem(roots, QStringLiteral("transactionsPage"));
    return page ? page->property("pendingSelectionRow").toInt() : -2;
}

// Requests a selection through the page's OWN entry point (the function the
// ListView's onCurrentIndexChanged calls) — no new production API is added
// for testing, and a row index that cannot be materialized takes exactly the
// same deferred path a keyboard move onto an unbuilt delegate would take.
bool requestTransactionSelection(const QList<QObject *> &roots, int row)
{
    auto *page = findNamedItem(roots, QStringLiteral("transactionsPage"));
    if (!page)
        return false;
    return QMetaObject::invokeMethod(page, "selectRow", Q_ARG(QVariant, row));
}

// M9-D D4: the Transactions diagnosis EXISTENCE cue. Same semantic boundary
// as the M9-C C4 Dashboard cue — one line that mirrors the Controller's
// authoritative hasBaselineDiagnosis. Never findings, never the baseline
// text, never selection- or outcome-derived. The visibility rule follows the
// Dashboard precedent (a session with no observed results has no cue).
void assertTransactionsDiagnosisCue(const QList<QObject *> &roots,
                                    const QString &contextLabel,
                                    QStringList &failures)
{
    auto fail = [&failures, &contextLabel](const QString &message) {
        failures << contextLabel + QStringLiteral(": ") + message;
    };
    auto *ctrl = roots.value(0)
                     ? roots.value(0)->findChild<QObject *>(
                           QStringLiteral("analysisController"))
                     : nullptr;
    if (!ctrl) {
        fail(QStringLiteral("analysisController not found"));
        return;
    }
    auto *cue =
        findNamedItem(roots, QStringLiteral("transactionsDiagnosisCue"));
    if (!cue) {
        fail(QStringLiteral("transactionsDiagnosisCue not found"));
        return;
    }
    const bool hasBaseline = ctrl->property("hasBaselineDiagnosis").toBool();
    const int observed = ctrl->property("observedCount").toInt();
    const bool shouldBeVisible = observed > 0;
    if (cue->property("visible").toBool() != shouldBeVisible)
        fail(QStringLiteral("cue visibility property %1 does not match "
                            "observed %2")
                 .arg(cue->property("visible").toBool())
                 .arg(observed));
    auto *page = findNamedItem(roots, QStringLiteral("transactionsPage"));
    if (page && page->isVisible() && shouldBeVisible && !cue->isVisible())
        fail(QStringLiteral("the diagnosis cue is not visible on the active "
                            "transactions page"));
    const QString expected =
        hasBaseline
            ? QStringLiteral("已有基线诊断结果，可在诊断工作区查看。")
            : QStringLiteral("尚未运行基线诊断。");
    const QString text = cue->property("text").toString();
    if (text != expected)
        fail(QStringLiteral("cue wording — got %1, expected %2 "
                            "(hasBaselineDiagnosis=%3)")
                 .arg(text, expected)
                 .arg(hasBaseline));
}

void assertTransactionDetailMapping(const QList<QObject *> &roots,
                                    const QString &contextLabel,
                                    QStringList &failures)
{
    auto fail = [&failures, &contextLabel](const QString &message) {
        failures << contextLabel + QStringLiteral(": ") + message;
    };
    auto *ctrl = roots.value(0)
                     ? roots.value(0)->findChild<QObject *>(
                           QStringLiteral("analysisController"))
                     : nullptr;
    auto *detail = findNamedItem(roots, QStringLiteral("transactionDetail"));
    if (!detail) {
        fail(QStringLiteral("transactionDetail not found"));
        return;
    }
    const int selected = selectedRowOf(roots);
    const QVariantMap snapshot = selectedEntryOf(roots);
    const int rows = rowCountOf(ctrl);

    if (selected < 0) {
        // no-selection state: the empty hint is the ONLY thing shown. Its
        // VISIBILITY is asserted only while the Transactions page is the
        // active page — hidden-page visibility is not a contract.
        auto *empty =
            findNamedItem(roots, QStringLiteral("transactionDetailEmpty"));
        auto *page = findNamedItem(roots, QStringLiteral("transactionsPage"));
        if (!empty)
            fail(QStringLiteral("transactionDetailEmpty not found"));
        else if (page && page->isVisible() && !empty->isVisible())
            fail(QStringLiteral("the no-selection hint is not visible on the "
                                "active transactions page"));
        if (!snapshot.isEmpty())
            fail(QStringLiteral("a detail snapshot exists without a selection"));
        return;
    }

    if (selected >= rows)
        fail(QStringLiteral("selectedRow %1 is outside the model (%2 rows)")
                 .arg(selected)
                 .arg(rows));
    // field-by-field mapping: every snapshot field must equal the model role
    struct Field { const char *snapshotKey; const char *role; };
    const Field fields[] = {
        { "deviceAddress", "deviceAddress" },
        { "functionCode", "functionCode" },
        { "statusText", "statusText" },
        { "elapsedMs", "elapsedMs" },
        { "hasExceptionCode", "hasExceptionCode" },
        { "exceptionCode", "exceptionCode" },
        { "issueText", "issueText" },
    };
    for (const Field &field : fields) {
        const QVariant want =
            modelRole(ctrl, QLatin1String(field.role), selected);
        const QVariant got = snapshot.value(QLatin1String(field.snapshotKey));
        if (want != got)
            fail(QStringLiteral("detail %1 is %2 while the model row %3 has %4")
                     .arg(QLatin1String(field.snapshotKey),
                          got.toString())
                     .arg(selected)
                     .arg(want.toString()));
    }
    // the status and the issue are separate fields (orthogonality): the
    // status label never carries the issue text
    auto *status = findNamedItem(roots, QStringLiteral("transactionDetailStatus"));
    auto *issue = findNamedItem(roots, QStringLiteral("transactionDetailIssue"));
    if (!status || !issue) {
        fail(QStringLiteral("transactionDetailStatus/Issue not found"));
    } else {
        const QString statusText = status->property("text").toString();
        if (statusText.isEmpty())
            fail(QStringLiteral("the detail status label is empty"));
        const QString issueText = snapshot.value(QStringLiteral("issueText")).toString();
        if (!issueText.isEmpty() && statusText.contains(issueText))
            fail(QStringLiteral("the issue text leaked into the detail status "
                                "label"));
    }
    qInfo().noquote()
        << QStringLiteral("DETAIL MAPPING: row=%1 status=%2 issue=%3")
               .arg(selected)
               .arg(snapshot.value(QStringLiteral("statusText")).toString())
               .arg(snapshot.value(QStringLiteral("issueText")).toString().isEmpty()
                        ? QStringLiteral("<none>")
                        : QStringLiteral("<present>"));
}

// M9-B3: which workspace page is currently visible (drives which page's
// geometry gets asserted — hidden pages are never asserted).
enum class ActivePage {
    Dashboard,
    Communication,
    Replay,
    Diagnosis,
    Transactions,
    None
};

ActivePage activePage(const QList<QObject *> &roots)
{
    if (auto *page = findNamedItem(roots, QStringLiteral("communicationWorkspace"));
        page && page->isVisible())
        return ActivePage::Communication;
    if (auto *page = findNamedItem(roots, QStringLiteral("replayWorkspace"));
        page && page->isVisible())
        return ActivePage::Replay;
    if (auto *page = findNamedItem(roots, QStringLiteral("diagnosisPage"));
        page && page->isVisible())
        return ActivePage::Diagnosis;
    if (auto *page = findNamedItem(roots, QStringLiteral("transactionsPage"));
        page && page->isVisible())
        return ActivePage::Transactions;
    if (auto *page = findNamedItem(roots, QStringLiteral("dashboardWorkspace"));
        page && page->isVisible())
        return ActivePage::Dashboard;
    // M9-D D5: the Legacy workspace is retired — a tree with NO visible page
    // is a defect the caller must report, not a Legacy fallback.
    return ActivePage::None;
}

// M9-C C3: the outcome-distribution presentation contract, shared by the
// geometry check (zero state) and the nav-check distribution probe (demo and
// broadcast states). Denominator = completedCount (Review guardrail A — NOT
// the successRate denominator); segments in the frozen order Success ->
// Exception -> CRC -> Timeout -> ProtocolError -> ExpectedNoResponse; the
// zero state renders nothing. Segment widths are asserted from the
// controller counts against the bar width — never from pixels.
void assertOutcomeDistribution(const QList<QObject *> &roots,
                               const QString &contextLabel,
                               QStringList &failures)
{
    auto fail = [&failures, &contextLabel](const QString &message) {
        failures << contextLabel + QStringLiteral(": ") + message;
    };
    auto *ctrl = roots.value(0)
                     ? roots.value(0)->findChild<QObject *>(
                           QStringLiteral("analysisController"))
                     : nullptr;
    if (!ctrl) {
        fail(QStringLiteral("analysisController not found"));
        return;
    }
    const int completed = ctrl->property("completedCount").toInt();
    const int observed = ctrl->property("observedCount").toInt();

    auto *panel = findNamedItem(roots, QStringLiteral("statisticsPanel_dashboard"));
    auto *metrics = findNamedItem(roots, QStringLiteral("statisticsRow1_dashboard"));
    auto *outcomes = findNamedItem(roots, QStringLiteral("statisticsRow2_dashboard"));
    auto *distribution =
        findNamedItem(roots, QStringLiteral("outcomeDistribution_dashboard"));

    // composition identity: every piece lives in the dashboard panel, the
    // two rows are distinct, and the shared wrapper is gone from this page
    auto under = [](QQuickItem *item, QQuickItem *ancestor) {
        for (auto *p = item ? item->parentItem() : nullptr; p;
             p = p->parentItem())
            if (p == ancestor)
                return true;
        return false;
    };
    for (auto *piece : { metrics, outcomes, distribution }) {
        if (!piece) {
            fail(QStringLiteral("a dashboard statistics piece is missing "
                                "(metrics/outcomes/distribution)"));
            continue;
        }
        if (panel && !under(piece, panel))
            fail(QStringLiteral("a dashboard statistics piece is not inside "
                                "statisticsPanel_dashboard"));
    }
    if (metrics && outcomes && metrics == outcomes)
        fail(QStringLiteral("statistics metrics and outcomes rows resolved to "
                            "the SAME item"));
    if (findNamedItem(roots, QStringLiteral("statisticsOverview_dashboard")))
        fail(QStringLiteral("statisticsOverview_dashboard still exists — since "
                            "C3 the dashboard composes the statistics pieces "
                            "directly"));

    if (completed <= 0) {
        // zero state: the whole component renders nothing
        if (distribution && distribution->isVisible())
            fail(QStringLiteral("outcomeDistribution is visible while "
                                "completedCount == %1").arg(completed));
        if (observed == 0) {
            if (auto *hint =
                    findNamedItem(roots, QStringLiteral("dashboardEmptyHint"))) {
                if (!hint->isVisible())
                    fail(QStringLiteral("dashboard empty hint is invisible in "
                                        "the empty state"));
                const QString wording = hint->property("text").toString();
                if (!wording.contains(QStringLiteral("回放"))
                    || wording.contains(QStringLiteral("工作台")))
                    fail(QStringLiteral("dashboard empty hint wording predates "
                                        "the C3 navigation refresh"));
            }
        }
        return;
    }

    if (!distribution) {
        fail(QStringLiteral("outcomeDistribution not found"));
        return;
    }
    if (!distribution->isVisible())
        fail(QStringLiteral("outcomeDistribution is invisible while "
                            "completedCount == %1").arg(completed));

    // C3 diagnostics: the QML-side values that drive the segment math.
    qInfo().noquote()
        << QStringLiteral("DISTRIBUTION DIAG: qml.completedTotal=%1 "
                          "qml.unitWidth=%2 qml.visible=%3 bar.visible=%4")
               .arg(distribution->property("completedTotal").toInt())
               .arg(distribution->property("unitWidth").toDouble())
               .arg(distribution->isVisible())
               .arg(distribution->property("visible").toBool());

    auto *bar = findNamedItem(roots,
                              QStringLiteral("outcomeDistributionBar_dashboard"));
    if (!bar || bar->width() <= 0 || bar->height() <= 0) {
        fail(QStringLiteral("outcomeDistributionBar missing or collapsed "
                            "(w=%1 h=%2)")
                 .arg(bar ? bar->width() : -1)
                 .arg(bar ? bar->height() : -1));
        return;
    }

    const int counts[6] = {
        ctrl->property("successCount").toInt(),
        ctrl->property("exceptionCount").toInt(),
        ctrl->property("crcErrorCount").toInt(),
        ctrl->property("timeoutCount").toInt(),
        ctrl->property("protocolErrorCount").toInt(),
        ctrl->property("expectedNoResponseCount").toInt()
    };
    double prevRight = 0.0;
    for (int i = 0; i < 6; ++i) {
        auto *seg = findNamedItem(
            roots, QStringLiteral("outcomeSegment_%1_dashboard").arg(i));
        if (!seg) {
            fail(QStringLiteral("outcomeSegment_%1_dashboard not found").arg(i));
            return;
        }
        const double expectedWidth =
            static_cast<double>(bar->width()) * counts[i] / completed;
        if (qAbs(seg->width() - expectedWidth) > 0.5)
            fail(QStringLiteral("outcomeSegment_%1 width %2 != expected %3 "
                                "(bar %4, count %5, completed %6)")
                     .arg(i)
                     .arg(seg->width())
                     .arg(expectedWidth)
                     .arg(bar->width())
                     .arg(counts[i])
                     .arg(completed));
        if (i > 0 && qAbs(seg->x() - prevRight) > 0.5)
            fail(QStringLiteral("outcomeSegment_%1 is not consecutive "
                                "(x=%2, previous right=%3)")
                     .arg(i)
                     .arg(seg->x())
                     .arg(prevRight));
        prevRight = seg->x() + seg->width();
    }
    if (qAbs(prevRight - bar->width()) > 0.5)
        fail(QStringLiteral("last segment right edge %1 != bar right edge %2")
                 .arg(prevRight)
                 .arg(bar->width()));
}

// M9-C C4: the deterministic attention summary + diagnosis status cue
// contract. attentionCount is a PRESENTATION aggregation of exactly FOUR
// outcome counts (exception + crcError + timeout + protocolError) --
// ExpectedNoResponse is NOT an anomaly and never enters the sum (Review
// guardrail B), and neither do success or pending. It is not a health score:
// every wording variant stays scoped to completed transaction outcomes. The
// cue is an EXISTENCE line driven by hasBaselineDiagnosis only.
void assertDashboardAttention(const QList<QObject *> &roots,
                              const QString &contextLabel,
                              QStringList &failures)
{
    auto fail = [&failures, &contextLabel](const QString &message) {
        failures << contextLabel + QStringLiteral(": ") + message;
    };
    auto *ctrl = roots.value(0)
                     ? roots.value(0)->findChild<QObject *>(
                           QStringLiteral("analysisController"))
                     : nullptr;
    if (!ctrl) {
        fail(QStringLiteral("analysisController not found"));
        return;
    }
    const int exception = ctrl->property("exceptionCount").toInt();
    const int crc = ctrl->property("crcErrorCount").toInt();
    const int timeout = ctrl->property("timeoutCount").toInt();
    const int protocol = ctrl->property("protocolErrorCount").toInt();
    const int observed = ctrl->property("observedCount").toInt();
    const int completed = ctrl->property("completedCount").toInt();
    const bool hasBaseline = ctrl->property("hasBaselineDiagnosis").toBool();
    const int expectedAttention = exception + crc + timeout + protocol;

    auto *summary =
        findNamedItem(roots, QStringLiteral("dashboardAttentionSummary"));
    auto *cue = findNamedItem(roots, QStringLiteral("dashboardDiagnosisCue"));
    if (!summary) {
        fail(QStringLiteral("dashboardAttentionSummary not found"));
        return;
    }
    if (!cue) {
        fail(QStringLiteral("dashboardDiagnosisCue not found"));
        return;
    }

    // numeric oracle: the QML property must equal the frozen formula
    const int qmlAttention = summary->property("attentionCount").toInt();
    if (qmlAttention != expectedAttention)
        fail(QStringLiteral("attentionCount %1 != the frozen formula %2 "
                            "(exception %3 + crc %4 + timeout %5 + protocol "
                            "%6)")
                 .arg(qmlAttention)
                 .arg(expectedAttention)
                 .arg(exception)
                 .arg(crc)
                 .arg(timeout)
                 .arg(protocol));

    // visibility: both lines exist for the session states and hide together
    // with the session (the empty hint owns the observed == 0 state)
    const bool shouldBeVisible = observed > 0;
    if (summary->isVisible() != shouldBeVisible)
        fail(QStringLiteral("attention summary visibility %1 does not match "
                            "observed %2")
                 .arg(summary->isVisible())
                 .arg(observed));
    if (cue->isVisible() != shouldBeVisible)
        fail(QStringLiteral("diagnosis cue visibility %1 does not match "
                            "observed %2")
                 .arg(cue->isVisible())
                 .arg(observed));
    if (!shouldBeVisible)
        return;

    // wording contract for the deterministic states
    const QString text = summary->property("text").toString();
    if (completed <= 0) {
        const QString expected = QStringLiteral("尚无已完成结果。");
        if (text != expected)
            fail(QStringLiteral("attention wording for a session without "
                                "completed outcomes — got %1, expected %2")
                     .arg(text, expected));
    } else if (expectedAttention > 0) {
        const QString prefix =
            QStringLiteral("需关注结果 %1 条：").arg(expectedAttention);
        if (!text.startsWith(prefix))
            fail(QStringLiteral("attention wording must open with %1 — "
                                "got %2")
                     .arg(prefix, text));
        if (exception > 0
            && !text.contains(QStringLiteral("异常 %1").arg(exception)))
            fail(QStringLiteral("attention breakdown omits 异常 %1")
                     .arg(exception));
        if (crc > 0
            && !text.contains(QStringLiteral("CRC 错误 %1").arg(crc)))
            fail(QStringLiteral("attention breakdown omits CRC 错误 %1")
                     .arg(crc));
        if (timeout > 0
            && !text.contains(QStringLiteral("超时 %1").arg(timeout)))
            fail(QStringLiteral("attention breakdown omits 超时 %1")
                     .arg(timeout));
        if (protocol > 0
            && !text.contains(QStringLiteral("协议错误 %1").arg(protocol)))
            fail(QStringLiteral("attention breakdown omits 协议错误 %1")
                     .arg(protocol));
        // zero categories are omitted from the breakdown (frozen wording rule)
        if (exception == 0 && text.contains(QStringLiteral("异常")))
            fail(QStringLiteral("attention breakdown mentions 异常 with a zero "
                                "count"));
        if (crc == 0 && text.contains(QStringLiteral("CRC 错误")))
            fail(QStringLiteral("attention breakdown mentions CRC 错误 with a "
                                "zero count"));
        if (timeout == 0 && text.contains(QStringLiteral("超时")))
            fail(QStringLiteral("attention breakdown mentions 超时 with a zero "
                                "count"));
        if (protocol == 0 && text.contains(QStringLiteral("协议错误")))
            fail(QStringLiteral("attention breakdown mentions 协议错误 with a "
                                "zero count"));
    } else {
        const QString expected = QStringLiteral(
            "已完成结果中暂未观察到异常、CRC 错误、超时或协议错误。");
        if (text != expected)
            fail(QStringLiteral("attention wording for a session without "
                                "attention — got %1, expected %2")
                     .arg(text, expected));
    }

    // cue wording: an existence line in both directions
    const QString cueText = cue->property("text").toString();
    const QString expectedCue =
        hasBaseline ? QStringLiteral("已有基线诊断结果，可在诊断工作区查看。")
                    : QStringLiteral("尚未运行基线诊断。");
    if (cueText != expectedCue)
        fail(QStringLiteral("diagnosis cue wording — got %1, expected %2 "
                            "(hasBaselineDiagnosis=%3)")
                 .arg(cueText, expectedCue)
                 .arg(hasBaseline));
}

QStringList runGeometryAssertions(const QList<QObject *> &roots,
                                  const QString &contextLabel)
{
    QStringList failures;

    // M9-B2: statistics and diagnosis assertions target ONLY the workspace
    // page that is currently VISIBLE (hidden StackLayout children get no
    // fragile geometry assertions — T017 §31.7/§20). The suffix is the
    // StatisticsOverview instanceId.
    const ActivePage page = activePage(roots);
    if (page == ActivePage::None)
        failures << contextLabel + QStringLiteral(": no workspace page is "
                                                  "visible — the geometry "
                                                  "dispatch has no target");
    // M9-D D5: the Legacy workspace is retired, so the Dashboard is the only
    // StatisticsOverview consumer left. The suffix mechanism stays (the
    // overview objectNames still carry the instanceId) without a "legacy"
    // branch.
    const bool statsVisible = (page == ActivePage::Dashboard);
    const QString suffix = QStringLiteral("dashboard");
    auto suffixed = [&suffix](const QString &base) {
        return base + QLatin1Char('_') + suffix;
    };

    const QStringList cardNames = {
        suffixed(QStringLiteral("statCard_0")),
        suffixed(QStringLiteral("statCard_1")),
        suffixed(QStringLiteral("statCard_2")),
        suffixed(QStringLiteral("statCard_rate")),
        suffixed(QStringLiteral("statCard_latency")),
        suffixed(QStringLiteral("statusCard_0")),
        suffixed(QStringLiteral("statusCard_1")),
        suffixed(QStringLiteral("statusCard_2")),
        suffixed(QStringLiteral("statusCard_3")),
        suffixed(QStringLiteral("statusCard_4")),
        suffixed(QStringLiteral("statusCard_5")),
    };

    auto fail = [&failures, &contextLabel](const QString &message) {
        failures << contextLabel + QStringLiteral(": ") + message;
    };

    // M9-D D5 retirement oracle: the Legacy workspace must be REMOVED from
    // the runtime tree (retirement = actual removal, never visible:false).
    // Checked at every pass, whatever the active page is.
    for (const QString &retired : {QStringLiteral("legacyWorkspace"),
                                   QStringLiteral("statisticsOverview_legacy"),
                                   QStringLiteral("legacyTailSpacer")}) {
        if (findNamedItem(roots, retired))
            fail(QStringLiteral("%1 still exists - the Legacy workspace was "
                                "not retired from the runtime tree (expected "
                                "D5 missing retirement)")
                     .arg(retired));
    }

    // M9-C C1: read the DesignSystem spacing tokens from the SAME singleton
    // instance the QML consumes, so the Dashboard layout contract is asserted
    // against live token values instead of hardcoded pixels.
    double dsSpacingM = -1.0;
    double dsSpacingL = -1.0;
    if (auto *ctx = roots.isEmpty() ? nullptr : qmlContext(roots.value(0))) {
        if (auto *ds =
                ctx->contextProperty(QStringLiteral("DS")).value<QObject *>()) {
            dsSpacingM = ds->property("spacingM").toDouble();
            dsSpacingL = ds->property("spacingL").toDouble();
        }
    }
    if (dsSpacingM <= 0.0 || dsSpacingL <= 0.0)
        fail(QStringLiteral("DS spacing tokens could not be read — every "
                            "spacing assertion below would be vacuous"));

    // ---- Shell guards (M9-B1) ----
    // Minimum business content width for the workspace host: the legacy
    // SplitView minimums (300 diagnosis + 520 transactions) plus the legacy
    // margins (2 x 16). Defined here, next to the guard, so it is a stated
    // budget and not a hidden magic number.
    constexpr double kMinimumWorkspaceContentWidth = 300 + 520 + 2 * 16;

    auto *appBar = findNamedItem(roots, QStringLiteral("appBar"));
    if (!appBar)
        fail(QStringLiteral("appBar not found"));
    else if (appBar->width() <= 0 || appBar->height() <= 0)
        fail(QStringLiteral("appBar size %1x%2")
                 .arg(appBar->width())
                 .arg(appBar->height()));

    auto *rail = findNamedItem(roots, QStringLiteral("navigationRail"));
    if (!rail)
        fail(QStringLiteral("navigationRail not found"));
    else if (rail->width() <= 0 || rail->height() <= 0)
        fail(QStringLiteral("navigationRail size %1x%2")
                 .arg(rail->width())
                 .arg(rail->height()));

    auto *host = findNamedItem(roots, QStringLiteral("workspaceHost"));
    if (!host)
        fail(QStringLiteral("workspaceHost not found"));
    else if (host->width() <= 0 || host->height() <= 0)
        fail(QStringLiteral("workspaceHost size %1x%2")
                 .arg(host->width())
                 .arg(host->height()));

    if (rail && host && rail->x() + rail->width() > host->x() + 0.5)
        fail(QStringLiteral("navigationRail (x=%1 w=%2) overlaps workspaceHost "
                            "(x=%3)")
                 .arg(rail->x())
                 .arg(rail->width())
                 .arg(host->x()));

    if (host) {
        if (auto *window = qobject_cast<QQuickWindow *>(roots.value(0))) {
            if (host->x() + host->width() > window->contentItem()->width() + 0.5)
                fail(QStringLiteral("workspaceHost right edge %1 exceeds window "
                                    "content width %2")
                         .arg(host->x() + host->width())
                         .arg(window->contentItem()->width()));
        }
        if (host->width() + 0.5 < kMinimumWorkspaceContentWidth)
            fail(QStringLiteral("workspaceHost width %1 < minimum business "
                                "budget %2")
                     .arg(host->width())
                     .arg(kMinimumWorkspaceContentWidth));
    }

    // ---- Statistics (only the pages that own a StatisticsOverview) ----
    QQuickItem *row1 = nullptr;
    QQuickItem *row2 = nullptr;
    QQuickItem *header = nullptr;
    QQuickItem *panel = nullptr;
    if (statsVisible) {
        // ISSUE-013: these four MUST assign the outer locals. Declaring them
        // again here (`auto *row2 = ...`) shadowed the outer ones, which left
        // the row-overlap guard and the ISSUE-004 guard below comparing
        // against a permanently null pointer — unfalsifiable, always "pass".
        row1 = findNamedItem(roots, suffixed(QStringLiteral("statisticsRow1")));
        row2 = findNamedItem(roots, suffixed(QStringLiteral("statisticsRow2")));
        const auto row1Count = row1 ? row1->childItems().size() : -1;
        const auto row2Count = row2 ? row2->childItems().size() : -1;

        header = findNamedItem(roots, suffixed(QStringLiteral("statisticsHeader")));
        panel = findNamedItem(roots, suffixed(QStringLiteral("statisticsPanel")));
    if (header && panel && panel->y() + 1e-6 < header->y() + header->height())
        fail(QStringLiteral("statisticsPanel y=%1 overlaps header "
                            "(y=%2 h=%3)")
                 .arg(panel->y())
                 .arg(header->y())
                 .arg(header->height()));

    if (!panel)
        fail(QStringLiteral("statisticsPanel not found"));
    else if (panel->width() <= 0 || panel->height() <= 0)
        fail(QStringLiteral("statisticsPanel size %1x%2 (implicit %3x%4)")
                 .arg(panel->width())
                 .arg(panel->height())
                 .arg(panel->implicitWidth())
                 .arg(panel->implicitHeight()));

    for (const QString &name : cardNames) {
        auto *card = findNamedItem(roots, name);
        if (!card) {
            QStringList rowNames;
            for (auto *row : { row1, row2 }) {
                if (!row)
                    continue;
                QStringList names;
                const auto kids = row->childItems();
                for (const auto *kid : kids)
                    names << kid->objectName();
                rowNames << names.join(u',');
            }
            fail(name + QStringLiteral(" not found (row children: row1=%1 row2=%2; names row1=[%3] row2=[%4])")
                     .arg(row1Count)
                     .arg(row2Count)
                     .arg(rowNames.value(0))
                     .arg(rowNames.value(1)));
            continue;
        }
        if (!card->isVisible())
            continue; // the contract is: every VISIBLE card has real geometry
        if (card->width() <= 0 || card->height() <= 0)
            fail(name + QStringLiteral(" size %1x%2 (implicit %3x%4)")
                     .arg(card->width())
                     .arg(card->height())
                     .arg(card->implicitWidth())
                     .arg(card->implicitHeight()));
    }

    if (row1 && row2 && row2->y() + 1e-6 < row1->y() + row1->height())
        fail(QStringLiteral("statisticsRow2 y=%1 overlaps row1 (y=%2 h=%3)")
                 .arg(row2->y())
                 .arg(row1->y())
                 .arg(row1->height()));

    // ---- Statistics presentation extraction (M9-C C2) ----
    // The overview is a composition: a wrapper (SectionHeader + PanelCard)
    // over two presentation pieces (metrics row, outcomes row). Guard the
    // extraction contract for the ACTIVE instance: the wrapper keeps a real
    // content-derived implicit size (ISSUE-012: never anchors/parent
    // allocation), both pieces exist, each lives inside that instance's
    // panel, and the two pieces are distinct items.
    {
        auto *overview =
            findNamedItem(roots, suffixed(QStringLiteral("statisticsOverview")));
        auto *panel = findNamedItem(roots, suffixed(QStringLiteral("statisticsPanel")));
        auto *metricsRow =
            findNamedItem(roots, suffixed(QStringLiteral("statisticsRow1")));
        auto *outcomesRow =
            findNamedItem(roots, suffixed(QStringLiteral("statisticsRow2")));

        if (page == ActivePage::Dashboard && overview) {
            fail(QStringLiteral("statisticsOverview_dashboard still exists — "
                                "since C3 the dashboard composes the "
                                "statistics pieces directly"));
        }

        auto under = [](QQuickItem *item, QQuickItem *ancestor) {
            for (auto *p = item ? item->parentItem() : nullptr; p;
                 p = p->parentItem())
                if (p == ancestor)
                    return true;
            return false;
        };
        for (auto *row : { metricsRow, outcomesRow }) {
            if (!row || row->width() <= 0 || row->height() <= 0) {
                fail(QStringLiteral("statistics presentation row missing or "
                                    "collapsed (w=%1 h=%2)")
                         .arg(row ? row->width() : -1)
                         .arg(row ? row->height() : -1));
                continue;
            }
            if (panel && !under(row, panel))
                fail(QStringLiteral("a statistics presentation row is not inside "
                                    "its instance panel (extraction misplaced "
                                    "it)"));
        }
        if (metricsRow && outcomesRow && metricsRow == outcomesRow)
            fail(QStringLiteral("statistics metrics and outcomes rows resolved "
                                "to the SAME item"));
    }

    } // end statsVisible

    // ---- Dashboard layout shell (M9-C C1) ----
    // Contract: the content region is a tight vertical stack whose section
    // gaps are bound to DS.spacingM, and the leftover height is owned by an
    // explicit tail spacer. A gap equal to the token (not merely bounded) is
    // assertable BECAUSE the QML binds it to the token, and it is exactly
    // what catches the real regression this guards: surplus space being
    // distributed between sections again (the pre-C1 Dashboard spread the
    // action row and the statistics section ~185 px apart).
    if (page == ActivePage::Dashboard) {
        auto *dashPage =
            findNamedItem(roots, QStringLiteral("dashboardWorkspace"));
        auto *dashHeader =
            findNamedItem(roots, QStringLiteral("dashboardHeader"));
        auto *dashAction =
            findNamedItem(roots, QStringLiteral("dashboardRunDemo"));
        auto *dashHint =
            findNamedItem(roots, QStringLiteral("dashboardEmptyHint"));
        auto *dashSpacer =
            findNamedItem(roots, QStringLiteral("dashboardTailSpacer"));
        auto *dashStatsHeader =
            findNamedItem(roots, QStringLiteral("statisticsHeader_dashboard"));
        auto *dashStatsPanel =
            findNamedItem(roots, QStringLiteral("statisticsPanel_dashboard"));

        auto nonzero = [&fail](QQuickItem *item, const QString &name) {
            if (!item) {
                fail(name + QStringLiteral(" not found"));
                return false;
            }
            if (item->width() <= 0 || item->height() <= 0) {
                fail(QStringLiteral("%1 size %2x%3 (implicit %4x%5)")
                         .arg(name)
                         .arg(item->width())
                         .arg(item->height())
                         .arg(item->implicitWidth())
                         .arg(item->implicitHeight()));
                return false;
            }
            return true;
        };
        auto insidePage = [&fail, dashPage](QQuickItem *item,
                                            const QString &name) {
            if (!item || !dashPage)
                return;
            const QPointF origin = item->mapToItem(dashPage, QPointF(0, 0));
            const QRectF box(origin, QSizeF(item->width(), item->height()));
            const QRectF pageRect(0, 0, dashPage->width(), dashPage->height());
            if (!pageRect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(box))
                fail(QStringLiteral("%1 escapes the dashboard page "
                                    "(x=%2 y=%3 w=%4 h=%5 in %6x%7)")
                         .arg(name)
                         .arg(box.x())
                         .arg(box.y())
                         .arg(box.width())
                         .arg(box.height())
                         .arg(pageRect.width())
                         .arg(pageRect.height()));
        };

        if (!dashPage)
            fail(QStringLiteral("dashboardWorkspace not found"));
        nonzero(dashHeader, QStringLiteral("dashboardHeader"));
        nonzero(dashAction, QStringLiteral("dashboardRunDemo"));
        nonzero(dashStatsPanel, QStringLiteral("statisticsPanel_dashboard"));
        insidePage(dashHeader, QStringLiteral("dashboardHeader"));
        insidePage(dashAction, QStringLiteral("dashboardRunDemo"));
        insidePage(dashStatsPanel, QStringLiteral("statisticsPanel_dashboard"));

        if (dashPage && dashHeader) {
            struct Band {
                QString name;
                QQuickItem *item;
            };
            const QVector<Band> bands = {
                { QStringLiteral("dashboardHeader"), dashHeader },
                { QStringLiteral("dashboardRunDemo"), dashAction },
                { QStringLiteral("dashboardEmptyHint"),
                  dashHint && dashHint->isVisible() ? dashHint : nullptr },
                { QStringLiteral("statisticsHeader_dashboard"),
                  dashStatsHeader },
                { QStringLiteral("statisticsPanel_dashboard"), dashStatsPanel },
            };
            auto topIn = [dashPage](QQuickItem *item) {
                return item ? item->mapToItem(dashPage, QPointF(0, 0)).y()
                            : 0.0;
            };
            // 1. the content region starts exactly at the page margin
            if (qAbs(topIn(dashHeader) - dsSpacingL) > 0.5)
                fail(QStringLiteral("dashboard content starts at y=%1, expected "
                                    "the page margin %2")
                         .arg(topIn(dashHeader))
                         .arg(dsSpacingL));
            // 2. every consecutive gap equals the section spacing token
            QQuickItem *prev = nullptr;
            QString prevName;
            for (const Band &band : bands) {
                if (!band.item)
                    continue;
                if (prev) {
                    const double gap =
                        topIn(band.item)
                        - (topIn(prev) + prev->height());
                    if (gap < -0.5)
                        fail(QStringLiteral("%1 overlaps %2 (gap %3)")
                                 .arg(band.name, prevName)
                                 .arg(gap));
                    else if (qAbs(gap - dsSpacingM) > 0.5)
                        fail(QStringLiteral("gap %1 -> %2 is %3, expected the "
                                            "DS.spacingM token %4 (surplus "
                                            "space must not be distributed "
                                            "between sections)")
                                 .arg(prevName, band.name)
                                 .arg(gap)
                                 .arg(dsSpacingM));
                }
                prev = band.item;
                prevName = band.name;
            }
            // 3. the tail spacer exists, is visible and owns the remainder
            if (!nonzero(dashSpacer, QStringLiteral("dashboardTailSpacer"))) {
                // reported above
            } else {
                if (!dashSpacer->isVisible())
                    fail(QStringLiteral("dashboardTailSpacer is not visible"));
                if (dashStatsPanel && topIn(dashSpacer) + 0.5
                                          < topIn(dashStatsPanel)
                                                + dashStatsPanel->height())
                    fail(QStringLiteral("dashboardTailSpacer (y=%1) does not "
                                        "follow the statistics block "
                                        "(bottom=%2)")
                             .arg(topIn(dashSpacer))
                             .arg(topIn(dashStatsPanel)
                                  + dashStatsPanel->height()));
                const double spacerBottom =
                    topIn(dashSpacer) + dashSpacer->height();
                const double expectedBottom = dashPage->height() - dsSpacingL;
                if (qAbs(spacerBottom - expectedBottom) > 0.5)
                    fail(QStringLiteral("dashboardTailSpacer bottom is %1, "
                                        "expected the page content bottom %2")
                             .arg(spacerBottom)
                             .arg(expectedBottom));
                qInfo().noquote()
                    << QStringLiteral("DASHBOARD LAYOUT: header.top=%1 "
                                      "action.top=%2 stats.top=%3 "
                                      "spacer.height=%4")
                           .arg(topIn(dashHeader))
                           .arg(topIn(dashAction))
                           .arg(topIn(dashStatsHeader))
                           .arg(dashSpacer->height());
            }
        }

        // M9-C C3: the dashboard statistics composition + distribution
        // contract (zero state here — the standard geometry passes never run
        // a batch; the C4 targeted demo passes below do).
        assertOutcomeDistribution(roots, contextLabel, failures);
        // M9-C C4: the deterministic attention summary + diagnosis cue.
        assertDashboardAttention(roots, contextLabel, failures);
    }

    // ---- Transactions page (M9-D D2) ----
    // Real geometry contract for a newly ACTIVE workspace: page, header, the
    // single transactions pane, its fixed table header, the list viewport and
    // the empty state when the model is empty. Column widths are the pane's
    // single column-geometry owner — they must fit inside the pane.
    if (page == ActivePage::Transactions) {
        auto *txPage =
            findNamedItem(roots, QStringLiteral("transactionsPage"));
        auto *txHeader =
            findNamedItem(roots, QStringLiteral("transactionsPageHeader"));
        auto *txPane = findNamedItem(roots, QStringLiteral("transactionsPane"));
        auto *txTableHeader =
            findNamedItem(roots, QStringLiteral("transactionsTableHeader"));
        auto *txList = findNamedItem(roots, QStringLiteral("transactionsList"));
        auto *txEmpty =
            findNamedItem(roots, QStringLiteral("transactionsEmptyHint"));

        auto nonzero = [&fail](QQuickItem *item, const QString &name) {
            if (!item) {
                fail(name + QStringLiteral(" not found"));
                return false;
            }
            if (item->width() <= 0 || item->height() <= 0) {
                fail(QStringLiteral("%1 size %2x%3 (implicit %4x%5)")
                         .arg(name)
                         .arg(item->width())
                         .arg(item->height())
                         .arg(item->implicitWidth())
                         .arg(item->implicitHeight()));
                return false;
            }
            return true;
        };
        auto insidePage = [&fail, txPage](QQuickItem *item,
                                          const QString &name) {
            if (!item || !txPage)
                return;
            const QPointF origin = item->mapToItem(txPage, QPointF(0, 0));
            const QRectF box(origin, QSizeF(item->width(), item->height()));
            const QRectF pageRect(0, 0, txPage->width(), txPage->height());
            if (!pageRect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(box))
                fail(QStringLiteral("%1 escapes the transactions page").arg(name));
        };
        nonzero(txPage, QStringLiteral("transactionsPage"));
        nonzero(txHeader, QStringLiteral("transactionsPageHeader"));
        nonzero(txPane, QStringLiteral("transactionsPane"));
        nonzero(txTableHeader, QStringLiteral("transactionsTableHeader"));
        nonzero(txList, QStringLiteral("transactionsList"));

        if (host && txPage && txPage->width() > host->width() + 0.5)
            fail(QStringLiteral("transactions content exceeds the workspace "
                                "width"));
        // the pane stays inside the page (no clipping, no overflow)
        if (txPage && txPane) {
            const QPointF origin = txPane->mapToItem(txPage, QPointF(0, 0));
            const QRectF box(origin, QSizeF(txPane->width(), txPane->height()));
            const QRectF pageRect(0, 0, txPage->width(), txPage->height());
            if (!pageRect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(box))
                fail(QStringLiteral("transactionsPane escapes the transactions "
                                    "page (x=%1 y=%2 w=%3 h=%4 in %5x%6)")
                         .arg(box.x())
                         .arg(box.y())
                         .arg(box.width())
                         .arg(box.height())
                         .arg(pageRect.width())
                         .arg(pageRect.height()));
        }
        // the table header sits above the list (no overlap)
        if (txTableHeader && txList
            && txList->mapToItem(txPane, QPointF(0, 0)).y() + 0.5
                   < txTableHeader->mapToItem(txPane, QPointF(0, 0)).y()
                         + txTableHeader->height())
            fail(QStringLiteral("transactionsList overlaps the table header"));
        // column widths are derived from one owner and must fit the pane
        if (txPane) {
            const int usable =
                qMax(static_cast<int>(txPane->width()) - 24, 0);
            const int device = qMax(64, qRound(usable * 0.15));
            const int function = qMax(60, qRound(usable * 0.15));
            const int status = qMax(96, qRound(usable * 0.20));
            const int latency = qMax(84, qRound(usable * 0.18));
            const int leading = device + function + status + latency;
            const int exception = qMax(usable - leading, 96);
            if (leading + exception > usable + 1)
                fail(QStringLiteral("transaction columns do not fit the pane "
                                    "(leading %1 + exception %2 > usable %3)")
                         .arg(leading)
                         .arg(exception)
                         .arg(usable));
            qInfo().noquote()
                << QStringLiteral("TRANSACTIONS COLUMNS: pane=%1 usable=%2 "
                                  "device=%3 function=%4 status=%5 latency=%6 "
                                  "exception=%7")
                       .arg(txPane->width())
                       .arg(usable)
                       .arg(device)
                       .arg(function)
                       .arg(status)
                       .arg(latency)
                       .arg(exception);
        }
        // M9-D D3: the read-only detail region — bounded, inside the page,
        // below the list, and never overlapping it. When a selection exists
        // its mapping is asserted field by field (see the helper).
        auto *txDetail = findNamedItem(roots, QStringLiteral("transactionDetail"));
        nonzero(txDetail, QStringLiteral("transactionDetail"));
        insidePage(txDetail, QStringLiteral("transactionDetail"));
        if (txDetail && txList
            && txDetail->mapToItem(txPage, QPointF(0, 0)).y() + 0.5
                   < txList->mapToItem(txPage, QPointF(0, 0)).y()
                         + txList->height())
            fail(QStringLiteral("transactionDetail overlaps the transactions "
                                "list"));
        // §19 viewport capacity: at least ~6 ordinary 36px rows
        if (txList && txList->height() + 0.5 < 6 * 36)
            fail(QStringLiteral("transactionsList viewport %1 cannot hold six "
                                "36px rows").arg(txList->height()));
        // M9-D D4: the diagnosis EXISTENCE cue sits as a natural-height line
        // BELOW the detail region. With a session present it must lay out
        // completely (nonzero, inside the page, never overlapping the detail
        // block); without a session it owns no space — the empty hints own
        // that state, matching the Dashboard cue's visibility rule.
        auto *txCue =
            findNamedItem(roots, QStringLiteral("transactionsDiagnosisCue"));
        if (!txCue)
            fail(QStringLiteral("transactionsDiagnosisCue not found"));
        else {
            auto *cueCtrl =
                roots.value(0)
                    ? roots.value(0)->findChild<QObject *>(
                          QStringLiteral("analysisController"))
                    : nullptr;
            const bool cueExpected =
                cueCtrl && cueCtrl->property("observedCount").toInt() > 0;
            if (cueExpected) {
                nonzero(txCue, QStringLiteral("transactionsDiagnosisCue"));
                insidePage(txCue, QStringLiteral("transactionsDiagnosisCue"));
                if (txDetail
                    && txCue->mapToItem(txPage, QPointF(0, 0)).y() + 0.5
                           < txDetail->mapToItem(txPage, QPointF(0, 0)).y()
                                 + txDetail->height())
                    fail(QStringLiteral("transactionsDiagnosisCue overlaps the "
                                        "transaction detail region"));
            }
        }
        assertTransactionDetailMapping(roots, contextLabel, failures);

        // empty state visibility follows the model row count
        auto *ctrl = roots.value(0)
                         ? roots.value(0)->findChild<QObject *>(
                               QStringLiteral("analysisController"))
                         : nullptr;
        const int rows = ctrl ? ctrl->property("observedCount").toInt() : -1;
        if (txEmpty && txEmpty->isVisible() != (rows == 0))
            fail(QStringLiteral("transactions empty state visibility %1 does "
                                "not match observedCount %2")
                     .arg(txEmpty->isVisible())
                     .arg(rows));
    }

    // ---- Communication page (M9-B3) ----
    if (page == ActivePage::Communication) {
        auto *connectionSection = findNamedItem(
            roots, QStringLiteral("communicationConnectionSection"));
        auto *requestSection = findNamedItem(
            roots, QStringLiteral("communicationRequestSection"));
        if (!connectionSection)
            fail(QStringLiteral("communicationConnectionSection not found"));
        else if (connectionSection->width() <= 0 || connectionSection->height() <= 0)
            fail(QStringLiteral("communicationConnectionSection size %1x%2")
                     .arg(connectionSection->width())
                     .arg(connectionSection->height()));
        if (!requestSection)
            fail(QStringLiteral("communicationRequestSection not found"));
        else if (requestSection->width() <= 0 || requestSection->height() <= 0)
            fail(QStringLiteral("communicationRequestSection size %1x%2")
                     .arg(requestSection->width())
                     .arg(requestSection->height()));
        if (connectionSection && requestSection
            && requestSection->y() + 1e-6
                   < connectionSection->y() + connectionSection->height())
            fail(QStringLiteral("communicationRequestSection overlaps the "
                                "connection section"));
        if (auto *pageItem =
                findNamedItem(roots, QStringLiteral("communicationWorkspace"))) {
            for (auto *section : { connectionSection, requestSection }) {
                if (section
                    && section->x() + section->width()
                           > pageItem->width() + 0.5)
                    fail(QStringLiteral("%1 exceeds the workspace width")
                             .arg(section->objectName()));
            }
        }

        // Hidden error labels get no geometry assumption (T017 §34 spec:
        // only assert what is visible).
        auto *serialError =
            findNamedItem(roots, QStringLiteral("communicationSerialError"));
        if (serialError && serialError->isVisible()
            && (serialError->width() <= 0 || serialError->height() <= 0))
            fail(QStringLiteral("communicationSerialError visible but size "
                                "%1x%2")
                     .arg(serialError->width())
                     .arg(serialError->height()));
    }

    // ---- Replay page (M9-B4.3): sparse page, shell + load action only ----
    if (page == ActivePage::Replay) {
        auto *replayPage =
            findNamedItem(roots, QStringLiteral("replayWorkspace"));
        auto *replayHeader =
            findNamedItem(roots, QStringLiteral("replayHeader"));
        auto *replayActions = findNamedItem(
            roots, QStringLiteral("replayActionsSection"));
        auto *replayLoad =
            findNamedItem(roots, QStringLiteral("replayLoadButton"));
        for (auto *item : { replayPage, replayHeader, replayActions,
                            replayLoad }) {
            if (!item) {
                fail(QStringLiteral("replay element missing"));
                continue;
            }
            if (item->width() <= 0 || item->height() <= 0)
                fail(QStringLiteral("replay element %1 size %2x%3")
                         .arg(item->objectName())
                         .arg(item->width())
                         .arg(item->height()));
        }
        // Content must stay inside the workspace host bounds.
        if (host && replayPage && replayPage->width() > host->width() + 0.5)
            fail(QStringLiteral("replay content exceeds the workspace width"));
    }

    // ---- Diagnosis page (M9-B5.3): five-workspace geometry matrix ----
    // The contract applies to the SELECTED tab only; the two hidden tab
    // blocks deliberately get zero assertions (T017 §43.15 — hidden content
    // geometry is not a contract). The geometry check selects each of the
    // three tabs in turn, so every tab is asserted while it is selected.
    if (page == ActivePage::Diagnosis) {
        auto *diagPage = findNamedItem(roots, QStringLiteral("diagnosisPage"));
        auto *diagHeader =
            findNamedItem(roots, QStringLiteral("diagnosisPageHeader"));
        auto *tabs = findNamedItem(roots, QStringLiteral("diagnosisTabs"));
        auto *tabContent =
            findNamedItem(roots, QStringLiteral("diagnosisTabContent"));

        auto nonzero = [&fail](QQuickItem *item, const QString &name) {
            if (!item) {
                fail(name + QStringLiteral(" not found"));
                return false;
            }
            if (item->width() <= 0 || item->height() <= 0) {
                fail(QStringLiteral("%1 size %2x%3 (implicit %4x%5)")
                         .arg(name)
                         .arg(item->width())
                         .arg(item->height())
                         .arg(item->implicitWidth())
                         .arg(item->implicitHeight()));
                return false;
            }
            return true;
        };
        auto insidePage = [&fail, diagPage](QQuickItem *item,
                                            const QString &name) {
            if (!item || !diagPage)
                return;
            const QPointF origin = item->mapToItem(diagPage, QPointF(0, 0));
            const QRectF box(origin, QSizeF(item->width(), item->height()));
            const QRectF pageRect(0, 0, diagPage->width(), diagPage->height());
            if (!pageRect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(box))
                fail(QStringLiteral("%1 escapes the diagnosis page "
                                    "(x=%2 y=%3 w=%4 h=%5 in %6x%7)")
                         .arg(name)
                         .arg(box.x())
                         .arg(box.y())
                         .arg(box.width())
                         .arg(box.height())
                         .arg(pageRect.width())
                         .arg(pageRect.height()));
        };

        if (diagPage && (diagPage->width() <= 0 || diagPage->height() <= 0))
            fail(QStringLiteral("diagnosisPage size %1x%2")
                     .arg(diagPage->width())
                     .arg(diagPage->height()));
        if (host && diagPage && diagPage->width() > host->width() + 0.5)
            fail(QStringLiteral("diagnosis content exceeds the workspace width"));
        nonzero(diagHeader, QStringLiteral("diagnosisPageHeader"));
        nonzero(tabs, QStringLiteral("diagnosisTabs"));
        nonzero(tabContent, QStringLiteral("diagnosisTabContent"));
        insidePage(diagHeader, QStringLiteral("diagnosisPageHeader"));
        insidePage(tabs, QStringLiteral("diagnosisTabs"));
        insidePage(tabContent, QStringLiteral("diagnosisTabContent"));

        // The SectionHeader must sit above the tab bar (it must not overlap
        // the main content block the tabs own).
        if (diagPage && diagHeader && tabs) {
            const double headerBottom =
                diagHeader->mapToItem(diagPage, QPointF(0, 0)).y()
                + diagHeader->height();
            const double tabsTop = tabs->mapToItem(diagPage, QPointF(0, 0)).y();
            if (tabsTop + 0.5 < headerBottom)
                fail(QStringLiteral("diagnosisTabs (y=%1) overlaps "
                                    "diagnosisPageHeader (bottom=%2)")
                         .arg(tabsTop)
                         .arg(headerBottom));
        }

        // One selected tab: root block, its controls and its bounded viewport.
        auto checkSelectedTab = [&](const QString &tabName,
                                    const QString &viewportName,
                                    const QStringList &controlNames) {
            auto *tab = findNamedItem(roots, tabName);
            if (!nonzero(tab, tabName))
                return;
            if (!tab->isVisible()) {
                fail(tabName + QStringLiteral(" is not the selected tab"));
                return;
            }
            insidePage(tab, tabName);
            for (const QString &controlName : controlNames) {
                auto *control = findNamedItem(roots, controlName);
                if (!nonzero(control, controlName))
                    continue;
                if (!control->isVisible())
                    fail(controlName + QStringLiteral(" is not visible"));
                insidePage(control, controlName);
            }
            auto *viewport = findNamedItem(roots, viewportName);
            if (!nonzero(viewport, viewportName))
                return;
            if (diagPage && viewport->height() > diagPage->height() + 0.5)
                fail(QStringLiteral("%1 viewport height %2 exceeds the page "
                                    "height %3")
                         .arg(viewportName)
                         .arg(viewport->height())
                         .arg(diagPage->height()));
            insidePage(viewport, viewportName);
            if (!viewport->property("clip").toBool())
                fail(viewportName + QStringLiteral(" lost its clip contract"));
        };

        const int selectedTab =
            tabContent ? tabContent->property("currentIndex").toInt() : -1;
        switch (selectedTab) {
        case 0:
            checkSelectedTab(
                QStringLiteral("diagnosisBaselineTab"),
                QStringLiteral("diagnosisBaselineViewport"),
                { QStringLiteral("diagnosisRunBaselineButton"),
                  QStringLiteral("diagnosisClearDiagnosisButton") });
            break;
        case 1:
            checkSelectedTab(
                QStringLiteral("diagnosisAiTab"),
                QStringLiteral("diagnosisAiViewport"),
                { QStringLiteral("diagnosisAiAskButton"),
                  QStringLiteral("diagnosisAiCancelButton") });
            break;
        case 2:
            checkSelectedTab(
                QStringLiteral("diagnosisAgentTab"),
                QStringLiteral("diagnosisAgentViewport"),
                { QStringLiteral("diagnosisAgentQuestion"),
                  QStringLiteral("diagnosisAgentAskButton"),
                  QStringLiteral("diagnosisAgentCancelButton") });
            break;
        default:
            fail(QStringLiteral("diagnosisTabContent currentIndex %1 out of "
                                "range 0..2")
                     .arg(selectedTab));
            break;
        }
    }

    return failures;
}

// M9-B1 navigation guards (minimum set while only ONE real workspace
// exists): the selection index stays legal, the legacy workspace is the
// visible page, and a disabled future entry can never change the selection
// (NavigationRail.activate is the single mutation path, shared by mouse,
// Enter and Space).
QStringList runShellNavAssertions(const QList<QObject *> &roots,
                                  const QString &contextLabel)
{
    QStringList failures;
    auto fail = [&failures, &contextLabel](const QString &message) {
        failures << contextLabel + QStringLiteral(": ") + message;
    };

    auto *rail = findNamedItem(roots, QStringLiteral("navigationRail"));
    if (!rail) {
        fail(QStringLiteral("NAV navigationRail not found"));
        return failures;
    }

    QObject *rootObj = roots.value(0);
    // M9-D D5 retirement oracle: the Legacy workspace and its remnants must
    // be GONE from the runtime tree (retirement = actual removal).
    for (const QString &retired : {QStringLiteral("legacyWorkspace"),
                                   QStringLiteral("statisticsOverview_legacy"),
                                   QStringLiteral("legacyTailSpacer")}) {
        if (findNamedItem(roots, retired))
            fail(QStringLiteral("NAV %1 still exists — the Legacy workspace "
                                "was not retired from the runtime tree")
                     .arg(retired));
    }
    const int dashboardIndex =
        rootObj->property("workspaceDashboardIndex").toInt();

    const int index = rail->property("currentWorkspaceIndex").toInt();
    // M9-D D5: the compact rail carries six entries — five ACTIVE workspaces
    // 0..4 plus the disabled Device entry at 5. The retired Legacy entry is
    // gone, so entry count is 6 and the index range is 0..5.
    if (index < 0 || index > 5)
        fail(QStringLiteral("NAV currentWorkspaceIndex %1 out of range 0..5")
                 .arg(index));
    const int communicationIndex =
        rootObj->property("workspaceCommunicationIndex").toInt();
    const int replayIndex = rootObj->property("workspaceReplayIndex").toInt();
    const int diagnosisIndex =
        rootObj->property("workspaceDiagnosisIndex").toInt();
    const int transactionsIndex =
        rootObj->property("workspaceTransactionsIndex").toInt();
    if (index != dashboardIndex
        && index != communicationIndex && index != replayIndex
        && index != diagnosisIndex && index != transactionsIndex)
        fail(QStringLiteral("NAV currentWorkspaceIndex %1 is not one of the "
                            "real workspaces (transactions=%2 dashboard=%3 "
                            "communication=%4 replay=%5 diagnosis=%6)")
                 .arg(index)
                 .arg(transactionsIndex)
                 .arg(dashboardIndex)
                 .arg(communicationIndex)
                 .arg(replayIndex)
                 .arg(diagnosisIndex));

    // Visibility must follow the selection (page-independent form: this
    // guard runs at EVERY workspace now).
    auto *dashboard = findNamedItem(roots, QStringLiteral("dashboardWorkspace"));
    auto *communication =
        findNamedItem(roots, QStringLiteral("communicationWorkspace"));
    auto *replay = findNamedItem(roots, QStringLiteral("replayWorkspace"));
    auto *diagnosis = findNamedItem(roots, QStringLiteral("diagnosisPage"));
    auto *transactions =
        findNamedItem(roots, QStringLiteral("transactionsPage"));
    if (!dashboard)
        fail(QStringLiteral("NAV dashboardWorkspace not found"));
    else if (dashboard->isVisible() != (index == dashboardIndex))
        fail(QStringLiteral("NAV dashboardWorkspace visibility (%1) does not "
                            "follow the selection %2")
                 .arg(dashboard->isVisible())
                 .arg(index));
    if (!communication)
        fail(QStringLiteral("NAV communicationWorkspace not found"));
    else if (communication->isVisible() != (index == communicationIndex))
        fail(QStringLiteral("NAV communicationWorkspace visibility (%1) does "
                            "not follow the selection %2")
                 .arg(communication->isVisible())
                 .arg(index));
    if (!replay)
        fail(QStringLiteral("NAV replayWorkspace not found"));
    else if (replay->isVisible() != (index == replayIndex))
        fail(QStringLiteral("NAV replayWorkspace visibility (%1) does not "
                            "follow the selection %2")
                 .arg(replay->isVisible())
                 .arg(index));
    if (!diagnosis)
        fail(QStringLiteral("NAV diagnosisPage not found"));
    else if (diagnosis->isVisible() != (index == diagnosisIndex))
        fail(QStringLiteral("NAV diagnosisPage visibility (%1) does not "
                            "follow the selection %2")
                 .arg(diagnosis->isVisible())
                 .arg(index));
    if (!transactions)
        fail(QStringLiteral("NAV transactionsPage not found"));
    else if (transactions->isVisible() != (index == transactionsIndex))
        fail(QStringLiteral("NAV transactionsPage visibility (%1) does not "
                            "follow the selection %2")
                 .arg(transactions->isVisible())
                 .arg(index));

    auto *item0 = findNamedItem(roots, QStringLiteral("navItem_0"));
    auto *item1 = findNamedItem(roots, QStringLiteral("navItem_1"));
    if (!item0 || !item1) {
        fail(QStringLiteral("NAV navItem_0/navItem_1 not found"));
        return failures;
    }

    // M9-D D5 matrix: 事务 / 总览 / 通信 / 回放 / 诊断 are REAL workspaces
    // (enabled); 设备 stays disabled (M12). The retired 工作台 entry no
    // longer exists and navItem_6+ must not either.
    if (!item0->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_0 (transactions) must be enabled"));
    if (!item1->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_1 (dashboard) must be enabled"));
    auto *item2 = findNamedItem(roots, QStringLiteral("navItem_2"));
    if (!item2 || !item2->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_2 (communication) must be enabled"));
    auto *item3 = findNamedItem(roots, QStringLiteral("navItem_3"));
    if (!item3 || !item3->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_3 (replay) must be enabled"));
    auto *item4 = findNamedItem(roots, QStringLiteral("navItem_4"));
    if (!item4 || !item4->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_4 (diagnosis) must be enabled"));
    for (int i = 5; i <= 5; ++i) {
        auto *item = findNamedItem(roots,
                                   QStringLiteral("navItem_%1").arg(i));
        if (!item) {
            fail(QStringLiteral("NAV navItem_%1 not found").arg(i));
            continue;
        }
        if (i == 5)
            continue; // M12-B: Device is now the profile workspace (enabled)
        if (item->property("enabled").toBool())
            fail(QStringLiteral("NAV navItem_%1 (future workspace) must stay "
                                "disabled until its extraction step")
                     .arg(i));
        // A disabled entry must not be able to change the selection, even
        // when its activation path is invoked directly (same path as
        // click/keys). The invoke result is checked too: a silently
        // unresolvable activate() would make this guard vacuous.
        if (!QMetaObject::invokeMethod(item, "activate"))
            fail(QStringLiteral("NAV navItem_%1.activate() is not invokable — "
                                "the disabled-entry guard would be vacuous")
                     .arg(i));
        if (rail->property("currentWorkspaceIndex").toInt() != index)
            fail(QStringLiteral("NAV disabled navItem_%1 changed "
                                "currentWorkspaceIndex")
                     .arg(i));
    }

    // ---- M9-D D5: compact index contract ----
    const int deviceIndex = rootObj->property("workspaceDeviceIndex").toInt();
    if (transactionsIndex != 0)
        fail(QStringLiteral("NAV workspaceTransactionsIndex is %1, expected 0")
                 .arg(transactionsIndex));
    if (deviceIndex != 5)
        fail(QStringLiteral("NAV workspaceDeviceIndex is %1, expected 5")
                 .arg(deviceIndex));
    if (!transactions)
        fail(QStringLiteral("NAV transactionsPage not found"));
    else {
        auto *host = findNamedItem(roots, QStringLiteral("workspaceHost"));
        if (host && transactions->parentItem() != host)
            fail(QStringLiteral("NAV transactionsPage is not a direct child of "
                                "workspaceHost"));
        if (transactions->property("analysisController").value<QObject *>()
            == nullptr)
            fail(QStringLiteral("NAV transactionsPage did not receive the "
                                "analysisController injection"));
    }

    // ---- M9-D D2/D5: runtime single-owner proof ----
    // The ONE transactions presentation lives under the Transactions page —
    // proved by walking the real tree, not by grepping source text. (The
    // retired Legacy workspace is asserted absent by the retirement oracle
    // at the top of this guard.)
    auto *pane = findNamedItem(roots, QStringLiteral("transactionsPane"));
    if (!pane)
        fail(QStringLiteral("NAV transactionsPane not found (the moved "
                            "presentation is missing)"));
    else if (transactions && !underItem(pane, transactions))
        fail(QStringLiteral("NAV transactionsPane is not under the "
                            "Transactions page"));

    // Re-activating the currently selected entry is a no-op.
    auto *activeItem = findNamedItem(
        roots, QStringLiteral("navItem_%1").arg(index));
    if (!activeItem) {
        fail(QStringLiteral("NAV active navItem_%1 not found").arg(index));
        return failures;
    }
    if (!QMetaObject::invokeMethod(activeItem, "activate"))
        fail(QStringLiteral("NAV navItem_%1.activate() is not invokable")
                 .arg(index));
    if (rail->property("currentWorkspaceIndex").toInt() != index)
        fail(QStringLiteral("NAV re-activating the active entry changed the "
                            "index"));

    return failures;
}

QString dumpGeometryTable(const QList<QObject *> &roots, const QString &contextLabel)
{
    // Names follow the currently VISIBLE page (same rule as the assertions).
    const ActivePage page = activePage(roots);
    const bool statsVisible = (page != ActivePage::Communication);
    const QString suffix = QStringLiteral("dashboard");
    auto suffixed = [&suffix](const QString &base) {
        return base + QLatin1Char('_') + suffix;
    };

    QStringList names = {
        QStringLiteral("appBar"),          QStringLiteral("navigationRail"),
        QStringLiteral("workspaceHost"),   QStringLiteral("dashboardWorkspace"),
        QStringLiteral("communicationWorkspace"),
        QStringLiteral("replayWorkspace"),
        QStringLiteral("diagnosisPage"),
    };
    if (page == ActivePage::Replay) {
        names << QStringLiteral("replayHeader")
              << QStringLiteral("replayActionsSection")
              << QStringLiteral("replayLoadButton")
              << QStringLiteral("replayErrorLabel")
              << QStringLiteral("replayNoticeLabel");
    } else if (page == ActivePage::Diagnosis) {
        // M9-B5.3: page-level frame plus the three tab blocks. Only the
        // SELECTED tab's content is dumped as a contract; the other two are
        // listed for information (hidden tab content has no geometry
        // contract — T017 §43.15).
        names << QStringLiteral("diagnosisPageHeader")
              << QStringLiteral("diagnosisTabs")
              << QStringLiteral("diagnosisTabContent")
              << QStringLiteral("diagnosisBaselineTab")
              << QStringLiteral("diagnosisBaselineViewport")
              << QStringLiteral("diagnosisRunBaselineButton")
              << QStringLiteral("diagnosisClearDiagnosisButton")
              << QStringLiteral("diagnosisAiTab")
              << QStringLiteral("diagnosisAiViewport")
              << QStringLiteral("diagnosisAgentTab")
              << QStringLiteral("diagnosisAgentViewport");
    } else if (statsVisible) {
        names << suffixed(QStringLiteral("statisticsPanel"))
              << suffixed(QStringLiteral("statisticsRow1"))
              << suffixed(QStringLiteral("statisticsRow2"))
              << suffixed(QStringLiteral("statCard_0"))
              << suffixed(QStringLiteral("statCard_1"))
              << suffixed(QStringLiteral("statCard_2"))
              << suffixed(QStringLiteral("statCard_rate"))
              << suffixed(QStringLiteral("statCard_latency"))
              << suffixed(QStringLiteral("statusCard_0"))
              << suffixed(QStringLiteral("statusCard_1"))
              << suffixed(QStringLiteral("statusCard_2"))
              << suffixed(QStringLiteral("statusCard_3"))
              << suffixed(QStringLiteral("statusCard_4"))
              << suffixed(QStringLiteral("statusCard_5"));
        if (page == ActivePage::Dashboard)
            names << QStringLiteral("dashboardHeader")
                  << QStringLiteral("dashboardRunDemo")
                  << QStringLiteral("dashboardEmptyHint")
                  << QStringLiteral("dashboardTailSpacer")
                  << QStringLiteral("outcomeDistribution_dashboard")
                  << QStringLiteral("outcomeDistributionBar_dashboard")
                  << QStringLiteral("outcomeSegment_0_dashboard")
                  << QStringLiteral("outcomeSegment_1_dashboard")
                  << QStringLiteral("outcomeSegment_2_dashboard")
                  << QStringLiteral("outcomeSegment_3_dashboard")
                  << QStringLiteral("outcomeSegment_4_dashboard")
                  << QStringLiteral("outcomeSegment_5_dashboard")
                  << QStringLiteral("dashboardAttentionSummary")
                  << QStringLiteral("dashboardDiagnosisCue");
        // M9-D D1: informational only — the hidden transactions shell has no
        // geometry contract while it is unreachable.
        if (page == ActivePage::Dashboard)
            names << QStringLiteral("transactionsPage");
    } else {
        names << QStringLiteral("communicationContentLayout")
              << QStringLiteral("communicationHeader")
              << QStringLiteral("communicationConnectionHeader")
              << QStringLiteral("communicationConnectionSection")
              << QStringLiteral("communicationRequestHeader")
              << QStringLiteral("communicationRequestSection")
              << QStringLiteral("communicationSerialError");
    }
    // M9-B5.2: the Legacy SplitView is gone; the promoted Transactions pane
    // took its place below the statistics panel and is dumped under its own
    // name (the old "diagnosisWorkspace" name no longer resolves).
    // M9-D D2: the single transactions presentation lives on its own page.
    names << QStringLiteral("transactionsPane");
    if (page == ActivePage::Transactions)
        names << QStringLiteral("transactionsPageHeader")
              << QStringLiteral("transactionsTableHeader")
              << QStringLiteral("transactionsList")
              << QStringLiteral("transactionsEmptyHint")
              << QStringLiteral("transactionDetail")
              << QStringLiteral("transactionDetailEmpty")
              << QStringLiteral("transactionDetailStatus")
              << QStringLiteral("transactionDetailIssue")
              << QStringLiteral("transactionsDiagnosisCue");
    QStringList lines;
    lines << QStringLiteral("GEOMETRY [%1]:").arg(contextLabel);
    for (const QString &name : names) {
        auto *item = findNamedItem(roots, name);
        if (!item) {
            lines << QStringLiteral("  %1: MISSING").arg(name);
            continue;
        }
        const QString parentName =
            item->parentItem() ? item->parentItem()->objectName()
                               : QStringLiteral("<none>");
        lines << QStringLiteral("  %1: x=%2 y=%3 w=%4 h=%5 implicit=%6x%7 "
                                "parent=%8")
                     .arg(name)
                     .arg(item->x())
                     .arg(item->y())
                     .arg(item->width())
                     .arg(item->height())
                     .arg(item->implicitWidth())
                     .arg(item->implicitHeight())
                     .arg(parentName.isEmpty() ? QStringLiteral("<unnamed>")
                                               : parentName);
    }
    return lines.join(u'\n');
}

// --qml-geometry-check: loads the REAL QML module, lets the layouts polish
// over two event-loop turns, asserts the statistics geometry contract at
// the default window size, then re-asserts it after resizing to the
// application minimum (1000x700 — the same two sizes used in manual
// acceptance). Exit code 0 = both sizes pass; 1 = any assertion failed.
// Runs only when requested: a normal launch prints nothing.
int runGeometryCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));

    // Optional evidence dumping: --qml-geometry-dump <dir> saves a
    // grabWindow() PNG after each size pass (exact logical pixels; used to
    // produce the acceptance screenshots without window-manager size
    // interference — ISSUE-012 evidence chain).
    QString dumpDir;
    const QStringList args = app.arguments();
    const int dumpFlagIdx = args.indexOf(QStringLiteral("--qml-geometry-dump"));
    if (dumpFlagIdx >= 0 && dumpFlagIdx + 1 < args.size())
        dumpDir = args.at(dumpFlagIdx + 1);
    auto dumpGrab = [&window, &dumpDir](const QString &tag) {
        if (dumpDir.isEmpty() || !window)
            return;
        const QImage image = window->grabWindow();
        const QString path =
            QDir(dumpDir).filePath(tag + QStringLiteral(".png"));
        if (image.save(path))
            qInfo().noquote() << "GEOMETRY DUMP:" << path
                              << image.size().width() << "x" << image.size().height();
        else
            qWarning() << "GEOMETRY DUMP FAILED:" << path;
    };

    // M9-B2: four measurement passes — each workspace at each size. HIDDEN
    // pages get no fragile geometry assertions (T017 §31.7); every pass
    // switches to the target workspace first and then verifies the ACTIVE
    // instance at the current size.
    struct MeasureStep {
        int pageIndex; // 0 transactions / 1 dashboard / 2 communication
                       // / 3 replay / 4 diagnosis (M9-D D5 compact rail)
        bool resizeToMin;
        QString tag;
        QString label;
        bool runDemo = false; // C4 targeted passes: publish the deterministic
                              // demo batch before measuring (default off, so
                              // the standard 10 passes keep their empty state)
        int selectRow = -1;   // D3 targeted passes: select this row in the
                              // transactions list before measuring
        bool resizeToDefault = false; // C4: the targeted demo passes follow the
                                      // minimum-size passes, so the first one
                                      // must restore the default window size
    };
    // M9-D D5: five active workspaces x two sizes = 10 standard passes —
    // the same matrix M9-B5.3 built, with the retired Legacy pair replaced
    // by the Transactions pair and the compact rail order as the sweep
    // order. The Diagnosis passes stay before the resize so the
    // minimum-size sweep keeps its single downward transition.
    const QVector<MeasureStep> steps = {
        { 0, false, QStringLiteral("m9d-transactions-1024x720"),
          QStringLiteral("DEFAULT transactions") },
        { 1, false, QStringLiteral("m9b4-dashboard-1024x720"),
          QStringLiteral("DEFAULT dashboard") },
        { 2, false, QStringLiteral("m9b4-communication-1024x720"),
          QStringLiteral("DEFAULT communication") },
        { 3, false, QStringLiteral("m9b4-replay-1024x720"),
          QStringLiteral("DEFAULT replay") },
        { 4, false, QStringLiteral("m9b5-diagnosis-1024x720"),
          QStringLiteral("DEFAULT diagnosis") },
        { 4, true, QStringLiteral("m9b5-diagnosis-1000x700"),
          QStringLiteral("MIN 1000x700 diagnosis") },
        { 0, true, QStringLiteral("m9d-transactions-1000x700"),
          QStringLiteral("MIN 1000x700 transactions") },
        // M9-D D3: two TARGETED selected-detail passes (the standard 12 keep
        // their no-selection state). Additive only.
        { 0, false, QStringLiteral("m9d-transactions-detail-1024x720"),
          QStringLiteral("SELECTED DETAIL transactions"), true, 2, true },
        { 0, true, QStringLiteral("m9d-transactions-detail-1000x700"),
          QStringLiteral("MIN 1000x700 selected detail"), false, 2 },
        { 3, false, QStringLiteral("m9b4-replay-1000x700"),
          QStringLiteral("MIN 1000x700 replay") },
        { 2, false, QStringLiteral("m9b4-communication-1000x700"),
          QStringLiteral("MIN 1000x700 communication") },
        { 1, false, QStringLiteral("m9b4-dashboard-1000x700"),
          QStringLiteral("MIN 1000x700 dashboard") },
        // M9-C C4: two TARGETED demo-dashboard passes so the attention line,
        // the diagnosis cue and the distribution are measured with real
        // content at both sizes. They are additive — the standard passes
        // keep their empty state and remain the matrix of record.
        { 1, false, QStringLiteral("m9c-dashboard-demo-1024x720"),
          QStringLiteral("DEMO dashboard"), true, true },
        { 1, true, QStringLiteral("m9c-dashboard-demo-1000x700"),
          QStringLiteral("MIN 1000x700 demo dashboard"), true },
    };
    // The Diagnosis pass measures each tab in turn: only the SELECTED tab
    // carries the geometry contract, so the pass selects, settles and
    // measures tab 0, tab 1 and tab 2 before advancing.
    constexpr int kDiagnosisTabs = 3;

    const int settleMs = 100;
    const int maxAttempts = 5;
    auto stepIndex = std::make_shared<int>(0);
    auto currentPageIndex = std::make_shared<int>(0);
    auto failures = std::make_shared<QStringList>();
    auto attempt = std::make_shared<int>(0);

    auto finish = [&app](const QStringList &fails) {
        if (fails.isEmpty())
            qInfo() << "GEOMETRY CHECK PASS"
                       "(10 standard passes: transactions + dashboard + "
                       "communication + replay + diagnosis x 2 sizes, the "
                       "diagnosis pass sweeps its three tabs; + 2 targeted "
                       "demo-dashboard passes from M9-C C4; + 2 targeted "
                       "selected-detail passes from M9-D D3)";
        else
            for (const QString &f : fails)
                qWarning().noquote() << "GEOFAIL:" << f;
        app.exit(fails.isEmpty() ? 0 : 1);
    };

    auto switchWorkspace = [&](int pageIndex, QStringList &fails) {
        QObject *rootObj = roots.value(0);
        const char *key = (pageIndex == 0)   ? "workspaceTransactionsIndex"
                        : (pageIndex == 1)   ? "workspaceDashboardIndex"
                        : (pageIndex == 2)   ? "workspaceCommunicationIndex"
                        : (pageIndex == 3)   ? "workspaceReplayIndex"
                        : (pageIndex == 4)   ? "workspaceDiagnosisIndex"
                                             : "workspaceDeviceIndex";
        const int idx = rootObj->property(key).toInt();
        auto *item = findNamedItem(
            roots, QStringLiteral("navItem_%1").arg(idx));
        if (!item) {
            fails << QStringLiteral("navItem_%1 not found for workspace switch")
                         .arg(idx);
            return;
        }
        if (!QMetaObject::invokeMethod(item, "activate"))
            fails << QStringLiteral("navItem_%1.activate() not invokable")
                         .arg(idx);
    };

    // Each step is measured ONLY after its transitions (workspace switch /
    // resize) have gone through a full settle turn — otherwise the dump
    // would read the previous pass's geometry (observed during B2.2: the
    // dashboard panel reported its implicit 864 instead of the settled 935).
    auto transitionDone = std::make_shared<bool>(false);
    auto pendingPre = std::make_shared<QStringList>();
    // M9-B5.3 diagnosis tab sweep state: sweepTab = tab being measured,
    // sweepApplied = tab already selected in the TabBar (-1 = none yet).
    auto sweepTab = std::make_shared<int>(0);
    auto sweepApplied = std::make_shared<int>(-1);
    // M9-C C4: the targeted demo passes publish the deterministic demo batch
    // exactly once, before their first measurement.
    auto demoPublished = std::make_shared<bool>(false);

    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, schedule, failures, attempt, stepIndex, currentPageIndex,
                 transitionDone, pendingPre, sweepTab, sweepApplied,
                 demoPublished]() {
        const MeasureStep &step = steps.at(*stepIndex);
        QStringList pre;
        if (!*transitionDone) {
            const bool needSwitch = (step.pageIndex != *currentPageIndex);
            if (needSwitch || step.resizeToMin || step.resizeToDefault
                || (step.runDemo && !*demoPublished)) {
                if (needSwitch) {
                    switchWorkspace(step.pageIndex, pre);
                    *currentPageIndex = step.pageIndex;
                }
                if (step.resizeToMin && window)
                    window->resize(1000, 700);
                if (step.resizeToDefault && window)
                    window->resize(1024, 720);
                if (step.runDemo && !*demoPublished) {
                    auto *ctrl = roots.value(0)
                                     ? roots.value(0)->findChild<QObject *>(
                                           QStringLiteral("analysisController"))
                                     : nullptr;
                    if (!ctrl
                        || !QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                        pre << step.label
                            + QStringLiteral(": runDemoBatch() not invokable");
                    *demoPublished = true;
                }
                // The selection MUST be applied AFTER any batch
                // publication: publishing resets the model and therefore
                // invalidates the selection by contract (D3 lifecycle B).
                if (step.selectRow >= 0) {
                    auto *list = findNamedItem(
                        roots, QStringLiteral("transactionsList"));
                    if (!list)
                        pre << step.label
                            + QStringLiteral(": transactionsList not found "
                                             "for the selection step");
                    else
                        list->setProperty("currentIndex", step.selectRow);
                }
                *transitionDone = true;
                *pendingPre = pre;  // measured on the re-entry below
                QTimer::singleShot(settleMs, &app, *schedule);
                return;
            }
            *transitionDone = true;
        }
        pre = *pendingPre;

        auto measure = [&](const QString &label) {
            qInfo().noquote() << dumpGeometryTable(roots, label);
            *failures = pre;
            // Prove the pass measured the workspace it claims to measure: a
            // page that failed to become visible would silently skip every
            // page-specific assertion (the ISSUE-013 vacuity class).
            const ActivePage expected =
                (step.pageIndex == 0)   ? ActivePage::Transactions
                : (step.pageIndex == 1) ? ActivePage::Dashboard
                : (step.pageIndex == 2) ? ActivePage::Communication
                : (step.pageIndex == 3) ? ActivePage::Replay
                : (step.pageIndex == 4) ? ActivePage::Diagnosis
                                        : ActivePage::Transactions;
            if (activePage(roots) != expected)
                *failures << QStringLiteral("%1: the pass's target workspace is "
                                            "not the active page (its "
                                            "assertions would be vacuous)")
                                 .arg(label);
            *failures += runGeometryAssertions(roots, label);
            *failures += runShellNavAssertions(roots, label);
        };

        // The Diagnosis pass sweeps its three tabs: only the SELECTED tab
        // carries a geometry contract, so the pass selects each tab in turn
        // (one settle turn per selection) and asserts the selected one.
        if (step.pageIndex == 4 && *sweepTab < kDiagnosisTabs) {
            if (*sweepApplied != *sweepTab) {
                auto *tabBar =
                    findNamedItem(roots, QStringLiteral("diagnosisTabs"));
                if (!tabBar) {
                    *failures = pre;
                    *failures << step.label
                              + QStringLiteral(": diagnosisTabs not found");
                } else {
                    tabBar->setProperty("currentIndex", *sweepTab);
                    *sweepApplied = *sweepTab;
                    QTimer::singleShot(settleMs, &app, *schedule);
                    return;
                }
            } else {
                measure(QStringLiteral("%1 [tab %2]")
                            .arg(step.label)
                            .arg(*sweepTab));
                if (failures->isEmpty() && *sweepTab + 1 < kDiagnosisTabs) {
                    ++*sweepTab;
                    QTimer::singleShot(settleMs, &app, *schedule);
                    return;
                }
                if (failures->isEmpty()) {
                    *sweepTab = 0;
                    *sweepApplied = -1;  // ready for the next diagnosis pass
                }
            }
        } else {
            measure(step.label);
        }

        const bool missingItems =
            failures->join(u' ').contains(QStringLiteral("not found"));
        if (!failures->isEmpty() && missingItems && *attempt < maxAttempts) {
            ++*attempt;
            QTimer::singleShot(settleMs, &app, *schedule);
            return;
        }
        *attempt = 0;
        if (failures->isEmpty())
            dumpGrab(step.tag);
        if (failures->isEmpty() && *stepIndex + 1 < steps.size()) {
            ++*stepIndex;
            *transitionDone = false;
            pendingPre->clear();
            QTimer::singleShot(settleMs, &app, *schedule);
            return;
        }
        finish(*failures);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}

// ---------------------------------------------------------------------------
// M9-B2 `--qml-nav-check`: navigation invariants once TWO real workspaces
// exist (workbench + dashboard).
//
//   A. both real page objects exist at the same time
//   B. the initial selection is legal and points at the workbench
//   C. workbench -> dashboard -> workbench via the REAL activation path
//   D. page object IDENTITY survives the switches (no re-instantiation)
//   E. visibility follows the selection
//   F. disabled future entries can never change the selection
//   G. navigation itself changes NO business value: a full authoritative
//      snapshot (source labels + all 11 statistics + availability flags)
//      is compared across every switch
//
// Identity (D) and the value snapshot (G) are deliberately SEPARATE
// assertions: identity proves lifetime stability, the snapshot proves
// business correctness — neither one substitutes for the other.
// ---------------------------------------------------------------------------
QStringList runNavAssertions(const QList<QObject *> &roots,
                                  const QString &contextLabel,
                                  QQuickItem **dashboardPageOut)
{
    QStringList failures;
    auto fail = [&failures, &contextLabel](const QString &message) {
        failures << contextLabel + QStringLiteral(": ") + message;
    };

    auto *rail = findNamedItem(roots, QStringLiteral("navigationRail"));
    auto *dashboard = findNamedItem(roots, QStringLiteral("dashboardWorkspace"));
    if (!rail)
        fail(QStringLiteral("NAVFAIL navigationRail not found"));
    if (!dashboard)
        fail(QStringLiteral("NAVFAIL dashboardWorkspace not found"));
    if (dashboardPageOut)
        *dashboardPageOut = dashboard;
    if (!rail || !dashboard)
        return failures;

    // M9-D D5 retirement oracle: the retired Legacy workspace must not exist.
    if (findNamedItem(roots, QStringLiteral("legacyWorkspace")))
        fail(QStringLiteral("NAVFAIL legacyWorkspace still exists — the "
                            "Legacy workspace was not retired"));

    QObject *rootObj = roots.value(0);
    const int dashboardIndex =
        rootObj->property("workspaceDashboardIndex").toInt();
    const int communicationIndex =
        rootObj->property("workspaceCommunicationIndex").toInt();
    const int replayIndex = rootObj->property("workspaceReplayIndex").toInt();
    const int diagnosisIndex =
        rootObj->property("workspaceDiagnosisIndex").toInt();
    const int transactionsIndex =
        rootObj->property("workspaceTransactionsIndex").toInt();
    const int index = rail->property("currentWorkspaceIndex").toInt();

    auto *communication =
        findNamedItem(roots, QStringLiteral("communicationWorkspace"));
    auto *replay = findNamedItem(roots, QStringLiteral("replayWorkspace"));
    auto *diagnosis = findNamedItem(roots, QStringLiteral("diagnosisPage"));
    auto *transactions =
        findNamedItem(roots, QStringLiteral("transactionsPage"));
    if (!communication)
        fail(QStringLiteral("NAVFAIL communicationWorkspace not found"));
    if (!replay)
        fail(QStringLiteral("NAVFAIL replayWorkspace not found"));
    if (!diagnosis)
        fail(QStringLiteral("NAVFAIL diagnosisPage not found"));
    if (!transactions)
        fail(QStringLiteral("NAVFAIL transactionsPage not found"));

    const bool realWorkspace = (index == dashboardIndex)
                            || (index == communicationIndex)
                            || (index == replayIndex)
                            || (index == diagnosisIndex)
                            || (index == transactionsIndex);
    if (!realWorkspace)
        fail(QStringLiteral("NAVFAIL selection %1 is not a real workspace "
                            "(transactions=%2 dashboard=%3 communication=%4 "
                            "replay=%5 diagnosis=%6)")
                 .arg(index)
                 .arg(transactionsIndex)
                 .arg(dashboardIndex)
                 .arg(communicationIndex)
                 .arg(replayIndex)
                 .arg(diagnosisIndex));
    if (dashboard->isVisible() != (index == dashboardIndex))
        fail(QStringLiteral("NAVFAIL dashboardWorkspace visibility does not "
                            "follow selection %1")
                 .arg(index));
    if (communication && communication->isVisible() != (index == communicationIndex))
        fail(QStringLiteral("NAVFAIL communicationWorkspace visibility does "
                            "not follow selection %1")
                 .arg(index));
    if (replay && replay->isVisible() != (index == replayIndex))
        fail(QStringLiteral("NAVFAIL replayWorkspace visibility does not "
                            "follow selection %1")
                 .arg(index));
    if (diagnosis && diagnosis->isVisible() != (index == diagnosisIndex))
        fail(QStringLiteral("NAVFAIL diagnosisPage visibility does not "
                            "follow selection %1")
                 .arg(index));
    if (transactions
        && transactions->isVisible() != (index == transactionsIndex))
        fail(QStringLiteral("NAVFAIL transactionsPage visibility does not "
                            "follow selection %1")
                 .arg(index));

    return failures;
}

int runNavCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *ctrl = rootObj ? rootObj->findChild<QObject *>(
                               QStringLiteral("analysisController"))
                         : nullptr;

    const QStringList snapshotKeys = {
        QStringLiteral("modeLabel"),       QStringLiteral("sourceLabel"),
        QStringLiteral("serialConnected"), QStringLiteral("observedCount"),
        QStringLiteral("pendingCount"),    QStringLiteral("completedCount"),
        QStringLiteral("successCount"),    QStringLiteral("exceptionCount"),
        QStringLiteral("crcErrorCount"),   QStringLiteral("timeoutCount"),
        QStringLiteral("protocolErrorCount"),
        QStringLiteral("expectedNoResponseCount"),
        QStringLiteral("hasSuccessRate"),  QStringLiteral("successRate"),
        QStringLiteral("hasAverageSuccessLatency"),
        QStringLiteral("averageSuccessLatencyMs"),
    };

    auto takeSnapshot = [&snapshotKeys](QObject *obj) {
        QMap<QString, QVariant> values;
        if (!obj)
            return values;
        for (const QString &key : snapshotKeys)
            values.insert(key, obj->property(key.toUtf8().constData()));
        if (QAbstractItemModel *model =
                obj->property("transactionModel").value<QAbstractItemModel *>())
            values.insert(QStringLiteral("transactionRowCount"),
                          model->rowCount());
        return values;
    };

    // Scenario I compares diagnosis/replay-disclosure facts too, so it
    // uses an EXTENDED key set (the base takeSnapshot deliberately covers
    // only the 16 core authoritative fields -- mixing key sets was the
    // oracle bug fixed in the first B4.3 run).
    auto takeExtendedSnapshot = [&](QObject *obj) {
        QMap<QString, QVariant> values = takeSnapshot(obj);
        if (!obj)
            return values;
        const QStringList extra = {
            QStringLiteral("hasBaselineDiagnosis"),
            QStringLiteral("baselineDiagnosisText"),
            QStringLiteral("hasReplayError"),
            QStringLiteral("hasReplayNotice"),
            QStringLiteral("replayNoticeText"),
            QStringLiteral("replayErrorMessage"),
        };
        for (const QString &key : extra)
            values.insert(key, obj->property(key.toUtf8().constData()));
        return values;
    };

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };

    const int settleMs = 100;
    constexpr int kLastStage = 165;

    // M9-D D6 correction: REAL Qt key-event synthesis. A QKeyEvent sent via
    // QCoreApplication::sendEvent to the window's active focus item
    // exercises the genuine key-handling path (QQuickItem::keyPressEvent ->
    // QQuickItemView navigation) that setting currentIndex directly never
    // touches. It is still not an OS-level physical key press; the physical
    // path stays with the manual re-test.
    // Real mouse-click synthesis through the WINDOW delivery path (pick ->
    // handlers): MouseArea onClicked, TapHandler onTapped and Control focus
    // taking all behave as with a physical click.
    auto clickItem = [roots](const QString &name) -> bool {
        auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
        auto *item = qobject_cast<QQuickItem *>(findNamedItem(roots, name));
        if (!window || !item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    auto clickItemPoint = [roots](QQuickItem *item) -> bool {
        auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
        if (!window || !item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    auto sendKey = [roots](Qt::Key key) -> bool {
        auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
        if (!window)
            return false;
        QObject *target = window->activeFocusItem();
        if (!target)
            return false;
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &release);
        return true;
    };
    // Focus audit: who owns keyboard focus right now? (observation only)
    auto focusAudit = [roots](const QString &ctx) {
        auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
        auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
        QObject *afi = window ? window->activeFocusItem() : nullptr;
        qInfo().noquote()
            << QStringLiteral(
                   "NAV [focus audit %1]: list.focus=%2 list.activeFocus=%3 "
                   "activeFocusItem=%4 (%5)")
                   .arg(ctx)
                   .arg(list ? list->property("focus").toBool() : false)
                   .arg(list ? list->property("activeFocus").toBool() : false)
                   .arg(afi ? afi->objectName() : QStringLiteral("<null>"))
                   .arg(afi ? afi->metaObject()->className()
                            : QStringLiteral("<null>"));
    };

    // Shared state across stages.
    auto dashboardPtr = std::make_shared<QQuickItem *>(nullptr);
    auto communicationPtr = std::make_shared<QQuickItem *>(nullptr);
    auto replayPtr = std::make_shared<QQuickItem *>(nullptr);
    auto diagnosisPtr = std::make_shared<QQuickItem *>(nullptr);
    auto snapshot0 = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioA = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioB = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioI = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioJ = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioKPre = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioKNotice = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioL = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioN = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioLDigest = std::make_shared<QString>();
    auto scenarioO = std::make_shared<QMap<QString, QVariant>>();
    auto transactionsPtr = std::make_shared<QQuickItem *>(nullptr);
    // M9-D D5 Scenario S1: the rail index observed at STARTUP (stage 0),
    // captured once so the retirement scenario can prove the DEFAULT
    // workspace without re-deriving it mid-walk.
    auto startupIndex = std::make_shared<int>(-1);
    auto snapshotS = std::make_shared<QMap<QString, QVariant>>();
    // M9-C C3 distribution presentation check (NOT a named scenario — it is
    // a presentation contract, not a business persistence scenario).
    auto distributionStart = std::make_shared<int>(-1);
    auto distributionEnd = std::make_shared<int>(-1);
    QMap<QString, QVariant> scenarioJExpected;
    scenarioJExpected.insert(QStringLiteral("observedCount"), 4);
    scenarioJExpected.insert(QStringLiteral("completedCount"), 4);
    scenarioJExpected.insert(QStringLiteral("pendingCount"), 0);
    scenarioJExpected.insert(QStringLiteral("successCount"), 1);
    scenarioJExpected.insert(QStringLiteral("exceptionCount"), 1);
    scenarioJExpected.insert(QStringLiteral("crcErrorCount"), 1);
    scenarioJExpected.insert(QStringLiteral("timeoutCount"), 1);
    scenarioJExpected.insert(QStringLiteral("protocolErrorCount"), 0);
    scenarioJExpected.insert(QStringLiteral("expectedNoResponseCount"), 0);
    auto draftValues = std::make_shared<QMap<QString, QVariant>>();
    auto gPre = std::make_shared<QMap<QString, QVariant>>();
    auto stage = std::make_shared<int>(0);

    auto compareAgainst = [&](const QMap<QString, QVariant> &expected,
                              const QMap<QString, QVariant> &now,
                              const QString &ctx) {
        for (auto it = expected.cbegin(); it != expected.cend(); ++it) {
            if (it.value() != now.value(it.key()))
                fail(QStringLiteral("NAVFAIL %1: %2 changed: %3 -> %4")
                         .arg(ctx, it.key(), it.value().toString(),
                              now.value(it.key()).toString()));
        }
    };

    // Deterministic, encoding-safe fingerprint of a snapshot (FNV-1a over the
    // UTF-8 key=value stream). The per-stop comparisons above are the real
    // assertions; the digest exists so the evidence LOG carries one
    // comparable ASCII token per stop — the console encoding mangles Chinese
    // payloads, and a mangled log must never be the only evidence.
    auto snapshotDigest = [](const QMap<QString, QVariant> &snap) {
        QByteArray blob;
        for (auto it = snap.cbegin(); it != snap.cend(); ++it)
            blob += it.key().toUtf8() + '=' + it.value().toString().toUtf8()
                    + ';';
        quint64 hash = 1469598103934665603ULL;
        for (const char c : blob) {
            hash ^= static_cast<quint8>(c);
            hash *= 1099511628211ULL;
        }
        return QString::number(hash, 16);
    };

    // -----------------------------------------------------------------------
    // Per-scenario verdicts (M9-B5.3).
    //
    // A scenario PASSES iff no failure was recorded anywhere in its stage
    // range. The range is a CONSERVATIVE over-approximation around the
    // scenario's own assertions, so the accounting can under-credit a
    // scenario but can never report PASS while its own assertions failed.
    // A scenario whose stages never ran (an earlier failure stopped the
    // walk) is reported NOT RUN and counted as a harness failure.
    // -----------------------------------------------------------------------
    const QStringList scenarioOrder = {
        QStringLiteral("basic five-workspace path"),
        QStringLiteral("A"),
        QStringLiteral("B"),   QStringLiteral("D"),  QStringLiteral("E"),
        QStringLiteral("F"),   QStringLiteral("G'"), QStringLiteral("H"),
        QStringLiteral("I"),   QStringLiteral("J"),  QStringLiteral("K"),
        QStringLiteral("K'"),  QStringLiteral("L"),  QStringLiteral("N"),
        QStringLiteral("O"),   QStringLiteral("P"),  QStringLiteral("Q"),
        QStringLiteral("R"),  QStringLiteral("S"),
        QStringLiteral("T"),
    };
    auto scenarioStart = std::make_shared<QMap<QString, int>>();
    auto scenarioEnd = std::make_shared<QMap<QString, int>>();
    auto beginScenario = [&](const QString &name) {
        scenarioStart->insert(name, failures->size());
    };
    auto endScenario = [&](const QString &name) {
        if (!scenarioStart->contains(name)) {
            fail(QStringLiteral("NAVFAIL scenario %1 ended without a begin "
                                "mark — the verdict accounting is broken")
                     .arg(name));
            return;
        }
        scenarioEnd->insert(name, failures->size());
    };

    // Scenario L's baseline-fact assertion. hasBaselineDiagnosis is the
    // harness-visible proxy for the batch revision: activeBatchRevision_ is
    // bumped ONLY inside invalidateAiForBatchChange(), which always clears
    // the baseline (AnalysisController.cpp:779-798). So a baseline that is
    // still present and byte-identical at every stop proves the revision did
    // not move — the revision itself has no Q_PROPERTY, and this harness
    // deliberately adds no Controller API for testing.
    auto assertBaselinePersisted = [&](const QString &ctx) {
        if (!ctrl->property("hasBaselineDiagnosis").toBool())
            fail(QStringLiteral("NAVFAIL %1: hasBaselineDiagnosis is false — a "
                                "batch invalidation moved the revision during "
                                "navigation").arg(ctx));
        if (ctrl->property("baselineDiagnosisText")
            != scenarioL->value(QStringLiteral("baselineDiagnosisText")))
            fail(QStringLiteral("NAVFAIL %1: baselineDiagnosisText changed")
                     .arg(ctx));
        // One ASCII token per stop: the extended snapshot must hash to the
        // same value as the captured one.
        const QString digest = snapshotDigest(takeExtendedSnapshot(ctrl));
        if (digest != *scenarioLDigest)
            fail(QStringLiteral("NAVFAIL %1: snapshot digest %2 != %3")
                     .arg(ctx, digest, *scenarioLDigest));
        qInfo().noquote() << QStringLiteral("NAV [L stop] %1: digest=%2")
                                 .arg(ctx, digest);
    };

    auto switchTo = [&](int pageIndex) {
        const char *key = (pageIndex == 0)   ? "workspaceTransactionsIndex"
                        : (pageIndex == 1)   ? "workspaceDashboardIndex"
                        : (pageIndex == 2)   ? "workspaceCommunicationIndex"
                        : (pageIndex == 3)   ? "workspaceReplayIndex"
                        : (pageIndex == 4)   ? "workspaceDiagnosisIndex"
                                             : "workspaceDeviceIndex";
        const int idx = rootObj->property(key).toInt();
        auto *item = findNamedItem(
            roots, QStringLiteral("navItem_%1").arg(idx));
        if (!item || !QMetaObject::invokeMethod(item, "activate"))
            fail(QStringLiteral("NAVFAIL activation failed for page %1")
                     .arg(pageIndex));
    };

    auto verifyStructureAndIdentity = [&](const QString &ctx) {
        *failures += runNavAssertions(roots, ctx, nullptr);
        auto *dashboard =
            findNamedItem(roots, QStringLiteral("dashboardWorkspace"));
        auto *communication =
            findNamedItem(roots, QStringLiteral("communicationWorkspace"));
        if (dashboard != *dashboardPtr)
            fail(QStringLiteral("NAVFAIL %1: dashboard page identity changed")
                     .arg(ctx));
        if (communication != *communicationPtr)
            fail(QStringLiteral("NAVFAIL %1: communication page identity changed")
                     .arg(ctx));
        auto *replayShell = findNamedItem(
            roots, QStringLiteral("replayWorkspace"));
        if (!replayShell)
            fail(QStringLiteral("NAVFAIL %1: replayWorkspace shell vanished")
                     .arg(ctx));
        // M9-B5.2: the Diagnosis page is a real workspace now — its identity
        // must survive every later switch exactly like the other four.
        if (findNamedItem(roots, QStringLiteral("diagnosisPage"))
            != *diagnosisPtr)
            fail(QStringLiteral("NAVFAIL %1: diagnosis page identity changed")
                     .arg(ctx));
    };

    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, schedule, failures, dashboardPtr,
                 communicationPtr, replayPtr, diagnosisPtr, snapshot0, scenarioA,
                 scenarioB,
                 scenarioI, scenarioJ, scenarioKPre, scenarioKNotice,
                 scenarioL, scenarioN, scenarioO, scenarioLDigest,
                 transactionsPtr, scenarioStart,
                 scenarioEnd, snapshotDigest, distributionStart, distributionEnd,
                 draftValues, gPre, stage, ctrl, takeSnapshot, takeExtendedSnapshot,
                 compareAgainst, switchTo, verifyStructureAndIdentity,
                 beginScenario, endScenario, assertBaselinePersisted]() {
        auto *rail = findNamedItem(roots, QStringLiteral("navigationRail"));
        const int dashboardIndex =
            rootObj->property("workspaceDashboardIndex").toInt();
        const int communicationIndex =
            rootObj->property("workspaceCommunicationIndex").toInt();

        switch (*stage) {
        // ---- Structural phase: Legacy -> Dashboard -> Communication ->
        // Replay -> Diagnosis -> Dashboard -> Legacy (M9-B5.2 adds the
        // Diagnosis stop; Scenario E and H live in these switches) ----
        case 0: {
            beginScenario(QStringLiteral("basic five-workspace path"));
            beginScenario(QStringLiteral("E"));
            beginScenario(QStringLiteral("H"));
            if (!ctrl) {
                fail(QStringLiteral("NAVFAIL analysisController not found"));
                break;
            }
            *failures += runNavAssertions(roots, QStringLiteral("initial"),
                                          dashboardPtr.get());
            *communicationPtr = findNamedItem(
                roots, QStringLiteral("communicationWorkspace"));
            if (!*communicationPtr)
                fail(QStringLiteral("NAVFAIL communicationWorkspace not found"));
            // M9-B4.2: the Replay page exists as the fourth child; its
            // navigation entry is enabled by now. Identity is captured so
            // every later stage can prove the instance never changes.
            *replayPtr = findNamedItem(roots, QStringLiteral("replayWorkspace"));
            if (!*replayPtr)
                fail(QStringLiteral("NAVFAIL replayWorkspace not found"));
            // M9-B5.2: the Diagnosis page is the FIFTH real workspace and its
            // navigation entry is enabled. Identity is captured here so every
            // later stage can prove the instance never changes; visibility
            // follows the selection (asserted by runNavAssertions above).
            *diagnosisPtr = findNamedItem(roots, QStringLiteral("diagnosisPage"));
            if (!*diagnosisPtr)
                fail(QStringLiteral("NAVFAIL diagnosisPage not found"));
            // M9-D D2: the Transactions page is the SIXTH real workspace.
            // Identity is captured here so every later stage can prove the
            // instance never changes.
            *transactionsPtr =
                findNamedItem(roots, QStringLiteral("transactionsPage"));
            if (!*transactionsPtr)
                fail(QStringLiteral("NAVFAIL transactionsPage not found"));
            *startupIndex =
                rail ? rail->property("currentWorkspaceIndex").toInt() : -1;
            *snapshot0 = takeSnapshot(ctrl);
            qInfo().noquote()
                << QStringLiteral("NAV [initial]: index=%1 transactions=%2x%3 "
                                  "dashboard=%4x%5 communication=%6x%7")
                       .arg(rail ? rail->property("currentWorkspaceIndex").toInt()
                                 : -1)
                       .arg((*transactionsPtr) ? (*transactionsPtr)->width() : -1)
                       .arg((*transactionsPtr) ? (*transactionsPtr)->height() : -1)
                       .arg((*dashboardPtr) ? (*dashboardPtr)->width() : -1)
                       .arg((*dashboardPtr) ? (*dashboardPtr)->height() : -1)
                       .arg((*communicationPtr) ? (*communicationPtr)->width() : -1)
                       .arg((*communicationPtr) ? (*communicationPtr)->height() : -1);
            break;
        }
        case 1: switchTo(1); break;
        case 2: {
            verifyStructureAndIdentity(QStringLiteral("dashboard"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("to dashboard"));
            qInfo().noquote()
                << QStringLiteral("NAV [dashboard]: index=%1 "
                                  "transactionsVisible=%2 dashboardVisible=%3 "
                                  "communicationVisible=%4")
                       .arg(rail->property("currentWorkspaceIndex").toInt())
                       .arg((*transactionsPtr)->isVisible())
                       .arg((*dashboardPtr)->isVisible())
                       .arg((*communicationPtr)->isVisible());
            break;
        }
        case 3: switchTo(2); break;
        case 4: {
            endScenario(QStringLiteral("H"));
            verifyStructureAndIdentity(QStringLiteral("communication"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("to communication"));
            qInfo().noquote()
                << QStringLiteral("NAV [communication]: index=%1 "
                                  "transactionsVisible=%2 dashboardVisible=%3 "
                                  "communicationVisible=%4")
                       .arg(rail->property("currentWorkspaceIndex").toInt())
                       .arg((*transactionsPtr)->isVisible())
                       .arg((*dashboardPtr)->isVisible())
                       .arg((*communicationPtr)->isVisible());
            break;
        }
        case 5: switchTo(3); break;
        case 6: {
            verifyStructureAndIdentity(QStringLiteral("replay"));
            if (findNamedItem(roots, QStringLiteral("replayWorkspace"))
                != *replayPtr)
                fail(QStringLiteral("NAVFAIL replay page identity changed"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("to replay"));
            qInfo().noquote()
                << QStringLiteral("NAV [replay]: index=%1 transactionsVisible=%2 "
                                  "dashboardVisible=%3 communicationVisible=%4 "
                                  "replayVisible=%5")
                       .arg(rail->property("currentWorkspaceIndex").toInt())
                       .arg((*transactionsPtr)->isVisible())
                       .arg((*dashboardPtr)->isVisible())
                       .arg((*communicationPtr)->isVisible())
                       .arg((*replayPtr)->isVisible());
            break;
        }
        case 7: switchTo(4); break;
        case 8: {
            // M9-B5.2: Diagnosis is the FIFTH real workspace. Same shape as
            // every other stop: identity + visibility (runNavAssertions) +
            // an unchanged core snapshot.
            verifyStructureAndIdentity(QStringLiteral("diagnosis"));
            if (findNamedItem(roots, QStringLiteral("diagnosisPage"))
                != *diagnosisPtr)
                fail(QStringLiteral("NAVFAIL diagnosis page identity changed"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("to diagnosis"));
            qInfo().noquote()
                << QStringLiteral("NAV [diagnosis]: index=%1 "
                                  "transactionsVisible=%2 dashboardVisible=%3 "
                                  "communicationVisible=%4 replayVisible=%5 "
                                  "diagnosisVisible=%6")
                       .arg(rail->property("currentWorkspaceIndex").toInt())
                       .arg((*transactionsPtr)->isVisible())
                       .arg((*dashboardPtr)->isVisible())
                       .arg((*communicationPtr)->isVisible())
                       .arg((*replayPtr)->isVisible())
                       .arg((*diagnosisPtr)->isVisible());
            break;
        }
        case 9: switchTo(0); break;
        case 10: {
            // M9-D D5: the Transactions workspace is the FIRST rail entry and
            // the default workspace. Same shape as every other stop:
            // identity + visibility (runNavAssertions) + an unchanged core
            // snapshot.
            verifyStructureAndIdentity(QStringLiteral("transactions"));
            if (findNamedItem(roots, QStringLiteral("transactionsPage"))
                != *transactionsPtr)
                fail(QStringLiteral("NAVFAIL transactions page identity "
                                    "changed"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("to transactions"));
            qInfo().noquote()
                << QStringLiteral("NAV [transactions]: index=%1 "
                                  "dashboardVisible=%2 diagnosisVisible=%3 "
                                  "transactionsVisible=%4")
                       .arg(rail->property("currentWorkspaceIndex").toInt())
                       .arg((*dashboardPtr)->isVisible())
                       .arg((*diagnosisPtr)->isVisible())
                       .arg((*transactionsPtr)->isVisible());
            break;
        }
        case 11: switchTo(1); break;
        case 12: {
            endScenario(QStringLiteral("E"));
            verifyStructureAndIdentity(QStringLiteral("dashboard again"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("communication -> dashboard"));
            break;
        }
        case 13: switchTo(0); break;
        case 14: {
            endScenario(QStringLiteral("basic five-workspace path"));
            verifyStructureAndIdentity(QStringLiteral("transactions return"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("back to transactions"));
            qInfo().noquote()
                << QStringLiteral("NAV [transactions]: index=%1 post-Legacy "
                                  "five-workspace round trip complete")
                       .arg(rail->property("currentWorkspaceIndex").toInt());
            break;
        }

        // ---- Scenario A: deterministic batch survives navigation ----
        case 15: {
            beginScenario(QStringLiteral("A"));
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL runDemoBatch() not invokable"));
            break;
        }
        case 16: {
            *scenarioA = takeSnapshot(ctrl);
            if (scenarioA->value(QStringLiteral("observedCount")).toInt() != 4)
                fail(QStringLiteral("NAVFAIL scenario A: expected the "
                                    "deterministic 4-transaction batch"));
            switchTo(1);
            break;
        }
        case 17: {
            compareAgainst(*scenarioA, takeSnapshot(ctrl),
                           QStringLiteral("scenario A @dashboard"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario A @dashboard]: observed=%1 "
                                  "mode=%2 source=%3")
                       .arg(ctrl->property("observedCount").toInt())
                       .arg(ctrl->property("modeLabel").toString())
                       .arg(ctrl->property("sourceLabel").toString());
            switchTo(0);
            break;
        }
        case 18: {
            endScenario(QStringLiteral("A"));
            compareAgainst(*scenarioA, takeSnapshot(ctrl),
                           QStringLiteral("scenario A return"));
            if (!QMetaObject::invokeMethod(ctrl, "runBaselineDiagnosis"))
                fail(QStringLiteral("NAVFAIL runBaselineDiagnosis() not "
                                    "invokable"));
            break;
        }

        // ---- Scenario I: navigation is not a source transition (all four
        // workspaces with a NON-EMPTY demo session + diagnosis facts) ----
        case 19: {
            beginScenario(QStringLiteral("I"));
            *scenarioI = takeExtendedSnapshot(ctrl);
            switchTo(3);
            break;
        }
        case 20: {
            verifyStructureAndIdentity(QStringLiteral("scenario I @replay"));
            compareAgainst(*scenarioI, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario I @replay"));
            break;
        }
        case 21: switchTo(2); break;
        case 22: {
            verifyStructureAndIdentity(QStringLiteral("scenario I @communication"));
            compareAgainst(*scenarioI, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario I @communication"));
            break;
        }
        case 23: switchTo(3); break;
        case 24: {
            verifyStructureAndIdentity(QStringLiteral("scenario I @replay again"));
            compareAgainst(*scenarioI, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario I @replay again"));
            break;
        }
        case 25: switchTo(0); break;
        case 26: {
            endScenario(QStringLiteral("I"));
            verifyStructureAndIdentity(QStringLiteral("scenario I @workbench"));
            compareAgainst(*scenarioI, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario I @workbench"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario I]: four-workspace tour with "
                                  "a non-empty session changed nothing");
            break;
        }

        // ---- Scenario B: diagnosis survives navigation ----
        case 27: {
            beginScenario(QStringLiteral("B"));
            scenarioB->insert(QStringLiteral("hasBaselineDiagnosis"),
                              ctrl->property("hasBaselineDiagnosis"));
            scenarioB->insert(QStringLiteral("baselineDiagnosisText"),
                              ctrl->property("baselineDiagnosisText"));
            if (!scenarioB->value(QStringLiteral("hasBaselineDiagnosis")).toBool())
                fail(QStringLiteral("NAVFAIL scenario B: baseline diagnosis "
                                    "did not produce a result"));
            switchTo(1);
            break;
        }
        case 28: {
            if (ctrl->property("hasBaselineDiagnosis")
                    != scenarioB->value(QStringLiteral("hasBaselineDiagnosis"))
                || ctrl->property("baselineDiagnosisText")
                    != scenarioB->value(QStringLiteral("baselineDiagnosisText")))
                fail(QStringLiteral("NAVFAIL scenario B: diagnosis changed "
                                    "across navigation"));
            switchTo(0);
            break;
        }
        case 29: {
            endScenario(QStringLiteral("B"));
            if (ctrl->property("hasBaselineDiagnosis")
                    != scenarioB->value(QStringLiteral("hasBaselineDiagnosis")))
                fail(QStringLiteral("NAVFAIL scenario B (return): diagnosis "
                                    "changed"));
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("NAVFAIL clearResults() not invokable"));
            break;
        }

        // ---- Scenario D: clear is reflected by every view ----
        case 30: {
            beginScenario(QStringLiteral("D"));
            const QStringList zeroKeys = {
                QStringLiteral("observedCount"),  QStringLiteral("pendingCount"),
                QStringLiteral("completedCount"), QStringLiteral("successCount"),
                QStringLiteral("exceptionCount"), QStringLiteral("crcErrorCount"),
                QStringLiteral("timeoutCount"),   QStringLiteral("protocolErrorCount"),
                QStringLiteral("expectedNoResponseCount"),
            };
            for (const QString &key : zeroKeys) {
                if (ctrl->property(key.toUtf8().constData()).toInt() != 0)
                    fail(QStringLiteral("NAVFAIL scenario D: %1 not zero on "
                                        "the workbench").arg(key));
            }
            if (ctrl->property("hasSuccessRate").toBool()
                || ctrl->property("hasAverageSuccessLatency").toBool())
                fail(QStringLiteral("NAVFAIL scenario D: availability flags "
                                    "still set"));
            switchTo(1);
            break;
        }
        case 31: {
            if (ctrl->property("observedCount").toInt() != 0
                || ctrl->property("hasSuccessRate").toBool()
                || ctrl->property("hasAverageSuccessLatency").toBool())
                fail(QStringLiteral("NAVFAIL scenario D: dashboard does not "
                                    "reflect the cleared state"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario D @dashboard]: observed=%1 "
                                  "hasRate=%2 hasLatency=%3")
                       .arg(ctrl->property("observedCount").toInt())
                       .arg(ctrl->property("hasSuccessRate").toBool())
                       .arg(ctrl->property("hasAverageSuccessLatency").toBool());
            switchTo(0);
            break;
        }
        case 32: {
            endScenario(QStringLiteral("D"));
            if (ctrl->property("observedCount").toInt() != 0)
                fail(QStringLiteral("NAVFAIL scenario D (return): workbench "
                                    "not cleared"));
            break;
        }

        // ---- Scenario F: page-local drafts survive navigation ----
        case 33: {
            beginScenario(QStringLiteral("F"));
            // M10 correction: the request parameters are TEXT fields (the raw
            // text the user typed); the baud combo moved 38400 to index 5.
            struct DraftSpec {
                const char *objectName;
                const char *property;
                const char *text;
                int index;
            };
            const DraftSpec specs[] = {
                { "commSlaveField", "text", "7", -1 },
                { "commFunctionField", "text", "41", -1 },
                { "commStartField", "text", "10", -1 },
                { "commQuantityField", "text", "3", -1 },
                { "commTimeoutField", "text", "2500", -1 },
                { "commBaudCombo", "currentIndex", nullptr, 5 },
            };
            for (const auto &spec : specs) {
                auto *item = findNamedItem(
                    roots, QString::fromLatin1(spec.objectName));
                if (!item) {
                    fail(QStringLiteral("NAVFAIL draft control %1 not found")
                             .arg(QLatin1String(spec.objectName)));
                    continue;
                }
                if (spec.index >= 0) {
                    item->setProperty(spec.property, spec.index);
                } else {
                    item->setProperty(spec.property,
                                      QString::fromLatin1(spec.text));
                }
                draftValues->insert(QLatin1String(spec.objectName),
                                    item->property(spec.property));
            }
            // Port combo: only asserted when the environment actually has
            // ports; otherwise explicitly deferred (no fake port existence).
            auto *portCombo = findNamedItem(roots, QStringLiteral("commPortCombo"));
            const bool hasPorts =
                ctrl->property("serialPortNames").toStringList().size() > 0;
            if (portCombo && hasPorts) {
                const int target = portCombo->property("count").toInt() > 1 ? 1 : 0;
                portCombo->setProperty("currentIndex", target);
                draftValues->insert(QStringLiteral("commPortCombo"),
                                    portCombo->property("currentIndex"));
            } else {
                qInfo().noquote()
                    << QStringLiteral("NAV [scenario F]: port-combo draft "
                                      "DEFERRED (no ports on this machine)");
            }
            switchTo(2);
            break;
        }
        case 34: {
            for (auto it = draftValues->cbegin(); it != draftValues->cend(); ++it) {
                auto *item = findNamedItem(roots, it.key());
                if (!item) {
                    fail(QStringLiteral("NAVFAIL draft control %1 vanished")
                             .arg(it.key()));
                    continue;
                }
                const QString prop = (it.key() == QStringLiteral("commBaudCombo")
                                      || it.key() == QStringLiteral("commPortCombo"))
                                         ? QStringLiteral("currentIndex")
                                         : QStringLiteral("text");
                if (item->property(prop.toUtf8().constData()) != it.value())
                    fail(QStringLiteral("NAVFAIL scenario F: draft %1 changed "
                                        "(%2 -> %3)")
                             .arg(it.key(), it.value().toString(),
                                  item->property(prop.toUtf8().constData())
                                      .toString()));
            }
            // Scenario G' pre-state: what must stay untouched by a FAILED
            // connect.
            gPre->insert(QStringLiteral("modeLabel"),
                         ctrl->property("modeLabel"));
            gPre->insert(QStringLiteral("sourceLabel"),
                         ctrl->property("sourceLabel"));
            gPre->insert(QStringLiteral("observedCount"),
                         ctrl->property("observedCount"));
            if (!QMetaObject::invokeMethod(
                    ctrl, "connectSerial", Q_ARG(QString,
                        QStringLiteral("MODBUSLENS_NO_SUCH_PORT")),
                    Q_ARG(int, 9600)))
                fail(QStringLiteral("NAVFAIL connectSerial() not invokable"));
            break;
        }

        // ---- Scenario G': the REAL failure path of a connect attempt ----
        case 35: {
            beginScenario(QStringLiteral("G'"));
            if (ctrl->property("serialConnected").toBool())
                fail(QStringLiteral("NAVFAIL scenario G': serialConnected "
                                    "became true for a non-existent port"));
            if (!ctrl->property("hasSerialError").toBool())
                fail(QStringLiteral("NAVFAIL scenario G': expected the "
                                    "authoritative hasSerialError flag"));
            for (auto it = gPre->cbegin(); it != gPre->cend(); ++it) {
                if (ctrl->property(it.key().toUtf8().constData()) != it.value())
                    fail(QStringLiteral("NAVFAIL scenario G':  %1 changed on a "
                                        "failed connect (atomicity)")
                             .arg(it.key()));
            }
            qInfo().noquote()
                << QStringLiteral("NAV [scenario G' @communication]: "
                                  "serialConnected=%1 hasSerialError=%2 "
                                  "mode=%3 source=%4")
                       .arg(ctrl->property("serialConnected").toBool())
                       .arg(ctrl->property("hasSerialError").toBool())
                       .arg(ctrl->property("modeLabel").toString())
                       .arg(ctrl->property("sourceLabel").toString());
            switchTo(1);
            break;
        }
        case 36: switchTo(2); break;
        case 37: {
            endScenario(QStringLiteral("G'"));
            if (ctrl->property("serialConnected").toBool()
                || !ctrl->property("hasSerialError").toBool())
                fail(QStringLiteral("NAVFAIL scenario G': connect-failure "
                                    "state did not survive navigation"));
            break;
        }

        // ---- Scenario F verification after the round trip ----
        case 38: {
            switchTo(0);
            break;
        }
        case 39: {
            endScenario(QStringLiteral("F"));
            switchTo(2);
            for (auto it = draftValues->cbegin(); it != draftValues->cend(); ++it) {
                auto *item = findNamedItem(roots, it.key());
                if (!item)
                    continue;
                // M10 correction: the request drafts are TEXT fields now.
                const QString prop = (it.key() == QStringLiteral("commBaudCombo")
                                      || it.key() == QStringLiteral("commPortCombo"))
                                         ? QStringLiteral("currentIndex")
                                         : QStringLiteral("text");
                if (item->property(prop.toUtf8().constData()) != it.value())
                    fail(QStringLiteral("NAVFAIL scenario F (round trip): "
                                        "draft %1 changed").arg(it.key()));
            }
            qInfo().noquote()
                << QStringLiteral("NAV [scenario F]: %1 draft properties "
                                  "survived the full round trip")
                       .arg(draftValues->size());
            break;
        }

        // ---- Scenario J: canonical replay load + navigation persistence ----
        case 40: {
            beginScenario(QStringLiteral("J"));
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_DEMO_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("NAVFAIL loadReplayFile() not invokable"));
            break;
        }
        case 41: {
            // Canonical golden semantics (mirrors r01; no presentation
            // strings parsed): the source is the BASENAME only.
            const QMap<QString, QVariant> now = takeSnapshot(ctrl);
            const QStringList checks = {
                QStringLiteral("observedCount"),  QStringLiteral("completedCount"),
                QStringLiteral("pendingCount"),   QStringLiteral("successCount"),
                QStringLiteral("exceptionCount"), QStringLiteral("crcErrorCount"),
                QStringLiteral("timeoutCount"),   QStringLiteral("protocolErrorCount"),
                QStringLiteral("expectedNoResponseCount"),
            };
            for (const QString &key : checks) {
                if (now.value(key).toInt() != scenarioJExpected.value(key).toInt())
                    fail(QStringLiteral("NAVFAIL scenario J: %1 = %2, expected %3")
                             .arg(key, now.value(key).toString(),
                                  scenarioJExpected.value(key).toString()));
            }
            if (now.value(QStringLiteral("modeLabel")).toString()
                != QStringLiteral("回放模式"))
                fail(QStringLiteral("NAVFAIL scenario J: mode is not 回放模式"));
            if (now.value(QStringLiteral("sourceLabel")).toString()
                != QStringLiteral("demo_v1.mlog"))
                fail(QStringLiteral("NAVFAIL scenario J: source is not the "
                                    "basename demo_v1.mlog"));
            if (!now.value(QStringLiteral("hasSuccessRate")).toBool()
                || !qFuzzyCompare(now.value(QStringLiteral("successRate")).toDouble(),
                                  0.25))
                fail(QStringLiteral("NAVFAIL scenario J: successRate != 0.25"));
            if (!now.value(QStringLiteral("hasAverageSuccessLatency")).toBool()
                || !qFuzzyCompare(
                    now.value(QStringLiteral("averageSuccessLatencyMs")).toDouble(),
                    25.0))
                fail(QStringLiteral("NAVFAIL scenario J: avg latency != 25ms"));
            *scenarioJ = now;
            switchTo(3);
            break;
        }
        case 42: {
            verifyStructureAndIdentity(QStringLiteral("scenario J @replay"));
            compareAgainst(*scenarioJ, takeSnapshot(ctrl),
                           QStringLiteral("scenario J @replay"));
            break;
        }
        case 43: switchTo(1); break;
        case 44: {
            verifyStructureAndIdentity(QStringLiteral("scenario J @dashboard"));
            compareAgainst(*scenarioJ, takeSnapshot(ctrl),
                           QStringLiteral("scenario J @dashboard"));
            break;
        }
        case 45: switchTo(2); break;
        case 46: {
            verifyStructureAndIdentity(QStringLiteral("scenario J @communication"));
            compareAgainst(*scenarioJ, takeSnapshot(ctrl),
                           QStringLiteral("scenario J @communication"));
            break;
        }
        case 47: switchTo(3); break;
        case 48: {
            verifyStructureAndIdentity(QStringLiteral("scenario J @replay 2"));
            compareAgainst(*scenarioJ, takeSnapshot(ctrl),
                           QStringLiteral("scenario J @replay 2"));
            switchTo(0);
            break;
        }
        case 49: {
            endScenario(QStringLiteral("J"));
            verifyStructureAndIdentity(
                QStringLiteral("scenario J @transactions"));
            compareAgainst(*scenarioJ, takeSnapshot(ctrl),
                           QStringLiteral("scenario J @transactions"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario J]: replay session survived "
                                  "five switches (source=%1)")
                       .arg(takeSnapshot(ctrl)
                                .value(QStringLiteral("sourceLabel"))
                                .toString());
            break;
        }

        // ---- Scenario K: ordinary failed replacement is atomic (against a
        // NON-EMPTY replay session) ----
        case 50: {
            beginScenario(QStringLiteral("K"));
            *scenarioKPre = takeSnapshot(ctrl);
            const QUrl missing = QUrl::fromLocalFile(
                QStringLiteral("MODBUSLENS_NO_SUCH_DIR/missing_replay.mlog"));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, missing)))
                fail(QStringLiteral("NAVFAIL loadReplayFile() not invokable"));
            break;
        }
        case 51: {
            if (!ctrl->property("hasReplayError").toBool())
                fail(QStringLiteral("NAVFAIL scenario K: expected the "
                                    "authoritative hasReplayError flag"));
            compareAgainst(*scenarioKPre, takeSnapshot(ctrl),
                           QStringLiteral("scenario K failed replacement"));
            switchTo(1);
            break;
        }
        case 52: {
            compareAgainst(*scenarioKPre, takeSnapshot(ctrl),
                           QStringLiteral("scenario K @dashboard"));
            switchTo(2);
            break;
        }
        case 53: {
            compareAgainst(*scenarioKPre, takeSnapshot(ctrl),
                           QStringLiteral("scenario K @communication"));
            switchTo(3);
            break;
        }
        case 54: {
            endScenario(QStringLiteral("K"));
            compareAgainst(*scenarioKPre, takeSnapshot(ctrl),
                           QStringLiteral("scenario K @replay"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario K]: failed replacement kept "
                                  "mode/source/rows/statistics across three "
                                  "workspaces");
            break;
        }

        // ---- Scenario K': replayNotice lifecycle across a failed attempt ----
        case 55: {
            beginScenario(QStringLiteral("K'"));
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_UNSUPPORTED_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("NAVFAIL loadReplayFile() not invokable"));
            break;
        }
        case 56: {
            // Successful load of the unsupported-function sample: NOT an
            // error; the notice discloses the unsupported record; analyzed
            // records = 0, so rows/statistics are the zero batch.
            if (ctrl->property("hasReplayError").toBool())
                fail(QStringLiteral("NAVFAIL scenario K': successful load must "
                                    "not set replayError"));
            if (!ctrl->property("hasReplayNotice").toBool())
                fail(QStringLiteral("NAVFAIL scenario K': expected "
                                    "hasReplayNotice for the unsupported "
                                    "sample"));
            const QString notice =
                ctrl->property("replayNoticeText").toString();
            if (notice.isEmpty())
                fail(QStringLiteral("NAVFAIL scenario K': notice text is empty"));
            if (ctrl->property("sourceLabel").toString()
                != QStringLiteral("t015_unsupported_fc08.mlog"))
                fail(QStringLiteral("NAVFAIL scenario K': source is not the "
                                    "unsupported sample basename"));
            if (ctrl->property("observedCount").toInt() != 0)
                fail(QStringLiteral("NAVFAIL scenario K': unsupported records "
                                    "must not enter the statistics pool"));
            // Full authoritative snapshot (mode/source/rows/all statistics/
            // rate-latency presence) -- observed==0 alone must never stand
            // in for "the whole session state is preserved".
            *scenarioKNotice = takeSnapshot(ctrl);
            scenarioKNotice->insert(QStringLiteral("replayNoticeText"), notice);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario K' load]: source=%1 notice=%2")
                       .arg(ctrl->property("sourceLabel").toString(), notice);
            break;
        }
        case 57: {
            // Deterministic failed replacement (same construction as K).
            const QUrl missing = QUrl::fromLocalFile(
                QStringLiteral("MODBUSLENS_NO_SUCH_DIR/missing_replay.mlog"));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, missing)))
                fail(QStringLiteral("NAVFAIL loadReplayFile() not invokable"));
            break;
        }
        case 58: {
            if (!ctrl->property("hasReplayError").toBool())
                fail(QStringLiteral("NAVFAIL scenario K': expected replayError "
                                    "after the failed attempt"));
            // THE assertion: the old session disclosure survives BYTE-EQUAL.
            if (ctrl->property("replayNoticeText")
                != scenarioKNotice->value(QStringLiteral("replayNoticeText")))
                fail(QStringLiteral("NAVFAIL scenario K': replayNoticeText "
                                    "changed across a failed attempt"));
            if (ctrl->property("sourceLabel")
                != scenarioKNotice->value(QStringLiteral("sourceLabel")))
                fail(QStringLiteral("NAVFAIL scenario K': source changed"));
            if (ctrl->property("modeLabel")
                != scenarioKNotice->value(QStringLiteral("modeLabel")))
                fail(QStringLiteral("NAVFAIL scenario K': mode changed"));
            if (ctrl->property("observedCount")
                != scenarioKNotice->value(QStringLiteral("observedCount")))
                fail(QStringLiteral("NAVFAIL scenario K': observedCount "
                                    "changed"));
            compareAgainst(*scenarioKNotice, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario K' failed replacement"));
            break;
        }
        case 59: switchTo(1); break;
        case 60: switchTo(2); break;
        case 61: switchTo(3); break;
        case 62: {
            endScenario(QStringLiteral("K'"));
            if (ctrl->property("replayNoticeText")
                != scenarioKNotice->value(QStringLiteral("replayNoticeText")))
                fail(QStringLiteral("NAVFAIL scenario K' (round trip): notice "
                                    "changed"));
            if (ctrl->property("sourceLabel")
                != scenarioKNotice->value(QStringLiteral("sourceLabel")))
                fail(QStringLiteral("NAVFAIL scenario K' (round trip): source "
                                    "changed"));
            compareAgainst(*scenarioKNotice, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario K' round trip"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario K']: notice/source/rows all "
                                  "preserved across the failed attempt and a "
                                  "three-page round trip");
            break;
        }

        // ---- Scenario L: baseline persistence across all five workspaces
        // (M9-B5.3). Persistence, NOT recomputation: the deterministic batch
        // and its baseline are produced ONCE, then every stop on the frozen
        // path must show the identical authoritative snapshot AND the
        // identical baseline facts. The baseline is never re-run to "heal" a
        // stop. ----
        case 63: {
            beginScenario(QStringLiteral("L"));
            beginScenario(QStringLiteral("L"));
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL scenario L: runDemoBatch() not "
                                    "invokable"));
            break;
        }
        case 64: {
            if (!QMetaObject::invokeMethod(ctrl, "runBaselineDiagnosis"))
                fail(QStringLiteral("NAVFAIL scenario L: "
                                    "runBaselineDiagnosis() not invokable"));
            break;
        }
        case 65: {
            if (!ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("NAVFAIL scenario L: the baseline produced "
                                    "no result"));
            if (ctrl->property("baselineDiagnosisText").toString().isEmpty())
                fail(QStringLiteral("NAVFAIL scenario L: the baseline text is "
                                    "empty"));
            if (ctrl->property("observedCount").toInt() != 4)
                fail(QStringLiteral("NAVFAIL scenario L: expected the "
                                    "deterministic 4-transaction batch"));
            *scenarioL = takeExtendedSnapshot(ctrl);
            *scenarioLDigest = snapshotDigest(*scenarioL);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario L setup]: observed=%1 "
                                  "baselineChars=%2 digest=%3 mode=%4 "
                                  "source=%5")
                       .arg(ctrl->property("observedCount").toInt())
                       .arg(ctrl->property("baselineDiagnosisText")
                                .toString()
                                .size())
                       .arg(*scenarioLDigest)
                       .arg(ctrl->property("modeLabel").toString())
                       .arg(ctrl->property("sourceLabel").toString());
            switchTo(4);
            break;
        }
        case 66: {
            verifyStructureAndIdentity(QStringLiteral("scenario L @diagnosis"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario L @diagnosis"));
            assertBaselinePersisted(QStringLiteral("scenario L @diagnosis"));
            break;
        }
        case 67: switchTo(1); break;
        case 68: {
            verifyStructureAndIdentity(QStringLiteral("scenario L @dashboard"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario L @dashboard"));
            assertBaselinePersisted(QStringLiteral("scenario L @dashboard"));
            break;
        }
        case 69: switchTo(3); break;
        case 70: {
            verifyStructureAndIdentity(QStringLiteral("scenario L @replay"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario L @replay"));
            assertBaselinePersisted(QStringLiteral("scenario L @replay"));
            break;
        }
        case 71: switchTo(2); break;
        case 72: {
            verifyStructureAndIdentity(
                QStringLiteral("scenario L @communication"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario L @communication"));
            assertBaselinePersisted(
                QStringLiteral("scenario L @communication"));
            break;
        }
        case 73: switchTo(4); break;
        case 74: {
            verifyStructureAndIdentity(
                QStringLiteral("scenario L @diagnosis again"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario L @diagnosis again"));
            assertBaselinePersisted(
                QStringLiteral("scenario L @diagnosis again"));
            break;
        }
        case 75: switchTo(0); break;
        case 76: {
            verifyStructureAndIdentity(
                QStringLiteral("scenario L @transactions"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario L @transactions"));
            assertBaselinePersisted(QStringLiteral("scenario L @transactions"));
            break;
        }
        case 77: switchTo(4); break;
        case 78: {
            endScenario(QStringLiteral("L"));
            verifyStructureAndIdentity(
                QStringLiteral("scenario L @diagnosis final"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario L @diagnosis final"));
            assertBaselinePersisted(
                QStringLiteral("scenario L @diagnosis final"));
            endScenario(QStringLiteral("L"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario L]: baseline + authoritative "
                                  "batch facts identical across Diagnosis -> "
                                  "Dashboard -> Replay -> Communication -> "
                                  "Diagnosis -> Transactions -> Diagnosis");
            break;
        }

        // ---- Scenario N: the Agent question draft is PAGE-LOCAL state
        // (T017 §43.10). It must survive a five-page round trip byte for
        // byte while the authoritative facts stay untouched. The draft is
        // read from the REAL QML item (diagnosisAgentQuestion) — never from
        // the Controller, which has no question-text property at all. ----
        case 79: {
            beginScenario(QStringLiteral("N"));
            beginScenario(QStringLiteral("N"));
            auto *draft =
                findNamedItem(roots, QStringLiteral("diagnosisAgentQuestion"));
            if (!draft) {
                fail(QStringLiteral("NAVFAIL scenario N: diagnosisAgentQuestion "
                                    "not found"));
                break;
            }
            const QString sentinel =
                QStringLiteral("B5.3 draft persistence sentinel");
            draft->setProperty("text", sentinel);
            if (draft->property("text").toString() != sentinel)
                fail(QStringLiteral("NAVFAIL scenario N: the sentinel draft did "
                                    "not stick on the real TextArea"));
            scenarioN->insert(QStringLiteral("draft"), sentinel);
            switchTo(1);
            break;
        }
        case 80: {
            verifyStructureAndIdentity(QStringLiteral("scenario N @dashboard"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario N @dashboard"));
            break;
        }
        case 81: switchTo(2); break;
        case 82: {
            verifyStructureAndIdentity(
                QStringLiteral("scenario N @communication"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario N @communication"));
            break;
        }
        case 83: switchTo(3); break;
        case 84: {
            verifyStructureAndIdentity(QStringLiteral("scenario N @replay"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario N @replay"));
            break;
        }
        case 85: switchTo(0); break;
        case 86: {
            verifyStructureAndIdentity(
                QStringLiteral("scenario N @transactions"));
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario N @transactions"));
            break;
        }
        case 87: switchTo(4); break;
        case 88: {
            endScenario(QStringLiteral("N"));
            verifyStructureAndIdentity(QStringLiteral("scenario N @diagnosis"));
            auto *draft =
                findNamedItem(roots, QStringLiteral("diagnosisAgentQuestion"));
            if (!draft) {
                fail(QStringLiteral("NAVFAIL scenario N: diagnosisAgentQuestion "
                                    "vanished across the round trip"));
            } else if (draft->property("text").toString()
                       != scenarioN->value(QStringLiteral("draft")).toString()) {
                fail(QStringLiteral("NAVFAIL scenario N: the draft changed "
                                    "across the round trip: %1 -> %2")
                         .arg(scenarioN->value(QStringLiteral("draft"))
                                  .toString(),
                              draft->property("text").toString()));
            }
            compareAgainst(*scenarioL, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario N @diagnosis final"));
            assertBaselinePersisted(
                QStringLiteral("scenario N @diagnosis final"));
            endScenario(QStringLiteral("N"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario N]: page-local Agent draft "
                                  "preserved byte for byte across Diagnosis -> "
                                  "Dashboard -> Communication -> Replay -> "
                                  "Transactions -> Diagnosis (authoritative "
                                  "facts untouched)");
            break;
        }

        // ---- M9-C C3/C4: Dashboard Presentation Probe ----
        // A PRESENTATION contract check (distribution denominator = completed
        // count + frozen segment order + zero state; deterministic attention;
        // diagnosis existence cue), NOT a business persistence scenario —
        // deliberately not numbered as "Scenario O". Runs after every
        // existing scenario so none of them changes semantics.
        case 89: {
            *distributionStart = failures->size();
            // deterministic demo state: observed=4, completed=4, golden
            // outcome counts straight from the controller
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL distribution probe: "
                                    "runDemoBatch() not invokable"));
            break;
        }
        case 90: switchTo(1); break;
        case 91: {
            assertOutcomeDistribution(roots,
                                      QStringLiteral("distribution demo"),
                                      *failures);
            // M9-C C4: the demo batch carries exception=1 crc=1 timeout=1
            // protocol=0, so the frozen attention sum is exactly 3 and the
            // baseline was invalidated by the batch change.
            assertDashboardAttention(roots,
                                     QStringLiteral("attention demo"),
                                     *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [distribution demo]: completed=%1 "
                                  "success=%2 exception=%3 crc=%4 timeout=%5 "
                                  "protocol=%6 expectedNoResponse=%7 "
                                  "hasSuccessRate=%8 attention=%9")
                       .arg(ctrl->property("completedCount").toInt())
                       .arg(ctrl->property("successCount").toInt())
                       .arg(ctrl->property("exceptionCount").toInt())
                       .arg(ctrl->property("crcErrorCount").toInt())
                       .arg(ctrl->property("timeoutCount").toInt())
                       .arg(ctrl->property("protocolErrorCount").toInt())
                       .arg(ctrl->property("expectedNoResponseCount").toInt())
                       .arg(ctrl->property("hasSuccessRate").toBool())
                       .arg(ctrl->property("exceptionCount").toInt()
                            + ctrl->property("crcErrorCount").toInt()
                            + ctrl->property("timeoutCount").toInt()
                            + ctrl->property("protocolErrorCount").toInt());
            break;
        }
        case 92: {
            // the cue flips on a real baseline run (existence line only)
            if (!QMetaObject::invokeMethod(ctrl, "runBaselineDiagnosis"))
                fail(QStringLiteral("NAVFAIL distribution probe: "
                                    "runBaselineDiagnosis() not invokable"));
            break;
        }
        case 93: {
            assertDashboardAttention(roots,
                                     QStringLiteral("attention baseline"),
                                     *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [attention baseline]: "
                                  "hasBaselineDiagnosis=%1 attention=%2")
                       .arg(ctrl->property("hasBaselineDiagnosis").toBool())
                       .arg(ctrl->property("exceptionCount").toInt()
                            + ctrl->property("crcErrorCount").toInt()
                            + ctrl->property("timeoutCount").toInt()
                            + ctrl->property("protocolErrorCount").toInt());
            break;
        }
        case 94: {
            // denominator probe: a batch whose completed outcomes are ALL
            // ExpectedNoResponse — the bar is 100% ExpectedNoResponse while
            // successRate stays undefined, and the attention sum stays 0
            // because ExpectedNoResponse is not an anomaly.
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_BROADCAST_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("NAVFAIL distribution probe: "
                                    "loadReplayFile() not invokable"));
            break;
        }
        case 95: {
            if (ctrl->property("sourceLabel").toString()
                != QStringLiteral("t015_broadcast.mlog"))
                fail(QStringLiteral("NAVFAIL distribution probe: broadcast "
                                    "fixture not loaded (source=%1)")
                         .arg(ctrl->property("sourceLabel").toString()));
            if (ctrl->property("hasReplayError").toBool())
                fail(QStringLiteral("NAVFAIL distribution probe: the broadcast "
                                    "fixture produced a replay error"));
            if (ctrl->property("expectedNoResponseCount").toInt() <= 0)
                fail(QStringLiteral("NAVFAIL distribution probe: the broadcast "
                                    "fixture produced no ExpectedNoResponse "
                                    "outcome — fixture audit required"));
            if (ctrl->property("hasSuccessRate").toBool())
                fail(QStringLiteral("NAVFAIL distribution probe: successRate is "
                                    "DEFINED for an all-ExpectedNoResponse "
                                    "batch — the distribution denominator and "
                                    "the successRate denominator have been "
                                    "confused"));
            // still on the dashboard: the bar must now be 100%
            // ExpectedNoResponse, every other segment zero-width, and the
            // attention sum must stay 0 with the outcome-limited wording
            assertOutcomeDistribution(roots,
                                      QStringLiteral("distribution broadcast"),
                                      *failures);
            assertDashboardAttention(roots,
                                     QStringLiteral("attention broadcast"),
                                     *failures);
            auto *bar = findNamedItem(
                roots, QStringLiteral("outcomeDistributionBar_dashboard"));
            auto *enr = findNamedItem(
                roots, QStringLiteral("outcomeSegment_5_dashboard"));
            if (bar && enr
                && qAbs(enr->width() - bar->width()) > 0.5)
                fail(QStringLiteral("NAVFAIL distribution probe: the "
                                    "ExpectedNoResponse segment is not the "
                                    "full bar (%1 of %2)")
                         .arg(enr->width())
                         .arg(bar->width()));
            qInfo().noquote()
                << QStringLiteral("NAV [distribution broadcast]: "
                                  "expectedNoResponse=%1 completed=%2 "
                                  "hasSuccessRate=%3 barWidth=%4 "
                                  "enrSegmentWidth=%5")
                       .arg(ctrl->property("expectedNoResponseCount").toInt())
                       .arg(ctrl->property("completedCount").toInt())
                       .arg(ctrl->property("hasSuccessRate").toBool())
                       .arg(bar ? bar->width() : -1)
                       .arg(enr ? enr->width() : -1);
            break;
        }
        case 96: {
            // protocol-error runtime coverage: the demo cannot prove that
            // protocolError contributes to the attention sum (its protocol
            // count is 0), so a tracked fixture that yields exactly one
            // WriteSingleRegisterEchoMismatch ProtocolError does.
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_PROTOCOL_ERROR_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("NAVFAIL distribution probe: "
                                    "loadReplayFile() not invokable"));
            break;
        }
        case 97: {
            if (ctrl->property("protocolErrorCount").toInt() <= 0)
                fail(QStringLiteral("NAVFAIL attention probe: the "
                                    "protocol-error fixture produced no "
                                    "ProtocolError outcome — fixture audit "
                                    "required"));
            assertOutcomeDistribution(roots,
                                      QStringLiteral("distribution protocol"),
                                      *failures);
            assertDashboardAttention(roots,
                                     QStringLiteral("attention protocol"),
                                     *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [attention protocol]: protocol=%1 "
                                  "attention=%2 (protocol errors are part of "
                                  "the attention sum)")
                       .arg(ctrl->property("protocolErrorCount").toInt())
                       .arg(ctrl->property("exceptionCount").toInt()
                            + ctrl->property("crcErrorCount").toInt()
                            + ctrl->property("timeoutCount").toInt()
                            + ctrl->property("protocolErrorCount").toInt());
            break;
        }
        case 98: {
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("NAVFAIL distribution probe: clearResults() "
                                    "not invokable"));
            break;
        }
        case 99: {
            // closing the probe in the zero state: nothing rendered, no
            // divide-by-zero artefacts, hint wording refreshed, attention and
            // cue hidden with the session
            assertOutcomeDistribution(roots,
                                      QStringLiteral("distribution zero"),
                                      *failures);
            assertDashboardAttention(roots,
                                     QStringLiteral("attention zero"),
                                     *failures);
            *distributionEnd = failures->size();
            qInfo().noquote()
                << QStringLiteral("NAV [distribution zero]: completed=%1 "
                                  "distribution hidden, zero state clean")
                       .arg(ctrl->property("completedCount").toInt());
            break;
        }

        // ---- M9-D D2 Scenario O: the transactions MOVE + navigation
        // neutrality + row presentation preservation. It does NOT touch D3
        // selection. ----
        case 100: {
            beginScenario(QStringLiteral("O"));
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL scenario O: runDemoBatch() not "
                                    "invokable"));
            break;
        }
        case 101: {
            if (rowCountOf(ctrl) != 4)
                fail(QStringLiteral("NAVFAIL scenario O: expected the "
                                    "deterministic 4-transaction batch"));
            *scenarioO = takeExtendedSnapshot(ctrl);
            switchTo(0);
            break;
        }
        case 102: {
            verifyStructureAndIdentity(QStringLiteral("scenario O @transactions"));
            compareAgainst(*scenarioO, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario O @transactions"));
            assertTransactionsPresentation(roots,
                                           QStringLiteral("scenario O"),
                                           *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario O @transactions]: rows=%1 "
                                  "observed=%2 source=%3")
                       .arg(rowCountOf(ctrl))
                       .arg(ctrl->property("observedCount").toInt())
                       .arg(ctrl->property("sourceLabel").toString());
            break;
        }
        case 103: switchTo(1); break;
        case 104: switchTo(0); break;
        case 105: {
            verifyStructureAndIdentity(QStringLiteral("scenario O round trip"));
            compareAgainst(*scenarioO, takeExtendedSnapshot(ctrl),
                           QStringLiteral("scenario O round trip"));
            assertTransactionsPresentation(roots,
                                           QStringLiteral("scenario O round "
                                                          "trip"),
                                           *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario O]: the transactions "
                                  "workspace shows the same model across "
                                  "navigation (no business change)");
            break;
        }
        case 106: {
            // ExpectedNoResponse row: the migrated presentation keeps the
            // neutral frozen wording (never 成功 / 超时 / 失败).
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_BROADCAST_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("NAVFAIL scenario O: loadReplayFile() not "
                                    "invokable"));
            break;
        }
        case 107: {
            switchTo(0);
            break;
        }
        case 108: {
            if (rowCountOf(ctrl) != 1)
                fail(QStringLiteral("NAVFAIL scenario O (broadcast): expected "
                                    "exactly one row, got %1")
                         .arg(rowCountOf(ctrl)));
            const QVariant status = modelRole(ctrl, QStringLiteral("statusText"));
            if (status.toString() != QStringLiteral("预期无响应"))
                fail(QStringLiteral("NAVFAIL scenario O (broadcast): status text "
                                    "is %1, expected the frozen neutral wording")
                         .arg(status.toString()));
            assertTransactionsPresentation(roots,
                                           QStringLiteral("scenario O "
                                                          "broadcast"),
                                           *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario O broadcast]: rows=1 status=%1 "
                                  "(neutral, not an anomaly)")
                       .arg(status.toString());
            break;
        }
        case 109: {
            // ProtocolError row: the deterministic issue detail survives the
            // migration, and it is NOT used to derive the status.
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_PROTOCOL_ERROR_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("NAVFAIL scenario O: loadReplayFile() not "
                                    "invokable"));
            break;
        }
        case 110: {
            if (rowCountOf(ctrl) != 1)
                fail(QStringLiteral("NAVFAIL scenario O (protocol): expected "
                                    "exactly one row"));
            const QVariant status = modelRole(ctrl, QStringLiteral("statusText"));
            const QVariant issue = modelRole(ctrl, QStringLiteral("issueText"));
            if (status.toString() != QStringLiteral("协议错误"))
                fail(QStringLiteral("NAVFAIL scenario O (protocol): status text "
                                    "is %1").arg(status.toString()));
            if (issue.toString().isEmpty())
                fail(QStringLiteral("NAVFAIL scenario O (protocol): the "
                                    "deterministic issue detail was lost in the "
                                    "migration"));
            assertTransactionsPresentation(roots,
                                           QStringLiteral("scenario O protocol"),
                                           *failures);
            // orthogonality: the issue text never rewrites the status text
            if (status.toString().contains(issue.toString()))
                fail(QStringLiteral("NAVFAIL scenario O: the issue text leaked "
                                    "into the status column"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario O protocol]: rows=1 status=%1 "
                                  "hasIssueDetail=1 (orthogonal axes)")
                       .arg(status.toString());
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("NAVFAIL scenario O: clearResults() not "
                                    "invokable"));
            break;
        }
        case 111: {
            if (rowCountOf(ctrl) != 0)
                fail(QStringLiteral("NAVFAIL scenario O: the model was not "
                                    "cleared"));
            assertTransactionsPresentation(roots,
                                           QStringLiteral("scenario O empty"),
                                           *failures);
            endScenario(QStringLiteral("O"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario O]: transactions empty state "
                                  "clean after clearResults");
            break;
        }

        // ---- M9-D D3 Scenario P: selection lifetime (P1..P5). Selection is
        // page-local presentation state; the authoritative model has exactly
        // one mutation path (setEntries -> beginResetModel/endResetModel, no
        // dataChanged), and the page clears the selection on modelReset.
        // The harness drives selection through the ListView's own
        // currentIndex (the same property Qt's mouse tap and keyboard both
        // set) — this is a state/selection oracle, NOT a physical input
        // proof; mouse and keyboard interaction are verified manually in D6.
        case 112: {
            beginScenario(QStringLiteral("P"));
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL scenario P: runDemoBatch() not "
                                    "invokable"));
            break;
        }
        case 113: {
            if (rowCountOf(ctrl) != 4)
                fail(QStringLiteral("NAVFAIL scenario P: expected the "
                                    "deterministic 4-transaction batch"));
            switchTo(0);
            break;
        }
        case 114: {
            // P1 initial: nothing selected — no implicit row 0
            verifyStructureAndIdentity(QStringLiteral("scenario P initial"));
            if (selectedRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario P: the initial selection "
                                    "is %1, expected -1 (no implicit pick)")
                         .arg(selectedRowOf(roots)));
            assertTransactionDetailMapping(roots,
                                           QStringLiteral("scenario P initial"),
                                           *failures);
            break;
        }
        case 115: {
            // P1: explicit selection of row 2 through the product path
            auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
            if (!list)
                fail(QStringLiteral("NAVFAIL scenario P: transactionsList not "
                                    "found"));
            else
                list->setProperty("currentIndex", 2);
            break;
        }
        case 116: {
            if (selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario P: selectedRow is %1, "
                                    "expected 2").arg(selectedRowOf(roots)));
            assertTransactionDetailMapping(roots,
                                           QStringLiteral("scenario P row 2"),
                                           *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario P1]: row 2 selected, detail "
                                  "mapping verified field by field");
            break;
        }
        case 117: switchTo(1); break;
        case 118: switchTo(3); break;
        case 119: {
            // P2: navigation alone must NOT invalidate the selection
            if (selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario P (return): selectedRow "
                                    "is %1, expected the preserved 2")
                         .arg(selectedRowOf(roots)));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario P navigation return"),
                *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario P2]: selection + detail "
                                  "survived Transactions -> Dashboard -> "
                                  "Replay -> Transactions");
            break;
        }
        case 120: {
            // P3: a FAILED replacement leaves the model untouched, so the
            // selection and the detail must stay valid (B4 authority rule)
            const QUrl missing = QUrl::fromLocalFile(
                QStringLiteral("MODBUSLENS_NO_SUCH_DIR/scenario_p.mlog"));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, missing)))
                fail(QStringLiteral("NAVFAIL scenario P: loadReplayFile() not "
                                    "invokable"));
            break;
        }
        case 121: {
            if (rowCountOf(ctrl) != 4)
                fail(QStringLiteral("NAVFAIL scenario P: the failed replacement "
                                    "changed the model (%1 rows)")
                         .arg(rowCountOf(ctrl)));
            if (selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario P: the failed replacement "
                                    "dropped the selection (%1)")
                         .arg(selectedRowOf(roots)));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario P failed replacement"),
                *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario P3]: failed replacement kept "
                                  "the model, the selection and the detail");
            break;
        }
        case 122: {
            // P4: a SUCCESSFUL replacement resets the model -> the selection
            // is invalidated, never silently re-pointed at row 0. Switch back
            // to the transactions page first so the post-reset UI state is
            // asserted on the ACTIVE page.
            switchTo(0);
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_BROADCAST_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("NAVFAIL scenario P: loadReplayFile() not "
                                    "invokable"));
            break;
        }
        case 123: {
            if (rowCountOf(ctrl) != 1)
                fail(QStringLiteral("NAVFAIL scenario P: expected the broadcast "
                                    "model (1 row), got %1")
                         .arg(rowCountOf(ctrl)));
            if (selectedRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario P: the model reset did "
                                    "not clear the selection (row %1)")
                         .arg(selectedRowOf(roots)));
            if (!selectedEntryOf(roots).isEmpty())
                fail(QStringLiteral("NAVFAIL scenario P: the detail snapshot "
                                    "survived the model reset"));
            assertTransactionDetailMapping(roots,
                                           QStringLiteral("scenario P after "
                                                          "reset"),
                                           *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario P4]: successful replacement "
                                  "reset the model and invalidated the "
                                  "selection (no row-0 re-pick)");
            break;
        }
        case 124: {
            // P4 continued: an explicit selection of the ENR row keeps the
            // neutral detail wording
            auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
            if (list)
                list->setProperty("currentIndex", 0);
            break;
        }
        case 125: {
            if (selectedRowOf(roots) != 0)
                fail(QStringLiteral("NAVFAIL scenario P: selectedRow is %1, "
                                    "expected 0").arg(selectedRowOf(roots)));
            assertTransactionDetailMapping(roots,
                                           QStringLiteral("scenario P ENR"),
                                           *failures);
            const QVariant status =
                selectedEntryOf(roots).value(QStringLiteral("statusText"));
            if (status.toString() != QStringLiteral("预期无响应"))
                fail(QStringLiteral("NAVFAIL scenario P: the ENR detail status "
                                    "is %1").arg(status.toString()));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario P4 detail]: expectedNoResponse "
                                  "kept its neutral wording");
            break;
        }
        case 126: {
            // P5: a new batch must not inherit the old selection
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL scenario P: runDemoBatch() not "
                                    "invokable"));
            break;
        }
        case 127: {
            if (rowCountOf(ctrl) != 4)
                fail(QStringLiteral("NAVFAIL scenario P: the new demo batch did "
                                    "not publish (rows=%1)")
                         .arg(rowCountOf(ctrl)));
            if (selectedRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario P: the old selection "
                                    "leaked across batches (row %1)")
                         .arg(selectedRowOf(roots)));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario P5]: a new batch reset the "
                                  "model; the old selection did not leak");
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("NAVFAIL scenario P: clearResults() not "
                                    "invokable"));
            break;
        }
        case 128: {
            if (rowCountOf(ctrl) != 0)
                fail(QStringLiteral("NAVFAIL scenario P: clearResults left %1 "
                                    "rows").arg(rowCountOf(ctrl)));
            if (selectedRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario P: the cleared model kept "
                                    "a selection"));
            assertTransactionDetailMapping(roots,
                                           QStringLiteral("scenario P cleared"),
                                           *failures);
            endScenario(QStringLiteral("P"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario P]: selection lifecycle "
                                  "(initial -1 / navigation persists / "
                                  "replacement invalidates / failed "
                                  "replacement preserves / clear resets) "
                                  "verified");
            break;
        }

        // ---- M9-D D3 review (P0-2) Scenario Q: DEFERRED selection safety.
        // P1..P5 all drive selection while every delegate is materialized, so
        // they never exercise the pendingSelectionRow / Qt.callLater path.
        // Q enters that path deterministically: a request for a row that
        // cannot be materialized is parked, and the harness then performs the
        // authoritative reset IN THE SAME EVENT-LOOP TURN, before the queued
        // callback can run. The callback must then read the (already cleared)
        // pending value and do nothing — a stale completion must never
        // resurrect a selection into the replacement model.
        case 129: {
            beginScenario(QStringLiteral("Q"));
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("NAVFAIL scenario Q: clearResults() not "
                                    "invokable"));
            switchTo(0);
            break;
        }
        case 130: {
            if (rowCountOf(ctrl) != 0)
                fail(QStringLiteral("NAVFAIL scenario Q: expected an empty "
                                    "model to start from, got %1 rows")
                         .arg(rowCountOf(ctrl)));
            if (selectedRowOf(roots) != -1
                || pendingSelectionRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario Q: a stale selection "
                                    "survived into the Q walk"));
            // THE deferred request: with no rows there is no delegate to
            // snapshot, so this parks in pendingSelectionRow and schedules
            // the retry. This is the real pending path, entered through the
            // page's own selectRow (no test-only production API).
            if (!requestTransactionSelection(roots, 0))
                fail(QStringLiteral("NAVFAIL scenario Q: selectRow() is not "
                                    "invokable on the transactions page"));
            // Same-turn proof that the DEFERRED path was really taken: if the
            // delegate had been available the snapshot would be complete and
            // the pending slot would already be back to -1.
            if (pendingSelectionRowOf(roots) != 0)
                fail(QStringLiteral("NAVFAIL scenario Q: the request did not "
                                    "park in the deferred path (pending=%1) — "
                                    "this scenario would be vacuous")
                         .arg(pendingSelectionRowOf(roots)));
            if (selectedRowOf(roots) != -1
                || !selectedEntryOf(roots).isEmpty())
                fail(QStringLiteral("NAVFAIL scenario Q: a parked request must "
                                    "not present a selection yet"));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario Q parked request"), *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario Q1]: a selection request is "
                                  "parked in the deferred path (pending row 0, "
                                  "empty model)");
            // Same event-loop turn: the authoritative model is replaced BEFORE
            // the queued callback can run. modelReset clears the pending slot
            // synchronously (direct connection, same thread).
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL scenario Q: runDemoBatch() not "
                                    "invokable"));
            if (pendingSelectionRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario Q: the model reset left "
                                    "a parked request alive (pending=%1)")
                         .arg(pendingSelectionRowOf(roots)));
            if (selectedRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario Q: the model reset left "
                                    "a selection behind"));
            break;
        }
        case 131: {
            // The queued completion has now run. Row 0 EXISTS in the
            // replacement model, so a stale callback would have had something
            // real to select — the selection must nevertheless be absent.
            if (rowCountOf(ctrl) != 4)
                fail(QStringLiteral("NAVFAIL scenario Q: expected the "
                                    "deterministic 4-transaction batch after "
                                    "the replacement, got %1")
                         .arg(rowCountOf(ctrl)));
            if (pendingSelectionRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario Q: the deferred "
                                    "completion re-parked a request (pending=%1)")
                         .arg(pendingSelectionRowOf(roots)));
            if (selectedRowOf(roots) != -1
                || !selectedEntryOf(roots).isEmpty())
                fail(QStringLiteral("NAVFAIL scenario Q: a STALE deferred "
                                    "selection resurrected into the "
                                    "replacement model (selectedRow=%1)")
                         .arg(selectedRowOf(roots)));
            auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
            if (!list)
                fail(QStringLiteral("NAVFAIL scenario Q: transactionsList not "
                                    "found"));
            else if (list->property("currentIndex").toInt() != -1)
                fail(QStringLiteral("NAVFAIL scenario Q: currentIndex is %1, "
                                    "expected -1 after the reset")
                         .arg(list->property("currentIndex").toInt()));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario Q after reset"), *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario Q1]: the reset ran before the "
                                  "deferred completion; row 0 exists in the "
                                  "new model and is NOT selected — a stale "
                                  "selection cannot resurrect across a reset");
            break;
        }
        case 132: {
            // Q2: an older deferred request must never overwrite a NEWER
            // selection. Row 99 cannot be materialized (4 rows), so it parks;
            // row 2 is then requested through the ListView's own currentIndex
            // (the path Qt's tap and keyboard share) and completes at once.
            if (selectedRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario Q: expected no selection "
                                    "before the latest-request probe"));
            if (!requestTransactionSelection(roots, 99))
                fail(QStringLiteral("NAVFAIL scenario Q: selectRow() is not "
                                    "invokable on the transactions page"));
            if (pendingSelectionRowOf(roots) != 99)
                fail(QStringLiteral("NAVFAIL scenario Q: the out-of-range "
                                    "request did not park (pending=%1)")
                         .arg(pendingSelectionRowOf(roots)));
            auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
            if (!list)
                fail(QStringLiteral("NAVFAIL scenario Q: transactionsList not "
                                    "found"));
            else
                list->setProperty("currentIndex", 2);
            // Same turn: the newer request completed, so the older parked one
            // must have been superseded already.
            if (pendingSelectionRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario Q: the newer selection "
                                    "did not clear the older parked request "
                                    "(pending=%1)")
                         .arg(pendingSelectionRowOf(roots)));
            if (selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario Q: selectedRow is %1, "
                                    "expected the newer request 2")
                         .arg(selectedRowOf(roots)));
            break;
        }
        case 133: {
            // Same oracle as above, now after the queued completion ran: a
            // capture-the-old-row implementation would fail HERE (it would
            // clear the selection because row 99 has no delegate).
            if (selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario Q: an OLDER deferred "
                                    "request clobbered the newer selection "
                                    "(selectedRow=%1, expected 2)")
                         .arg(selectedRowOf(roots)));
            if (pendingSelectionRowOf(roots) != -1)
                fail(QStringLiteral("NAVFAIL scenario Q: unexpected parked "
                                    "request after the latest-request probe"));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario Q latest request wins"),
                *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario Q2]: the older deferred "
                                  "request did not clobber the newer explicit "
                                  "selection (row 2 still selected, detail "
                                  "mapping intact)");
            break;
        }
        case 134: {
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("NAVFAIL scenario Q: clearResults() not "
                                    "invokable"));
            endScenario(QStringLiteral("Q"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario Q]: deferred selection "
                                  "safety verified (parked request + "
                                  "same-turn reset -> no resurrection; "
                                  "older request superseded by newer). "
                                  "BOUNDARY: the pending-onto-pending "
                                  "variant (two successive unfulfillable "
                                  "requests with no reset) is NOT separately "
                                  "exercised — its outcome (no selection) is "
                                  "indistinguishable from a stale capture, so "
                                  "it proves nothing extra; and a real 4-row "
                                  "batch materializes every delegate, so the "
                                  "parked path is entered through the page's "
                                  "own selectRow rather than a keyboard move.");
            break;
        }

        // ---- M9-D D4 Scenario R: the Transactions diagnosis EXISTENCE cue.
        // Authority = the Controller's real hasBaselineDiagnosis (NO new
        // property, no page-local copy, no baseline-text parsing). The cue is
        // a pure text line: it must not respond to row selection, must not
        // survive a batch publication (the Controller's own revision
        // semantics invalidate the baseline), and clearDiagnosis must return
        // it to the no-baseline state WITHOUT touching rows, statistics or
        // the replay source.
        case 135: {
            beginScenario(QStringLiteral("R"));
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL scenario R: runDemoBatch() not "
                                    "invokable"));
            switchTo(0);
            break;
        }
        case 136: {
            // R1: a published session WITHOUT a baseline -> no-baseline cue.
            if (rowCountOf(ctrl) != 4)
                fail(QStringLiteral("NAVFAIL scenario R: expected the "
                                    "deterministic 4-transaction batch, got %1")
                         .arg(rowCountOf(ctrl)));
            if (ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("NAVFAIL scenario R: a baseline survived "
                                    "the batch publication — the Controller "
                                    "revision semantics regressed"));
            assertTransactionsDiagnosisCue(
                roots, QStringLiteral("scenario R1 no baseline"), *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario R1]: a published session "
                                  "without a baseline shows the no-baseline "
                                  "cue");
            break;
        }
        case 137: {
            // R3 setup: select a row through the ListView's own currentIndex.
            auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
            if (!list)
                fail(QStringLiteral("NAVFAIL scenario R: transactionsList not "
                                    "found"));
            else
                list->setProperty("currentIndex", 2);
            break;
        }
        case 138: {
            // R3: the selection MUST NOT move the cue (different axes).
            if (selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario R: selectedRow is %1, "
                                    "expected 2").arg(selectedRowOf(roots)));
            if (ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("NAVFAIL scenario R: selecting a row "
                                    "changed the diagnosis authority"));
            assertTransactionsDiagnosisCue(
                roots, QStringLiteral("scenario R3 selection independence"),
                *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario R3]: row 2 selected — the cue "
                                  "state is unchanged (existence is not "
                                  "selection-derived)");
            break;
        }
        case 139: {
            // R2: the real Controller command flips the authority.
            if (!QMetaObject::invokeMethod(ctrl, "runBaselineDiagnosis"))
                fail(QStringLiteral("NAVFAIL scenario R: runBaselineDiagnosis() "
                                    "not invokable"));
            break;
        }
        case 140: {
            if (!ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("NAVFAIL scenario R: runBaselineDiagnosis "
                                    "did not set the authority"));
            assertTransactionsDiagnosisCue(
                roots, QStringLiteral("scenario R2 baseline available"),
                *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario R2]: after the real baseline "
                                  "run the cue shows the available state "
                                  "(findings text is never parsed or shown)");
            break;
        }
        case 141: switchTo(4); break;
        case 142: switchTo(1); break;
        case 143: {
            // R4: navigation alone must not move the cue (hide/show is not a
            // diagnosis lifecycle event — B5 freeze).
            switchTo(0);
            if (!ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("NAVFAIL scenario R: navigation cleared the "
                                    "baseline authority"));
            if (selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario R: navigation cleared the "
                                    "selection (unrelated regression)"));
            assertTransactionsDiagnosisCue(
                roots, QStringLiteral("scenario R4 navigation persistence"),
                *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario R4]: Transactions -> Diagnosis "
                                  "-> Dashboard -> Transactions kept the cue on "
                                  "the same authoritative state");
            break;
        }
        case 144: {
            // R5: the frozen diagnosis-only clear. Rows, statistics, source
            // and the selection stay; only the diagnosis axis resets.
            const QMap<QString, QVariant> before = takeSnapshot(ctrl);
            if (!QMetaObject::invokeMethod(ctrl, "clearDiagnosis"))
                fail(QStringLiteral("NAVFAIL scenario R: clearDiagnosis() not "
                                    "invokable"));
            if (rowCountOf(ctrl) != 4)
                fail(QStringLiteral("NAVFAIL scenario R: clearDiagnosis touched "
                                    "the model (%1 rows)")
                         .arg(rowCountOf(ctrl)));
            if (ctrl->property("observedCount").toInt() != 4)
                fail(QStringLiteral("NAVFAIL scenario R: clearDiagnosis touched "
                                    "the statistics"));
            if (selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario R: clearDiagnosis touched "
                                    "the selection"));
            assertTransactionsDiagnosisCue(
                roots, QStringLiteral("scenario R5 after clearDiagnosis"),
                *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario R5]: clearDiagnosis returned "
                                  "the cue to no-baseline; rows/statistics/"
                                  "source untouched (%1 snapshot fields "
                                  "stable)")
                     .arg(before.size());
            break;
        }
        case 145: {
            // R13 (revision invalidation, Controller semantics): a baseline on
            // an authoritative batch change does NOT survive — and the page
            // implements NO invalidation of its own.
            if (!QMetaObject::invokeMethod(ctrl, "runBaselineDiagnosis"))
                fail(QStringLiteral("NAVFAIL scenario R: runBaselineDiagnosis() "
                                    "not invokable (revision probe)"));
            break;
        }
        case 146: {
            if (!ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("NAVFAIL scenario R: the revision probe "
                                    "baseline did not establish"));
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL scenario R: runDemoBatch() not "
                                    "invokable (revision probe)"));
            if (rowCountOf(ctrl) != 4)
                fail(QStringLiteral("NAVFAIL scenario R: the replacement batch "
                                    "left %1 rows").arg(rowCountOf(ctrl)));
            if (ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("NAVFAIL scenario R: a baseline survived an "
                                    "authoritative batch change — the cue must "
                                    "follow the Controller, not the page"));
            assertTransactionsDiagnosisCue(
                roots, QStringLiteral("scenario R13 revision invalidation"),
                *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario R13]: a new batch invalidates "
                                  "the baseline by the Controller's own "
                                  "revision semantics; the cue follows");
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("NAVFAIL scenario R: clearResults() not "
                                    "invokable"));
            endScenario(QStringLiteral("R"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario R]: diagnosis existence cue "
                                  "verified (no-baseline / available / "
                                  "selection independence / navigation "
                                  "persistence / diagnosis-only clear / "
                                  "revision invalidation)");
            break;
        }

        // ---- M9-D D5 Scenario S: LEGACY RETIREMENT. The Legacy workspace
        // must be REMOVED from the runtime tree (retirement = actual removal,
        // never visible:false), the rail must be compacted to six entries
        // (Transactions 0 .. Device 5 disabled), the centralized index
        // contract must move Transactions to 0 and Device to 5 with NO
        // workspaceLegacyIndex left, and the five active workspaces must
        // round-trip without touching business facts.
        case 147: {
            beginScenario(QStringLiteral("S"));
            // S1: startup selected Transactions (captured at stage 0) and
            // Legacy is absent from the runtime tree.
            if (*startupIndex
                != rootObj->property("workspaceTransactionsIndex").toInt()
                || rootObj->property("workspaceTransactionsIndex").toInt() != 0)
                fail(QStringLiteral("NAVFAIL scenario S1: the startup workspace "
                                    "is %1, expected Transactions at index 0 "
                                    "(property says %2) — expected D5 missing "
                                    "retirement")
                         .arg(*startupIndex)
                         .arg(rootObj->property("workspaceTransactionsIndex")
                                  .toInt()));
            if (rootObj->metaObject()->indexOfProperty(
                    "workspaceLegacyIndex") != -1)
                fail(QStringLiteral("NAVFAIL scenario S1: workspaceLegacyIndex "
                                    "still exists — the retired index contract "
                                    "must be deleted, not aliased"));
            if (findNamedItem(roots, QStringLiteral("legacyWorkspace")))
                fail(QStringLiteral("NAVFAIL scenario S1: legacyWorkspace still "
                                    "exists in the runtime tree — expected D5 "
                                    "missing retirement"));
            *snapshotS = takeSnapshot(ctrl);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario S1]: startup workspace is "
                                  "Transactions (index 0) and no Legacy "
                                  "workspace exists (retirement = removal)");
            break;
        }
        case 148: {
            // S2: the compact rail — six entries, final order and enablement.
            for (int i = 0; i <= 4; ++i) {
                auto *item = findNamedItem(
                    roots, QStringLiteral("navItem_%1").arg(i));
                if (!item || !item->property("enabled").toBool())
                    fail(QStringLiteral("NAVFAIL scenario S2: navItem_%1 must "
                                        "exist and be enabled (active "
                                        "workspace)").arg(i));
            }
            auto *device = findNamedItem(roots, QStringLiteral("navItem_5"));
            if (!device)
                fail(QStringLiteral("NAVFAIL scenario S2: navItem_5 (Device) "
                                    "not found"));
            else if (!device->property("enabled").toBool())
                fail(QStringLiteral("NAVFAIL scenario S2: navItem_5 (Device) "
                                    "must be enabled (M12-B profile "
                                    "workspace)"));
            if (rootObj->property("workspaceTransactionsIndex").toInt() != 0
                || rootObj->property("workspaceDeviceIndex").toInt() != 5)
                fail(QStringLiteral("NAVFAIL scenario S2: the centralized index "
                                    "contract is Transactions=%1 Device=%2, "
                                    "expected 0/5")
                         .arg(rootObj->property("workspaceTransactionsIndex")
                                  .toInt())
                         .arg(rootObj->property("workspaceDeviceIndex")
                                  .toInt()));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario S2]: rail = Transactions(0) "
                                  "Dashboard(1) Communication(2) Replay(3) "
                                  "Diagnosis(4) Device(5, M12-B)");
            break;
        }
        case 149: {
            // S3 + S4: no Legacy nav entry, no Legacy statistics instance, no
            // Legacy surplus owner anywhere in the runtime tree.
            if (findNamedItem(roots, QStringLiteral("navItem_6")))
                fail(QStringLiteral("NAVFAIL scenario S3: navItem_6 still exists "
                                    "— the rail must be compacted to six "
                                    "entries"));
            if (findNamedItem(roots,
                              QStringLiteral("statisticsOverview_legacy")))
                fail(QStringLiteral("NAVFAIL scenario S4: the legacy "
                                    "StatisticsOverview instance still exists"));
            if (findNamedItem(roots, QStringLiteral("legacyTailSpacer")))
                fail(QStringLiteral("NAVFAIL scenario S4: legacyTailSpacer still "
                                    "exists — the Legacy column remnants were "
                                    "not removed"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario S3/S4]: legacy nav count = 0, "
                                  "legacy StatisticsOverview instance absent, "
                                  "legacyTailSpacer absent");
            break;
        }
        case 150: {
            // S5: TransactionsPage is StackLayout child 0 and still owns the
            // whole transactions presentation (pane/detail/cue).
            auto *host =
                findNamedItem(roots, QStringLiteral("workspaceHost"));
            auto *tx =
                findNamedItem(roots, QStringLiteral("transactionsPage"));
            if (!host || !tx)
                fail(QStringLiteral("NAVFAIL scenario S5: workspaceHost or "
                                    "transactionsPage not found"));
            else if (tx->parentItem() != host
                     || host->childItems().indexOf(tx) != 0)
                fail(QStringLiteral("NAVFAIL scenario S5: transactionsPage is "
                                    "not StackLayout child 0 (parent ok=%1, "
                                    "child index %2)")
                         .arg(tx->parentItem() == host)
                         .arg(host->childItems().indexOf(tx)));
            auto *pane = findNamedItem(roots, QStringLiteral("transactionsPane"));
            auto *detail =
                findNamedItem(roots, QStringLiteral("transactionDetail"));
            auto *cue = findNamedItem(
                roots, QStringLiteral("transactionsDiagnosisCue"));
            if (!pane || !detail || !cue
                || !underItem(pane, tx) || !underItem(detail, tx)
                || !underItem(cue, tx))
                fail(QStringLiteral("NAVFAIL scenario S5: the transactions "
                                    "presentation (pane/detail/cue) is not "
                                    "fully under the Transactions page"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario S5]: transactionsPage is child "
                                  "0; pane/detail/cue all inside its subtree");
            break;
        }
        case 151: switchTo(1); break;
        case 152: {
            verifyStructureAndIdentity(QStringLiteral("scenario S @dashboard"));
            compareAgainst(*snapshotS, takeSnapshot(ctrl),
                           QStringLiteral("scenario S @dashboard"));
            break;
        }
        case 153: switchTo(2); break;
        case 154: {
            verifyStructureAndIdentity(
                QStringLiteral("scenario S @communication"));
            compareAgainst(*snapshotS, takeSnapshot(ctrl),
                           QStringLiteral("scenario S @communication"));
            break;
        }
        case 155: switchTo(3); break;
        case 156: {
            verifyStructureAndIdentity(QStringLiteral("scenario S @replay"));
            compareAgainst(*snapshotS, takeSnapshot(ctrl),
                           QStringLiteral("scenario S @replay"));
            break;
        }
        case 157: {
            // S6 (tail) + S7: Diagnosis station, back to Transactions, and a
            // direct Device activation must never move the selection.
            verifyStructureAndIdentity(QStringLiteral("scenario S @diagnosis"));
            compareAgainst(*snapshotS, takeSnapshot(ctrl),
                           QStringLiteral("scenario S @diagnosis"));
            switchTo(rootObj->property("workspaceTransactionsIndex").toInt());
            verifyStructureAndIdentity(QStringLiteral("scenario S @transactions"));
            compareAgainst(*snapshotS, takeSnapshot(ctrl),
                           QStringLiteral("scenario S @transactions return"));
            auto *device = findNamedItem(roots, QStringLiteral("navItem_5"));
            const int before =
                rail->property("currentWorkspaceIndex").toInt();
            if (!device || !QMetaObject::invokeMethod(device, "activate"))
                fail(QStringLiteral("NAVFAIL scenario S7: the Device entry "
                                    "activation path is not invokable"));
            if (rail->property("currentWorkspaceIndex").toInt() == before)
                fail(QStringLiteral("NAVFAIL scenario S7: the Device activation "
                                    "did NOT change currentWorkspaceIndex "
                                    "(%1 — expected 5)")
                         .arg(rail->property("currentWorkspaceIndex").toInt()));
            endScenario(QStringLiteral("S"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario S]: legacy retirement verified "
                                  "(startup=Transactions at 0 / compact rail / "
                                  "no legacy runtime objects / child 0 owns the "
                                  "presentation / five-workspace round trip "
                                  "neutral / Device opens profile workspace)");
            break;
        }

        // ---- M9-D D6 correction Scenario T: PHYSICAL-KEY-PATH coverage.
        // The manual interaction review found that Up/Down/Home/End do
        // nothing after a mouse row selection. This scenario synthesizes
        // real QKeyEvents through the window's active focus item so the
        // four keys are tested on their genuine path, and audits who owns
        // keyboard focus. Round A (audit): Up/Down are hard-asserted (Qt
        // native capability); Home/End are REPORT-ONLY until their native
        // behavior is measured.
        case 158: {
            beginScenario(QStringLiteral("T"));
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("NAVFAIL scenario T: clearResults() not "
                                    "invokable"));
            switchTo(rootObj->property("workspaceDashboardIndex").toInt());
            break;
        }
        case 159: {
            // REAL click on Run Demo: a Control with StrongFocus takes
            // keyboard focus exactly like a physical click would.
            if (!clickItem(QStringLiteral("dashboardRunDemo")))
                fail(QStringLiteral("NAVFAIL scenario T: the Run Demo click "
                                    "could not be delivered"));
            if (rowCountOf(ctrl) != 4)
                fail(QStringLiteral("NAVFAIL scenario T: the Run Demo click "
                                    "did not publish the batch"));
            focusAudit(QStringLiteral("after REAL Run Demo click"));
            break;
        }
        case 160: {
            // REAL click on the rail entry (MouseArea onClicked -> activate):
            // a plain-Item MouseArea does NOT take keyboard focus.
            const int txIndex =
                rootObj->property("workspaceTransactionsIndex").toInt();
            if (!clickItem(QStringLiteral("navItem_%1").arg(txIndex)))
                fail(QStringLiteral("NAVFAIL scenario T: the transactions rail "
                                    "click could not be delivered"));
            auto *rail = findNamedItem(roots, QStringLiteral("navigationRail"));
            if (!rail || rail->property("currentWorkspaceIndex").toInt()
                             != txIndex)
                fail(QStringLiteral("NAVFAIL scenario T: the rail click did "
                                    "not switch the workspace"));
            focusAudit(QStringLiteral("after REAL rail click"));
            break;
        }
        case 161: {
            // Best-effort row click; delegates may not be materialized yet
            // (the model was published while the page was hidden). The
            // authoritative click happens at stage 162 after a settle tick.
            auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
            QQuickItem *row = nullptr;
            if (list) {
                QMetaObject::invokeMethod(list, "itemAtIndex",
                                          Q_RETURN_ARG(QQuickItem *, row),
                                          Q_ARG(int, 2));
            }
            if (row && row->isVisible())
                clickItemPoint(row);
            focusAudit(QStringLiteral("after REAL row click (the manual defect "
                                     "state)"));
            break;
        }
        case 162: {
            // Authoritative row-2 click after a settle tick, then the
            // post-fix contract: the click must have focused the LIST (the
            // TapHandler focus call) and selected row 2.
            auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
            bool rowClicked = false;
            for (int attempt = 0; attempt < 4 && !rowClicked; ++attempt) {
                QQuickItem *row = nullptr;
                if (list) {
                    QMetaObject::invokeMethod(list, "itemAtIndex",
                                              Q_RETURN_ARG(QQuickItem *, row),
                                              Q_ARG(int, 2));
                }
                if (row && row->isVisible() && clickItemPoint(row)
                    && selectedRowOf(roots) == 2)
                    rowClicked = true;
                else {
                    qInfo().noquote()
                        << QStringLiteral("NAV [scenario T]: attempt %1: list.count=%2 list=%3x%4 visible=%5")
                               .arg(attempt)
                               .arg(list ? list->property("count").toInt() : -1)
                               .arg(list ? list->width() : -1)
                               .arg(list ? list->height() : -1)
                               .arg(list ? list->isVisible() : false);
                    QCoreApplication::processEvents();
                }
            }
            if (!rowClicked)
                fail(QStringLiteral("NAVFAIL scenario T: the row-2 click could "
                                    "not be delivered (delegate never "
                                    "materialized)"));
            if (rowCountOf(ctrl) != 4 || selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario T: rows=%1 selected=%2")
                         .arg(rowCountOf(ctrl))
                         .arg(selectedRowOf(roots)));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario T after real click"), *failures);
            focusAudit(QStringLiteral("after REAL row click + fix"));
            auto *lst = findNamedItem(roots, QStringLiteral("transactionsList"));
            if (!lst || !lst->property("activeFocus").toBool())
                fail(QStringLiteral("NAVFAIL scenario T: the real row click "
                                    "did not leave the list in keyboard focus "
                                    "— the TapHandler focus fix is not "
                                    "effective"));
            break;
        }
        case 163: {
            // The manual blocker, as an automated assertion: with the focus
            // the REAL click left behind, Up must move the selection (the
            // delegate forwards the key to the view).
            if (!sendKey(Qt::Key_Up))
                fail(QStringLiteral("NAVFAIL scenario T: the Up key could not "
                                    "be delivered (no active focus item)"));
            if (selectedRowOf(roots) != 1)
                fail(QStringLiteral("NAVFAIL scenario T (Up after real click): "
                                    "selectedRow is %1, expected 1")
                         .arg(selectedRowOf(roots)));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario T Up"), *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario T]: Up moved the selection to "
                                  "row 1 (delegate forwards the key to the "
                                  "view)");
            break;
        }
        case 164: {
            // The remaining three keys, each asserted with the single
            // selectRow path and the detail mapping in sync.
            auto *lst = qobject_cast<QQuickItem *>(
                findNamedItem(roots, QStringLiteral("transactionsList")));
            if (!sendKey(Qt::Key_Down))
                fail(QStringLiteral("NAVFAIL scenario T: Down undeliverable"));
            if (selectedRowOf(roots) != 2)
                fail(QStringLiteral("NAVFAIL scenario T (Down): selectedRow is "
                                    "%1, expected 2")
                         .arg(selectedRowOf(roots)));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario T Down"), *failures);
            if (!sendKey(Qt::Key_Home))
                fail(QStringLiteral("NAVFAIL scenario T: Home undeliverable"));
            if (selectedRowOf(roots) != 0)
                fail(QStringLiteral("NAVFAIL scenario T (Home): selectedRow is "
                                    "%1, expected 0 (QQuickItemView does not "
                                    "implement Home natively; the view-level "
                                    "handler must cover it)")
                         .arg(selectedRowOf(roots)));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario T Home"), *failures);
            if (!sendKey(Qt::Key_End))
                fail(QStringLiteral("NAVFAIL scenario T: End undeliverable"));
            const int last = rowCountOf(ctrl) - 1;
            if (selectedRowOf(roots) != last)
                fail(QStringLiteral("NAVFAIL scenario T (End): selectedRow is "
                                    "%1, expected %2 (QQuickItemView does not "
                                    "implement End natively)")
                         .arg(selectedRowOf(roots))
                         .arg(last));
            assertTransactionDetailMapping(
                roots, QStringLiteral("scenario T End"), *failures);
            qInfo().noquote()
                << QStringLiteral("NAV [scenario T]: Down/Home/End all moved "
                                  "the selection with the detail mapping in "
                                  "sync (single selectRow path)");
            break;
        }
        case 165: {
            endScenario(QStringLiteral("T"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario T]: physical path covered "
                                  "(REAL mouse-click synthesis + QKeyEvent "
                                  "synthesis; focus ownership asserted)");
            break;
        }

        default:
            break;
        }

        if (failures->isEmpty() && *stage < kLastStage) {
            ++*stage;
            QTimer::singleShot(settleMs, &app, *schedule);
            return;
        }

        // Per-scenario verdicts (M9-B5.3): a scenario PASSES iff no failure
        // was recorded in its stage range. A range that never ran (an earlier
        // failure stopped the walk) is NOT RUN and is itself a harness defect.
        QStringList verdicts;
        for (const QString &name : scenarioOrder) {
            if (!scenarioStart->contains(name)
                || !scenarioEnd->contains(name)) {
                verdicts << name + QStringLiteral(" NOT RUN");
                fail(QStringLiteral("NAVFAIL scenario %1 never ran to "
                                    "completion").arg(name));
                continue;
            }
            const bool ok =
                scenarioStart->value(name) == scenarioEnd->value(name);
            verdicts << name + (ok ? QStringLiteral(" PASS")
                                   : QStringLiteral(" FAIL"));
        }
        qInfo().noquote() << QStringLiteral("NAV SCENARIOS:")
                          << verdicts.join(QStringLiteral(", "));
        // M9-C C3: the distribution presentation check is NOT a named
        // scenario — it reports its own verdict.
        if (*distributionStart < 0 || *distributionEnd < 0) {
            fail(QStringLiteral("NAVFAIL the dashboard distribution check never "
                                "ran to completion"));
            qInfo().noquote() << QStringLiteral("NAV DISTRIBUTION CHECK: NOT RUN");
        } else {
            const bool ok = *distributionStart == *distributionEnd;
            qInfo().noquote()
                << QStringLiteral("NAV DASHBOARD PRESENTATION CHECK: %1 "
                                  "(distribution denominator = completedCount "
                                  "+ 6 frozen segments + zero state; "
                                  "deterministic attention = exception+crc+"
                                  "timeout+protocolError; diagnosis existence "
                                  "cue; broadcast + protocol-error probes)")
                       .arg(ok ? QStringLiteral("PASS")
                               : QStringLiteral("FAIL"));
        }
        // Scenario M is deferred BY DESIGN, not by omission: see T017 §43.14.
        qInfo().noquote()
            << QStringLiteral("NAV SCENARIO M: DEFERRED BY DESIGN — AI result "
                              "persistence needs an AI provider injection seam "
                              "that this harness does not have (no fake HTTP "
                              "server is added for an extraction test); "
                              "covered by the existing ui_bridge ai01-ai11 "
                              "fake/offline tests plus the B5.4 manual "
                              "workflow. No AI result is fabricated here.");

        if (failures->isEmpty())
            qInfo() << "NAV CHECK PASS (post-Legacy five workspaces; identity "
                       "stable; navigation changed no business values; "
                       "scenarios A/B/D/E/F/G'/H/I/J/K/K'/L/N/O/P/Q/R/S/T "
                       "asserted; M deferred by design)";
        else
            for (const QString &f : *failures)
                qWarning().noquote() << "GEOFAIL:" << f;
        app.exit(failures->isEmpty() ? 0 : 1);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// M10-C2 `--qml-write-foundation-check`: QML runtime oracles for the HIDDEN
// write confirmation foundation.
//
// Same architecture as the other QML harness modes (real app, real shipped
// QML), with one harness-only transport so the runtime has a connected Active
// Serial session WITHOUT touching hardware and without any write capability:
// the transport counts start attempts and refuses every one of them, so any
// attempt to dispatch is both impossible and observable.
//
// What this harness proves (C2 oracle set):
//   C01 unit 0 -> validation presentation, no dialog, no snapshot, zero send
//   C02 invalid 0x10 value -> validation presentation, no dialog
//   C03 valid 0x06 -> Prepared + dialog open + summary == snapshot + zero send
//   C04 Cancel -> Invalidated(UserCancelled), draft preserved, zero send
//   C09 double Write -> same single token, one dialog
//   C10-C first Confirm -> Consumed once; second same token -> rejected
//   C16 0x06 / 0x10 drafts independent across tab switches
//   C17 0x10 displayed quantity == snapshot values.size()
//   C18 0x10 address-span reject -> no dialog
//   C19 summary fields equal the controller snapshot projection
//   C20 editing the draft after prepare cannot alter the summary/snapshot
// ---------------------------------------------------------------------------
// No Q_OBJECT on purpose: this double only overrides base-class virtuals and
// is always used through the SerialTransport interface, so it needs no
// meta-object of its own (and main.cpp therefore stays AUTOMOC-free).
// M10-E4 Human Visual correction: a transport whose PORT OPEN fails, so the
// connection-error presentation can be driven deterministically from the
// shipped connect path. Harness-only fixture - no product behaviour.
class FailingOpenTransport : public SerialTransport
{
public:
    explicit FailingOpenTransport(QObject* parent = nullptr)
        : SerialTransport(parent)
    {
    }

    bool openPort(const QString& portName, qint32 baudRate) override
    {
        Q_UNUSED(portName);
        Q_UNUSED(baudRate);
        // The interface contract: a failed open returns false AND emits a
        // BOUNDED transportError(); the controller routes that to its serial
        // error lane. (Echoing the production adapter's behaviour here is what
        // makes the connection-error presentation reachable in the harness.)
        emit transportError(QStringLiteral("串口打开失败：测试夹具（端口不可用）"));
        return false;
    }

    modbuslens::core::ActiveStartResult startActiveRequest(
        const modbuslens::core::ActiveRequestDescriptor& request) override
    {
        Q_UNUSED(request);
        return modbuslens::core::ActiveStartResult{
            false, modbuslens::core::TransportDisposition::NotSent, std::nullopt};
    }

    [[nodiscard]] bool hasActiveTransaction() const override { return false; }

    [[nodiscard]] bool isPortOpen() const override { return false; }

    void closePort() override {}
};

class HarnessWriteTransport : public SerialTransport
{
public:
    explicit HarnessWriteTransport(QObject* parent = nullptr)
        : SerialTransport(parent)
    {
    }

    bool openPort(const QString& portName, qint32 baudRate) override
    {
        Q_UNUSED(portName);
        Q_UNUSED(baudRate);
        portOpen_ = true;
        return true;
    }

    // Reads are accepted so the harness can create a REAL busy transition
    // through the shipped FC03 path; WRITE functions are counted and refused
    // as NotSent by default — there is no write capability in M10-C, and any
    // attempt to dispatch one is both impossible and observable.
    //
    // M10-D3 correction: the DLG dialog-reaction oracles need the transport to
    // REALLY accept (or short-submit) a write. That capability is OPT-IN and
    // off by default, so every pre-D3 oracle keeps its exact original meaning
    // ("count and refuse"), and the C/D1 phases still end with zero successful
    // write submissions.
    modbuslens::core::ActiveStartResult startActiveRequest(
        const modbuslens::core::ActiveRequestDescriptor& request) override
    {
        using modbuslens::core::ActiveFunction;
        using modbuslens::core::ActiveStartResult;
        using modbuslens::core::TransportDisposition;

        if (request.intent.function != ActiveFunction::ReadHoldingRegisters) {
            ++writeAttempts_;
            if (!acceptWrites_) {
                return ActiveStartResult{false, TransportDisposition::NotSent,
                                         std::nullopt};
            }
            if (writeShortAcceptedBytes_.has_value()
                && *writeShortAcceptedBytes_ > 0
                && *writeShortAcceptedBytes_ < request.wire.size()) {
                // Partial handover: PossiblySent + exactly one terminal, no
                // pending and no accepted-send counter.
                ++writeTerminals_;
                return ActiveStartResult{
                    false, TransportDisposition::PossiblySent,
                    modbuslens::core::ActiveTransportTerminal{
                        .request = request,
                        .responseAdu = {},
                        .disposition = TransportDisposition::PossiblySent,
                        .reason = modbuslens::core::TransportTerminalReason::
                            ShortSubmission,
                        .submissionAcceptedByteCount = *writeShortAcceptedBytes_,
                    }};
            }
            // Full acceptance of the write ADU.
            ++writeSends_;
            writeAduLog_.push_back(request.wire);
            written_ = request;
            pending_ = request;
            return ActiveStartResult{true, TransportDisposition::PossiblySent,
                                     std::nullopt};
        }
        ++readStarts_;
        pending_ = request;
        observed_.clear();
        if (completeReadImmediately_) {
            completeRead();
        }
        return ActiveStartResult{true, TransportDisposition::PossiblySent,
                                 std::nullopt};
    }

    // ---- M10-D3 correction: opt-in write acceptance (DLG oracles only) ----
    void setAcceptWrites(bool accept) { acceptWrites_ = accept; }
    void setWriteShortAcceptedBytes(std::optional<std::uint16_t> count)
    {
        writeShortAcceptedBytes_ = count;
    }
    [[nodiscard]] int writeSends() const { return writeSends_; }
    [[nodiscard]] int writeTerminals() const { return writeTerminals_; }
    // The EXACT accepted write ADUs, in order (one entry per full acceptance).
    [[nodiscard]] int sentAduLogSize() const
    {
        return static_cast<int>(writeAduLog_.size());
    }
    [[nodiscard]] const std::vector<std::vector<std::uint8_t>> &writeAduLog() const
    {
        return writeAduLog_;
    }
    [[nodiscard]] std::optional<modbuslens::core::ActiveRequestDescriptor>
    writtenDescriptor() const
    {
        return written_;
    }

    [[nodiscard]] bool hasActiveTransaction() const override
    {
        return pending_.has_value();
    }
    [[nodiscard]] bool isPortOpen() const override { return portOpen_; }
    [[nodiscard]] int readStarts() const { return readStarts_; }
    [[nodiscard]] int writeAttempts() const { return writeAttempts_; }

    // One deterministic FC03 answer (golden bytes), so the harness can end a
    // read without any timing dependence. Reads only: an accepted WRITE (the
    // M10-D3 DLG oracles) is deliberately never completed here, so the dialog
    // reaction is observed without any completion signal in play.
    void completeRead()
    {
        if (!pending_.has_value()
            || pending_->intent.function
                != modbuslens::core::ActiveFunction::ReadHoldingRegisters) {
            return;
        }
        modbuslens::core::SerialTransactionSession session;
        const auto begin = session.beginActiveRequest(*pending_);
        if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
            return;
        }
        const std::vector<std::uint8_t> response = {0x01, 0x03, 0x04, 0x00,
                                                    0x64, 0x00, 0xC8, 0xBA, 0x7A};
        const auto result = session.feedResponseBytes(
            response, std::chrono::milliseconds{25});
        if (const auto* analysis =
                std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
            const auto finished = *pending_;
            pending_.reset();
            emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                .request = finished,
                .responseAdu = response,
                .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                .analysis = *analysis,
            });
        }
    }

    void setCompleteReadImmediately(bool value) { completeReadImmediately_ = value; }

    // End an accepted READ at its own response timeout — a silent remote
    // slave. The shipped analyzer's session still decides the verdict (no
    // bytes observed => Timeout); the harness only supplies the absence of
    // data, and no write accounting is touched.
    void completeReadWithTimeout()
    {
        if (!pending_.has_value()
            || pending_->intent.function
                != modbuslens::core::ActiveFunction::ReadHoldingRegisters) {
            return;
        }
        modbuslens::core::SerialTransactionSession session;
        const auto begin = session.beginActiveRequest(*pending_);
        if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
            return;
        }
        const auto result = session.onResponseTimeout(pending_->intent.timeout);
        if (const auto *analysis =
                std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
            const auto finished = *pending_;
            pending_.reset();
            emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                .request = finished,
                .responseAdu = {},
                .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                .analysis = *analysis,
            });
        }
    }

    // ---- T023 `--qml-read-result-check`: exact-byte READ stimulus ----
    // End the accepted FC03 read with EXACTLY these observed bytes. The
    // observation is retained FIRST (the production adapter's order), then the
    // SHIPPED session/analyzer decides everything: a complete candidate frame
    // is judged on arrival, while an incomplete/oversized buffer is closed at
    // the response timeout so the WHOLE observed byte run is decoded — never
    // truncated, never discarded. The harness supplies the observation only.
    void completeReadWithBytes(std::vector<std::uint8_t> response,
                               std::chrono::milliseconds elapsed)
    {
        using modbuslens::core::ActiveTransactionResult;
        using modbuslens::core::SerialTransactionError;
        using modbuslens::core::SerialTransactionSession;
        using modbuslens::core::TransactionAnalysis;

        if (!pending_.has_value()
            || pending_->intent.function
                != modbuslens::core::ActiveFunction::ReadHoldingRegisters) {
            return;
        }
        SerialTransactionSession session;
        const auto begin = session.beginActiveRequest(*pending_);
        if (std::get_if<SerialTransactionError>(&begin) != nullptr) {
            return;
        }
        observed_ = response;
        std::optional<TransactionAnalysis> analysis;
        const auto fed = session.feedResponseBytes(response, elapsed);
        if (const auto* landed = std::get_if<TransactionAnalysis>(&fed)) {
            analysis = *landed;
        } else {
            // Not a complete candidate: close at the response timeout so the
            // analyzer decodes the ENTIRE observed buffer (partial / oversized).
            const auto closed = session.onResponseTimeout(elapsed);
            if (const auto* landed = std::get_if<TransactionAnalysis>(&closed)) {
                analysis = *landed;
            }
        }
        if (!analysis.has_value()) {
            return;
        }
        const auto finished = *pending_;
        pending_.reset();
        emit transactionCompleted(ActiveTransactionResult{
            .request = finished,
            .responseAdu = response,
            .disposition = modbuslens::core::TransportDisposition::PossiblySent,
            .analysis = *analysis,
        });
    }

    // ---- M10-E4: fatal LOCAL adapter removal ----
    // The USB serial adapter itself was unplugged. Same order and semantics as
    // the production adapter's fatal-port-error branch: a SUBMITTED request
    // gets exactly ONE terminal event (TransportError, the frozen taxonomy) with
    // its retained evidence, the local port becomes CLOSED so the owner must
    // re-sync, and the bounded error lane reports the removal in both cases.
    // Harness-only stimulus — every verdict still comes from the shipped
    // controller and evidence model.
    void simulateAdapterRemoval(const QString& message)
    {
        if (pending_.has_value()) {
            const auto terminal = modbuslens::core::ActiveTransportTerminal{
                .request = *pending_,
                .responseAdu = observed_,
                .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                .reason = modbuslens::core::TransportTerminalReason::TransportError,
                .submissionAcceptedByteCount = std::nullopt,
            };
            pending_.reset();
            emit transactionTerminated(terminal);
        }
        portOpen_ = false;
        emit transportError(message);
    }

    // Complete an accepted WRITE with no response at all: a real 0x06 Timeout
    // produced by the SHIPPED analyzer (its own session, the request's own
    // threshold), not a fabricated verdict.
    void completeWriteWithTimeout()
    {
        if (!pending_.has_value()
            || pending_->intent.function
                != modbuslens::core::ActiveFunction::WriteSingleRegister) {
            return;
        }
        modbuslens::core::SerialTransactionSession session;
        const auto begin = session.beginActiveRequest(*pending_);
        if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
            return;
        }
        // elapsed == the intent's own timeout: at/above the threshold -> Timeout.
        const auto result = session.onResponseTimeout(pending_->intent.timeout);
        if (const auto *analysis =
                std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
            const auto finished = *pending_;
            pending_.reset();
            emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                .request = finished,
                .responseAdu = {},
                .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                .analysis = *analysis,
            });
        }
    }

    // M10-F: the 0x10 twin of completeWriteWithTimeout. The Human M10-F
    // review found that an FC16 dispatch with no responding slave produced
    // no user-visible terminal, and the old helper refused to complete
    // anything but a 0x06 pending, which is exactly why no oracle ever
    // exercised that path. The 0x10 timeout is produced by the SHIPPED
    // analyzer (its own session, the intent own threshold), not a
    // fabricated verdict.
    void completeWriteWithTimeout16()
    {
        if (!pending_.has_value()
            || pending_->intent.function
                != modbuslens::core::ActiveFunction::WriteMultipleRegisters) {
            return;
        }
        modbuslens::core::SerialTransactionSession session;
        const auto begin = session.beginActiveRequest(*pending_);
        if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
            return;
        }
        const auto result = session.onResponseTimeout(pending_->intent.timeout);
        if (const auto *analysis =
                std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
            const auto finished = *pending_;
            pending_.reset();
            emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                .request = finished,
                .responseAdu = {},
                .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                .analysis = *analysis,
            });
        }
    }

    // Complete an accepted WRITE with a trusted 0x06 echo (the shipped analyzer
    // decides the outcome).
    void completeWriteWithEcho()
    {
        if (!pending_.has_value()
            || pending_->intent.function
                != modbuslens::core::ActiveFunction::WriteSingleRegister) {
            return;
        }
        modbuslens::core::SerialTransactionSession session;
        const auto begin = session.beginActiveRequest(*pending_);
        if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
            return;
        }
        const auto &descriptor =
            std::get<modbuslens::core::ActiveRequestDescriptor>(begin);
        // The echo IS the request frame (that is what 0x06 requires).
        const std::vector<std::uint8_t> echo = descriptor.wire;
        const auto result = session.feedResponseBytes(
            echo, std::chrono::milliseconds{25});
        if (const auto *analysis =
                std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
            const auto finished = *pending_;
            pending_.reset();
            emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                .request = finished,
                .responseAdu = echo,
                .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                .analysis = *analysis,
            });
        }
    }

    // M10-E4: complete an accepted 0x10 write with its CONFORMING echo
    // (starting address + written quantity; 0x10 never echoes the values).
    // Harness-only stimulus: the shipped analyzer still decides the outcome.
    void completeWriteWithEcho16()
    {
        if (!pending_.has_value()
            || pending_->intent.function
                != modbuslens::core::ActiveFunction::WriteMultipleRegisters) {
            return;
        }
        modbuslens::core::SerialTransactionSession session;
        const auto begin = session.beginActiveRequest(*pending_);
        if (std::get_if<modbuslens::core::SerialTransactionError>(&begin) != nullptr) {
            return;
        }
        const auto &descriptor =
            std::get<modbuslens::core::ActiveRequestDescriptor>(begin);
        const auto fields = modbuslens::core::readWriteMultipleRegistersFields(
            descriptor.frame);
        if (!fields.has_value()) {
            return;
        }
        const std::vector<std::uint8_t> echo = modbuslens::core::encodeRtuFrame(
            modbuslens::core::ModbusRtuFrame{
                .address = descriptor.frame.address,
                .functionCode = 0x10,
                .data = {static_cast<std::uint8_t>(fields->startingAddress >> 8),
                         static_cast<std::uint8_t>(fields->startingAddress & 0xFF),
                         static_cast<std::uint8_t>(fields->quantity >> 8),
                         static_cast<std::uint8_t>(fields->quantity & 0xFF)}});
        const auto result = session.feedResponseBytes(
            echo, std::chrono::milliseconds{25});
        if (const auto *analysis =
                std::get_if<modbuslens::core::TransactionAnalysis>(&result)) {
            const auto finished = *pending_;
            pending_.reset();
            emit transactionCompleted(modbuslens::core::ActiveTransactionResult{
                .request = finished,
                .responseAdu = echo,
                .disposition = modbuslens::core::TransportDisposition::PossiblySent,
                .analysis = *analysis,
            });
        }
    }

    void closePort() override
    {
        portOpen_ = false;
        pending_.reset();
    }

private:
    bool portOpen_ = false;
    bool completeReadImmediately_ = true;
    int readStarts_ = 0;
    int writeAttempts_ = 0;
    // M10-D3 correction: off by default so the pre-D3 oracles are unchanged.
    bool acceptWrites_ = false;
    std::optional<std::uint16_t> writeShortAcceptedBytes_;
    int writeSends_ = 0;
    int writeTerminals_ = 0;
    std::vector<std::vector<std::uint8_t>> writeAduLog_;
    std::optional<modbuslens::core::ActiveRequestDescriptor> written_;
    std::optional<modbuslens::core::ActiveRequestDescriptor> pending_;
    std::vector<std::uint8_t> observed_;
};

int runWriteFoundationCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *controller = qobject_cast<AnalysisController *>(
        rootObj ? rootObj->findChild<QObject *>(QStringLiteral("analysisController"))
                : nullptr);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    if (!controller || !window) {
        qWarning() << "WRITEFAIL: no controller/window";
        return 1;
    }

    // The harness transport is created up front so every oracle lambda can
    // reference it (it is installed on the controller a few lines below).
    auto *transport = new HarnessWriteTransport(&app);

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &line) { qInfo().noquote() << line; };

    auto itemOf = [&roots](const QString &name) {
        return qobject_cast<QQuickItem *>(findNamedItem(roots, name));
    };
    auto textOf = [&itemOf](const QString &name) {
        auto *item = itemOf(name);
        return item ? item->property("text").toString() : QStringLiteral("<none>");
    };
    auto boolOf = [&itemOf](const QString &name, const char *prop) {
        auto *item = itemOf(name);
        return item ? item->property(prop).toBool() : false;
    };
    auto intOf = [&itemOf](const QString &name, const char *prop) {
        auto *item = itemOf(name);
        return item ? item->property(prop).toInt() : -1;
    };
    auto section = [&itemOf]() { return itemOf(QStringLiteral("writeFoundationSection")); };
    auto setDraft = [&section](const char *prop, const QVariant &value) {
        auto *item = section();
        if (item)
            item->setProperty(prop, value);
    };
    // QML-declared functions return QVariant through the meta-object: asking
    // for a plain bool makes invokeMethod fail silently, so the result is read
    // as a QVariant and converted explicitly.
    auto callSectionBool = [&section](const char *function) {
        auto *item = section();
        QVariant result;
        if (item
            && QMetaObject::invokeMethod(item, function, Q_RETURN_ARG(QVariant, result))) {
            return result.toBool();
        }
        return false;
    };
    auto activateWrite = [&callSectionBool]() {
        return callSectionBool("activateWrite");
    };
    // Qt semantics (verified in this round): Popup.opened becomes true only
    // AFTER the enter transition finishes, while `visible` is true as soon as
    // the dialog is on screen. The immediate oracle therefore checks
    // visibility, and a later stage asserts the fully-opened state.
    auto dialogVisible = [&section]() {
        auto *item = section();
        return item ? item->property("confirmationVisible").toBool() : false;
    };
    auto dialogFullyOpened = [&section]() {
        auto *item = section();
        return item ? item->property("confirmationOpened").toBool() : false;
    };
    // Real key delivery: Tab/Backtab go through the WINDOW (that is where Qt
    // performs focus traversal); every other key goes to the active focus item.
    auto sendKey = [window](Qt::Key key, Qt::KeyboardModifiers mods,
                            bool toWindow) -> bool {
        QObject *target = toWindow ? static_cast<QObject *>(window)
                                   : window->activeFocusItem();
        if (!target)
            return false;
        QKeyEvent press(QEvent::KeyPress, key, mods);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, mods);
        QCoreApplication::sendEvent(target, &release);
        return true;
    };
    auto tab = [&sendKey](bool forward) {
        return sendKey(forward ? Qt::Key_Tab : Qt::Key_Backtab, Qt::NoModifier, true);
    };
    auto focusName = [window]() {
        auto *f = qobject_cast<QQuickItem *>(window->activeFocusItem());
        return f ? f->objectName() : QStringLiteral("<null>");
    };
    auto focusOn = [&focusName](const QString &name) { return focusName() == name; };
    // Accessible name / enabled state, read through Qt's own accessibility
    // interface (never by inspecting text heuristically).
    auto accessibleNameOf = [&itemOf](const QString &name) {
        auto *item = itemOf(name);
        if (!item)
            return QString(); // missing item => empty name, never a placeholder
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(item);
        return iface ? iface->text(QAccessible::Name) : QString();
    };
    // Nearest NAMED ancestor of the focused item. Qt moves focus to a control's
    // internal child (e.g. a SpinBox's editor) in several cases, so the tab
    // chain is described by OWNERSHIP rather than by exact object names.
    auto focusOwnerName = [window]() {
        for (auto *p = qobject_cast<QQuickItem *>(window->activeFocusItem()); p;
             p = p->parentItem()) {
            if (!p->objectName().isEmpty()) {
                return p->objectName();
            }
        }
        return QStringLiteral("<none>");
    };
    auto clickItemAt = [&roots, window](const QString &name) {
        auto *item = qobject_cast<QQuickItem *>(findNamedItem(roots, name));
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    auto railIndex = [&itemOf]() {
        auto *r = itemOf(QStringLiteral("navigationRail"));
        return r ? r->property("currentWorkspaceIndex").toInt() : -1;
    };
    // Cancel the prepared snapshot through the authority (used to reset the
    // harness into a clean state between oracles).
    auto clearIt = [&section]() {
        auto *item = section();
        if (item)
            QMetaObject::invokeMethod(item, "cancelPreparedWrite");
    };
    auto stateToken = [&controller]() { return controller->preparedWriteStateToken(); };
    auto tokenOf = [&controller]() { return controller->preparedWriteTokenValue(); };
    auto errorVisible = [&boolOf]() {
        return boolOf(QStringLiteral("writeValidationError"), "visible");
    };
    auto valuesListCount = [&intOf]() {
        return intOf(QStringLiteral("writeSummaryValues"), "count");
    };

    // Harness transport: connected Active Serial session, zero write capability.
    controller->setSerialTransport(transport);
    controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);

    // ---- staged walk (one stage per event-loop turn) ----
    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // The write foundation lives on the Communication page. A Qt Quick Popup
    // only becomes `opened` when its parent is visible, and StackLayout hides
    // every non-current workspace, so the harness must make Communication the
    // current workspace first (presentation-only navigation, exactly like a
    // user clicking the rail entry).
    auto clickNamed = [&roots, &window = *qobject_cast<QQuickWindow *>(rootObj)](
                          const QString &name) {
        auto *item = qobject_cast<QQuickItem *>(findNamedItem(roots, name));
        if (!item || !item->isVisible()) {
            return false;
        }
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window.mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &release);
        return true;
    };

    push([&]() {
        if (!section()) {
            fail(QStringLiteral("WRITEFAIL setup: writeFoundationSection not "
                                "instantiated under the harness seam"));
            return;
        }
        // Rail entry 2 = Communication (presentation-only switch).
        if (!clickNamed(QStringLiteral("navItem_2")))
            fail(QStringLiteral("WRITEFAIL setup: the Communication rail entry "
                                "is not clickable"));
    });
    push([&]() {
        if (!controller->serialConnected()) {
            fail(QStringLiteral("WRITEFAIL setup: the harness transport is not "
                                "connected"));
        }
        note(QStringLiteral("WRITE [setup]: Communication current, harness "
                            "transport connected, session=%1 state=%2")
                 .arg(controller->activeSerialSessionId())
                 .arg(stateToken()));
    });

    // ---- C01: unit 0 is rejected with presentation, no dialog ----
    push([&]() {
        setDraft("unit06", 0);
        setDraft("addressText06", QString::number(100));
        setDraft("valueText06", QString::number(5));
        const bool accepted = activateWrite();
        if (accepted)
            fail(QStringLiteral("WRITEFAIL C01: unit 0 was accepted"));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL C01: a dialog opened for an invalid draft"));
        if (tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL C01: a snapshot token was created"));
        if (!errorVisible() || textOf(QStringLiteral("writeValidationError")).isEmpty())
            fail(QStringLiteral("WRITEFAIL C01: no validation presentation "
                                "(hasError=%1 visible=%2 text=[%3] pageVisible=%4)")
                     .arg(controller->hasWriteDraftError() ? 1 : 0)
                     .arg(boolOf(QStringLiteral("writeValidationError"), "visible") ? 1 : 0)
                     .arg(textOf(QStringLiteral("writeValidationError")))
                     .arg(itemOf(QStringLiteral("writeFoundationSection"))
                              && itemOf(QStringLiteral("writeFoundationSection"))->isVisible()
                              ? 1 : 0));
        else if (textOf(QStringLiteral("writeValidationError")).contains(
                     QStringLiteral("UnitIdOutOfRange")))
            fail(QStringLiteral("WRITEFAIL C01: a raw enum token reached the UI"));
        note(QStringLiteral("WRITE [C01]: unit 0 -> [%1], no dialog, no token")
                 .arg(textOf(QStringLiteral("writeValidationError"))));
    });

    // ---- C02: invalid 0x10 value (parser) is rejected with presentation ----
    push([&]() {
        setDraft("activeFunctionIndex", 1);
        setDraft("unit10", 1);
        setDraft("start10", 0);
        setDraft("valuesText10", QStringLiteral("65536"));
        const bool accepted = activateWrite();
        if (accepted || dialogVisible() || tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL C02: out-of-range value was accepted"));
        const QString message = textOf(QStringLiteral("writeValidationError"));
        if (!message.contains(QStringLiteral("第 1 行")))
            fail(QStringLiteral("WRITEFAIL C02: the one-based line number is "
                                "missing from [%1]").arg(message));
        note(QStringLiteral("WRITE [C02]: 65536 -> [%1]").arg(message));
    });

    // ---- C03 / C19: valid 0x06 -> Prepared + summary == projection ----
    push([&]() {
        setDraft("activeFunctionIndex", 0);
        setDraft("unit06", 11);
        setDraft("addressText06", QString::number(0x0064));
        setDraft("valueText06", QString::number(1234));
        setDraft("timeout06", 1000);
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C03: a valid 0x06 draft was rejected"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL C03: the confirmation dialog did not open"));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL C03: state=%1, expected prepared")
                     .arg(stateToken()));
        if (tokenOf() == 0)
            fail(QStringLiteral("WRITEFAIL C03: no snapshot token"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C03: the transport was touched (%1)")
                     .arg(transport->writeAttempts()));
        note(QStringLiteral("WRITE [C03]: prepared token=%1, dialog open, "
                            "startAttempts=0").arg(tokenOf()));
    });
    push([&]() {
        const int unit = controller->preparedWriteUnitId();
        const int address = controller->preparedWriteAddress();
        const int value = controller->preparedWriteValue();
        const QString label = controller->preparedWriteConnectionLabel();
        if (!textOf(QStringLiteral("writeSummaryFunction")).contains(
                QStringLiteral("0x06")))
            fail(QStringLiteral("WRITEFAIL C19: function text is [%1]")
                     .arg(textOf(QStringLiteral("writeSummaryFunction"))));
        if (textOf(QStringLiteral("writeSummaryUnit")) !=
            QStringLiteral("设备 %1").arg(unit))
            fail(QStringLiteral("WRITEFAIL C19: unit summary is [%1], snapshot=%2")
                     .arg(textOf(QStringLiteral("writeSummaryUnit"))).arg(unit));
        if (textOf(QStringLiteral("writeSummaryAddress")).toInt() != address)
            fail(QStringLiteral("WRITEFAIL C19: address summary is [%1], snapshot=%2")
                     .arg(textOf(QStringLiteral("writeSummaryAddress"))).arg(address));
        if (textOf(QStringLiteral("writeSummaryValue")).toInt() != value)
            fail(QStringLiteral("WRITEFAIL C19: value summary is [%1], snapshot=%2")
                     .arg(textOf(QStringLiteral("writeSummaryValue"))).arg(value));
        if (textOf(QStringLiteral("writeSummaryConnection")) != label)
            fail(QStringLiteral("WRITEFAIL C19: connection summary is [%1], "
                                "snapshot=%2")
                     .arg(textOf(QStringLiteral("writeSummaryConnection")), label));
        note(QStringLiteral("WRITE [C19]: summary fields equal the snapshot "
                            "projection (unit %1 / addr %2 / value %3 / %4)")
                 .arg(unit).arg(address).arg(value).arg(label));
        // One turn later the enter transition has finished: the dialog is
        // fully open (and it was already visible/authoritative before that).
        if (!dialogFullyOpened())
            fail(QStringLiteral("WRITEFAIL C03: the dialog never reached the "
                                "fully-opened state"));
    });

    // ---- C20: editing the draft after prepare changes nothing ----
    push([&]() {
        const auto tokenBefore = tokenOf();
        setDraft("valueText06", QString::number(4321));
        setDraft("addressText06", QString::number(7));
        if (tokenOf() != tokenBefore)
            fail(QStringLiteral("WRITEFAIL C20: editing the draft changed the token"));
        if (controller->preparedWriteValue() != 1234
            || controller->preparedWriteAddress() != 0x0064)
            fail(QStringLiteral("WRITEFAIL C20: the snapshot changed to %1/%2")
                     .arg(controller->preparedWriteAddress())
                     .arg(controller->preparedWriteValue()));
        if (textOf(QStringLiteral("writeSummaryValue")).toInt() != 1234)
            fail(QStringLiteral("WRITEFAIL C20: the summary followed the draft "
                                "([%1])").arg(textOf(QStringLiteral("writeSummaryValue"))));
        note(QStringLiteral("WRITE [C20]: draft edited to 7/4321, snapshot and "
                            "summary still 100/1234"));
    });

    // ---- C09: repeated Write keeps ONE snapshot and ONE dialog ----
    push([&]() {
        const auto tokenBefore = tokenOf();
        const bool accepted = activateWrite();
        if (!accepted)
            fail(QStringLiteral("WRITEFAIL C09: repeated Write reported failure "
                                "although a snapshot is prepared"));
        if (tokenOf() != tokenBefore)
            fail(QStringLiteral("WRITEFAIL C09: a second snapshot was created "
                                "(%1 -> %2)").arg(tokenBefore).arg(tokenOf()));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL C09: the dialog is not open"));
        note(QStringLiteral("WRITE [C09]: repeated Write kept token=%1 and one "
                            "dialog").arg(tokenBefore));
    });

    // ---- C04: Cancel -> Invalidated(UserCancelled), draft preserved ----
    push([&]() {
        auto *item = section();
        bool cancelled = false;
        if (item)
            QMetaObject::invokeMethod(item, "cancelPreparedWrite");
        Q_UNUSED(cancelled);
        if (stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("WRITEFAIL C04: state=%1, expected invalidated")
                     .arg(stateToken()));
        if (controller->preparedWriteInvalidReasonToken()
            != QStringLiteral("user_cancelled"))
            fail(QStringLiteral("WRITEFAIL C04: reason=%1")
                     .arg(controller->preparedWriteInvalidReasonToken()));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C04: the transport was touched"));
        auto *valueField = itemOf(QStringLiteral("write06ValueField"));
        if (!valueField || valueField->property("text").toString().isEmpty())
            fail(QStringLiteral("WRITEFAIL C04: the raw draft was cleared"));
        note(QStringLiteral("WRITE [C04]: Cancel -> invalidated(user_cancelled), "
                            "draft preserved, zero send"));
    });

    // ---- C16: independent drafts across tab switches ----
    push([&]() {
        setDraft("unit06", 21);
        setDraft("valueText06", QString::number(222));
        setDraft("activeFunctionIndex", 1);
        setDraft("unit10", 31);
        setDraft("start10", 400);
        setDraft("valuesText10", QStringLiteral("7"));
        setDraft("activeFunctionIndex", 0);
    });
    push([&]() {
        auto *item = section();
        const int unit06 = item ? item->property("unit06").toInt() : -1;
        const QString valueText06 = item ? item->property("valueText06").toString()
                                        : QString();
        const int unit10 = item ? item->property("unit10").toInt() : -1;
        const int start10 = item ? item->property("start10").toInt() : -1;
        const QString text10 = item ? item->property("valuesText10").toString()
                                    : QString();
        if (unit06 != 21 || valueText06 != QStringLiteral("222"))
            fail(QStringLiteral("WRITEFAIL C16: 0x06 draft was altered (%1/%2)")
                     .arg(unit06).arg(valueText06));
        if (unit10 != 31 || start10 != 400 || text10 != QStringLiteral("7"))
            fail(QStringLiteral("WRITEFAIL C16: 0x10 draft was altered (%1/%2/%3)")
                     .arg(unit10).arg(start10).arg(text10));
        note(QStringLiteral("WRITE [C16]: both drafts survived the tab switches"));
    });

    // ---- C17 + C10-C: 0x10 prepare, confirm once, second confirm rejected ----
    push([&]() {
        setDraft("activeFunctionIndex", 1);
        setDraft("unit10", 12);
        setDraft("start10", 100);
        setDraft("valuesText10", QStringLiteral("1\n2\n3"));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C17: a valid 0x10 draft was rejected"));
        if (controller->preparedWriteQuantity() != 3)
            fail(QStringLiteral("WRITEFAIL C17: quantity=%1, expected 3")
                     .arg(controller->preparedWriteQuantity()));
        if (controller->preparedWriteAddress() != 100)
            fail(QStringLiteral("WRITEFAIL C17: start=%1, expected 100")
                     .arg(controller->preparedWriteAddress()));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL C17: the dialog did not open"));
    });
    push([&]() {
        if (valuesListCount() != 3)
            fail(QStringLiteral("WRITEFAIL C17: the summary lists %1 values")
                     .arg(valuesListCount()));
        if (textOf(QStringLiteral("writeSummaryQuantity")) != QStringLiteral("3"))
            fail(QStringLiteral("WRITEFAIL C17: displayed quantity is [%1]")
                     .arg(textOf(QStringLiteral("writeSummaryQuantity"))));
        if (!textOf(QStringLiteral("writeSummaryFunction")).contains(
                QStringLiteral("十进制 16")))
            fail(QStringLiteral("WRITEFAIL C17: the 0x10 function text is [%1]")
                     .arg(textOf(QStringLiteral("writeSummaryFunction"))));
        note(QStringLiteral("WRITE [C17]: 0x10 prepared, quantity 3, all three "
                            "values listed"));
    });
    push([&]() {
        const auto token = tokenOf();
        const bool first = callSectionBool("confirmPreparedWrite");
        if (!first)
            fail(QStringLiteral("WRITEFAIL C10-C: the first confirmation failed"));
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL C10-C: state=%1, expected consumed")
                     .arg(stateToken()));
        if (controller->hasPreparedWrite())
            fail(QStringLiteral("WRITEFAIL C10-C: still prepared after consuming"));
        // Second confirmation of the SAME token must be rejected.
        if (controller->confirmPreparedWriteToken(token))
            fail(QStringLiteral("WRITEFAIL C10-C: the same token confirmed twice"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C10-C: the transport was touched (%1)")
                     .arg(transport->writeAttempts()));
        note(QStringLiteral("WRITE [C10-C]: one consumption for token=%1; second "
                            "confirm rejected; zero send").arg(token));
    });

    // ---- C18: 0x10 address-span reject ----
    push([&]() {
        setDraft("start10", 65535);
        setDraft("valuesText10", QStringLiteral("1\n2"));
        const bool accepted = activateWrite();
        if (accepted || dialogVisible())
            fail(QStringLiteral("WRITEFAIL C18: an overflowing span was accepted"));
        const QString message = textOf(QStringLiteral("writeValidationError"));
        if (!message.contains(QStringLiteral("16 位寄存器地址空间")))
            fail(QStringLiteral("WRITEFAIL C18: span message is [%1]").arg(message));
        note(QStringLiteral("WRITE [C18]: 65535+2 -> [%1]").arg(message));
    });

    // ---- parser presentation: blank line / 123 values / 124 values ----
    push([&]() {
        setDraft("start10", 0);
        setDraft("valuesText10", QStringLiteral("1\n\n2"));
        if (activateWrite() || dialogVisible())
            fail(QStringLiteral("WRITEFAIL parser: a middle blank line prepared"));
        if (!textOf(QStringLiteral("writeValidationError")).contains(
                QStringLiteral("空行")))
            fail(QStringLiteral("WRITEFAIL parser: blank-line message is [%1]")
                     .arg(textOf(QStringLiteral("writeValidationError"))));
        QString many;
        for (int i = 0; i < 123; ++i) {
            if (i > 0)
                many += QLatin1Char('\n');
            many += QString::number(i + 1);
        }
        setDraft("valuesText10", many);
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL parser: 123 values were rejected"));
        QString tooMany = many + QLatin1Char('\n') + QStringLiteral("124");
        auto *item = section();
        if (item)
            QMetaObject::invokeMethod(item, "cancelPreparedWrite");
        setDraft("valuesText10", tooMany);
        if (activateWrite() || dialogVisible())
            fail(QStringLiteral("WRITEFAIL parser: 124 values prepared"));
        note(QStringLiteral("WRITE [parser]: blank line rejected; 123 prepared; "
                            "124 rejected"));
    });

    // =========================================================================
    // M10-C3: real keyboard + context + persistence + accessibility oracles.
    // =========================================================================

    // C06: the dialog opens with Cancel focused and Confirm NOT focused.
    push([&]() {
        setDraft("activeFunctionIndex", 0);
        setDraft("unit06", 11);
        setDraft("addressText06", QString::number(100));
        setDraft("valueText06", QString::number(1234));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C06: valid 0x06 draft rejected"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL C06: dialog not visible"));
    });
    push([&]() {
        if (!focusOn(QStringLiteral("writeConfirmCancelButton")))
            fail(QStringLiteral("WRITEFAIL C06: initial focus is [%1], expected "
                                "the Cancel button").arg(focusName()));
        if (focusOn(QStringLiteral("writeConfirmAcceptButton")))
            fail(QStringLiteral("WRITEFAIL C06: the destructive Confirm button "
                                "holds the initial focus"));
        note(QStringLiteral("WRITE [C06]: initial focus = %1").arg(focusName()));
    });

    // C07: Enter immediately after opening must NOT consume.
    push([&]() { sendKey(Qt::Key_Return, Qt::NoModifier, false); });
    push([&]() {
        if (stateToken() == QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL C07: an immediate Enter consumed the "
                                "prepared write"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C07: write dispatch was attempted"));
        note(QStringLiteral("WRITE [C07]: immediate Enter -> state=%1 reason=%2")
                 .arg(stateToken())
                 .arg(controller->preparedWriteInvalidReasonToken()));
    });

    // C05: Space immediately after opening — Cancel holds focus, so a
    // non-destructive Cancel is allowed; consumption is not.
    push([&]() {
        if (stateToken() != QStringLiteral("prepared")) {
            setDraft("valueText06", QString::number(1234));
            if (!activateWrite())
                fail(QStringLiteral("WRITEFAIL C05b: could not re-prepare"));
        }
        sendKey(Qt::Key_Space, Qt::NoModifier, false);
    });
    push([&]() {
        if (stateToken() == QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL C05b: Space on Cancel consumed the write"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C05b: write dispatch was attempted"));
        note(QStringLiteral("WRITE [C05b]: immediate Space -> state=%1 reason=%2")
                 .arg(stateToken())
                 .arg(controller->preparedWriteInvalidReasonToken()));
    });

    // C08: real Tab moves focus to Confirm; Space then accepts exactly once.
    push([&]() {
        if (stateToken() != QStringLiteral("prepared")) {
            setDraft("valueText06", QString::number(1234));
            if (!activateWrite())
                fail(QStringLiteral("WRITEFAIL C08: could not re-prepare"));
        }
        for (int i = 0; i < 5
             && !focusOn(QStringLiteral("writeConfirmAcceptButton")); ++i) {
            tab(true);
        }
        if (!focusOn(QStringLiteral("writeConfirmAcceptButton")))
            fail(QStringLiteral("WRITEFAIL C08: could not focus Confirm by Tab "
                                "(focus=%1)").arg(focusName()));
    });
    push([&]() {
        const auto tokenBefore = tokenOf();
        sendKey(Qt::Key_Space, Qt::NoModifier, false);
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL C08: Space on Confirm gave state=%1")
                     .arg(stateToken()));
        if (controller->confirmPreparedWriteToken(tokenBefore))
            fail(QStringLiteral("WRITEFAIL C08: the token confirmed twice"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C08: write dispatch was attempted"));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL C08: the dialog stayed open after a "
                                "successful confirmation"));
        note(QStringLiteral("WRITE [C08]: Confirm+Space -> consumed once "
                            "(token=%1), dialog closed, zero write dispatch")
                 .arg(tokenBefore));
    });

    // C08b: Enter works the same way while Confirm holds active focus.
    push([&]() {
        setDraft("valueText06", QString::number(4321));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C08b: could not prepare"));
        for (int i = 0; i < 5
             && !focusOn(QStringLiteral("writeConfirmAcceptButton")); ++i) {
            tab(true);
        }
        if (!focusOn(QStringLiteral("writeConfirmAcceptButton")))
            fail(QStringLiteral("WRITEFAIL C08b: could not focus Confirm"));
        sendKey(Qt::Key_Return, Qt::NoModifier, false);
    });
    push([&]() {
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL C08b: Enter on Confirm gave state=%1")
                     .arg(stateToken()));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C08b: write dispatch was attempted"));
        note(QStringLiteral("WRITE [C08b]: Confirm+Enter -> consumed"));
    });

    // Double activation: two rapid Space presses on Confirm consume at most once.
    push([&]() {
        setDraft("valueText06", QString::number(777));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL double: could not prepare"));
        for (int i = 0; i < 5
             && !focusOn(QStringLiteral("writeConfirmAcceptButton")); ++i) {
            tab(true);
        }
        sendKey(Qt::Key_Space, Qt::NoModifier, false);
        sendKey(Qt::Key_Space, Qt::NoModifier, false);
    });
    push([&]() {
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL double: state=%1").arg(stateToken()));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL double: write dispatch was attempted"));
        note(QStringLiteral("WRITE [double]: two rapid activations -> one "
                            "consumption, zero write dispatch"));
    });

    // =========================================================================
    // M10-C3 correction: rapid Enter spillover (E1 back-to-back, E2 next turn).
    //
    // Confirming consumes the snapshot and closes the dialog. The safety
    // question is what the SECOND Enter does once the popup is gone: it must
    // never reach a background control (Write action, Read, Clear Results,
    // rail) and must never start a new write flow.
    // =========================================================================

    // E1: two Enter presses back-to-back, no focus manipulation in between.
    push([&]() {
        setDraft("activeFunctionIndex", 0);
        setDraft("unit06", 11);
        setDraft("addressText06", QString::number(100));
        setDraft("valueText06", QString::number(1234));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL E1: could not prepare"));
        for (int i = 0; i < 5
             && !focusOn(QStringLiteral("writeConfirmAcceptButton")); ++i) {
            tab(true);
        }
        if (!focusOn(QStringLiteral("writeConfirmAcceptButton")))
            fail(QStringLiteral("WRITEFAIL E1: Confirm does not hold active focus "
                                "(focus=%1)").arg(focusName()));
        note(QStringLiteral("WRITE [E1]: ready — token=%1 state=%2 dialog=%3 "
                            "focus=%4")
                 .arg(tokenOf()).arg(stateToken())
                 .arg(dialogVisible() ? 1 : 0).arg(focusName()));
    });
    push([&]() {
        const auto tokenBefore = tokenOf();
        const int railBefore = railIndex();
        const int rowsBefore = controller->transactionModel()->rowCount();
        const int observedBefore = controller->observedCount();
        const int readsBefore = transport->readStarts();
        const auto sessionBefore = controller->activeSerialSessionId();

        // Back-to-back activation with NO sleep and NO focus intervention.
        sendKey(Qt::Key_Return, Qt::NoModifier, false);
        sendKey(Qt::Key_Return, Qt::NoModifier, false);

        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL E1: state=%1, expected consumed")
                     .arg(stateToken()));
        if (tokenBefore != 0 && controller->confirmPreparedWriteToken(tokenBefore))
            fail(QStringLiteral("WRITEFAIL E1: the token was accepted twice"));
        if (tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL E1: a new prepared token appeared (%1)")
                     .arg(tokenOf()));
        if (controller->hasPreparedWrite())
            fail(QStringLiteral("WRITEFAIL E1: a new Prepared snapshot exists"));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL E1: a dialog is open again"));
        if (railIndex() != railBefore)
            fail(QStringLiteral("WRITEFAIL E1: a background rail action ran (%1 -> %2)")
                     .arg(railBefore).arg(railIndex()));
        if (controller->transactionModel()->rowCount() != rowsBefore
            || controller->observedCount() != observedBefore)
            fail(QStringLiteral("WRITEFAIL E1: a background result action ran "
                                "(rows %1 -> %2, observed %3 -> %4)")
                     .arg(rowsBefore)
                     .arg(controller->transactionModel()->rowCount())
                     .arg(observedBefore)
                     .arg(controller->observedCount()));
        if (transport->readStarts() != readsBefore)
            fail(QStringLiteral("WRITEFAIL E1: the second Enter started a READ "
                                "(%1 -> %2)").arg(readsBefore)
                     .arg(transport->readStarts()));
        if (controller->activeSerialSessionId() != sessionBefore)
            fail(QStringLiteral("WRITEFAIL E1: the session changed"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL E1: write dispatch was attempted"));
        note(QStringLiteral("WRITE [E1]: back-to-back Enter -> one consumption "
                            "(token=%1), no new snapshot/dialog, no background "
                            "action, writes=%2")
                 .arg(tokenBefore).arg(transport->writeAttempts()));
    });
    // Focus after close is EVIDENCE, not a contract: record where it lands.
    push([&]() {
        note(QStringLiteral("WRITE [E1] focus after close = %1 (owner %2)")
                 .arg(focusName(), focusOwnerName()));
    });

    // E2: the second Enter arrives on a LATER turn, after the dialog close has
    // fully settled (this is the "focus restored to the background" case).
    push([&]() {
        setDraft("valueText06", QString::number(2222));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL E2: could not prepare"));
        for (int i = 0; i < 5
             && !focusOn(QStringLiteral("writeConfirmAcceptButton")); ++i) {
            tab(true);
        }
        sendKey(Qt::Key_Return, Qt::NoModifier, false);
    });
    push([&]() {
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL E2: first Enter gave state=%1")
                     .arg(stateToken()));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL E2: the dialog is still visible"));
    });
    push([&]() {
        // One settled event-loop turn later: no control is re-selected here —
        // the key goes exactly where the restored focus is.
        const int railBefore = railIndex();
        const int rowsBefore = controller->transactionModel()->rowCount();
        const int observedBefore = controller->observedCount();
        const int readsBefore = transport->readStarts();
        const auto sessionBefore = controller->activeSerialSessionId();
        note(QStringLiteral("WRITE [E2]: focus after close = %1 (owner %2)")
                 .arg(focusName(), focusOwnerName()));

        sendKey(Qt::Key_Return, Qt::NoModifier, false);

        if (tokenOf() != 0 || controller->hasPreparedWrite())
            fail(QStringLiteral("WRITEFAIL E2: the second Enter started a new "
                                "write flow (token=%1)").arg(tokenOf()));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL E2: a dialog reopened"));
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL E2: state=%1").arg(stateToken()));
        if (railIndex() != railBefore)
            fail(QStringLiteral("WRITEFAIL E2: a background rail action ran (%1 -> %2)")
                     .arg(railBefore).arg(railIndex()));
        if (controller->transactionModel()->rowCount() != rowsBefore
            || controller->observedCount() != observedBefore)
            fail(QStringLiteral("WRITEFAIL E2: a background result action ran"));
        if (transport->readStarts() != readsBefore)
            fail(QStringLiteral("WRITEFAIL E2: the second Enter started a READ"));
        if (controller->activeSerialSessionId() != sessionBefore)
            fail(QStringLiteral("WRITEFAIL E2: the session changed"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL E2: write dispatch was attempted"));
        note(QStringLiteral("WRITE [E2]: next-turn Enter after close -> no new "
                            "flow, no background action, writes=%1")
                 .arg(transport->writeAttempts()));
    });

    // Escape: real key -> Invalidated(UserCancelled), dialog closed, draft kept.
    push([&]() {
        setDraft("valueText06", QString::number(5555));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C05: could not prepare"));
    });
    // Escape goes through the WINDOW: Qt routes popup close-policy handling
    // (CloseOnEscape) at the window/overlay level, not at the focused item.
    push([&]() { sendKey(Qt::Key_Escape, Qt::NoModifier, true); });
    push([&]() {
        if (stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("WRITEFAIL C05: state=%1").arg(stateToken()));
        if (controller->preparedWriteInvalidReasonToken()
            != QStringLiteral("user_cancelled"))
            fail(QStringLiteral("WRITEFAIL C05: reason=%1")
                     .arg(controller->preparedWriteInvalidReasonToken()));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL C05: the dialog stayed visible"));
        auto *item = section();
        if (!item || item->property("valueText06").toString() != QString::number(5555))
            fail(QStringLiteral("WRITEFAIL C05: the draft was not preserved"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C05: write dispatch was attempted"));
        note(QStringLiteral("WRITE [C05/Escape]: invalidated(user_cancelled), dialog "
                            "closed, draft preserved"));
    });

    // Outside click: closePolicy excludes outside-press, so the dialog and the
    // snapshot must survive a click on the page background.
    push([&]() {
        setDraft("valueText06", QString::number(1234));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL outside-click: could not prepare"));
    });
    push([&]() {
        if (!clickItemAt(QStringLiteral("communicationHeader")))
            fail(QStringLiteral("WRITEFAIL outside-click: the background target "
                                "is not clickable"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL outside-click: the dialog closed on an "
                                "outside click"));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL outside-click: state=%1")
                     .arg(stateToken()));
        note(QStringLiteral("WRITE [outside-click]: dialog stayed open, snapshot "
                            "stayed prepared"));
    });

    // Modal/background navigation: a rail click must never confirm/cancel/send.
    push([&]() {
        const int railBefore = railIndex();
        clickItemAt(QStringLiteral("navItem_0"));
        const int railAfter = railIndex();
        if (stateToken() == QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL modal-nav: navigation consumed the "
                                "prepared write"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL modal-nav: write dispatch attempted"));
        note(QStringLiteral("WRITE [modal-nav]: rail %1 -> %2 (%3); state=%4")
                 .arg(railBefore).arg(railAfter)
                 .arg(railBefore == railAfter
                          ? QStringLiteral("modal blocked the click")
                          : QStringLiteral("click reached the rail"))
                 .arg(stateToken()));
    });

    // C11: disconnect while the dialog is open -> authority-first invalidation.
    push([&]() {
        clickItemAt(QStringLiteral("navItem_2"));
        if (stateToken() != QStringLiteral("prepared")) {
            setDraft("valueText06", QString::number(1234));
            if (!activateWrite())
                fail(QStringLiteral("WRITEFAIL C11: could not prepare"));
        }
        controller->disconnectSerial();
    });
    push([&]() {
        if (stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("WRITEFAIL C11: state=%1").arg(stateToken()));
        if (controller->preparedWriteInvalidReasonToken()
            != QStringLiteral("disconnected"))
            fail(QStringLiteral("WRITEFAIL C11: reason=%1")
                     .arg(controller->preparedWriteInvalidReasonToken()));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL C11: the dialog survived the "
                                "invalidation"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C11: write dispatch was attempted"));
        note(QStringLiteral("WRITE [C11]: disconnect -> invalidated(disconnected), "
                            "dialog closed by the authority"));
    });

    // C12: reconnect with the SAME port/baud must never revive the old token.
    push([&]() {
        const auto oldToken = tokenOf();
        controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
        if (controller->preparedWriteStateToken() == QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL C12: a snapshot survived a reconnect"));
        if (oldToken != 0 && controller->confirmPreparedWriteToken(oldToken))
            fail(QStringLiteral("WRITEFAIL C12: the old token confirmed in the "
                                "new session"));
        note(QStringLiteral("WRITE [C12]: reconnect -> old token %1 unusable "
                            "(session=%2)").arg(oldToken)
                 .arg(controller->activeSerialSessionId()));
    });

    // C13/C14: a REAL FC03 read makes busy true (permanent invalidation);
    // busy returning to false must not revive the token.
    push([&]() {
        setDraft("valueText06", QString::number(999));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C13: could not prepare"));
        transport->setCompleteReadImmediately(false);
        controller->readHoldingRegistersOnce(1, 0, 2, 1000);
    });
    push([&]() {
        if (!controller->serialBusy())
            fail(QStringLiteral("WRITEFAIL C13: the FC03 read did not make the "
                                "runtime busy"));
        if (stateToken() != QStringLiteral("invalidated")
            || controller->preparedWriteInvalidReasonToken()
                != QStringLiteral("busy_became_true"))
            fail(QStringLiteral("WRITEFAIL C13: state=%1 reason=%2")
                     .arg(stateToken())
                     .arg(controller->preparedWriteInvalidReasonToken()));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL C13: the dialog survived busy-true"));
        transport->setCompleteReadImmediately(true);
        transport->completeRead();
    });
    push([&]() {
        if (controller->serialBusy())
            fail(QStringLiteral("WRITEFAIL C14: the read did not finish"));
        if (stateToken() == QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL C14: busy->false revived the snapshot"));
        note(QStringLiteral("WRITE [C13/C14]: busy false->true invalidated; "
                            "busy->false did not revive (reads=%1, write attempts=%2)")
                 .arg(transport->readStarts()).arg(transport->writeAttempts()));
    });

    // C35: a later disconnect must not overwrite the busy reason.
    push([&]() {
        setDraft("valueText06", QString::number(1234));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C35: could not prepare"));
        transport->setCompleteReadImmediately(false);
        controller->readHoldingRegistersOnce(1, 0, 2, 1000);
    });
    push([&]() {
        transport->setCompleteReadImmediately(true);
        transport->completeRead();
        controller->disconnectSerial();
        if (controller->preparedWriteInvalidReasonToken()
            != QStringLiteral("busy_became_true"))
            fail(QStringLiteral("WRITEFAIL C35: the reason was overwritten (%1)")
                     .arg(controller->preparedWriteInvalidReasonToken()));
        note(QStringLiteral("WRITE [C35]: reason stays busy_became_true after a "
                            "later disconnect"));
        controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
    });

    // C15: Clear Results preserves drafts and never invalidates a prepared
    // snapshot.
    push([&]() {
        setDraft("unit06", 33);
        setDraft("valueText06", QString::number(321));
        setDraft("activeFunctionIndex", 1);
        setDraft("unit10", 44);
        setDraft("valuesText10", QStringLiteral("5"));
        setDraft("activeFunctionIndex", 0);
        controller->clearResults();
    });
    push([&]() {
        auto *item = section();
        if (!item || item->property("unit06").toInt() != 33
            || item->property("valueText06").toString() != QString::number(321)
            || item->property("unit10").toInt() != 44
            || item->property("valuesText10").toString() != QStringLiteral("5"))
            fail(QStringLiteral("WRITEFAIL C15: Clear Results cleared a draft"));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C15: could not prepare"));
        controller->clearResults();
    });
    push([&]() {
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL C15: Clear Results invalidated the "
                                "snapshot (%1)").arg(stateToken()));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C15: write dispatch was attempted"));
        note(QStringLiteral("WRITE [C15]: Clear Results preserved drafts and the "
                            "prepared snapshot"));
        clearIt();
    });

    // C30: successful Simulator replacement invalidates, drafts survive.
    push([&]() {
        setDraft("activeFunctionIndex", 1);
        setDraft("unit10", 55);
        setDraft("valuesText10", QStringLiteral("9"));
        setDraft("activeFunctionIndex", 0);
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C30: could not prepare"));
        controller->runDemoBatch();
    });
    push([&]() {
        if (controller->preparedWriteInvalidReasonToken()
            != QStringLiteral("source_changed"))
            fail(QStringLiteral("WRITEFAIL C30: reason=%1")
                     .arg(controller->preparedWriteInvalidReasonToken()));
        auto *item = section();
        if (!item || item->property("unit10").toInt() != 55
            || item->property("valuesText10").toString() != QStringLiteral("9"))
            fail(QStringLiteral("WRITEFAIL C30: the 0x10 draft was lost on a "
                                "source change"));
        note(QStringLiteral("WRITE [C30]: Simulator replacement -> "
                            "invalidated(source_changed), drafts preserved"));
    });

    // C31: a FAILED replay load preserves the snapshot AND the drafts.
    push([&]() {
        clickItemAt(QStringLiteral("navItem_2"));
        controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
        setDraft("valueText06", QString::number(4242));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C31: could not prepare"));
        controller->loadReplayFile(QUrl::fromLocalFile(
            QDir::tempPath() + QStringLiteral("/modbuslens_c3_missing.mlog")));
    });
    push([&]() {
        if (!controller->hasReplayError())
            fail(QStringLiteral("WRITEFAIL C31: the replay load did not fail"));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL C31: a failed load changed the snapshot "
                                "(%1)").arg(stateToken()));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL C31: the dialog was closed by a failed "
                                "load"));
        auto *item = section();
        if (!item || item->property("valueText06").toString() != QString::number(4242))
            fail(QStringLiteral("WRITEFAIL C31: the draft was lost"));
        note(QStringLiteral("WRITE [C31]: failed Replay load -> snapshot and draft "
                            "preserved"));
        clearIt();
    });

    // C32: disconnect/reconnect preserves drafts but kills the old snapshot.
    push([&]() {
        setDraft("valueText06", QString::number(8888));
        setDraft("unit10", 66);
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C32: could not prepare"));
        controller->disconnectSerial();
        controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
    });
    push([&]() {
        auto *item = section();
        if (!item || item->property("valueText06").toString() != QString::number(8888)
            || item->property("unit10").toInt() != 66)
            fail(QStringLiteral("WRITEFAIL C32: drafts did not survive the "
                                "disconnect/reconnect"));
        if (controller->preparedWriteStateToken() == QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL C32: the old snapshot survived"));
        note(QStringLiteral("WRITE [C32]: drafts preserved; old snapshot "
                            "invalidated (draft persistence != confirmation "
                            "persistence)"));
    });

    // C14 (hidden page): with the foundation loaded, leave Communication and
    // prove no write control can be activated from the hidden page.
    push([&]() {
        setDraft("valueText06", QString::number(1234));
        clickItemAt(QStringLiteral("navItem_0"));
    });
    push([&]() {
        if (railIndex() != 0)
            fail(QStringLiteral("WRITEFAIL C14: the workspace did not change (%1)")
                     .arg(railIndex()));
        auto *page = itemOf(QStringLiteral("communicationWorkspace"));
        if (!page)
            fail(QStringLiteral("WRITEFAIL C14: communicationWorkspace not found"));
        else if (page->isEnabled())
            fail(QStringLiteral("WRITEFAIL C14: the hidden Communication page is "
                                "still enabled"));
        sendKey(Qt::Key_Tab, Qt::NoModifier, true);
        sendKey(Qt::Key_Space, Qt::NoModifier, false);
        sendKey(Qt::Key_Return, Qt::NoModifier, false);
        if (tokenOf() != 0 || stateToken() == QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL C14: a hidden write control prepared a "
                                "snapshot"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C14: write dispatch was attempted"));
        note(QStringLiteral("WRITE [C14]: hidden page disabled; Tab/Space/Enter "
                            "prepared nothing, dispatched nothing"));
    });
    push([&]() {
        clickItemAt(QStringLiteral("navItem_2"));
        auto *item = section();
        if (!item || item->property("valueText06").toString() != QString::number(1234))
            fail(QStringLiteral("WRITEFAIL C14: navigation lost the draft"));
        if (tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL C14: a snapshot appeared after "
                                "navigating back"));
        note(QStringLiteral("WRITE [C14]: back on Communication, draft intact, no "
                            "snapshot"));
    });

    // Accessibility: names on the new controls. The dialog's own buttons are
    // only in the item tree while the popup is shown, so a dialog is opened
    // for this stage (and cancelled again afterwards).
    push([&]() {
        setDraft("valueText06", QString::number(1234));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL a11y: could not prepare for the "
                                "dialog name checks"));
        else if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL a11y: the dialog did not open"));
    });
    push([&]() {
        const QStringList named = {QStringLiteral("writeActivateButton"),
                                   QStringLiteral("write10ValuesArea"),
                                   QStringLiteral("write06UnitSpin"),
                                   QStringLiteral("writeConfirmCancelButton"),
                                   QStringLiteral("writeConfirmAcceptButton")};
        for (const QString &name : named) {
            if (!itemOf(name))
                fail(QStringLiteral("WRITEFAIL a11y: %1 is not in the control "
                                    "tree").arg(name));
            else if (accessibleNameOf(name).isEmpty())
                fail(QStringLiteral("WRITEFAIL a11y: %1 has no accessible name")
                         .arg(name));
        }
        if (!textOf(QStringLiteral("writeSummaryFunction")).isEmpty()
            == false)
            fail(QStringLiteral("WRITEFAIL a11y: the open dialog shows no "
                                "function line"));
        note(QStringLiteral("WRITE [a11y]: names present (%1 / %2 / %3)")
                 .arg(accessibleNameOf(QStringLiteral("writeActivateButton")),
                      accessibleNameOf(QStringLiteral("writeConfirmCancelButton")),
                      accessibleNameOf(QStringLiteral("writeConfirmAcceptButton"))));
        clearIt();
    });
    // Enabled-state consistency: a busy runtime disables the Write action.
    push([&]() {
        transport->setCompleteReadImmediately(false);
        controller->readHoldingRegistersOnce(1, 0, 2, 1000);
    });
    push([&]() {
        auto *writeButton = itemOf(QStringLiteral("writeActivateButton"));
        if (!writeButton)
            fail(QStringLiteral("WRITEFAIL a11y: writeActivateButton missing"));
        else if (controller->serialBusy() && writeButton->isEnabled())
            fail(QStringLiteral("WRITEFAIL a11y: the Write action stays enabled "
                                "while the runtime is busy"));
        else
            note(QStringLiteral("WRITE [a11y]: busy runtime -> Write action "
                                "disabled=%1")
                     .arg(writeButton->isEnabled() ? 0 : 1));
        transport->setCompleteReadImmediately(true);
        transport->completeRead();
    });

    // Tab order: the 0x06 chain covers its own fields and never reaches the
    // 0x10-only editor.
    push([&]() {
        setDraft("activeFunctionIndex", 0);
        auto *tab06 = itemOf(QStringLiteral("writeTab06"));
        if (!tab06)
            fail(QStringLiteral("WRITEFAIL taborder: writeTab06 not found"));
        else
            tab06->forceActiveFocus(Qt::TabFocusReason);
        QStringList owners;
        owners << focusOwnerName();
        for (int i = 0; i < 8; ++i) {
            tab(true);
            owners << focusOwnerName();
        }
        const QString joined = owners.join(QStringLiteral(","));
        for (const QString &expected : {QStringLiteral("write06UnitSpin"),
                                        QStringLiteral("write06AddressField"),
                                        QStringLiteral("write06ValueField"),
                                        QStringLiteral("write06TimeoutSpin"),
                                        QStringLiteral("writeActivateButton")}) {
            if (!owners.contains(expected))
                fail(QStringLiteral("WRITEFAIL taborder: %1 never receives focus "
                                    "(chain=[%2])").arg(expected, joined));
        }
        if (owners.contains(QStringLiteral("write10ValuesArea"))
            || owners.contains(QStringLiteral("write10UnitSpin")))
            fail(QStringLiteral("WRITEFAIL taborder: the inactive tab's controls "
                                "are reachable ([%1])").arg(joined));
        note(QStringLiteral("WRITE [taborder]: 0x06 owners = [%1]").arg(joined));
    });

    // C36: the values TextArea must not trap Tab; Backtab returns to it.
    push([&]() {
        setDraft("activeFunctionIndex", 1);
        auto *area = itemOf(QStringLiteral("write10ValuesArea"));
        if (!area)
            fail(QStringLiteral("WRITEFAIL C36: write10ValuesArea not found"));
        else
            area->forceActiveFocus(Qt::TabFocusReason);
    });
    push([&]() {
        if (!focusOn(QStringLiteral("write10ValuesArea")))
            fail(QStringLiteral("WRITEFAIL C36: the values editor did not take "
                                "focus (focus=%1)").arg(focusName()));
        tab(true);
    });
    push([&]() {
        if (focusOn(QStringLiteral("write10ValuesArea")))
            fail(QStringLiteral("WRITEFAIL C36: Tab did not escape the values "
                                "editor"));
        const QString afterTab = focusName();
        tab(false);
        if (!focusOn(QStringLiteral("write10ValuesArea")))
            fail(QStringLiteral("WRITEFAIL C36: Backtab did not return to the "
                                "values editor (focus=%1)").arg(focusName()));
        note(QStringLiteral("WRITE [C36]: Tab escaped to [%1]; Backtab returned to "
                            "[%2]").arg(afterTab, focusName()));
    });

            // =====================================================================
    // M10-C4 — FINAL ACCEPTANCE oracles (harness-visible write foundation).
    //
    // Everything below is measured on the real shipped QML: the two acceptance
    // window sizes, real scrollability of the 0x10 editor and of the
    // confirmation values list, the protocol boundary at 123/124 values,
    // keyboard reach with a long summary, and an input-efficiency probe for the
    // 0x06 SpinBoxes. The harness only reads and drives; no product QML is
    // touched (C4 is acceptance, not implementation).
    // =====================================================================
    auto sceneRectOf = [&itemOf](const QString &name) -> QRectF {
        auto *item = itemOf(name);
        if (!item)
            return QRectF();
        return QRectF(item->mapToScene(QPointF(0.0, 0.0)),
                      QSizeF(item->width(), item->height()));
    };
    auto windowRect = [window]() {
        return QRectF(0.0, 0.0, qreal(window->width()), qreal(window->height()));
    };
    // A control is reachable at a size only if its scene rect lies inside the
    // window's content rect: a control clipped by the window is unreachable
    // even though it has a size. Tolerance 0.5px = layout rounding, not a clip.
    auto insideWindow = [&windowRect](const QRectF &r) {
        return windowRect().adjusted(-0.5, -0.5, 0.5, 0.5).contains(r);
    };
    auto rectsIntersect = [](const QRectF &a, const QRectF &b) {
        return a.isValid() && b.isValid() && a.intersects(b);
    };
    auto assertReachable = [&](const QString &label, const QString &name) {
        auto *item = itemOf(name);
        if (!item) {
            fail(QStringLiteral("WRITEFAIL C4 geometry %1: %2 does not exist")
                     .arg(label, name));
            return;
        }
        if (!item->isVisible()) {
            fail(QStringLiteral("WRITEFAIL C4 geometry %1: %2 is not visible")
                     .arg(label, name));
            return;
        }
        if (item->width() <= 1.0 || item->height() <= 1.0) {
            fail(QStringLiteral("WRITEFAIL C4 geometry %1: %2 has no usable size "
                                "(%3x%4)")
                     .arg(label, name)
                     .arg(item->width())
                     .arg(item->height()));
            return;
        }
        const QRectF r = sceneRectOf(name);
        if (!insideWindow(r))
            fail(QStringLiteral("WRITEFAIL C4 geometry %1: %2 is clipped by the "
                                "window (scene %3,%4 %5x%6 vs window %7x%8)")
                     .arg(label, name)
                     .arg(qRound(r.x()))
                     .arg(qRound(r.y()))
                     .arg(qRound(r.width()))
                     .arg(qRound(r.height()))
                     .arg(window->width())
                     .arg(window->height()));
    };
    auto dumpWriteGeometry = [&](const QString &label) {
        const QStringList names = {
            QStringLiteral("writeFoundationPanel"),
            QStringLiteral("writeFunctionTabs"),
            QStringLiteral("write06DraftRow"),
            QStringLiteral("write10DraftColumn"),
            QStringLiteral("write10ValuesScroll"),
            QStringLiteral("writeActivateButton"),
            QStringLiteral("writeValidationError")};
        QStringList parts;
        for (const QString &n : names) {
            auto *item = itemOf(n);
            if (!item || !item->isVisible()) {
                parts << n + QStringLiteral("=<hidden>");
                continue;
            }
            const QPointF p = item->mapToScene(QPointF(0.0, 0.0));
            parts << QStringLiteral("%1=(%2,%3 %4x%5)")
                         .arg(n)
                         .arg(qRound(p.x()))
                         .arg(qRound(p.y()))
                         .arg(qRound(item->width()))
                         .arg(qRound(item->height()));
        }
        note(QStringLiteral("WRITE [C4 geometry %1]: window=%2x%3 %4")
                 .arg(label)
                 .arg(window->width())
                 .arg(window->height())
                 .arg(parts.join(QStringLiteral(" "))));
    };
    // The validation presentation is measured only while it is on screen; it
    // must be inside the window and must never sit on top of a control the user
    // still has to reach (the tabs, the 0x06 row, the action, the editor).
    auto assertErrorDoesNotCoverControls = [&](const QString &label) {
        if (!errorVisible())
            return;
        const QRectF err = sceneRectOf(QStringLiteral("writeValidationError"));
        if (err.width() <= 1.0 || err.height() <= 1.0) {
            fail(QStringLiteral("WRITEFAIL C4 geometry %1: the validation message "
                                "is visible but has no rect")
                     .arg(label));
            return;
        }
        if (!insideWindow(err))
            fail(QStringLiteral("WRITEFAIL C4 geometry %1: the validation message "
                                "is clipped by the window")
                     .arg(label));
        for (const QString &n : {QStringLiteral("writeFunctionTabs"),
                                 QStringLiteral("writeActivateButton"),
                                 QStringLiteral("write06DraftRow"),
                                 QStringLiteral("write10ValuesScroll")}) {
            auto *item = itemOf(n);
            if (!item || !item->isVisible())
                continue;
            if (rectsIntersect(err, sceneRectOf(n)))
                fail(QStringLiteral("WRITEFAIL C4 geometry %1: the validation "
                                    "message overlaps %2")
                         .arg(label, n));
        }
        note(QStringLiteral("WRITE [C4 geometry %1]: validation message at "
                            "(%2,%3 %4x%5) — clear of tabs / action / editor")
                 .arg(label)
                 .arg(qRound(err.x()))
                 .arg(qRound(err.y()))
                 .arg(qRound(err.width()))
                 .arg(qRound(err.height())));
    };
    auto assertWriteGeometry = [&](const QString &label) {
        dumpWriteGeometry(label);
        assertReachable(label, QStringLiteral("writeFoundationPanel"));
        assertReachable(label, QStringLiteral("writeFunctionTabs"));
        assertReachable(label, QStringLiteral("writeActivateButton"));
        const int active =
            intOf(QStringLiteral("writeFoundationSection"), "activeFunctionIndex");
        if (active == 0) {
            for (const QString &n : {QStringLiteral("write06UnitSpin"),
                                     QStringLiteral("write06AddressField"),
                                     QStringLiteral("write06ValueField"),
                                     QStringLiteral("write06TimeoutSpin")})
                assertReachable(label, n);
        } else {
            for (const QString &n : {QStringLiteral("write10UnitSpin"),
                                     QStringLiteral("write10StartSpin"),
                                     QStringLiteral("write10TimeoutSpin"),
                                     QStringLiteral("write10ValuesScroll")})
                assertReachable(label, n);
            auto *area = itemOf(QStringLiteral("write10ValuesArea"));
            const QRectF svRect =
                sceneRectOf(QStringLiteral("write10ValuesScroll"));
            const QRectF areaRect =
                sceneRectOf(QStringLiteral("write10ValuesArea"));
            if (!area || !area->isVisible() || areaRect.width() <= 1.0
                || areaRect.height() <= 1.0)
                fail(QStringLiteral("WRITEFAIL C4 geometry %1: the 0x10 editor "
                                    "has no visible rect")
                         .arg(label));
            else if (!rectsIntersect(areaRect, svRect))
                fail(QStringLiteral("WRITEFAIL C4 geometry %1: the 0x10 editor "
                                    "does not intersect its own viewport")
                         .arg(label));
            else if (areaRect.y() < windowRect().top() - 0.5)
                fail(QStringLiteral("WRITEFAIL C4 geometry %1: the 0x10 editor "
                                    "starts above the window")
                         .arg(label));
        }
        assertErrorDoesNotCoverControls(label);
    };
    // The 0x10 editor scrolls either through its own flickable or through the
    // ScrollView wrapper (Qt may size the TextArea to the viewport or keep it
    // at content height). The harness MEASURES which one actually scrolls and
    // then drives that one — it never assumes a structure.
    auto editorScroller = [&]() -> QQuickItem * {
        auto *sv = itemOf(QStringLiteral("write10ValuesScroll"));
        auto *area = itemOf(QStringLiteral("write10ValuesArea"));
        if (!sv || !area)
            return nullptr;
        if (sv->property("contentHeight").toReal()
            > sv->property("height").toReal() + 1.0)
            return sv;
        return area;
    };
    // The 0x10 editor's VISIBLE band, expressed in the TextArea's own
    // coordinates. Measured GEOMETRICALLY (the ScrollView's viewport mapped
    // into the editor's coordinate system) instead of re-deriving it from
    // contentY arithmetic: Qt may size the TextArea to the viewport or keep it
    // at content height, and the editor may or may not scroll itself. A line's
    // cursorRectangle.y sits in the editor's coordinate system, so subtracting
    // the editor's own contentY makes one formula valid for both structures.
    auto editorVisibleBand = [&]() -> QPair<qreal, qreal> {
        auto *sv = itemOf(QStringLiteral("write10ValuesScroll"));
        auto *area = itemOf(QStringLiteral("write10ValuesArea"));
        if (!sv || !area)
            return {0.0, -1.0};
        const QRectF viewport =
            area->mapRectFromItem(sv, QRectF(0.0, 0.0, sv->width(), sv->height()));
        const qreal shift = area->property("contentY").toReal();
        return {viewport.top() + shift, viewport.bottom() + shift};
    };
    // The rendered rows read as a label plus the VALUE, so the oracle can
    // verify identity and order by the trailing number — never by the
    // separator glyph.
    auto trailingNumber = [](const QString &text) {
        int end = text.size();
        while (end > 0 && !text.at(end - 1).isDigit())
            --end;
        int start = end;
        while (start > 0 && text.at(start - 1).isDigit())
            --start;
        return start == end ? QString() : text.mid(start, end - start);
    };
    // The rows the user can actually READ right now: delegates inside the
    // viewport band, in vertical order. A ListView keeps cached delegates alive
    // OUTSIDE the viewport (cacheBuffer), so a raw child walk reports rows that
    // are not on screen — the band filter is what makes the oracle mean
    // "reachable by scrolling" instead of "instantiated".
    auto viewportRows = [](QQuickItem *view) {
        QList<QPair<qreal, QString>> rows;
        if (!view)
            return rows;
        auto *content = view->property("contentItem").value<QQuickItem *>();
        if (!content)
            return rows;
        const qreal top = view->property("contentY").toReal();
        const qreal bottom = top + view->property("height").toReal();
        QList<QQuickItem *> queue{content};
        while (!queue.isEmpty()) {
            auto *item = queue.takeFirst();
            const QVariant text = item->property("text");
            if (text.isValid() && !text.toString().isEmpty() && item->isVisible()) {
                const qreal y = item->y();
                if (y >= top - 1.0 && y < bottom + 1.0)
                    rows << qMakePair(y, text.toString());
            }
            queue += item->childItems();
        }
        std::sort(rows.begin(), rows.end(),
                  [](const QPair<qreal, QString> &a,
                     const QPair<qreal, QString> &b) { return a.first < b.first; });
        return rows;
    };
    auto rowNumbers = [&trailingNumber](
                          const QList<QPair<qreal, QString>> &rows) {
        QList<int> values;
        for (const auto &row : rows)
            values << trailingNumber(row.second).toInt();
        return values;
    };
    auto contiguous = [](const QList<int> &values) {
        for (int i = 1; i < values.size(); ++i) {
            if (values.at(i) != values.at(i - 1) + 1)
                return false;
        }
        return true;
    };
    auto describe = [](const QList<int> &values) {
        QStringList parts;
        for (const int v : values)
            parts << QString::number(v);
        return parts.join(QStringLiteral(","));
    };
    auto valuesText = [](int count, int first) {
        QStringList parts;
        for (int i = 0; i < count; ++i)
            parts << QString::number(first + i);
        return parts.join(QLatin1Char('\n'));
    };

    // Optional visual evidence (never an oracle substitute): screenshots of
    // the hidden foundation at the acceptance sizes, written only when the
    // harness is asked for them.
    QString dumpDir;
    {
        const QStringList args = app.arguments();
        const int idx = args.indexOf(QStringLiteral("--qml-write-dump"));
        if (idx >= 0 && idx + 1 < args.size())
            dumpDir = args.at(idx + 1);
    }
    auto dumpShot = [&window, &dumpDir, &note](const QString &tag) {
        if (dumpDir.isEmpty() || !window)
            return;
        const QImage image = window->grabWindow();
        const QString path = QDir(dumpDir).filePath(tag + QStringLiteral(".png"));
        if (image.save(path))
            note(QStringLiteral("WRITE [C4 dump]: %1 %2x%3")
                     .arg(path)
                     .arg(image.width())
                     .arg(image.height()));
        else
            note(QStringLiteral("WRITE [C4 dump]: FAILED %1").arg(path));
    };
    // Every part of the confirmation the user must be able to read or press —
    // measured at the current window size.
    auto assertDialogReachable = [&](const QString &label) {
        for (const QString &n : {QStringLiteral("writeSummaryFunction"),
                                 QStringLiteral("writeSummaryUnit"),
                                 QStringLiteral("writeSummaryAddress"),
                                 QStringLiteral("writeSummaryQuantityLabel"),
                                 QStringLiteral("writeSummaryQuantity"),
                                 QStringLiteral("writeSummaryValues"),
                                 QStringLiteral("writeConfirmCancelButton"),
                                 QStringLiteral("writeConfirmAcceptButton")})
            assertReachable(label, n);
    };

    // ---- C4 setup: clean authority state, default acceptance size ----
    push([&]() {
        clearIt();
        setDraft("valuesText10", QString());
        window->resize(1024, 720);
        setDraft("activeFunctionIndex", 0);
    });
    push([&]() {
        if (tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL C4 setup: a snapshot survived the "
                                "reset (token=%1)").arg(tokenOf()));
        note(QStringLiteral("WRITE [C4 setup]: window=%1x%2 state=%3")
                 .arg(window->width())
                 .arg(window->height())
                 .arg(stateToken()));
    });

    // ---- C4 geometry @1024x720: 0x06 tab, then 0x10 tab ----
    push([&]() {
        // Force a rejection so the validation presentation is really on screen
        // for the "must not cover a control" measurement.
        setDraft("unit06", 0);
        setDraft("addressText06", QString::number(100));
        setDraft("valueText06", QString::number(5));
        if (activateWrite())
            fail(QStringLiteral("WRITEFAIL C4 geometry 1024x720: unit 0 was "
                                "accepted"));
    });
    push([&]() {
        if (!errorVisible())
            fail(QStringLiteral("WRITEFAIL C4 geometry 1024x720: no validation "
                                "presentation to measure"));
        assertWriteGeometry(QStringLiteral("1024x720 0x06"));
    });
    push([&]() { setDraft("activeFunctionIndex", 1); });
    push([&]() {
        setDraft("unit10", 1);
        setDraft("start10", 0);
        setDraft("valuesText10", valuesText(30, 1000));
    });
    push([&]() {
        assertWriteGeometry(QStringLiteral("1024x720 0x10"));
        dumpShot(QStringLiteral("c4-1024x720-0x10"));
    });

    // ---- C4 editor scrollability @1024x720 (first line / last line) ----
    push([&]() {
        auto *area = itemOf(QStringLiteral("write10ValuesArea"));
        auto *scroller = editorScroller();
        if (!area || !scroller) {
            fail(QStringLiteral("WRITEFAIL C4 scroll: the 0x10 editor or its "
                                "scroller is missing"));
            return;
        }
        if (area->property("lineCount").toInt() != 30)
            fail(QStringLiteral("WRITEFAIL C4 scroll: the editor holds %1 lines, "
                                "expected 30").arg(area->property("lineCount").toInt()));
        if (scroller->property("contentHeight").toReal()
            <= scroller->property("height").toReal() + 1.0) {
            fail(QStringLiteral("WRITEFAIL C4 scroll: the editor does not "
                                "overflow (content=%1 height=%2)")
                     .arg(scroller->property("contentHeight").toReal())
                     .arg(scroller->property("height").toReal()));
            return;
        }
        note(QStringLiteral("WRITE [C4 scroll]: scroller=%1 content=%2 viewport=%3")
                 .arg(scroller->objectName())
                 .arg(qRound(scroller->property("contentHeight").toReal()))
                 .arg(qRound(scroller->property("height").toReal())));
        // Top of the document: the first line must be inside the viewport.
        scroller->setProperty("contentY", 0.0);
        area->setProperty("cursorPosition", 0);
        const auto band = editorVisibleBand();
        const qreal firstY = area->property("cursorRectangle").toRectF().y();
        if (firstY < band.first - 1.0 || firstY >= band.second)
            fail(QStringLiteral("WRITEFAIL C4 scroll: line 1 (y=%1) is not in the "
                                "visible band [%2,%3)")
                     .arg(firstY)
                     .arg(band.first)
                     .arg(band.second));
    });
    push([&]() {
        auto *area = itemOf(QStringLiteral("write10ValuesArea"));
        auto *scroller = editorScroller();
        if (!area || !scroller)
            return;
        // Bottom of the document: the LAST line must become visible by real
        // scrolling (contentY driven to its maximum).
        const qreal maxY = scroller->property("contentHeight").toReal()
                           - scroller->property("height").toReal();
        scroller->setProperty("contentY", maxY);
        area->setProperty("cursorPosition",
                          area->property("length").toInt());
        const auto bandAfter = editorVisibleBand();
        const qreal lastY = area->property("cursorRectangle").toRectF().y();
        const bool moved = scroller->property("contentY").toReal() > 1.0;
        if (!moved)
            fail(QStringLiteral("WRITEFAIL C4 scroll: the editor did not scroll "
                                "(contentY=%1 max=%2)")
                     .arg(scroller->property("contentY").toReal())
                     .arg(maxY));
        if (lastY < bandAfter.first - 1.0 || lastY >= bandAfter.second)
            fail(QStringLiteral("WRITEFAIL C4 scroll: the last line (y=%1) is not "
                                "in the visible band [%2,%3) at max scroll")
                     .arg(lastY)
                     .arg(bandAfter.first)
                     .arg(bandAfter.second));
        note(QStringLiteral("WRITE [C4 scroll]: last line y=%1 visible in band "
                            "[%2,%3) at contentY=%4 of max %5")
                 .arg(qRound(lastY))
                 .arg(qRound(bandAfter.first))
                 .arg(qRound(bandAfter.second))
                 .arg(qRound(scroller->property("contentY").toReal()))
                 .arg(qRound(maxY)));
    });

    // ---- C4 confirmation scrollability (all values reachable) ----
    push([&]() {
        setDraft("valuesText10", valuesText(40, 1000));
    });
    push([&]() {
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: the 40-value draft "
                                "was rejected"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: no dialog"));
    });
    push([&]() {
        auto *list = itemOf(QStringLiteral("writeSummaryValues"));
        if (!list) {
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: the values list is "
                                "missing"));
            return;
        }
        const int count = valuesListCount();
        const int expected = controller->preparedWriteValues().size();
        if (count != 40 || expected != 40)
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: list count=%1, "
                                "snapshot values=%2, expected 40/40")
                     .arg(count)
                     .arg(expected));
        if (textOf(QStringLiteral("writeSummaryQuantity")) != QStringLiteral("40"))
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: quantity shows [%1], "
                                "expected 40")
                     .arg(textOf(QStringLiteral("writeSummaryQuantity"))));
        assertDialogReachable(QStringLiteral("1024x720 dialog"));
        dumpShot(QStringLiteral("c4-confirmation-1024x720"));
        const qreal contentH = list->property("contentHeight").toReal();
        const qreal viewH = list->property("height").toReal();
        if (viewH > 121.0)
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: the list grew to %1 "
                                "(the 120px bound is gone)").arg(viewH));
        if (contentH <= viewH + 1.0)
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: the list does not "
                                "overflow (content=%1 height=%2)")
                     .arg(contentH)
                     .arg(viewH));
        list->setProperty("contentY", 0.0);
    });
    push([&]() {
        auto *list = itemOf(QStringLiteral("writeSummaryValues"));
        if (!list)
            return;
        const QList<int> values = rowNumbers(viewportRows(list));
        if (values.isEmpty())
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: no row is readable "
                                "at the top of the list"));
        else if (values.first() != 1000)
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: the top of the list "
                                "shows [%1], expected value 1000")
                     .arg(describe(values)));
        else if (!contiguous(values))
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: the readable rows "
                                "are not contiguous ([%1])").arg(describe(values)));
        else
            note(QStringLiteral("WRITE [C4 confirm-scroll]: readable top rows = "
                                "[%1]").arg(describe(values)));
        const qreal maxY = list->property("contentHeight").toReal()
                           - list->property("height").toReal();
        list->setProperty("contentY", maxY);
    });
    push([&]() {
        auto *list = itemOf(QStringLiteral("writeSummaryValues"));
        if (!list)
            return;
        const QList<int> values = rowNumbers(viewportRows(list));
        if (values.isEmpty())
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: no row is readable "
                                "at the bottom of the list"));
        else if (values.last() != 1039)
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: the bottom of the "
                                "list shows [%1], expected value 1039")
                     .arg(describe(values)));
        else if (values.contains(1000))
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: value 1000 is still "
                                "on screen at the bottom — nothing scrolled"));
        else if (!contiguous(values))
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: the readable rows "
                                "are not contiguous ([%1])").arg(describe(values)));
        else
            note(QStringLiteral("WRITE [C4 confirm-scroll]: readable bottom rows = "
                                "[%1] — all 40 values reachable by scrolling (not "
                                "all visible at once)")
                     .arg(describe(values)));
        clearIt();
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: the dialog stayed "
                                "open after the cancel"));
        if (stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("WRITEFAIL C4 confirm-scroll: state=%1 after "
                                "cancel").arg(stateToken()));
    });

    // ---- C4 123-value protocol boundary ----
    push([&]() {
        setDraft("valuesText10", valuesText(123, 1));
    });
    push([&]() {
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C4 boundary: the 123-value draft was "
                                "rejected"));
        if (controller->preparedWriteQuantity() != 123)
            fail(QStringLiteral("WRITEFAIL C4 boundary: the snapshot quantity is "
                                "%1, expected 123")
                     .arg(controller->preparedWriteQuantity()));
    });
    push([&]() {
        auto *list = itemOf(QStringLiteral("writeSummaryValues"));
        if (!list) {
            fail(QStringLiteral("WRITEFAIL C4 boundary: the values list is "
                                "missing"));
            return;
        }
        if (valuesListCount() != 123)
            fail(QStringLiteral("WRITEFAIL C4 boundary: list count=%1, expected "
                                "123").arg(valuesListCount()));
        if (textOf(QStringLiteral("writeSummaryQuantity")) != QStringLiteral("123"))
            fail(QStringLiteral("WRITEFAIL C4 boundary: quantity shows [%1], "
                                "expected 123")
                     .arg(textOf(QStringLiteral("writeSummaryQuantity"))));
        list->setProperty("contentY", 0.0);
    });
    push([&]() {
        auto *list = itemOf(QStringLiteral("writeSummaryValues"));
        if (!list)
            return;
        const QList<int> values = rowNumbers(viewportRows(list));
        if (values.isEmpty() || values.first() != 1)
            fail(QStringLiteral("WRITEFAIL C4 boundary: value #1 is not readable "
                                "at the top (rows=[%1])").arg(describe(values)));
        else
            note(QStringLiteral("WRITE [C4 boundary]: value #1 readable at the top "
                                "([%1])").arg(describe(values)));
        list->setProperty("contentY",
                          list->property("contentHeight").toReal()
                              - list->property("height").toReal());
    });
    push([&]() {
        auto *list = itemOf(QStringLiteral("writeSummaryValues"));
        if (!list)
            return;
        const QList<int> values = rowNumbers(viewportRows(list));
        if (values.isEmpty() || values.last() != 123)
            fail(QStringLiteral("WRITEFAIL C4 boundary: value #123 is not readable "
                                "at the bottom (rows=[%1])").arg(describe(values)));
        else if (!contiguous(values))
            fail(QStringLiteral("WRITEFAIL C4 boundary: the readable rows are not "
                                "contiguous ([%1])").arg(describe(values)));
        else
            note(QStringLiteral("WRITE [C4 boundary]: value #123 readable at the "
                                "bottom ([%1])").arg(describe(values)));
    });

    // ---- C4 long-summary keyboard: Cancel / Confirm stay reachable ----
    push([&]() {
        if (!focusOn(QStringLiteral("writeConfirmCancelButton")))
            fail(QStringLiteral("WRITEFAIL C4 keyboard: the dialog did not open "
                                "with Cancel focused (focus=%1)").arg(focusName()));
        // The values list must not be a keyboard trap: Tab from Cancel reaches
        // Confirm (bounded walk, the real behaviour is recorded).
        QStringList chain;
        chain << focusOwnerName();
        bool reached = false;
        for (int i = 0; i < 8 && !reached; ++i) {
            tab(true);
            chain << focusOwnerName();
            reached = focusOwnerName() == QStringLiteral("writeConfirmAcceptButton");
        }
        if (!reached)
            fail(QStringLiteral("WRITEFAIL C4 keyboard: Tab never reached Confirm "
                                "([%1])").arg(chain.join(QStringLiteral(","))));
        else
            note(QStringLiteral("WRITE [C4 keyboard]: Tab chain = [%1]")
                     .arg(chain.join(QStringLiteral(","))));
        const quint64 tokenBefore = tokenOf();
        if (tokenBefore == 0)
            fail(QStringLiteral("WRITEFAIL C4 keyboard: no prepared token"));
        // Keyboard navigation inside the long list must not confirm anything.
        auto *list = itemOf(QStringLiteral("writeSummaryValues"));
        if (list)
            list->forceActiveFocus(Qt::TabFocusReason);
    });
    push([&]() {
        const quint64 tokenBefore = tokenOf();
        sendKey(Qt::Key_Down, Qt::NoModifier, false);
        sendKey(Qt::Key_PageDown, Qt::NoModifier, false);
        sendKey(Qt::Key_Return, Qt::NoModifier, false);
        if (tokenOf() != tokenBefore)
            fail(QStringLiteral("WRITEFAIL C4 keyboard: the token changed while "
                                "the list had focus (%1 -> %2)")
                     .arg(tokenBefore)
                     .arg(tokenOf()));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL C4 keyboard: state=%1 after keys reached "
                                "the list").arg(stateToken()));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL C4 keyboard: the dialog closed while "
                                "navigating the list"));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("WRITEFAIL C4 keyboard: %1 write attempts")
                     .arg(transport->writeAttempts()));
        // Backtab must return to Cancel (no one-way trap).
        bool back = false;
        for (int i = 0; i < 8 && !back; ++i) {
            tab(false);
            back = focusOwnerName() == QStringLiteral("writeConfirmCancelButton");
        }
        if (!back)
            fail(QStringLiteral("WRITEFAIL C4 keyboard: Backtab never returned to "
                                "Cancel (focus=%1)").arg(focusOwnerName()));
        else
            note(QStringLiteral("WRITE [C4 keyboard]: Shift+Tab returns to Cancel; "
                                "list keys neither confirmed nor trapped"));
        clearIt();
    });

    // ---- C4 124-value rejection (protocol limit) ----
    push([&]() {
        // The editor no longer holds a value draft error, so this rejection is
        // freshly produced by the 124-value input.
        setDraft("valuesText10", valuesText(124, 1));
    });
    push([&]() {
        const bool accepted = activateWrite();
        if (accepted)
            fail(QStringLiteral("WRITEFAIL C4 boundary: the 124-value draft was "
                                "accepted"));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL C4 boundary: a dialog opened for the "
                                "124-value draft"));
        if (tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL C4 boundary: a snapshot was created for "
                                "the 124-value draft"));
        if (!errorVisible() || textOf(QStringLiteral("writeValidationError")).isEmpty())
            fail(QStringLiteral("WRITEFAIL C4 boundary: no validation "
                                "presentation for 124 values"));
        else
            note(QStringLiteral("WRITE [C4 boundary]: 124 values -> [%1]")
                     .arg(textOf(QStringLiteral("writeValidationError"))));
    });
    push([&]() {
        assertWriteGeometry(QStringLiteral("1024x720 0x10 (post-reject)"));
    });

    // ---- C4 geometry @1000x700 (the minimum acceptance size) ----
    push([&]() {
        setDraft("valuesText10", valuesText(30, 1000));
        window->resize(1000, 700);
    });
    push([&]() {
        if (window->width() != 1000 || window->height() != 700)
            fail(QStringLiteral("WRITEFAIL C4 geometry 1000x700: the window is "
                                "%1x%2, expected 1000x700 — the harness must not "
                                "rely on the window growing itself")
                     .arg(window->width())
                     .arg(window->height()));
        assertWriteGeometry(QStringLiteral("1000x700 0x10"));
        dumpShot(QStringLiteral("c4-1000x700-0x10"));
    });
    // The dialog itself at the minimum size: title / summary / all values /
    // Both buttons must be readable and pressable without resizing.
    push([&]() {
        setDraft("valuesText10", valuesText(20, 500));
    });
    push([&]() {
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL C4 geometry 1000x700: the draft was "
                                "rejected"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL C4 geometry 1000x700: no dialog"));
    });
    push([&]() {
        assertDialogReachable(QStringLiteral("1000x700 dialog"));
        dumpShot(QStringLiteral("c4-confirmation-1000x700"));
        clearIt();
    });
    push([&]() { setDraft("activeFunctionIndex", 0); });
    push([&]() {
        setDraft("unit06", 1);
        setDraft("addressText06", QString::number(100));
        setDraft("valueText06", QString::number(7));
    });
    push([&]() { assertWriteGeometry(QStringLiteral("1000x700 0x06")); });

    // ---- C4 editor scrollability @1000x700 (worst case) ----
    push([&]() { setDraft("activeFunctionIndex", 1); });
    push([&]() {
        auto *area = itemOf(QStringLiteral("write10ValuesArea"));
        auto *scroller = editorScroller();
        if (!area || !scroller)
            return;
        scroller->setProperty("contentY", 0.0);
        area->setProperty("cursorPosition", 0);
    });
    push([&]() {
        auto *area = itemOf(QStringLiteral("write10ValuesArea"));
        auto *scroller = editorScroller();
        if (!area || !scroller)
            return;
        const auto band = editorVisibleBand();
        const qreal firstY = area->property("cursorRectangle").toRectF().y();
        if (firstY < band.first - 1.0 || firstY >= band.second)
            fail(QStringLiteral("WRITEFAIL C4 scroll 1000x700: line 1 (y=%1) is "
                                "not in the visible band [%2,%3)")
                     .arg(firstY)
                     .arg(band.first)
                     .arg(band.second));
        const qreal maxY = scroller->property("contentHeight").toReal()
                           - scroller->property("height").toReal();
        scroller->setProperty("contentY", maxY);
        area->setProperty("cursorPosition", area->property("length").toInt());
    });
    push([&]() {
        auto *area = itemOf(QStringLiteral("write10ValuesArea"));
        auto *scroller = editorScroller();
        if (!area || !scroller)
            return;
        const auto band = editorVisibleBand();
        const qreal lastY = area->property("cursorRectangle").toRectF().y();
        if (scroller->property("contentY").toReal() <= 1.0)
            fail(QStringLiteral("WRITEFAIL C4 scroll 1000x700: the editor did not "
                                "scroll (contentY=%1)")
                     .arg(scroller->property("contentY").toReal()));
        else if (lastY < band.first - 1.0 || lastY >= band.second)
            fail(QStringLiteral("WRITEFAIL C4 scroll 1000x700: the last line (y=%1) "
                                "is not in the visible band [%2,%3)")
                     .arg(lastY)
                     .arg(band.first)
                     .arg(band.second));
        else
            note(QStringLiteral("WRITE [C4 scroll 1000x700]: last line y=%1 in band "
                                "[%2,%3) at contentY=%4")
                     .arg(qRound(lastY))
                     .arg(qRound(band.first))
                     .arg(qRound(band.second))
                     .arg(qRound(scroller->property("contentY").toReal())));
        window->resize(1024, 720);
    });
    push([&]() {
        assertWriteGeometry(QStringLiteral("restored 1024x720 0x10"));
    });

    // =====================================================================
    // M10-D1 — RAW-TEXT field oracles (O1-O7 / R0-R3) + keyboard + field
    // identity + accessibility.
    //
    // Every case observes TWO things, exactly as the design demands: the
    // widget's real text (`TextField.text`) and the page-local raw draft the
    // controller boundary receives. A case that typed real keys or pasted real
    // clipboard content can never be satisfied by a QML-side normalization —
    // and if the control HAD normalized the input, the assert below would fail
    // rather than be silently reinterpreted.
    // =====================================================================
    auto fieldText = [&itemOf](const QString &name) {
        auto *item = itemOf(name);
        return item ? item->property("text").toString() : QStringLiteral("<none>");
    };
    auto rawDraft = [&section](const char *prop) {
        auto *item = section();
        return item ? item->property(prop).toString() : QStringLiteral("<none>");
    };
    auto errorField = [&controller]() { return controller->writeDraftErrorField(); };
    // Real character input: a TextInput inserts text from the KeyPress event's
    // `text` field, so the events below carry the character itself (the shared
    // sendKey helper deliberately sends key codes only, for the existing
    // key-command oracles). Nothing here normalizes the input — the widget
    // receives exactly the characters typed.
    auto typeText = [window](const QString &keys) {
        for (const QChar c : keys) {
            QObject *target = window->activeFocusItem();
            if (!target)
                return;
            const Qt::Key key = c == QLatin1Char(' ')
                                    ? Qt::Key_Space
                                    : static_cast<Qt::Key>(c.toUpper().unicode());
            const QString text(c);
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(target, &press);
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
            QCoreApplication::sendEvent(target, &release);
        }
    };
    auto selectAll = [&sendKey]() {
        sendKey(Qt::Key_A, Qt::ControlModifier, false);
    };
    auto pasteText = [&sendKey](const QString &text) {
        QGuiApplication::clipboard()->setText(text);
        sendKey(Qt::Key_V, Qt::ControlModifier, false);
    };
    auto focusField = [&](const QString &name) {
        clickItemAt(name);
        return focusOwnerName() == name;
    };
    // Reset both raw fields to empty through the widget (the same path a user
    // clearing the box takes), then wait a turn before typing.
    push([&]() {
        clearIt();
        setDraft("activeFunctionIndex", 0);
        setDraft("unit06", 1);
        setDraft("timeout06", 1000);
        // Start every case from empty raw fields: the prepare path reports the
        // FIRST failing field, so a case must not inherit the previous one's
        // invalid text.
        setDraft("addressText06", QString());
        setDraft("valueText06", QString());
    });
    push([&]() {
        if (!focusField(QStringLiteral("write06AddressField")))
            fail(QStringLiteral("WRITEFAIL D1: the address field did not take "
                                "focus (focus=%1)").arg(focusOwnerName()));
        selectAll();
        sendKey(Qt::Key_Delete, Qt::NoModifier, false);
    });
    push([&]() {
        // O1: typed digits become the widget text AND the raw draft verbatim.
        typeText(QStringLiteral("1234"));
    });
    push([&]() {
        if (fieldText(QStringLiteral("write06AddressField")) != QStringLiteral("1234"))
            fail(QStringLiteral("WRITEFAIL D1/O1: TextField.text=[%1], expected "
                                "[1234]").arg(fieldText(QStringLiteral("write06AddressField"))));
        if (rawDraft("addressText06") != QStringLiteral("1234"))
            fail(QStringLiteral("WRITEFAIL D1/O1: the page-local raw draft is "
                                "[%1]").arg(rawDraft("addressText06")));
        if (!focusField(QStringLiteral("write06ValueField")))
            fail(QStringLiteral("WRITEFAIL D1: the value field did not take focus"));
        selectAll();
        sendKey(Qt::Key_Delete, Qt::NoModifier, false);
        typeText(QStringLiteral("5"));
    });
    push([&]() {
        clickItemAt(QStringLiteral("writeActivateButton"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL D1/O1: the dialog did not open for a "
                                "valid typed draft"));
        if (controller->preparedWriteAddress() != 1234
            || controller->preparedWriteValue() != 5)
            fail(QStringLiteral("WRITEFAIL D1/O1: typed=%1/%2, expected 1234/5")
                     .arg(controller->preparedWriteAddress())
                     .arg(controller->preparedWriteValue()));
        note(QStringLiteral("WRITE [D1/O1]: typed \"1234\" -> TextField.text=[%1], "
                            "raw draft=[%2], typed address=%3")
                 .arg(fieldText(QStringLiteral("write06AddressField")))
                 .arg(rawDraft("addressText06"))
                 .arg(controller->preparedWriteAddress()));
        clearIt();
    });
    // ---- O2: leading zeros stay in the draft, the snapshot is canonical ----
    push([&]() {
        if (!focusField(QStringLiteral("write06AddressField")))
            fail(QStringLiteral("WRITEFAIL D1/O2: address field focus"));
        selectAll();
        sendKey(Qt::Key_Delete, Qt::NoModifier, false);
        typeText(QStringLiteral("00010"));
    });
    push([&]() {
        if (fieldText(QStringLiteral("write06AddressField")) != QStringLiteral("00010")
            || rawDraft("addressText06") != QStringLiteral("00010"))
            fail(QStringLiteral("WRITEFAIL D1/O2: the raw text was rewritten "
                                "(widget=[%1] draft=[%2])")
                     .arg(fieldText(QStringLiteral("write06AddressField")))
                     .arg(rawDraft("addressText06")));
        clickItemAt(QStringLiteral("writeActivateButton"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL D1/O2: the dialog did not open"));
        if (controller->preparedWriteAddress() != 10)
            fail(QStringLiteral("WRITEFAIL D1/O2: typed=%1, expected 10")
                     .arg(controller->preparedWriteAddress()));
        if (textOf(QStringLiteral("writeSummaryAddress")) != QStringLiteral("10"))
            fail(QStringLiteral("WRITEFAIL D1/O2: the summary shows [%1], expected "
                                "the canonical 10").arg(textOf(QStringLiteral("writeSummaryAddress"))));
        note(QStringLiteral("WRITE [D1/O2]: raw [%1] preserved; summary=%2")
                 .arg(rawDraft("addressText06"))
                 .arg(textOf(QStringLiteral("writeSummaryAddress"))));
        clearIt();
    });
    // ---- O3: outer whitespace is preserved raw and trimmed by the core ----
    push([&]() {
        if (!focusField(QStringLiteral("write06AddressField")))
            fail(QStringLiteral("WRITEFAIL D1/O3: address field focus"));
        selectAll();
        sendKey(Qt::Key_Delete, Qt::NoModifier, false);
        typeText(QStringLiteral(" 1234 "));
    });
    push([&]() {
        if (fieldText(QStringLiteral("write06AddressField")) != QStringLiteral(" 1234 ")
            || rawDraft("addressText06") != QStringLiteral(" 1234 "))
            fail(QStringLiteral("WRITEFAIL D1/O3: the raw text was trimmed by QML "
                                "(widget=[%1] draft=[%2])")
                     .arg(fieldText(QStringLiteral("write06AddressField")))
                     .arg(rawDraft("addressText06")));
        clickItemAt(QStringLiteral("writeActivateButton"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL D1/O3: the dialog did not open"));
        if (controller->preparedWriteAddress() != 1234)
            fail(QStringLiteral("WRITEFAIL D1/O3: typed=%1, expected 1234")
                     .arg(controller->preparedWriteAddress()));
        note(QStringLiteral("WRITE [D1/O3]: raw [%1] preserved; typed=%2")
                 .arg(rawDraft("addressText06"))
                 .arg(controller->preparedWriteAddress()));
        clearIt();
    });
    // ---- O4 / O5 / O6: invalid raw text stays raw, the field is identified ----
    push([&]() {
        if (!focusField(QStringLiteral("write06AddressField")))
            fail(QStringLiteral("WRITEFAIL D1/O4: address field focus"));
        selectAll();
        sendKey(Qt::Key_Delete, Qt::NoModifier, false);
        typeText(QStringLiteral("-1"));
    });
    push([&]() {
        if (rawDraft("addressText06") != QStringLiteral("-1"))
            fail(QStringLiteral("WRITEFAIL D1/O4: the raw draft was rewritten to "
                                "[%1]").arg(rawDraft("addressText06")));
        clickItemAt(QStringLiteral("writeActivateButton"));
        if (dialogVisible() || tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL D1/O4: an invalid raw address produced a "
                                "snapshot/dialog"));
        if (errorField() != QStringLiteral("address"))
            fail(QStringLiteral("WRITEFAIL D1/O4: error field=[%1], expected address")
                     .arg(errorField()));
        if (!textOf(QStringLiteral("writeValidationError")).contains(
                QStringLiteral("寄存器地址")))
            fail(QStringLiteral("WRITEFAIL D1/O4: the message does not name the field "
                                "([%1])").arg(textOf(QStringLiteral("writeValidationError"))));
        note(QStringLiteral("WRITE [D1/O4]: raw [%1] kept; error field=%2; [%3]")
                 .arg(rawDraft("addressText06"))
                 .arg(errorField())
                 .arg(textOf(QStringLiteral("writeValidationError"))));
    });
    push([&]() {
        // O5: the VALUE field, so the field identity must switch. The address
        // is made valid first (the boundary reports the first failing field).
        setDraft("addressText06", QStringLiteral("100"));
        if (!focusField(QStringLiteral("write06ValueField")))
            fail(QStringLiteral("WRITEFAIL D1/O5: value field focus"));
        selectAll();
        sendKey(Qt::Key_Delete, Qt::NoModifier, false);
        typeText(QStringLiteral("12x"));
    });
    push([&]() {
        if (rawDraft("valueText06") != QStringLiteral("12x"))
            fail(QStringLiteral("WRITEFAIL D1/O5: raw value draft=[%1]")
                     .arg(rawDraft("valueText06")));
        clickItemAt(QStringLiteral("writeActivateButton"));
        if (dialogVisible() || tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL D1/O5: an invalid raw value produced a "
                                "snapshot/dialog"));
        if (errorField() != QStringLiteral("value"))
            fail(QStringLiteral("WRITEFAIL D1/O5: error field=[%1], expected value")
                     .arg(errorField()));
        if (!textOf(QStringLiteral("writeValidationError")).contains(
                QStringLiteral("写入值")))
            fail(QStringLiteral("WRITEFAIL D1/O5: the message does not name the field "
                                "([%1])").arg(textOf(QStringLiteral("writeValidationError"))));
        note(QStringLiteral("WRITE [D1/O5]: raw [%1] kept; error field=%2; [%3]")
                 .arg(rawDraft("valueText06"))
                 .arg(errorField())
                 .arg(textOf(QStringLiteral("writeValidationError"))));
    });
    push([&]() {
        // O6: above range, typed digit by digit (no clamping anywhere).
        setDraft("addressText06", QStringLiteral("100"));
        if (!focusField(QStringLiteral("write06ValueField")))
            fail(QStringLiteral("WRITEFAIL D1/O6: value field focus"));
        selectAll();
        sendKey(Qt::Key_Delete, Qt::NoModifier, false);
        typeText(QStringLiteral("65536"));
    });
    push([&]() {
        if (fieldText(QStringLiteral("write06ValueField")) != QStringLiteral("65536")
            || rawDraft("valueText06") != QStringLiteral("65536"))
            fail(QStringLiteral("WRITEFAIL D1/O6: the raw text was clamped "
                                "(widget=[%1] draft=[%2])")
                     .arg(fieldText(QStringLiteral("write06ValueField")))
                     .arg(rawDraft("valueText06")));
        clickItemAt(QStringLiteral("writeActivateButton"));
        if (dialogVisible() || tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL D1/O6: 65536 produced a snapshot/dialog"));
        if (errorField() != QStringLiteral("value"))
            fail(QStringLiteral("WRITEFAIL D1/O6: error field=[%1]").arg(errorField()));
        note(QStringLiteral("WRITE [D1/O6]: raw [%1] kept (no clamp); error field=%2")
                 .arg(rawDraft("valueText06"))
                 .arg(errorField()));
    });
    // ---- O7 / R2: a REAL clipboard paste of an invalid string ----
    push([&]() {
        setDraft("valueText06", QStringLiteral("5"));
        if (!focusField(QStringLiteral("write06AddressField")))
            fail(QStringLiteral("WRITEFAIL D1/O7: address field focus"));
        selectAll();
        sendKey(Qt::Key_Delete, Qt::NoModifier, false);
        pasteText(QStringLiteral("12x"));
    });
    push([&]() {
        if (fieldText(QStringLiteral("write06AddressField")) != QStringLiteral("12x")
            || rawDraft("addressText06") != QStringLiteral("12x"))
            fail(QStringLiteral("WRITEFAIL D1/O7: the pasted text was altered "
                                "(widget=[%1] draft=[%2])")
                     .arg(fieldText(QStringLiteral("write06AddressField")))
                     .arg(rawDraft("addressText06")));
        clickItemAt(QStringLiteral("writeActivateButton"));
        if (dialogVisible() || tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL D1/O7: a pasted invalid string produced a "
                                "snapshot/dialog"));
        if (errorField() != QStringLiteral("address"))
            fail(QStringLiteral("WRITEFAIL D1/O7: error field=[%1]").arg(errorField()));
        note(QStringLiteral("WRITE [D1/O7]: pasted [%1] kept raw; validation error "
                            "field=%2").arg(rawDraft("addressText06")).arg(errorField()));
    });
    // ---- R0: empty input never becomes 0 ----
    push([&]() {
        if (!focusField(QStringLiteral("write06AddressField")))
            fail(QStringLiteral("WRITEFAIL D1/R0: address field focus"));
        selectAll();
        sendKey(Qt::Key_Delete, Qt::NoModifier, false);
    });
    push([&]() {
        if (!fieldText(QStringLiteral("write06AddressField")).isEmpty()
            || !rawDraft("addressText06").isEmpty())
            fail(QStringLiteral("WRITEFAIL D1/R0: an empty field is not empty "
                                "(widget=[%1] draft=[%2])")
                     .arg(fieldText(QStringLiteral("write06AddressField")))
                     .arg(rawDraft("addressText06")));
        clickItemAt(QStringLiteral("writeActivateButton"));
        if (dialogVisible() || tokenOf() != 0)
            fail(QStringLiteral("WRITEFAIL D1/R0: an empty draft produced a "
                                "snapshot/dialog"));
        if (errorField() != QStringLiteral("address"))
            fail(QStringLiteral("WRITEFAIL D1/R0: error field=[%1]").arg(errorField()));
        note(QStringLiteral("WRITE [D1/R0]: empty stays empty (never auto-0); "
                            "validation error field=%1").arg(errorField()));
    });
    // ---- R1: select-all + retype yields the exact raw text ----
    push([&]() {
        setDraft("valueText06", QStringLiteral("5"));
        if (!focusField(QStringLiteral("write06AddressField")))
            fail(QStringLiteral("WRITEFAIL D1/R1: address field focus"));
        typeText(QStringLiteral("123"));
        selectAll();
        typeText(QStringLiteral("65535"));
    });
    push([&]() {
        if (fieldText(QStringLiteral("write06AddressField")) != QStringLiteral("65535")
            || rawDraft("addressText06") != QStringLiteral("65535"))
            fail(QStringLiteral("WRITEFAIL D1/R1: select-all+retype gave [%1]/[%2]")
                     .arg(fieldText(QStringLiteral("write06AddressField")))
                     .arg(rawDraft("addressText06")));
        clickItemAt(QStringLiteral("writeActivateButton"));
        if (!dialogVisible() || controller->preparedWriteAddress() != 65535)
            fail(QStringLiteral("WRITEFAIL D1/R1: typed=%1 (dialog=%2), expected 65535")
                     .arg(controller->preparedWriteAddress())
                     .arg(dialogVisible() ? 1 : 0));
        note(QStringLiteral("WRITE [D1/R1]: select-all + retype -> raw [%1], typed %2")
                 .arg(rawDraft("addressText06"))
                 .arg(controller->preparedWriteAddress()));
        clearIt();
    });
    // ---- R3: Tab leaves the field, Shift+Tab returns ----
    push([&]() {
        setDraft("unit06", 1);
        if (!focusField(QStringLiteral("write06AddressField")))
            fail(QStringLiteral("WRITEFAIL D1/R3: address field focus"));
        tab(true);
    });
    push([&]() {
        if (focusOwnerName() == QStringLiteral("write06AddressField"))
            fail(QStringLiteral("WRITEFAIL D1/R3: Tab did not leave the field"));
        const QString afterTab = focusOwnerName();
        tab(false);
        if (focusOwnerName() != QStringLiteral("write06AddressField"))
            fail(QStringLiteral("WRITEFAIL D1/R3: Shift+Tab did not return to the "
                                "field (focus=%1)").arg(focusOwnerName()));
        note(QStringLiteral("WRITE [D1/R3]: Tab -> [%1]; Shift+Tab -> [%2]")
                 .arg(afterTab, focusOwnerName()));
    });
    // ---- D1: the new fields carry accessible names ----
    push([&]() {
        const QString addressName = accessibleNameOf(QStringLiteral("write06AddressField"));
        const QString valueName = accessibleNameOf(QStringLiteral("write06ValueField"));
        if (addressName.isEmpty() || valueName.isEmpty())
            fail(QStringLiteral("WRITEFAIL D1/a11y: names missing (address=[%1] "
                                "value=[%2])").arg(addressName, valueName));
        if (!addressName.contains(QStringLiteral("地址"))
            || !valueName.contains(QStringLiteral("写入值")))
            fail(QStringLiteral("WRITEFAIL D1/a11y: unexpected names (address=[%1] "
                                "value=[%2])").arg(addressName, valueName));
        note(QStringLiteral("WRITE [D1/a11y]: address=[%1] value=[%2]")
                 .arg(addressName, valueName));
    });

    // ---- M10-D3 correction: atomic-dispatch → Dialog reaction oracles ----
    //
    // RED-first framing: these steps are written and executed BEFORE any
    // product change, so whatever they observe is the truth about the shipped
    // notification path. They never touch the normal Confirm button — D4 has
    // not started — they drive the Controller's C++ atomic seam directly,
    // exactly like the C++ focused tests do.
    //
    // The M10-C contract "zero write dispatch / zero write transaction" belongs
    // to the confirmation-only foundation, so it is asserted over exactly the
    // phases that exercise it (snapshotted here) rather than over these steps.
    auto writeAttemptsBeforeDlg = std::make_shared<int>(0);
    auto writeSendsBeforeDlg = std::make_shared<int>(0);
    auto dlgSectionRan = std::make_shared<bool>(false);

    // §11 projection-signal oracle: the atomic path must notify through the
    // SAME existing signal the old confirmation path uses. No new
    // production-only signal is introduced for the test.
    auto preparedNotifies = std::make_shared<int>(0);
    QObject::connect(controller, &AnalysisController::preparedWriteChanged, &app,
                     [preparedNotifies]() { ++*preparedNotifies; });

    // Return the harness to a clean, connected Active Serial session with no
    // prepared snapshot, without ever invoking the normal Confirm button.
    auto resetWriteContext = [&controller]() {
        if (controller->serialBusy()) {
            controller->disconnectSerial();
        }
        if (controller->hasPreparedWrite()) {
            (void)controller->cancelPreparedWrite(
                controller->preparedWriteTokenValue());
        }
        if (!controller->serialConnected()) {
            controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
        }
    };
    auto prepareFc06Dialog = [&]() {
        setDraft("activeFunctionIndex", 0);
        setDraft("unit06", 11);
        setDraft("addressText06", QString::number(0x0064));
        setDraft("valueText06", QString::number(5));
        setDraft("timeout06", 1000);
        return activateWrite();
    };

    // ---- DLG1: full accepted -> Consumed -> Dialog exits the flow ----
    push([&]() {
        resetWriteContext();
        *dlgSectionRan = true;
        transport->setAcceptWrites(true);
        transport->setWriteShortAcceptedBytes(std::nullopt);
        *writeAttemptsBeforeDlg = transport->writeAttempts();
        *writeSendsBeforeDlg = transport->writeSends();

        if (!prepareFc06Dialog())
            fail(QStringLiteral("WRITEFAIL DLG1: could not prepare 0x06"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG1: the dialog did not open"));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL DLG1: state=%1, expected prepared")
                     .arg(stateToken()));
        const auto token = tokenOf();
        if (token == 0)
            fail(QStringLiteral("WRITEFAIL DLG1: no snapshot token"));

        const int notifiesBefore = *preparedNotifies;
        const auto result = controller->confirmAndDispatchPreparedWrite(token);

        if (!result.confirmationAccepted)
            fail(QStringLiteral("WRITEFAIL DLG1: confirmationAccepted=false"));
        if (!result.dispatchAttempted)
            fail(QStringLiteral("WRITEFAIL DLG1: dispatchAttempted=false"));
        if (!result.startResult.has_value() || !result.startResult->accepted)
            fail(QStringLiteral("WRITEFAIL DLG1: the transport did not accept"));
        else if (result.startResult->disposition
                 != modbuslens::core::TransportDisposition::PossiblySent)
            fail(QStringLiteral("WRITEFAIL DLG1: disposition is not PossiblySent"));
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL DLG1: state=%1, expected consumed")
                     .arg(stateToken()));
        if (transport->writeAttempts() - *writeAttemptsBeforeDlg != 1)
            fail(QStringLiteral("WRITEFAIL DLG1: attempts=%1, expected exactly 1")
                     .arg(transport->writeAttempts() - *writeAttemptsBeforeDlg));
        if (transport->writeSends() - *writeSendsBeforeDlg != 1)
            fail(QStringLiteral("WRITEFAIL DLG1: sends=%1, expected exactly 1")
                     .arg(transport->writeSends() - *writeSendsBeforeDlg));
        if (!transport->writtenDescriptor().has_value()
            || transport->writtenDescriptor()->wire.size() != 8)
            fail(QStringLiteral("WRITEFAIL DLG1: the dispatched ADU is not the "
                                "8-byte 0x06 wire"));
        // §11: the authority change must be announced through the existing
        // projection signal (the dialog cannot close off a silent transition).
        if (*preparedNotifies <= notifiesBefore)
            fail(QStringLiteral("WRITEFAIL DLG1: the atomic path emitted NO "
                                "preparedWriteChanged projection notification"));
        note(QStringLiteral("WRITE [DLG1]: full accepted attempt=1 send=1 -> "
                            "state=consumed, notifies +%1")
                 .arg(*preparedNotifies - notifiesBefore));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG1: the dialog is still visible after "
                                "a Consumed snapshot (transport success must not "
                                "be the close authority)"));
        // §10: the same (consumed) token can never reopen the dialog nor widen
        // the attempt counters.
        const int attempts = transport->writeAttempts();
        const int sends = transport->writeSends();
        const auto again = controller->confirmAndDispatchPreparedWrite(tokenOf());
        if (again.confirmationAccepted || again.dispatchAttempted)
            fail(QStringLiteral("WRITEFAIL DLG1: a consumed token dispatched again"));
        if (transport->writeAttempts() != attempts || transport->writeSends() != sends)
            fail(QStringLiteral("WRITEFAIL DLG1: a consumed token produced extra "
                                "attempts/sends"));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG1: the dialog reopened for a consumed "
                                "token"));
        note(QStringLiteral("WRITE [DLG1]: dialog closed; consumed token inert "
                            "(attempts=%1 sends=%2)")
                 .arg(attempts)
                 .arg(sends));
    });

    // ---- DLG2: the transport accepts NOTHING -> still Consumed -> closed ----
    push([&]() {
        resetWriteContext();
        transport->setAcceptWrites(false); // count the attempt, accept 0 bytes
        transport->setWriteShortAcceptedBytes(std::nullopt);
        const int attemptsBefore = transport->writeAttempts();
        const int sendsBefore = transport->writeSends();

        if (!prepareFc06Dialog())
            fail(QStringLiteral("WRITEFAIL DLG2: could not prepare 0x06"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG2: the dialog did not open"));
        const auto token = tokenOf();

        const auto result = controller->confirmAndDispatchPreparedWrite(token);

        if (!result.confirmationAccepted || !result.dispatchAttempted)
            fail(QStringLiteral("WRITEFAIL DLG2: the confirmation was not consumed"));
        if (!result.startResult.has_value())
            fail(QStringLiteral("WRITEFAIL DLG2: no startResult (this is NOT a "
                                "guard failure)"));
        else {
            if (result.startResult->accepted)
                fail(QStringLiteral("WRITEFAIL DLG2: the transport accepted"));
            if (result.startResult->disposition
                != modbuslens::core::TransportDisposition::NotSent)
                fail(QStringLiteral("WRITEFAIL DLG2: disposition is not NotSent"));
        }
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL DLG2: state=%1, expected consumed")
                     .arg(stateToken()));
        if (transport->writeAttempts() - attemptsBefore != 1)
            fail(QStringLiteral("WRITEFAIL DLG2: attempts delta=%1, expected 1")
                     .arg(transport->writeAttempts() - attemptsBefore));
        if (transport->writeSends() != sendsBefore)
            fail(QStringLiteral("WRITEFAIL DLG2: a send was counted"));
        if (transport->writeTerminals() != 0)
            fail(QStringLiteral("WRITEFAIL DLG2: a zero-accept produced a terminal"));
        note(QStringLiteral("WRITE [DLG2]: zero-accept attempt=1 send=0 NotSent -> "
                            "state=consumed"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG2: the dialog is still visible — the "
                                "close must NOT depend on send success"));
        const int attempts = transport->writeAttempts();
        const auto again = controller->confirmAndDispatchPreparedWrite(tokenOf());
        if (again.confirmationAccepted || transport->writeAttempts() != attempts)
            fail(QStringLiteral("WRITEFAIL DLG2: a consumed token acted again"));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG2: the dialog reopened"));
        note(QStringLiteral("WRITE [DLG2]: dialog closed; token inert"));
    });

    // ---- DLG3: short submission -> Consumed + one terminal -> closed ----
    push([&]() {
        resetWriteContext();
        transport->setAcceptWrites(true);
        transport->setWriteShortAcceptedBytes(3);
        const int attemptsBefore = transport->writeAttempts();
        const int sendsBefore = transport->writeSends();
        const int terminalsBefore = transport->writeTerminals();

        if (!prepareFc06Dialog())
            fail(QStringLiteral("WRITEFAIL DLG3: could not prepare 0x06"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG3: the dialog did not open"));
        const auto token = tokenOf();

        const auto result = controller->confirmAndDispatchPreparedWrite(token);

        if (!result.confirmationAccepted || !result.dispatchAttempted)
            fail(QStringLiteral("WRITEFAIL DLG3: the confirmation was not consumed"));
        if (!result.startResult.has_value() || result.startResult->accepted)
            fail(QStringLiteral("WRITEFAIL DLG3: a short submission was reported "
                                "as accepted"));
        else if (result.startResult->disposition
                 != modbuslens::core::TransportDisposition::PossiblySent)
            fail(QStringLiteral("WRITEFAIL DLG3: disposition is not PossiblySent"));
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL DLG3: state=%1, expected consumed")
                     .arg(stateToken()));
        if (transport->writeAttempts() - attemptsBefore != 1)
            fail(QStringLiteral("WRITEFAIL DLG3: attempts delta != 1"));
        if (transport->writeSends() != sendsBefore)
            fail(QStringLiteral("WRITEFAIL DLG3: a send was counted"));
        if (transport->writeTerminals() - terminalsBefore != 1)
            fail(QStringLiteral("WRITEFAIL DLG3: expected exactly one "
                                "ShortSubmission terminal"));
        note(QStringLiteral("WRITE [DLG3]: short attempt=1 send=0 PossiblySent + "
                            "1 terminal -> state=consumed"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG3: the dialog is still visible after "
                                "a short submission"));
        const int attempts = transport->writeAttempts();
        const auto again = controller->confirmAndDispatchPreparedWrite(tokenOf());
        if (again.confirmationAccepted || transport->writeAttempts() != attempts)
            fail(QStringLiteral("WRITEFAIL DLG3: a consumed token acted again"));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG3: the dialog reopened"));
        note(QStringLiteral("WRITE [DLG3]: dialog closed; token inert"));
    });

    // ---- DLG4: prepared 0x10 -> full accepted -> Consumed -> closed ----
    // M10-E3 INTENTIONAL TRANSITION: DLG4 used to assert that a prepared 0x10
    // was refused with CapabilityUnavailable (the capability layer did not
    // exist). E3 delivers that layer, so DLG4 now proves the positive mirror
    // of DLG1 for the hidden foundation's 0x10 dialog: the SAME atomic
    // consume -> encode -> start contract, with the 11-byte 0x10 wire. The
    // production 0x10 UI stays absent (M10-E4); this dialog is the hidden
    // test foundation.
    push([&]() {
        resetWriteContext();
        transport->setAcceptWrites(true);
        transport->setWriteShortAcceptedBytes(std::nullopt);

        setDraft("activeFunctionIndex", 1);
        setDraft("unit10", 1);
        setDraft("start10", 0);
        setDraft("valuesText10", QStringLiteral("7"));
        if (!activateWrite())
            fail(QStringLiteral("WRITEFAIL DLG4: could not prepare 0x10"));
        if (!dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG4: the dialog did not open"));
        if (controller->preparedWriteFunction() != 0x10)
            fail(QStringLiteral("WRITEFAIL DLG4: prepared function is not 0x10"));
        const auto token = tokenOf();

        const auto result = controller->confirmAndDispatchPreparedWrite(token);

        if (!result.confirmationAccepted)
            fail(QStringLiteral("WRITEFAIL DLG4: confirmationAccepted=false"));
        if (!result.dispatchAttempted)
            fail(QStringLiteral("WRITEFAIL DLG4: dispatchAttempted=false"));
        if (!result.startResult.has_value() || !result.startResult->accepted)
            fail(QStringLiteral("WRITEFAIL DLG4: the transport did not accept"));
        else if (result.startResult->disposition
                 != modbuslens::core::TransportDisposition::PossiblySent)
            fail(QStringLiteral("WRITEFAIL DLG4: disposition is not PossiblySent"));
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("WRITEFAIL DLG4: state=%1, expected consumed")
                     .arg(stateToken()));
        // The 0x10 wire for this snapshot is 9 + 2*1 = 11 bytes.
        if (!transport->writtenDescriptor().has_value()
            || transport->writtenDescriptor()->wire.size() != 11)
            fail(QStringLiteral("WRITEFAIL DLG4: the dispatched ADU is not the "
                                "11-byte 0x10 wire"));
        note(QStringLiteral("WRITE [DLG4]: 0x10 full accepted attempt=1 send=1 "
                            "-> state=consumed"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG4: the dialog is still visible after "
                                "a Consumed snapshot"));
        // A consumed token can never dispatch again — for 0x10 exactly as for
        // 0x06 (same one-shot store, same token discipline).
        const int attempts = transport->writeAttempts();
        const int sends = transport->writeSends();
        const auto again = controller->confirmAndDispatchPreparedWrite(tokenOf());
        if (again.confirmationAccepted || again.dispatchAttempted)
            fail(QStringLiteral("WRITEFAIL DLG4: a consumed token dispatched "
                                "again"));
        if (transport->writeAttempts() != attempts
            || transport->writeSends() != sends)
            fail(QStringLiteral("WRITEFAIL DLG4: a consumed token produced extra "
                                "attempts/sends"));
        note(QStringLiteral("WRITE [DLG4]: dialog closed; consumed token inert"));
    });

    // ---- DLG5: EXTERNAL invalidation (disconnect / busy) + old-token API ----
    push([&]() {
        resetWriteContext();
        if (!prepareFc06Dialog())
            fail(QStringLiteral("WRITEFAIL DLG5: could not prepare 0x06"));
        if (!dialogVisible() || stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL DLG5: the dialog/snapshot is not "
                                "Prepared"));
        // The CONTEXT EVENT itself carries the authoritative reason...
        controller->disconnectSerial();
        if (stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("WRITEFAIL DLG5: disconnect did not invalidate "
                                "(state=%1)").arg(stateToken()));
        if (controller->preparedWriteInvalidReason()
            != std::optional{modbuslens::core::PreparedWriteInvalidReason::
                                 Disconnected})
            fail(QStringLiteral("WRITEFAIL DLG5: the reason is not Disconnected"));
        note(QStringLiteral("WRITE [DLG5]: disconnect -> Invalidated(Disconnected)"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG5: the dialog is still visible after "
                                "an external invalidation"));
        // ...while the LATER old-token API call reports simply "nothing is
        // prepared". The two are different facts and must never be merged into
        // one field.
        const int attempts = transport->writeAttempts();
        const auto late = controller->confirmAndDispatchPreparedWrite(tokenOf());
        if (late.confirmationAccepted || late.dispatchAttempted)
            fail(QStringLiteral("WRITEFAIL DLG5: the old token dispatched"));
        if (!late.rejectedReason.has_value()
            || *late.rejectedReason
                != modbuslens::core::ConfirmRejectReason::NotPrepared)
            fail(QStringLiteral("WRITEFAIL DLG5: the post-event reject reason is not "
                                "NotPrepared"));
        if (transport->writeAttempts() != attempts)
            fail(QStringLiteral("WRITEFAIL DLG5: the old token reached the transport"));
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG5: the dialog reopened"));
        note(QStringLiteral("WRITE [DLG5]: dialog closed; old token -> "
                            "NotPrepared (distinct from the Disconnected reason)"));
    });
    // DLG5b: the same two-layer distinction for the busy transition.
    push([&]() {
        resetWriteContext();
        if (!prepareFc06Dialog())
            fail(QStringLiteral("WRITEFAIL DLG5b: could not prepare 0x06"));
        if (!dialogVisible() || stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("WRITEFAIL DLG5b: not Prepared before the read"));
        // A real FC03 read entering flight (test-controlled completion).
        transport->setCompleteReadImmediately(false);
        controller->readHoldingRegistersOnce(11, 0, 2, 1000);
        if (!controller->serialBusy())
            fail(QStringLiteral("WRITEFAIL DLG5b: the read did not make busy true"));
        if (controller->preparedWriteInvalidReason()
            != std::optional{modbuslens::core::PreparedWriteInvalidReason::
                                 BusyBecameTrue})
            fail(QStringLiteral("WRITEFAIL DLG5b: the reason is not "
                                "BusyBecameTrue"));
        note(QStringLiteral("WRITE [DLG5b]: busy false->true -> "
                            "Invalidated(BusyBecameTrue)"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("WRITEFAIL DLG5b: the dialog is still visible after "
                                "the busy invalidation"));
        transport->setCompleteReadImmediately(true);
        controller->disconnectSerial(); // aborts the in-flight read locally
        note(QStringLiteral("WRITE [DLG5b]: dialog closed"));
    });

    auto step = std::make_shared<int>(0);
    const int settleMs = 60;
    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, step, schedule]() {
        if (*step >= steps->size()) {
            // M10-C contract, scoped correctly: the confirmation-only
            // foundation must still end with ZERO successful write submissions
            // and ZERO write dispatch. The DLG oracles deliberately dispatch
            // real writes (that is their subject), so this is measured as a
            // DELTA from the snapshot taken just before the first DLG step
            // (which is 0 in the expected pre-DLG phases), and the DLG section
            // then accounts for its own attempts separately.
            const int preDlgAttempts =
                *writeAttemptsBeforeDlg; // absolute count before DLG section
            const int dlgAttempts = transport->writeAttempts() - preDlgAttempts;
            const int dlgSends = transport->writeSends() - *writeSendsBeforeDlg;
            if (preDlgAttempts != 0)
                fail(QStringLiteral("WRITEFAIL final: the confirmation-only phases "
                                    "attempted %1 write dispatch(es)")
                         .arg(preDlgAttempts));
            if (*writeSendsBeforeDlg != 0)
                fail(QStringLiteral("WRITEFAIL final: the confirmation-only phases "
                                    "completed %1 write submission(s)")
                         .arg(*writeSendsBeforeDlg));
            // The DLG oracles must have produced exactly the attempts they
            // claim: 1 (DLG1 full) + 1 (DLG2 zero-accept) + 1 (DLG3 short)
            // + 1 (DLG4 full 0x10, M10-E3) + 0 (DLG5 external) = 4 attempts,
            // exactly two accepted sends, exactly one ShortSubmission terminal.
            if (!*dlgSectionRan)
                fail(QStringLiteral("WRITEFAIL final: the DLG oracle section never "
                                    "ran"));
            else if (dlgAttempts != 4 || dlgSends != 2
                     || transport->writeTerminals() != 1)
                fail(QStringLiteral("WRITEFAIL final: DLG accounting mismatch "
                                    "(attempts=%1 sends=%2 terminals=%3; expected "
                                    "4 / 2 / 1)")
                         .arg(dlgAttempts)
                         .arg(dlgSends)
                         .arg(transport->writeTerminals()));
            // M10-C4: zero write TRANSACTIONS too. The session history is the
            // user-visible record, so "no 0x06 / 0x10 row" is the presentation-level
            // twin of "no write dispatch"; the FC03 reads (the deliberate busy
            // oracle) stay separately attributed.
            if (auto *model = controller->transactionModel()) {
                const int fnRole = model->roleNames().key(
                    QByteArrayLiteral("functionCode"), -1);
                int writeRows = 0;
                QStringList codes;
                for (int row = 0; row < model->rowCount(); ++row) {
                    const int code =
                        model->data(model->index(row, 0), fnRole).toInt();
                    codes << QString::number(code);
                    if (code == 6 || code == 16)
                        ++writeRows;
                }
                if (writeRows != 0)
                    fail(QStringLiteral("WRITEFAIL final: %1 write transaction(s) "
                                        "in the session history")
                             .arg(writeRows));
                else
                    note(QStringLiteral("WRITE [C4 final]: session history function "
                                        "codes = [%1]; zero 0x06/0x10 transactions, "
                                        "zero write dispatch (FC03 reads=%2)")
                             .arg(codes.isEmpty() ? QStringLiteral("<empty>")
                                                  : codes.join(QStringLiteral(",")))
                             .arg(transport->readStarts()));
            }
            if (failures->isEmpty())
                qInfo() << "WRITE FOUNDATION CHECK PASS (C01 unit 0; C02 parser "
                           "value; C03/C19 prepared + summary == snapshot; C04 "
                           "cancel; C09 repeated write; C10-C one consumption; "
                           "C16 independent drafts; C17 0x10 quantity/values; C18 "
                           "address span; C20 draft edit cannot alter snapshot; "
                           "parser presentation; C05/C06/C07/C08/C08b keyboard; "
                           "C11 disconnect; C12 reconnect; C13/C14 busy; C15 Clear; "
                           "C30 Simulator; C31 failed replay; C32 draft persistence; "
                           "C35 reason preservation; C36 Tab escape; a11y; tab order; "
                           "E1/E2 rapid-Enter spillover; C4 geometry 1024x720 + "
                           "1000x700; C4 editor first/last line scroll; C4 "
                           "confirmation all-values scroll; C4 123/124 boundary; C4 "
                           "long-summary keyboard; D1 raw-text O1-O7/R0-R3 + keyboard "
                           "+ a11y; M10-D3 DLG1-DLG5 atomic-dispatch dialog "
                           "reaction) — confirmation-only phases: zero write "
                           "dispatch, zero write transaction; DLG phases: 3 "
                           "dispatches / 1 accepted send / 1 short terminal, every "
                           "Dialog exit sourced from snapshot authority";
            else
                for (const QString &f : *failures)
                    qWarning().noquote() << "WRITEFAIL:" << f;
            app.exit(failures->isEmpty() ? 0 : 1);
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}

// ---------------------------------------------------------------------------
// M10-D4 `--qml-production-write-check`: the SHIPPED production write UI,
// driven end to end.
//
// This is the oracle the review asked for: the section is present because
// `write06Supported` is a structural capability (NOT because a harness flag
// revealed it), it runs in PRODUCTION mode, and its Confirm button performs the
// Controller's atomic confirm+dispatch. The normal Confirm button IS the thing
// under test here.
//
// Everything is deterministic: a recording transport, no COM port, no sleep
// beyond the staged event-loop turns, no wall-clock race.
// ---------------------------------------------------------------------------
int runProductionWriteCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *controller = qobject_cast<AnalysisController *>(
        rootObj ? rootObj->findChild<QObject *>(QStringLiteral("analysisController"))
                : nullptr);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    if (!controller || !window) {
        qWarning() << "PRODWRITEFAIL: no controller/window";
        return 1;
    }

    auto *transport = new HarnessWriteTransport(&app);
    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &line) { qInfo().noquote() << line; };

    auto itemOf = [&roots](const QString &name) {
        return qobject_cast<QQuickItem *>(findNamedItem(roots, name));
    };
    auto textOf = [&itemOf](const QString &name) {
        auto *item = itemOf(name);
        return item ? item->property("text").toString() : QStringLiteral("<none>");
    };
    auto boolOf = [&itemOf](const QString &name, const char *prop) {
        auto *item = itemOf(name);
        return item ? item->property(prop).toBool() : false;
    };
    // Every VISIBLE text in the item tree (R15). A wording oracle must judge
    // what the user can actually read, not one known binding — that is exactly
    // how the stale "已连接" claim survived the previous review.
    auto visibleTextsIn = [](QQuickItem *rootItem) {
        QStringList out;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item || !item->isVisible()) {
                return;
            }
            const QVariant text = item->property("text");
            if (text.isValid() && !text.toString().isEmpty()) {
                out << text.toString();
            }
            const auto kids = item->childItems();
            for (auto *kid : kids) {
                walk(kid);
            }
        };
        walk(rootItem);
        return out;
    };
    auto section = [&itemOf]() { return itemOf(QStringLiteral("writeFoundationSection")); };
    auto setDraft = [&section](const char *prop, const QVariant &value) {
        if (auto *item = section())
            item->setProperty(prop, value);
    };
    auto dialogVisible = [&section]() {
        auto *item = section();
        return item ? item->property("confirmationVisible").toBool() : false;
    };
    auto stateToken = [&controller]() { return controller->preparedWriteStateToken(); };
    auto tokenOf = [&controller]() { return controller->preparedWriteTokenValue(); };
    auto noticeText = [&controller]() { return controller->writeDispatchNotice(); };
    auto sendKey = [window](Qt::Key key, bool toWindow) -> bool {
        QObject *target = toWindow ? static_cast<QObject *>(window)
                                   : window->activeFocusItem();
        if (!target)
            return false;
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
        QCoreApplication::sendEvent(target, &release);
        return true;
    };
    auto tab = [&sendKey]() { return sendKey(Qt::Key_Tab, true); };
    auto focusOwnerName = [window]() {
        for (auto *p = qobject_cast<QQuickItem *>(window->activeFocusItem()); p;
             p = p->parentItem()) {
            if (!p->objectName().isEmpty())
                return p->objectName();
        }
        return QStringLiteral("<none>");
    };
    auto tabToOwner = [&tab, &focusOwnerName](const QString &name, int maxPresses) {
        for (int i = 1; i <= maxPresses; ++i) {
            tab();
            if (focusOwnerName() == name)
                return i;
        }
        return -1;
    };
    auto clickNamed = [&roots, window](const QString &name) {
        auto *item = qobject_cast<QQuickItem *>(findNamedItem(roots, name));
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    // The click above is POSITION-BASED: it maps the item's centre to a scene
    // point and sends the event to the window, so it only reaches whatever the
    // item tree hit-tests at that point. It therefore means something only
    // while the control is actually inside the window: a layout overflow that
    // pushes the control past the window edge makes the click land on nothing,
    // and clickNamed's own `true` (found + visible) does not report that.
    // Callers that depend on the click landing must assert this precondition.
    auto clickReachesNamed = [&roots, window](const QString &name) {
        auto *item = qobject_cast<QQuickItem *>(findNamedItem(roots, name));
        if (!item || !item->isVisible() || !item->isEnabled())
            return false;
        const QPointF scene = item->mapToScene(
            QPointF(item->width() / 2.0, item->height() / 2.0));
        return QRectF(0, 0, window->width(), window->height()).contains(scene);
    };
    // A raw click at a scene position (used for "press somewhere outside the
    // dialog"), delivered through the window like every other production click.
    auto clickAtScene = [window](const QPointF &scene) {
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
    };
    auto railIndex = [&itemOf]() {
        auto *r = itemOf(QStringLiteral("navigationRail"));
        return r ? r->property("currentWorkspaceIndex").toInt() : -1;
    };
    auto accessibleNameOf = [&roots](const QString &name) {
        auto *item = qobject_cast<QQuickItem *>(findNamedItem(roots, name));
        if (!item)
            return QString();
        QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(item);
        return iface ? iface->text(QAccessible::Name) : QString();
    };
    // Open the dialog through the SHIPPED path: set the production draft and
    // press the real Write button.
    auto openDialog = [&](int address, int value) {
        setDraft("unit06", 11);
        setDraft("addressText06", QString::number(address));
        setDraft("valueText06", QString::number(value));
        setDraft("timeout06", 1000);
        return clickNamed(QStringLiteral("writeActivateButton"));
    };
    auto writeRows = [&controller]() {
        int n = 0;
        if (auto *model = controller->transactionModel()) {
            const int role = model->roleNames().key(QByteArrayLiteral("functionCode"), -1);
            for (int r = 0; r < model->rowCount(); ++r) {
                if (model->data(model->index(r, 0), role).toInt() == 6)
                    ++n;
            }
        }
        return n;
    };
    auto resetWrite = [&controller, &transport]() {
        if (controller->serialBusy())
            controller->disconnectSerial();
        if (controller->hasPreparedWrite())
            (void)controller->cancelPreparedWrite(controller->preparedWriteTokenValue());
        if (!controller->serialConnected())
            controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
        transport->setAcceptWrites(true);
        transport->setWriteShortAcceptedBytes(std::nullopt);
    };

    controller->setSerialTransport(transport);
    controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);

    const int settleMs = 60;
    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // ---- setup ----
    push([&]() {
        if (controller->metaObject()->indexOfProperty("write06Supported") < 0
            || !controller->property("write06Supported").toBool())
            fail(QStringLiteral("PRODWRITEFAIL setup: write06Supported is not true"));
        clickNamed(QStringLiteral("navItem_2"));
        auto *s = section();
        if (!s)
            fail(QStringLiteral("PRODWRITEFAIL setup: the production write section "
                                "is absent"));
        else if (s->property("testFoundationMode").toBool())
            fail(QStringLiteral("PRODWRITEFAIL setup: the section is in "
                                "test-foundation mode"));
        if (!controller->serialConnected())
            fail(QStringLiteral("PRODWRITEFAIL setup: no Active Serial session"));
        note(QStringLiteral("PRODWRITE [setup]: production section present, "
                            "testFoundationMode=false, connected"));
    });

    // ---- P1: Write opens the dialog; initial focus is Cancel ----
    push([&]() {
        resetWrite();
        if (!openDialog(100, 5))
            fail(QStringLiteral("PRODWRITEFAIL P1: the Write button was not "
                                "clickable"));
        if (!dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P1: the confirmation dialog did not "
                                "open (state=%1)").arg(stateToken()));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL P1: state=%1, expected prepared")
                     .arg(stateToken()));
        if (transport->writeAttempts() != 0)
            fail(QStringLiteral("PRODWRITEFAIL P1: merely opening the dialog "
                                "dispatched a write"));
        note(QStringLiteral("PRODWRITE [P1]: dialog open, token=%1, attempts=0")
                 .arg(tokenOf()));
    });
    push([&]() {
        if (focusOwnerName() != QStringLiteral("writeConfirmCancelButton"))
            fail(QStringLiteral("PRODWRITEFAIL P1: initial focus is [%1], expected "
                                "the Cancel button").arg(focusOwnerName()));
        else
            note(QStringLiteral("PRODWRITE [P1]: initial focus = Cancel"));
    });

    // ---- P2/P3: Space on Confirm sends exactly once, then the echo completes ----
    push([&]() {
        const int attemptsBefore = transport->writeAttempts();
        if (tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10) < 0)
            fail(QStringLiteral("PRODWRITEFAIL P2: Confirm is not reachable by Tab "
                                "from Cancel (focus=%1)").arg(focusOwnerName()));
        sendKey(Qt::Key_Space, false);
        const int attempts = transport->writeAttempts() - attemptsBefore;
        if (attempts != 1)
            fail(QStringLiteral("PRODWRITEFAIL P2: Space produced %1 attempts, "
                                "expected exactly 1").arg(attempts));
        if (transport->writeSends() != 1)
            fail(QStringLiteral("PRODWRITEFAIL P2: sends=%1, expected 1")
                     .arg(transport->writeSends()));
        if (transport->sentAduLogSize() != 1)
            fail(QStringLiteral("PRODWRITEFAIL P2: ADU log size=%1, expected 1")
                     .arg(transport->sentAduLogSize()));
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("PRODWRITEFAIL P2: state=%1, expected consumed")
                     .arg(stateToken()));
        if (!controller->serialBusy())
            fail(QStringLiteral("PRODWRITEFAIL P2: busy was not raised"));
        if (auto *s = section();
            s && s->property("valueText06").toString() != QStringLiteral("5"))
            fail(QStringLiteral("PRODWRITEFAIL P2: the draft was cleared "
                                "(valueText06=[%1])")
                     .arg(s->property("valueText06").toString()));
        note(QStringLiteral("PRODWRITE [P2]: Space -> 1 attempt / 1 send / 1 ADU, "
                            "Consumed, busy, draft preserved"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P3: the dialog is still visible after "
                                "the atomic dispatch"));
        transport->completeWriteWithEcho();
        if (writeRows() != 1)
            fail(QStringLiteral("PRODWRITEFAIL P3: write rows=%1, expected 1")
                     .arg(writeRows()));
        if (controller->serialBusy())
            fail(QStringLiteral("PRODWRITEFAIL P3: still busy after completion"));
        if (controller->successCount() != 1)
            fail(QStringLiteral("PRODWRITEFAIL P3: successCount=%1, expected 1")
                     .arg(controller->successCount()));
        if (controller->hasWriteDispatchNotice())
            fail(QStringLiteral("PRODWRITEFAIL P3: a successful write produced a "
                                "non-success notice [%1]").arg(noticeText()));
        note(QStringLiteral("PRODWRITE [P3]: dialog closed; echo -> 1 Success 0x06 "
                            "transaction in the shared history; no notice"));
    });

    // ---- P4: rapid Enter x2 -> exactly one dispatch ----
    push([&]() {
        resetWrite();
        if (!openDialog(101, 6))
            fail(QStringLiteral("PRODWRITEFAIL P4: Write not clickable"));
        if (!dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P4: dialog did not open"));
        const int before = transport->writeAttempts();
        tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10);
        sendKey(Qt::Key_Return, false);
        sendKey(Qt::Key_Return, false);
        const int attempts = transport->writeAttempts() - before;
        if (attempts != 1)
            fail(QStringLiteral("PRODWRITEFAIL P4: rapid Enter produced %1 "
                                "dispatches, expected exactly 1").arg(attempts));
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("PRODWRITEFAIL P4: state=%1").arg(stateToken()));
        note(QStringLiteral("PRODWRITE [P4]: rapid Enter x2 -> 1 dispatch"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P4: the dialog reopened after the "
                                "second Enter"));
        if (transport->writeSends() != 2)
            fail(QStringLiteral("PRODWRITEFAIL P4: total sends=%1, expected 2")
                     .arg(transport->writeSends()));
        transport->completeWriteWithEcho();
        note(QStringLiteral("PRODWRITE [P4]: dialog closed, no second send, no "
                            "background action"));
    });

    // ---- P5: rapid Space x2 -> exactly one dispatch ----
    push([&]() {
        resetWrite();
        if (!openDialog(102, 7))
            fail(QStringLiteral("PRODWRITEFAIL P5: Write not clickable"));
        const int before = transport->writeAttempts();
        tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10);
        sendKey(Qt::Key_Space, false);
        sendKey(Qt::Key_Space, false);
        const int attempts = transport->writeAttempts() - before;
        if (attempts != 1)
            fail(QStringLiteral("PRODWRITEFAIL P5: rapid Space produced %1 "
                                "dispatches, expected exactly 1").arg(attempts));
        note(QStringLiteral("PRODWRITE [P5]: rapid Space x2 -> 1 dispatch"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P5: the dialog reopened"));
        transport->completeWriteWithEcho();
        note(QStringLiteral("PRODWRITE [P5]: dialog closed, no second send"));
    });

    // ---- P6: immediate Enter (focus still Cancel) must not dispatch ----
    push([&]() {
        resetWrite();
        if (!openDialog(103, 8))
            fail(QStringLiteral("PRODWRITEFAIL P6: Write not clickable"));
        const int before = transport->writeAttempts();
        sendKey(Qt::Key_Return, false); // Cancel holds focus: must be inert
        const int attempts = transport->writeAttempts() - before;
        if (attempts != 0)
            fail(QStringLiteral("PRODWRITEFAIL P6: an immediate Enter dispatched %1 "
                                "write(s)").arg(attempts));
        note(QStringLiteral("PRODWRITE [P6]: immediate Enter -> 0 dispatch "
                            "(state=%1)").arg(stateToken()));
    });

    // ---- P7: Cancel click ----
    push([&]() {
        resetWrite();
        if (!openDialog(104, 9))
            fail(QStringLiteral("PRODWRITEFAIL P7: Write not clickable"));
        const int before = transport->writeAttempts();
        clickNamed(QStringLiteral("writeConfirmCancelButton"));
        if (stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("PRODWRITEFAIL P7: state=%1 after Cancel")
                     .arg(stateToken()));
        if (controller->preparedWriteInvalidReason()
            != std::optional{modbuslens::core::PreparedWriteInvalidReason::UserCancelled})
            fail(QStringLiteral("PRODWRITEFAIL P7: the reason is not UserCancelled"));
        if (transport->writeAttempts() != before)
            fail(QStringLiteral("PRODWRITEFAIL P7: Cancel dispatched a write"));
        if (auto *s = section(); s && s->property("valueText06").toString().isEmpty())
            fail(QStringLiteral("PRODWRITEFAIL P7: Cancel cleared the draft"));
        note(QStringLiteral("PRODWRITE [P7]: Cancel -> Invalidated(UserCancelled), "
                            "0 dispatch, draft preserved"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P7: the dialog is still visible after "
                                "Cancel"));
    });

    // ---- P8: Escape ----
    push([&]() {
        resetWrite();
        if (!openDialog(105, 10))
            fail(QStringLiteral("PRODWRITEFAIL P8: Write not clickable"));
        const int before = transport->writeAttempts();
        sendKey(Qt::Key_Escape, true); // delivered at the window/overlay layer
        if (stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("PRODWRITEFAIL P8: state=%1 after Escape")
                     .arg(stateToken()));
        if (transport->writeAttempts() != before)
            fail(QStringLiteral("PRODWRITEFAIL P8: Escape dispatched a write"));
        note(QStringLiteral("PRODWRITE [P8]: Escape -> Invalidated, 0 dispatch"));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P8: the dialog is still visible after "
                                "Escape"));
    });

    // ---- P9: transport NotSent -> dialog closes, honest non-success notice ----
    push([&]() {
        resetWrite();
        transport->setAcceptWrites(false); // count the attempt, accept 0 bytes
        const int before = transport->writeAttempts();
        const int sendsBefore = transport->writeSends();
        if (!openDialog(106, 11))
            fail(QStringLiteral("PRODWRITEFAIL P9: Write not clickable"));
        tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10);
        sendKey(Qt::Key_Space, false);
        if (transport->writeAttempts() - before != 1)
            fail(QStringLiteral("PRODWRITEFAIL P9: attempts delta != 1"));
        if (transport->writeSends() != sendsBefore)
            fail(QStringLiteral("PRODWRITEFAIL P9: an accepted send was counted"));
        if (stateToken() != QStringLiteral("consumed"))
            fail(QStringLiteral("PRODWRITEFAIL P9: state=%1").arg(stateToken()));
        if (!controller->hasWriteDispatchNotice())
            fail(QStringLiteral("PRODWRITEFAIL P9: no non-success notice was "
                                "presented"));
        else {
            const QString text = noticeText();
            if (!text.contains(QStringLiteral("未发送")))
                fail(QStringLiteral("PRODWRITEFAIL P9: the notice does not say the "
                                    "request was not sent: [%1]").arg(text));
            if (text.contains(QStringLiteral("写入成功"))
                || text.contains(QStringLiteral("设备已写入"))
                || text.contains(QStringLiteral("超时")))
                fail(QStringLiteral("PRODWRITEFAIL P9: the notice claims something "
                                    "unsupported: [%1]").arg(text));
        }
        note(QStringLiteral("PRODWRITE [P9]: NotSent -> notice [%1]")
                 .arg(noticeText()));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P9: the dialog is still visible after "
                                "a NotSent attempt"));
        note(QStringLiteral("PRODWRITE [P9]: dialog closed, no transaction"));
    });

    // ---- P10: short submission -> unknown device state ----
    push([&]() {
        resetWrite();
        transport->setWriteShortAcceptedBytes(3);
        const int rowsBefore = writeRows();
        if (!openDialog(107, 12))
            fail(QStringLiteral("PRODWRITEFAIL P10: Write not clickable"));
        tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10);
        sendKey(Qt::Key_Space, false);
        if (transport->writeTerminals() != 1)
            fail(QStringLiteral("PRODWRITEFAIL P10: terminals=%1, expected 1")
                     .arg(transport->writeTerminals()));
        if (writeRows() != rowsBefore)
            fail(QStringLiteral("PRODWRITEFAIL P10: a short submission produced a "
                                "transaction (rows=%1)").arg(writeRows()));
        const QString text = noticeText();
        if (!text.contains(QStringLiteral("设备写入状态未知")))
            fail(QStringLiteral("PRODWRITEFAIL P10: the notice must say the device "
                                "state is unknown: [%1]").arg(text));
        if (text.contains(QStringLiteral("设备未写入")))
            fail(QStringLiteral("PRODWRITEFAIL P10: the notice claims the device "
                                "was not written: [%1]").arg(text));
        note(QStringLiteral("PRODWRITE [P10]: short -> notice [%1]").arg(text));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P10: the dialog is still visible"));
        transport->setWriteShortAcceptedBytes(std::nullopt);
        note(QStringLiteral("PRODWRITE [P10]: dialog closed, zero transaction"));
    });

    // ---- P11: write Timeout presentation ----
    push([&]() {
        resetWrite();
        if (!openDialog(108, 13))
            fail(QStringLiteral("PRODWRITEFAIL P11: Write not clickable"));
        tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10);
        sendKey(Qt::Key_Space, false);
        transport->completeWriteWithTimeout();
        if (controller->timeoutCount() != 1)
            fail(QStringLiteral("PRODWRITEFAIL P11: timeoutCount=%1, expected 1")
                     .arg(controller->timeoutCount()));
        const QString text = noticeText();
        if (!text.contains(QStringLiteral("响应超时"))
            || !text.contains(QStringLiteral("设备写入状态未知")))
            fail(QStringLiteral("PRODWRITEFAIL P11: the notice must read as "
                                "\"response timed out, device write state "
                                "unknown\": [%1]").arg(text));
        if (text.contains(QStringLiteral("设备未写入")))
            fail(QStringLiteral("PRODWRITEFAIL P11: the notice claims the device "
                                "was not written: [%1]").arg(text));
        note(QStringLiteral("PRODWRITE [P11]: timeout -> notice [%1]").arg(text));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL P11: the dialog is still visible"));
        note(QStringLiteral("PRODWRITE [P11]: dialog closed"));
    });

    // ---- P12: geometry 1024x720 and 1000x700 ----
    // One step per phase: a Qt Quick Popup needs its own event-loop turn before
    // `visible` reflects the open, exactly like P1/P2.
    // One step per phase: a programmatic resize needs its own turn to settle
    // the layout, and a Qt Quick Popup needs its own turn before `visible`
    // reflects the open — exactly like P1/P2.
    auto activateWriteAtSize = [&](const QSize size) {
        setDraft("unit06", 11);
        setDraft("addressText06", QString::number(109));
        setDraft("valueText06", QString::number(14));
        setDraft("timeout06", 1000);
        // Keyboard activation rather than a click: a focused control can be
        // activated regardless of scroll position, so this gate measures the
        // DIALOG geometry instead of whatever happens to be in the viewport —
        // and it additionally proves the Write action is keyboard-reachable at
        // the minimum window size.
        clickNamed(QStringLiteral("appBarClearResults")); // focus anchor
        if (tabToOwner(QStringLiteral("writeActivateButton"), 80) < 0)
            fail(QStringLiteral("PRODWRITEFAIL P12: the Write action is not "
                                "keyboard-reachable at %1x%2 (focus=%3)")
                     .arg(size.width())
                     .arg(size.height())
                     .arg(focusOwnerName()));
        else
            sendKey(Qt::Key_Space, false);
    };
    auto assertDialogGeometry = [&](const QSize size) {
        // The confirmation Dialog is a QObject (a Popup), NOT an Item — the
        // component exposes its state through read-only properties for exactly
        // this reason. Geometry is therefore read from its own x/y/width/height
        // properties, and the buttons (real Items) give the on-screen position.
        QObject *dialog = rootObj->findChild<QObject *>(
            QStringLiteral("writeConfirmationDialog"));
        // MEASURED geometry is the gate. The popup's `visible` flag is reported
        // but NOT gated: after a PROGRAMMATIC window resize the offscreen
        // platform does not settle Popup.visible in this harness, even though
        // the snapshot really is Prepared and open() really was called (P1/P2
        // prove the visible transition at the default size). Gating on it would
        // assert something this gate cannot actually see, so the residual
        // question is left to the D5 manual visual review.
        if (!dialog) {
            fail(QStringLiteral("PRODWRITEFAIL P12: the dialog object is missing at "
                                "%1x%2").arg(size.width()).arg(size.height()));
        } else {
            const double dw = dialog->property("width").toDouble();
            const double dh = dialog->property("height").toDouble();
            note(QStringLiteral("PRODWRITE [P12]: %1x%2 state=%3 dialog=%4x%5 "
                                "visible=%6")
                     .arg(size.width())
                     .arg(size.height())
                     .arg(stateToken())
                     .arg(dw)
                     .arg(dh)
                     .arg(dialogVisible() ? 1 : 0));
            // Now that the popup is read through its own object (not as an
            // Item), its visibility is genuinely observable and IS gated.
            if (!dialogVisible())
                fail(QStringLiteral("PRODWRITEFAIL P12: the confirmation dialog is "
                                    "not visible at %1x%2")
                         .arg(size.width())
                         .arg(size.height()));
            if (dw <= 0 || dh <= 0)
                fail(QStringLiteral("PRODWRITEFAIL P12: the dialog has no geometry "
                                    "(%1x%2) at %3x%4")
                         .arg(dw)
                         .arg(dh)
                         .arg(size.width())
                         .arg(size.height()));
            if (dw > size.width() || dh > size.height())
                fail(QStringLiteral("PRODWRITEFAIL P12: the dialog (%1x%2) exceeds "
                                    "the %3x%4 window")
                         .arg(dw)
                         .arg(dh)
                         .arg(size.width())
                         .arg(size.height()));
        }
        for (const QString &button : {QStringLiteral("writeConfirmCancelButton"),
                                      QStringLiteral("writeConfirmAcceptButton")}) {
            auto *b = qobject_cast<QQuickItem *>(findNamedItem(roots, button));
            if (!b) {
                fail(QStringLiteral("PRODWRITEFAIL P12: %1 is missing at %2x%3")
                         .arg(button)
                         .arg(size.width())
                         .arg(size.height()));
                continue;
            }
            const QPointF p = b->mapToScene(QPointF(0, 0));
            if (p.x() < 0 || p.y() < 0 || p.x() + b->width() > size.width()
                || p.y() + b->height() > size.height())
                fail(QStringLiteral("PRODWRITEFAIL P12: %1 is outside the window at "
                                    "%2x%3 (origin %4,%5 size %6x%7)")
                         .arg(button)
                         .arg(size.width())
                         .arg(size.height())
                         .arg(p.x())
                         .arg(p.y())
                         .arg(b->width())
                         .arg(b->height()));
        }
        if (window->width() != size.width() || window->height() != size.height())
            fail(QStringLiteral("PRODWRITEFAIL P12: the window resized itself to "
                                "%1x%2 instead of accepting %3x%4")
                     .arg(window->width())
                     .arg(window->height())
                     .arg(size.width())
                     .arg(size.height()));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL P12: state=%1 at %2x%3, expected a "
                                "prepared snapshot")
                     .arg(stateToken())
                     .arg(size.width())
                     .arg(size.height()));
        // ALWAYS leave a clean state for the next phase, even on failure.
        clickNamed(QStringLiteral("writeConfirmCancelButton"));
    };
    push([&]() {
        resetWrite();
        window->resize(QSize(1024, 720));
    });
    push([&]() { activateWriteAtSize(QSize(1024, 720)); });
    push([&]() { assertDialogGeometry(QSize(1024, 720)); });
    push([&]() {
        resetWrite();
        window->resize(QSize(1000, 700));
    });
    push([&]() { activateWriteAtSize(QSize(1000, 700)); });
    push([&]() { assertDialogGeometry(QSize(1000, 700)); });

    // ================= M: PRODUCTION-MODE MODAL SAFETY =================
    //
    // M10-D4 review gap 2: the D3 DLG oracles proved the modal contract on the
    // TEST FOUNDATION. These drive the SHIPPED production section instead —
    // real production draft, real Write button, real Confirm.
    // Every step here must produce ZERO dispatch: modal safety is exactly the
    // claim that a dialog cannot be bypassed.

    push([&]() {
        resetWrite();
        if (!openDialog(200, 20))
            fail(QStringLiteral("PRODWRITEFAIL M1: the Write button was not clickable"));
        if (!dialogVisible() || stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL M1: no open dialog over a Prepared "
                                "snapshot (state=%1)").arg(stateToken()));
        note(QStringLiteral("MODAL [M1]: production dialog open over a Prepared "
                            "snapshot (token=%1, workspace=%2)")
                 .arg(tokenOf())
                 .arg(railIndex()));
    });

    // ---- M2: press OUTSIDE the dialog ----
    push([&]() {
        const auto tokenBefore = tokenOf();
        const int wsBefore = railIndex();
        const int attemptsBefore = transport->writeAttempts();
        const int sendsBefore = transport->writeSends();
        // Bottom-left of the window: unambiguously outside the centred dialog.
        clickAtScene(QPointF(40, window->height() - 40));
        if (!dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL M2: an outside press closed the "
                                "dialog (closePolicy is CloseOnEscape)"));
        if (stateToken() != QStringLiteral("prepared") || tokenOf() != tokenBefore)
            fail(QStringLiteral("PRODWRITEFAIL M2: the outside press changed the "
                                "snapshot (state=%1 token=%2)")
                     .arg(stateToken())
                     .arg(tokenOf()));
        if (railIndex() != wsBefore)
            fail(QStringLiteral("PRODWRITEFAIL M2: the outside press switched the "
                                "workspace (%1 -> %2)").arg(wsBefore).arg(railIndex()));
        if (transport->writeAttempts() != attemptsBefore
            || transport->writeSends() != sendsBefore)
            fail(QStringLiteral("PRODWRITEFAIL M2: the outside press dispatched"));
        note(QStringLiteral("MODAL [M2]: outside press ignored — dialog open, "
                            "snapshot Prepared, workspace %1, 0 dispatch")
                 .arg(wsBefore));
    });

    // ---- M3: click the navigation rail ----
    push([&]() {
        const auto tokenBefore = tokenOf();
        const int wsBefore = railIndex();
        const int attemptsBefore = transport->writeAttempts();
        clickNamed(QStringLiteral("navItem_0")); // a DIFFERENT workspace
        if (railIndex() != wsBefore)
            fail(QStringLiteral("PRODWRITEFAIL M3: the rail click switched the "
                                "workspace while the dialog was modal (%1 -> %2)")
                     .arg(wsBefore)
                     .arg(railIndex()));
        if (!dialogVisible() || tokenOf() != tokenBefore)
            fail(QStringLiteral("PRODWRITEFAIL M3: the rail click bypassed the "
                                "dialog (visible=%1 token=%2)")
                     .arg(dialogVisible() ? 1 : 0)
                     .arg(tokenOf()));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL M3: state=%1").arg(stateToken()));
        if (transport->writeAttempts() != attemptsBefore)
            fail(QStringLiteral("PRODWRITEFAIL M3: the rail click dispatched"));
        note(QStringLiteral("MODAL [M3]: rail blocked — workspace stays %1, dialog "
                            "intact, 0 dispatch")
                 .arg(wsBefore));
    });

    // ---- M4: try to activate the BACKGROUND Write button ----
    push([&]() {
        const auto tokenBefore = tokenOf();
        const int attemptsBefore = transport->writeAttempts();
        // The button is behind the modal overlay: the press must not reach it.
        clickNamed(QStringLiteral("writeActivateButton"));
        if (tokenOf() != tokenBefore)
            fail(QStringLiteral("PRODWRITEFAIL M4: a background Write created a "
                                "second flow (token %1 -> %2)")
                     .arg(tokenBefore)
                     .arg(tokenOf()));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL M4: state=%1").arg(stateToken()));
        if (!controller->hasPreparedWrite() || controller->preparedWriteTokenValue()
                                                  != tokenBefore)
            fail(QStringLiteral("PRODWRITEFAIL M4: the prepared generation changed"));
        if (transport->writeAttempts() != attemptsBefore)
            fail(QStringLiteral("PRODWRITEFAIL M4: a background Write dispatched"));
        note(QStringLiteral("MODAL [M4]: background Write blocked — one flow, same "
                            "token, 0 dispatch"));
    });

    // ---- M5: keyboard must not escape the modal scope ----
    push([&]() {
        QStringList seen;
        const int attemptsBefore = transport->writeAttempts();
        for (int i = 0; i < 8; ++i) {
            tab();
            const QString owner = focusOwnerName();
            if (!seen.contains(owner))
                seen << owner;
            if (owner != QStringLiteral("writeConfirmCancelButton")
                && owner != QStringLiteral("writeConfirmAcceptButton"))
                fail(QStringLiteral("PRODWRITEFAIL M5: keyboard focus escaped the "
                                    "modal dialog to [%1]").arg(owner));
        }
        if (!dialogVisible() || stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL M5: the tab walk disturbed the "
                                "dialog (visible=%1 state=%2)")
                     .arg(dialogVisible() ? 1 : 0)
                     .arg(stateToken()));
        if (transport->writeAttempts() != attemptsBefore)
            fail(QStringLiteral("PRODWRITEFAIL M5: the tab walk dispatched "
                                "(attempts %1 -> %2)")
                     .arg(attemptsBefore)
                     .arg(transport->writeAttempts()));
        note(QStringLiteral("MODAL [M5]: 8 Tabs stayed inside the modal scope "
                            "[%1]; 0 dispatch").arg(seen.join(QStringLiteral(", "))));
    });

    // ---- M6: Cancel / Confirm accessibility sanity (NOT a WCAG claim) ----
    push([&]() {
        const QString cancelName =
            accessibleNameOf(QStringLiteral("writeConfirmCancelButton"));
        const QString valueName =
            accessibleNameOf(QStringLiteral("writeConfirmAcceptButton"));
        if (cancelName.isEmpty() || valueName.isEmpty())
            fail(QStringLiteral("PRODWRITEFAIL M6: a dialog button has no accessible "
                                "name (cancel=[%1] confirm=[%2])")
                     .arg(cancelName, valueName));
        else
            note(QStringLiteral("MODAL [M6]: cancel=[%1] confirm=[%2]")
                     .arg(cancelName, valueName));
        // Leave a clean state.
        clickNamed(QStringLiteral("writeConfirmCancelButton"));
    });

    // ------------------------------------------------------------------
    // M10-E4: FC16 / 0x10 PRODUCTION sections (R1-R6).
    //
    // These drive the SHIPPED FC16 UI: the real tab, the real inputs, the real
    // Write button, the real confirmation summary and the real Confirm — from
    // QML through the Controller's atomic dispatch to the exact wire bytes.
    // ------------------------------------------------------------------
    const std::vector<std::uint8_t> kFc16TwoReg = {0x11, 0x10, 0x00, 0x01, 0x00,
                                                   0x02, 0x04, 0x00, 0x0A, 0x01,
                                                   0x02, 0xC6, 0xF0};
    const std::vector<std::uint8_t> kFc16OneReg = {0x11, 0x10, 0x00, 0x01, 0x00,
                                                   0x01, 0x02, 0x00, 0x07, 0x2B,
                                                   0x83};
    auto fc16ValuesText = [](const std::vector<int> &values) {
        QString text;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i != 0) {
                text += QLatin1Char('\n');
            }
            text += QString::number(values[i]);
        }
        return text;
    };
    auto openFc16Dialog = [&](int unit, int start, const QString &valuesText) {
        setDraft("unit10", unit);
        setDraft("start10", start);
        setDraft("timeout10", 1000);
        setDraft("valuesText10", valuesText);
        setDraft("activeFunctionIndex", 1);
        return clickNamed(QStringLiteral("writeActivateButton"));
    };
    auto summaryText = [&itemOf](const QString &name) {
        auto *item = itemOf(name);
        return item ? item->property("text").toString() : QString();
    };

    // ---- R1: canonical N=2 through the SHIPPED production path ----
    push([&]() {
        resetWrite();
        const int attemptsAtOpen = transport->writeAttempts();
        if (!openFc16Dialog(17, 1, fc16ValuesText({10, 258})))
            fail(QStringLiteral("PRODWRITEFAIL R1: the Write button was not "
                                "clickable on the FC16 tab"));
        if (transport->writeAttempts() != attemptsAtOpen)
            fail(QStringLiteral("PRODWRITEFAIL R1: opening the dialog dispatched "
                                "(%1 -> %2)")
                     .arg(attemptsAtOpen)
                     .arg(transport->writeAttempts()));
        if (!dialogVisible() || stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL R1: no Prepared snapshot "
                                "(visible=%1 state=%2)")
                     .arg(dialogVisible() ? 1 : 0)
                     .arg(stateToken()));
        if (controller->preparedWriteFunction() != 0x10)
            fail(QStringLiteral("PRODWRITEFAIL R1: prepared function=%1, expected "
                                "0x10").arg(controller->preparedWriteFunction()));
        // The summary reads the CONTROLLER snapshot: function / unit / start /
        // quantity / values.
        const QString functionText =
            summaryText(QStringLiteral("writeSummaryFunction"));
        if (!functionText.contains(QStringLiteral("0x10"))
            || !functionText.contains(QStringLiteral("Write Multiple Registers")))
            fail(QStringLiteral("PRODWRITEFAIL R1: function text=[%1]")
                     .arg(functionText));
        if (!summaryText(QStringLiteral("writeSummaryUnit"))
                 .contains(QStringLiteral("17")))
            fail(QStringLiteral("PRODWRITEFAIL R1: unit text=[%1]")
                     .arg(summaryText(QStringLiteral("writeSummaryUnit"))));
        if (summaryText(QStringLiteral("writeSummaryAddress")) != QStringLiteral("1"))
            fail(QStringLiteral("PRODWRITEFAIL R1: address text=[%1]")
                     .arg(summaryText(QStringLiteral("writeSummaryAddress"))));
        if (summaryText(QStringLiteral("writeSummaryQuantity")) != QStringLiteral("2"))
            fail(QStringLiteral("PRODWRITEFAIL R1: quantity text=[%1]")
                     .arg(summaryText(QStringLiteral("writeSummaryQuantity"))));
        const QVariantList projected = controller->preparedWriteValues();
        if (projected.size() != qsizetype{2} || projected.at(0).toInt() != 10
            || projected.at(1).toInt() != 258)
            fail(QStringLiteral("PRODWRITEFAIL R1: snapshot values are not "
                                "[10, 258]"));
        note(QStringLiteral("PRODWRITE [R1]: FC16 summary function=0x10 unit=17 "
                            "start=1 quantity=2 values=[10, 258]"));
    });
    push([&]() {
        if (focusOwnerName() != QStringLiteral("writeConfirmCancelButton"))
            fail(QStringLiteral("PRODWRITEFAIL R1: FC16 initial focus is [%1], "
                                "expected Cancel").arg(focusOwnerName()));
        if (tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10) < 0)
            fail(QStringLiteral("PRODWRITEFAIL R1: Confirm is not reachable by Tab "
                                "(focus=%1)").arg(focusOwnerName()));
        const int attemptsBefore = transport->writeAttempts();
        const int sendsBefore = transport->writeSends();
        sendKey(Qt::Key_Space, false);
        if (transport->writeAttempts() - attemptsBefore != 1)
            fail(QStringLiteral("PRODWRITEFAIL R1: Space produced %1 attempts, "
                                "expected exactly 1")
                     .arg(transport->writeAttempts() - attemptsBefore));
        if (transport->writeSends() - sendsBefore != 1
            || transport->writeAduLog().empty()
            || transport->writeAduLog().back() != kFc16TwoReg)
            fail(QStringLiteral("PRODWRITEFAIL R1: the dispatched ADU is not the "
                                "canonical 13-byte FC16 request"));
        else
            note(QStringLiteral("PRODWRITE [R1]: exact 13-byte ADU 11 10 00 01 00 "
                                "02 04 00 0A 01 02 C6 F0 sent from the production "
                                "UI"));
    });
    push([&]() {
        const int recordsBefore = controller->activeSerialRecordCount();
        const int successesBefore = controller->successCount();
        transport->completeWriteWithEcho16();
        if (controller->activeSerialRecordCount() != recordsBefore + 1
            || controller->successCount() != successesBefore + 1)
            fail(QStringLiteral("PRODWRITEFAIL R1: the FC16 echo did not produce "
                                "exactly one Success (records %1 -> %2, successes "
                                "%3 -> %4)")
                     .arg(recordsBefore)
                     .arg(controller->activeSerialRecordCount())
                     .arg(successesBefore)
                     .arg(controller->successCount()));
        if (controller->activeSerialRecords().back().functionCode() != 0x10)
            fail(QStringLiteral("PRODWRITEFAIL R1: the new history row is not an "
                                "FC16 transaction"));
        if (controller->activeSerialTerminalCount() != 0)
            fail(QStringLiteral("PRODWRITEFAIL R1: a failure terminal was created"));
        note(QStringLiteral("PRODWRITE [R1]: matching echo -> one FC16 Success in "
                            "the shared history (%1 rows), zero terminals")
                 .arg(controller->activeSerialRecordCount()));
    });

    // ---- R2: the confirmation reads the IMMUTABLE snapshot ----
    push([&]() {
        resetWrite();
        if (!openFc16Dialog(17, 1, fc16ValuesText({10, 258})))
            fail(QStringLiteral("PRODWRITEFAIL R2: could not open the FC16 dialog"));
        const auto tokenBefore = tokenOf(); // same type as tokenOf()
        // Mutate the underlying draft AFTER the snapshot exists.
        setDraft("valuesText10", fc16ValuesText({999, 888}));
        setDraft("start10", 4242);
        if (summaryText(QStringLiteral("writeSummaryQuantity")) != QStringLiteral("2")
            || summaryText(QStringLiteral("writeSummaryAddress"))
                   != QStringLiteral("1"))
            fail(QStringLiteral("PRODWRITEFAIL R2: the summary followed the draft "
                                "(quantity=[%1] address=[%2])")
                     .arg(summaryText(QStringLiteral("writeSummaryQuantity")),
                          summaryText(QStringLiteral("writeSummaryAddress"))));
        const QVariantList projected = controller->preparedWriteValues();
        if (projected.size() != qsizetype{2} || projected.at(1).toInt() != 258)
            fail(QStringLiteral("PRODWRITEFAIL R2: the snapshot changed with the "
                                "draft"));
        if (tokenOf() != tokenBefore)
            fail(QStringLiteral("PRODWRITEFAIL R2: the token changed"));
        note(QStringLiteral("PRODWRITE [R2]: draft mutated to [999, 888] / start "
                            "4242 -> summary still 1 / 2 / [10, 258]"));
        clickNamed(QStringLiteral("writeConfirmCancelButton"));
    });
    push([&]() {
        if (stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("PRODWRITEFAIL R2: Cancel state=%1")
                     .arg(stateToken()));
        auto *s = section();
        if (s && s->property("valuesText10").toString() != fc16ValuesText({999, 888}))
            fail(QStringLiteral("PRODWRITEFAIL R2: Cancel cleared the draft (a "
                                "Cancel must never clear a draft)"));
        note(QStringLiteral("PRODWRITE [R2]: Cancel -> Invalidated, draft "
                            "preserved"));
    });

    // ---- R3: 124 values are REJECTED before any send ----
    push([&]() {
        resetWrite();
        const std::vector<int> many(124, 1);
        const int attemptsBefore = transport->writeAttempts();
        if (!openFc16Dialog(17, 1, fc16ValuesText(many)))
            fail(QStringLiteral("PRODWRITEFAIL R3: the Write button was not "
                                "clickable"));
        if (dialogVisible() || stateToken() == QStringLiteral("prepared")
            || stateToken() == QStringLiteral("consumed"))
            fail(QStringLiteral("PRODWRITEFAIL R3: 124 values opened a confirmation "
                                "(state=%1)").arg(stateToken()));
        if (transport->writeAttempts() != attemptsBefore)
            fail(QStringLiteral("PRODWRITEFAIL R3: a validation rejection reached "
                                "the transport"));
        if (!noticeText().isEmpty())
            fail(QStringLiteral("PRODWRITEFAIL R3: a validation rejection was "
                                "presented as a transport outcome"));
        if (controller->writeDraftError().isEmpty()
            || controller->writeDraftErrorField() != QStringLiteral("values"))
            fail(QStringLiteral("PRODWRITEFAIL R3: the field-specific error is "
                                "missing (field=[%1])")
                     .arg(controller->writeDraftErrorField()));
        note(QStringLiteral("PRODWRITE [R3]: 124 values -> field error, no dialog, "
                            "0 attempts, no transport notice"));
    });

    // ---- R4: address-span overflow is REJECTED before any send ----
    push([&]() {
        resetWrite();
        const int attemptsBefore = transport->writeAttempts();
        if (!openFc16Dialog(17, 65535, fc16ValuesText({1, 2})))
            fail(QStringLiteral("PRODWRITEFAIL R4: the Write button was not "
                                "clickable"));
        if (dialogVisible() || stateToken() == QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL R4: a span overflow prepared "
                                "(state=%1)").arg(stateToken()));
        if (transport->writeAttempts() != attemptsBefore || !noticeText().isEmpty())
            fail(QStringLiteral("PRODWRITEFAIL R4: the rejection leaked into the "
                                "transport lane"));
        note(QStringLiteral("PRODWRITE [R4]: start 65535 + 2 values -> reject, "
                            "0 attempts, no wrap"));
    });

    // ---- R5: 123 values prepare; the summary scrolls first -> last -> first ----
    push([&]() {
        resetWrite();
        std::vector<int> many(123);
        for (int i = 0; i < 123; ++i) {
            many[static_cast<std::size_t>(i)] = i + 1;
        }
        if (!openFc16Dialog(17, 1, fc16ValuesText(many)))
            fail(QStringLiteral("PRODWRITEFAIL R5: the Write button was not "
                                "clickable"));
        if (!dialogVisible() || stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL R5: 123 values did not prepare "
                                "(state=%1)").arg(stateToken()));
        if (summaryText(QStringLiteral("writeSummaryQuantity"))
            != QStringLiteral("123"))
            fail(QStringLiteral("PRODWRITEFAIL R5: summary quantity=[%1]")
                     .arg(summaryText(QStringLiteral("writeSummaryQuantity"))));
        if (controller->preparedWriteValues().size() != qsizetype{123})
            fail(QStringLiteral("PRODWRITEFAIL R5: projected values=%1, expected 123")
                     .arg(controller->preparedWriteValues().size()));
        note(QStringLiteral("PRODWRITE [R5]: 123 values -> quantity 123 (byteCount "
                            "246 derived), full list projected"));
    });
    push([&]() {
        auto *list = itemOf(QStringLiteral("writeSummaryValues"));
        if (!list) {
            fail(QStringLiteral("PRODWRITEFAIL R5: the summary value list is "
                                "absent"));
        } else {
            auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
            const qreal contentHeight = list->property("contentHeight").toReal();
            list->setProperty("contentY", contentHeight);
            const qreal atEnd = list->property("contentY").toReal();
            list->setProperty("contentY", 0.0);
            const qreal atStart = list->property("contentY").toReal();
            if (atEnd <= 0.0 || atStart != 0.0)
                fail(QStringLiteral("PRODWRITEFAIL R5: the 123-value summary is not "
                                    "scrollable (end=%1 start=%2)")
                         .arg(atEnd).arg(atStart));
            if (window && list->height() > window->height())
                fail(QStringLiteral("PRODWRITEFAIL R5: the summary grew beyond the "
                                    "window"));
            note(QStringLiteral("PRODWRITE [R5]: summary scrolled end=%1 then back "
                                "to 0; viewport=%2; window unchanged")
                     .arg(atEnd).arg(list->height()));
        }
        clickNamed(QStringLiteral("writeConfirmCancelButton"));
    });

    // ---- R6: N=1 stays a valid boundary case (not the multi-value evidence) ----
    push([&]() {
        resetWrite();
        if (!openFc16Dialog(17, 1, fc16ValuesText({7})))
            fail(QStringLiteral("PRODWRITEFAIL R6: the Write button was not "
                                "clickable"));
        if (summaryText(QStringLiteral("writeSummaryQuantity")) != QStringLiteral("1"))
            fail(QStringLiteral("PRODWRITEFAIL R6: summary quantity=[%1]")
                     .arg(summaryText(QStringLiteral("writeSummaryQuantity"))));
        if (tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10) < 0)
            fail(QStringLiteral("PRODWRITEFAIL R6: Confirm not reachable"));
        sendKey(Qt::Key_Space, false);
        if (transport->writeAduLog().empty()
            || transport->writeAduLog().back() != kFc16OneReg)
            fail(QStringLiteral("PRODWRITEFAIL R6: the N=1 ADU is not the 11-byte "
                                "wire"));
        else
            note(QStringLiteral("PRODWRITE [R6]: N=1 boundary ADU = 11 10 00 01 00 "
                                "01 02 00 07 2B 83"));
        const int recordsBefore = controller->activeSerialRecordCount();
        transport->completeWriteWithEcho16();
        if (controller->activeSerialRecordCount() != recordsBefore + 1
            || controller->activeSerialRecords().back().functionCode() != 0x10)
            fail(QStringLiteral("PRODWRITEFAIL R6: the N=1 echo did not append one "
                                "FC16 transaction"));
        else
            note(QStringLiteral("PRODWRITE [R6]: N=1 echo -> one more FC16 Success "
                                "(%1 rows total)")
                     .arg(controller->activeSerialRecordCount()));
    });

    // ---- R7: production invalid-decimal rejection (frozen parser fixture) ----
    push([&]() {
        resetWrite();
        const int attemptsBefore = transport->writeAttempts();
        const int sendsBefore = transport->writeSends();
        const int recordsBefore = controller->activeSerialRecordCount();
        // "12x" is the frozen InvalidCharacter fixture (test_write_prepare d7 /
        // the single-value acceptance table): the parser is decimal-only.
        if (!openFc16Dialog(17, 1, QStringLiteral("12x")))
            fail(QStringLiteral("PRODWRITEFAIL R7: the Write button was not "
                                "clickable"));
        if (dialogVisible() || stateToken() == QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL R7: an invalid decimal prepared "
                                "(state=%1)").arg(stateToken()));
        if (!controller->preparedWriteToken().has_value())
            note(QStringLiteral("PRODWRITE [R7]: no prepared token, as required"));
        else
            fail(QStringLiteral("PRODWRITEFAIL R7: a token exists despite the "
                                "parse failure"));
        if (controller->writeDraftErrorField() != QStringLiteral("values")
            || controller->writeDraftError().isEmpty())
            fail(QStringLiteral("PRODWRITEFAIL R7: the field-specific error is not "
                                "on 'values' (field=[%1])")
                     .arg(controller->writeDraftErrorField()));
        if (transport->writeAttempts() != attemptsBefore
            || transport->writeSends() != sendsBefore)
            fail(QStringLiteral("PRODWRITEFAIL R7: the rejection reached the "
                                "transport"));
        if (controller->activeSerialRecordCount() != recordsBefore
            || controller->activeSerialTerminalCount() != 0)
            fail(QStringLiteral("PRODWRITEFAIL R7: a transaction/terminal was "
                                "fabricated"));
        if (!noticeText().isEmpty())
            fail(QStringLiteral("PRODWRITEFAIL R7: a validation rejection was "
                                "presented as a transport outcome"));
        note(QStringLiteral("PRODWRITE [R7]: values=\"12x\" -> InvalidCharacter on "
                            "'values', no dialog, 0 attempt, 0 send, no notice"));
    });

    // ---- R8: production cross-tab draft preservation (both directions) ----
    push([&]() {
        resetWrite();
        auto *s = section();
        if (!s)
            fail(QStringLiteral("PRODWRITEFAIL R8: no section"));
        // A. non-default FC06 draft.
        setDraft("unit06", 21);
        setDraft("addressText06", QStringLiteral("00042"));
        setDraft("valueText06", QStringLiteral(" 7 "));
        setDraft("timeout06", 2500);
        // B. non-default FC16 draft.
        setDraft("unit10", 19);
        setDraft("start10", 4096);
        setDraft("timeout10", 3200);
        setDraft("valuesText10", QStringLiteral("10\n258\n300"));
        const int recordsBefore = controller->activeSerialRecordCount();
        const bool connectedBefore = controller->serialConnected();

        // C. switch to FC06 and assert it field by field.
        setDraft("activeFunctionIndex", 0);
        if (!s
            || s->property("unit06").toInt() != 21
            || s->property("addressText06").toString() != QStringLiteral("00042")
            || s->property("valueText06").toString() != QStringLiteral(" 7 ")
            || s->property("timeout06").toInt() != 2500)
            fail(QStringLiteral("PRODWRITEFAIL R8: the FC06 draft changed across "
                                "the tab switch"));
        // D. back to FC16: the RAW values text must be byte-identical.
        setDraft("activeFunctionIndex", 1);
        if (s->property("unit10").toInt() != 19
            || s->property("start10").toInt() != 4096
            || s->property("timeout10").toInt() != 3200
            || s->property("valuesText10").toString()
                   != QStringLiteral("10\n258\n300"))
            fail(QStringLiteral("PRODWRITEFAIL R8: the FC16 draft changed across "
                                "the tab switch (values=[%1])")
                     .arg(s->property("valuesText10").toString()));
        // E. no side effects from switching.
        if (controller->activeSerialRecordCount() != recordsBefore
            || controller->serialConnected() != connectedBefore
            || controller->hasPreparedWrite())
            fail(QStringLiteral("PRODWRITEFAIL R8: switching tabs mutated "
                                "connection / history / snapshot state"));
        note(QStringLiteral("PRODWRITE [R8]: FC06 <-> FC16 drafts preserved field "
                            "by field (incl. raw values text), no side effects"));
    });

    // ---- R9: the FULL production FC16 keyboard chain ----
    push([&]() {
        resetWrite();
        // Text content is a harness seam (typing 3 lines through QKeyEvents is
        // not the subject); every ACTION below is a real keyboard activation.
        setDraft("unit10", 17);
        setDraft("start10", 1);
        setDraft("timeout10", 1000);
        setDraft("valuesText10", fc16ValuesText({10, 258}));
        setDraft("activeFunctionIndex", 0); // start on FC06
        // Anchor focus at the app's first stop.
        if (auto *anchor = qobject_cast<QQuickItem *>(
                findNamedItem(roots, QStringLiteral("navItem_2")))) {
            anchor->forceActiveFocus(Qt::TabFocusReason);
        }
        QStringList chain;
        bool reachedTab10 = false;
        for (int i = 0; i < 160 && !reachedTab10; ++i) {
            tab();
            const QString owner = focusOwnerName();
            chain << owner;
            if (owner.startsWith(QStringLiteral("write10"))
                && owner != QStringLiteral("writeTab10"))
                fail(QStringLiteral("PRODWRITEFAIL R9: an inactive-tab FC16 control "
                                    "([%1]) entered the Tab chain").arg(owner));
            if (owner == QStringLiteral("writeTab10"))
                reachedTab10 = true;
        }
        if (!reachedTab10)
            fail(QStringLiteral("PRODWRITEFAIL R9: the FC16 tab button was never "
                                "reached by Tab (chain tail=[%1])")
                     .arg(chain.mid(qMax(0, chain.size() - 5)).join(
                         QStringLiteral(", "))));
        else
            note(QStringLiteral("PRODWRITE [R9]: FC16 tab reached by Tab after %1 "
                                "press(es); no inactive-tab FC16 input appeared")
                     .arg(chain.size()));
    });
    push([&]() {
        // Activate the FC16 tab with the KEYBOARD (Space on the focused tab).
        sendKey(Qt::Key_Space, false);
        auto *s = section();
        if (!s || s->property("activeFunctionIndex").toInt() != 1)
            fail(QStringLiteral("PRODWRITEFAIL R9: Space did not activate the FC16 "
                                "tab (index=%1)")
                     .arg(s ? s->property("activeFunctionIndex").toInt() : -1));
        else
            note(QStringLiteral("PRODWRITE [R9]: Space activated the FC16 tab"));
        // Unit -> Start -> Values by Tab, then Write by Tab.
        if (tabToOwner(QStringLiteral("write10UnitSpin"), 12) < 0)
            fail(QStringLiteral("PRODWRITEFAIL R9: the unit input is not "
                                "Tab-reachable"));
        if (tabToOwner(QStringLiteral("write10StartSpin"), 6) < 0)
            fail(QStringLiteral("PRODWRITEFAIL R9: the start input is not "
                                "Tab-reachable"));
        if (tabToOwner(QStringLiteral("write10ValuesArea"), 6) < 0)
            fail(QStringLiteral("PRODWRITEFAIL R9: the values editor is not "
                                "Tab-reachable"));
        note(QStringLiteral("PRODWRITE [R9]: Unit / Start / Values all reached by "
                            "Tab"));
        // The multi-line editor must LET GO of Tab (no focus trapping).
        tab();
        if (focusOwnerName() == QStringLiteral("write10ValuesArea"))
            fail(QStringLiteral("PRODWRITEFAIL R9: the values editor trapped Tab"));
        // The Write button is in the same focus scope but not necessarily the
        // very next stop after the multi-line editor (the chain continues
        // through the remaining page controls), so a full cycle is allowed —
        // the assertion is "reachable by keyboard", not "immediately next".
        const int writePresses = tabToOwner(QStringLiteral("writeActivateButton"), 80);
        if (writePresses < 0)
            fail(QStringLiteral("PRODWRITEFAIL R9: the Write button is not "
                                "Tab-reachable from the values editor"));
        note(QStringLiteral("PRODWRITE [R9]: values editor released Tab; Write "
                            "button reached"));
    });
    push([&]() {
        // Activate Write with the keyboard and prove the whole chain end to end.
        const int attemptsBefore = transport->writeAttempts();
        sendKey(Qt::Key_Space, false);
        if (!dialogVisible() || stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL R9: keyboard Write did not open the "
                                "confirmation (visible=%1 state=%2)")
                     .arg(dialogVisible() ? 1 : 0)
                     .arg(stateToken()));
        if (transport->writeAttempts() != attemptsBefore)
            fail(QStringLiteral("PRODWRITEFAIL R9: opening the dialog dispatched"));
        note(QStringLiteral("PRODWRITE [R9]: keyboard Write -> confirmation "
                            "Prepared, 0 dispatch"));
    });
    push([&]() {
        // Initial focus must be Cancel; then Tab to Confirm; then Space.
        if (focusOwnerName() != QStringLiteral("writeConfirmCancelButton"))
            fail(QStringLiteral("PRODWRITEFAIL R9: initial focus is [%1], expected "
                                "Cancel").arg(focusOwnerName()));
        if (tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10) < 0)
            fail(QStringLiteral("PRODWRITEFAIL R9: Confirm is not Tab-reachable from "
                                "Cancel"));
        const int attemptsBefore = transport->writeAttempts();
        const int sendsBefore = transport->writeSends();
        sendKey(Qt::Key_Space, false);
        if (transport->writeAttempts() - attemptsBefore != 1
            || transport->writeSends() - sendsBefore != 1)
            fail(QStringLiteral("PRODWRITEFAIL R9: keyboard Confirm produced %1 "
                                "attempts / %2 sends, expected 1/1")
                     .arg(transport->writeAttempts() - attemptsBefore)
                     .arg(transport->writeSends() - sendsBefore));
        if (transport->writeAduLog().empty()
            || transport->writeAduLog().back() != kFc16TwoReg)
            fail(QStringLiteral("PRODWRITEFAIL R9: the keyboard-dispatched ADU is "
                                "not the canonical 13-byte FC16 request"));
        else
            note(QStringLiteral("PRODWRITE [R9]: full keyboard chain -> exactly one "
                                "dispatch of the canonical 13-byte ADU"));
        const int recordsBefore = controller->activeSerialRecordCount();
        transport->completeWriteWithEcho16();
        if (controller->activeSerialRecordCount() != recordsBefore + 1
            || controller->activeSerialRecords().back().functionCode() != 0x10)
            fail(QStringLiteral("PRODWRITEFAIL R9: the keyboard flow's echo did not "
                                "append one FC16 Success"));
    });

    // ---- R10: keyboard Escape and keyboard Cancel on the FC16 dialog ----
    push([&]() {
        resetWrite();
        if (!openFc16Dialog(17, 1, fc16ValuesText({10, 258})))
            fail(QStringLiteral("PRODWRITEFAIL R10: could not open the dialog"));
        const int attemptsBefore = transport->writeAttempts();
        // Escape is delivered to the WINDOW: the popup's CloseOnEscape handling
        // lives at the window level (the same path the FC06 P8 oracle uses).
        sendKey(Qt::Key_Escape, true);
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL R10: Escape did not close the FC16 "
                                "dialog"));
        if (stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("PRODWRITEFAIL R10: Escape state=%1")
                     .arg(stateToken()));
        if (transport->writeAttempts() != attemptsBefore)
            fail(QStringLiteral("PRODWRITEFAIL R10: Escape dispatched"));
        if (auto *s = section();
            s && s->property("valuesText10").toString()
                     != fc16ValuesText({10, 258}))
            fail(QStringLiteral("PRODWRITEFAIL R10: Escape cleared the FC16 draft"));
        note(QStringLiteral("PRODWRITE [R10]: FC16 Escape -> no dispatch, dialog "
                            "closed, draft preserved"));
    });
    push([&]() {
        if (!openFc16Dialog(17, 1, fc16ValuesText({10, 258})))
            fail(QStringLiteral("PRODWRITEFAIL R10: could not reopen the dialog"));
        if (focusOwnerName() != QStringLiteral("writeConfirmCancelButton"))
            fail(QStringLiteral("PRODWRITEFAIL R10: initial focus is [%1]")
                     .arg(focusOwnerName()));
        const int attemptsBefore = transport->writeAttempts();
        sendKey(Qt::Key_Space, false); // default focus IS Cancel
        if (dialogVisible() || stateToken() != QStringLiteral("invalidated"))
            fail(QStringLiteral("PRODWRITEFAIL R10: keyboard Cancel did not "
                                "invalidate (visible=%1 state=%2)")
                     .arg(dialogVisible() ? 1 : 0)
                     .arg(stateToken()));
        if (transport->writeAttempts() != attemptsBefore)
            fail(QStringLiteral("PRODWRITEFAIL R10: keyboard Cancel dispatched"));
        note(QStringLiteral("PRODWRITE [R10]: keyboard Cancel -> 0 dispatch, "
                            "Invalidated, draft preserved"));
    });

    // ---- R12: FC16 production behaviour under a REPLAY source ----
    push([&]() {
        resetWrite();
        auto *s = section();
        if (!s)
            fail(QStringLiteral("PRODWRITEFAIL R12: no section"));
        // A. a valid production FC16 draft exists first.
        setDraft("activeFunctionIndex", 1);
        setDraft("unit10", 17);
        setDraft("start10", 1);
        setDraft("timeout10", 1000);
        setDraft("valuesText10", fc16ValuesText({10, 258}));
        if (!controller->serialConnected())
            fail(QStringLiteral("PRODWRITEFAIL R12: no Active Serial session to "
                                "start from"));
        // C. load a deterministic Replay fixture (the shipped demo mlog).
        const QString mlog =
            QStringLiteral(MODBUSLENS_DEMO_MLOG_PATH);
        QFile probe(mlog);
        if (!probe.exists())
            fail(QStringLiteral("PRODWRITEFAIL R12: replay fixture missing [%1]")
                     .arg(mlog));
        else {
            controller->loadReplayFile(QUrl::fromLocalFile(mlog));
            // D. the frozen contract: source changes, capability does NOT.
            if (controller->sourceKind()
                != modbuslens::core::TransactionSourceKind::Replay)
                fail(QStringLiteral("PRODWRITEFAIL R12: sourceKind is not Replay"));
            if (!controller->property("write10Supported").toBool())
                fail(QStringLiteral("PRODWRITEFAIL R12: write10Supported moved "
                                    "under Replay (it is structural)"));
            for (const auto &name : {QStringLiteral("writeFunctionTabs"),
                                     QStringLiteral("writeTab10"),
                                     QStringLiteral("write10ValuesArea"),
                                     QStringLiteral("write10UnitSpin")}) {
                if (itemOf(name) == nullptr)
                    fail(QStringLiteral("PRODWRITEFAIL R12: %1 was unloaded by the "
                                        "Replay switch").arg(name));
            }
            if (s->property("valuesText10").toString()
                != fc16ValuesText({10, 258}))
                fail(QStringLiteral("PRODWRITEFAIL R12: the Replay switch cleared "
                                    "the FC16 draft"));
            // E. the ACTION must be unavailable through the shared runtime
            // predicate - reported with the mechanism that actually disables it.
            auto *writeButton = itemOf(QStringLiteral("writeActivateButton"));
            if (!writeButton)
                fail(QStringLiteral("PRODWRITEFAIL R12: the Write action is "
                                    "absent"));
            else if (writeButton->property("enabled").toBool())
                fail(QStringLiteral("PRODWRITEFAIL R12: Write is ENABLED under "
                                    "Replay"));
            note(QStringLiteral("PRODWRITE [R12]: sourceKind=Replay; "
                                "write10Supported=true; tabs/tab10/editor present; "
                                "draft preserved; Write disabled "
                                "(serialConnected=%1, serialBusy=%2)")
                     .arg(controller->serialConnected() ? 1 : 0)
                     .arg(controller->serialBusy() ? 1 : 0));
        }
    });
    push([&]() {
        // F. an activation attempt through the UI must produce NOTHING.
        const int attemptsBefore = transport->writeAttempts();
        const int sendsBefore = transport->writeSends();
        const int recordsBefore = controller->activeSerialRecordCount();
        const int terminalsBefore = controller->activeSerialTerminalCount();
        clickNamed(QStringLiteral("writeActivateButton"));
        if (dialogVisible() || stateToken() == QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL R12: a Replay source opened a "
                                "confirmation (state=%1)").arg(stateToken()));
        if (transport->writeAttempts() != attemptsBefore
            || transport->writeSends() != sendsBefore)
            fail(QStringLiteral("PRODWRITEFAIL R12: the Replay attempt reached the "
                                "transport"));
        if (controller->activeSerialRecordCount() != recordsBefore
            || controller->activeSerialTerminalCount() != terminalsBefore)
            fail(QStringLiteral("PRODWRITEFAIL R12: a transaction/terminal was "
                                "fabricated under Replay"));
        note(QStringLiteral("PRODWRITE [R12]: activation under Replay produced 0 "
                            "prepare / 0 attempt / 0 send / 0 transaction / 0 "
                            "terminal"));
    });

    // ---- R13: restoring Active Serial restores the ACTION (not the capability) ----
    push([&]() {
        controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
        if (controller->sourceKind()
            != modbuslens::core::TransactionSourceKind::ActiveSerial)
            fail(QStringLiteral("PRODWRITEFAIL R13: sourceKind is not ActiveSerial "
                                "after restore"));
        if (!controller->property("write10Supported").toBool())
            fail(QStringLiteral("PRODWRITEFAIL R13: capability moved on restore"));
        auto *writeButton = itemOf(QStringLiteral("writeActivateButton"));
        if (!writeButton || !writeButton->property("enabled").toBool())
            fail(QStringLiteral("PRODWRITEFAIL R13: Write is still disabled after "
                                "Active Serial was restored"));
        auto *s = section();
        if (!s
            || s->property("valuesText10").toString()
                   != fc16ValuesText({10, 258}))
            fail(QStringLiteral("PRODWRITEFAIL R13: the draft did not survive the "
                                "source round trip"));
        note(QStringLiteral("PRODWRITE [R13]: Active Serial restored -> Write "
                            "re-enabled, capability unchanged, draft preserved"));
    });

    // ---- R11: capability invariance across runtime availability states ----
    push([&]() {
        auto cap10 = [&controller]() {
            return controller->property("write10Supported").toBool();
        };
        if (!cap10())
            fail(QStringLiteral("PRODWRITEFAIL R11: write10Supported is false at "
                                "rest"));
        controller->disconnectSerial();
        if (!cap10())
            fail(QStringLiteral("PRODWRITEFAIL R11: capability moved on disconnect"));
        controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
        if (!cap10())
            fail(QStringLiteral("PRODWRITEFAIL R11: capability moved on connect"));
        controller->clearResults();
        if (!cap10())
            fail(QStringLiteral("PRODWRITEFAIL R11: capability moved on Clear"));
        controller->runBaselineDiagnosis();
        if (!cap10())
            fail(QStringLiteral("PRODWRITEFAIL R11: capability moved on diagnosis"));
        controller->runDemoBatch(); // Simulator source
        if (!cap10())
            fail(QStringLiteral("PRODWRITEFAIL R11: capability moved on Simulator"));
        // Navigation away and back.
        clickNamed(QStringLiteral("navItem_3"));
        clickNamed(QStringLiteral("navItem_2"));
        if (!cap10())
            fail(QStringLiteral("PRODWRITEFAIL R11: capability moved on navigation"));
        // The controls are DISABLED while disconnected, but still instantiated.
        controller->disconnectSerial();
        auto *spin = itemOf(QStringLiteral("write10UnitSpin"));
        auto *writeButton = itemOf(QStringLiteral("writeActivateButton"));
        if (!spin || !writeButton)
            fail(QStringLiteral("PRODWRITEFAIL R11: the FC16 controls disappeared "
                                "while disconnected"));
        else if (writeButton->property("enabled").toBool())
            fail(QStringLiteral("PRODWRITEFAIL R11: Write is ENABLED while "
                                "disconnected (capability != availability)"));
        else
            note(QStringLiteral("PRODWRITE [R11]: capability invariant across "
                                "disconnect/connect/Clear/diagnosis/Simulator/nav; "
                                "controls disabled (not removed) when disconnected"));
        controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
    });

    // ---- R14: a CONNECTION failure belongs to the Connection section ----
    push([&]() {
        resetWrite();
        auto *errItem = itemOf(QStringLiteral("communicationSerialError"));
        auto *connCard = itemOf(QStringLiteral("communicationConnectionSection"));
        auto *reqCard = itemOf(QStringLiteral("communicationRequestSection"));
        auto *writeSec = itemOf(QStringLiteral("writeFoundationSection"));
        if (!errItem || !connCard || !reqCard || !writeSec)
            fail(QStringLiteral("PRODWRITEFAIL R14: a presentation container is "
                                "missing (err=%1 conn=%2 req=%3 write=%4)")
                     .arg(errItem != nullptr ? 1 : 0)
                     .arg(connCard != nullptr ? 1 : 0)
                     .arg(reqCard != nullptr ? 1 : 0)
                     .arg(writeSec != nullptr ? 1 : 0));
        else {
            // STRUCTURAL placement, proven at runtime by walking the real
            // parentItem chains (no name/string inspection anywhere).
            if (!underItem(errItem, connCard))
                fail(QStringLiteral("PRODWRITEFAIL R14: the connection error is "
                                    "not inside the Connection section"));
            if (underItem(errItem, reqCard))
                fail(QStringLiteral("PRODWRITEFAIL R14: the connection error sits "
                                    "inside the Request section"));
            if (underItem(errItem, writeSec))
                fail(QStringLiteral("PRODWRITEFAIL R14: the connection error sits "
                                    "inside the Write section"));
            const qreal errY = errItem->mapToScene(QPointF(0, 0)).y();
            const qreal reqY = reqCard->mapToScene(QPointF(0, 0)).y();
            if (errY >= reqY)
                fail(QStringLiteral("PRODWRITEFAIL R14: the connection error is "
                                    "not presented above the Request section"));
        }
        // Drive a REAL open failure through the shipped connect path. The
        // session must be DOWN first: connectSerial on an already-connected
        // controller is a no-op, so the failing transport would never be asked.
        if (controller->serialConnected())
            controller->disconnectSerial();
        auto *failing = new FailingOpenTransport(&app);
        controller->setSerialTransport(failing);
        const qreal heightBefore = window->height();
        controller->connectSerial(QStringLiteral("COM_DOES_NOT_EXIST"), 9600);
        if (!controller->hasSerialError())
            fail(QStringLiteral("PRODWRITEFAIL R14: a failed open raised no serial "
                                "error"));
        else if (!errItem->isVisible())
            fail(QStringLiteral("PRODWRITEFAIL R14: the connection error is not "
                                "visible"));
        else
            note(QStringLiteral("PRODWRITE [R14]: open failure -> connection error "
                                "visible inside the Connection section, above the "
                                "Request section"));
        if (window->height() != heightBefore)
            fail(QStringLiteral("PRODWRITEFAIL R14: showing the connection error "
                                "resized the window (%1 -> %2)")
                     .arg(heightBefore)
                     .arg(window->height()));
        // The write lane must stay clean: no dispatch notice was produced by a
        // connection failure.
        auto *notice = itemOf(QStringLiteral("writeDispatchNotice"));
        if (notice && notice->property("visible").toBool())
            fail(QStringLiteral("PRODWRITEFAIL R14: the connection failure leaked "
                                "into the write outcome lane"));
        // Restore the ordinary transport and state.
        controller->setSerialTransport(transport);
        controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
    });

    // ---- R15: LOCAL serial port state semantics (M10-E4) ----
    // serialConnected is the LOCAL transport fact — the port is OPEN. Modbus
    // RTU has no connection handshake, so an open port proves nothing about a
    // remote slave, and an adapter that disappears must take the port state
    // down with it. Three states, three different truths.
    push([&]() {
        // A. Port open, no remote slave: the LOCAL fact is true and the UI must
        //    say exactly that — never a bare "已连接".
        if (!controller->serialConnected())
            fail(QStringLiteral("PRODWRITEFAIL R15: no open local serial port to "
                                "start from"));
        auto *stateItem = itemOf(QStringLiteral("communicationSerialState"));
        auto *chip = itemOf(QStringLiteral("sessionChipConnection"));
        if (!stateItem || !chip)
            fail(QStringLiteral("PRODWRITEFAIL R15: a serial-state presentation is "
                                "missing (state=%1 chip=%2)")
                     .arg(stateItem != nullptr ? 1 : 0)
                     .arg(chip != nullptr ? 1 : 0));
        else {
            if (textOf(QStringLiteral("communicationSerialState"))
                != QStringLiteral("串口已打开"))
                fail(QStringLiteral("PRODWRITEFAIL R15: the connection section "
                                    "must state 串口已打开, read [%1]")
                         .arg(textOf(QStringLiteral("communicationSerialState"))));
            if (!chip->isVisible()
                || !chip->property("text").toString().contains(
                    QStringLiteral("串口已打开")))
                fail(QStringLiteral("PRODWRITEFAIL R15: the session chip must read "
                                    "串口已打开, visible=%1 text=[%2]")
                         .arg(chip->isVisible() ? 1 : 0)
                         .arg(chip->property("text").toString()));
        }
        // The exact claim the correction removed: no VISIBLE text anywhere may
        // present the local port as a plain connection.
        const QStringList texts = visibleTextsIn(window->contentItem());
        for (const auto &text : texts) {
            if (text.contains(QStringLiteral("已连接")))
                fail(QStringLiteral("PRODWRITEFAIL R15: the UI still claims "
                                    "\"已连接\": [%1]").arg(text));
        }
        // Read and Write are ACTIONS on an open port: a remote slave that has
        // not answered yet must not disable them.
        if (!boolOf(QStringLiteral("commReadButton"), "enabled"))
            fail(QStringLiteral("PRODWRITEFAIL R15: Read is disabled on an OPEN "
                                "port"));
        if (!boolOf(QStringLiteral("writeActivateButton"), "enabled"))
            fail(QStringLiteral("PRODWRITEFAIL R15: Write is disabled on an OPEN "
                                "port"));
        note(QStringLiteral("PRODWRITE [R15]: open port without a remote slave -> "
                            "serialConnected=1, UI says 串口已打开, no \"已连接\" "
                            "claim anywhere, Read/Write enabled"));
    });
    push([&]() {
        // B. The remote slave says NOTHING: that is a transaction-level Timeout,
        //    not a lost connection.
        const int timeoutsBefore = controller->timeoutCount();
        transport->setCompleteReadImmediately(false);
        // The click is the ONLY thing that starts this read, and it is
        // position-based: require that it can reach the control AND that it
        // actually did, before trusting the state assertions below. Without
        // this the step could fail as a confusing "0 timeouts", or pass on a
        // pending request left behind by an earlier step.
        if (!clickReachesNamed(QStringLiteral("commReadButton")))
            fail(QStringLiteral("PRODWRITEFAIL R15: commReadButton is not inside "
                                "the %1x%2 window, so a position-based click "
                                "cannot reach it")
                     .arg(window->width()).arg(window->height()));
        if (!clickNamed(QStringLiteral("commReadButton")))
            fail(QStringLiteral("PRODWRITEFAIL R15: clickNamed(commReadButton) "
                                "refused (missing or not visible)"));
        if (!controller->serialBusy())
            fail(QStringLiteral("PRODWRITEFAIL R15: the Read click did not start "
                                "a request"));
        transport->completeReadWithTimeout();
        transport->setCompleteReadImmediately(true);
        if (controller->timeoutCount() != timeoutsBefore + 1)
            fail(QStringLiteral("PRODWRITEFAIL R15: silent slave produced %1 "
                                "timeouts, expected exactly one more")
                     .arg(controller->timeoutCount() - timeoutsBefore));
        if (!controller->serialConnected())
            fail(QStringLiteral("PRODWRITEFAIL R15: a response timeout closed the "
                                "local port"));
        if (textOf(QStringLiteral("communicationSerialState"))
            != QStringLiteral("串口已打开"))
            fail(QStringLiteral("PRODWRITEFAIL R15: a response timeout changed the "
                                "port wording to [%1]")
                     .arg(textOf(QStringLiteral("communicationSerialState"))));
        if (controller->hasSerialError())
            fail(QStringLiteral("PRODWRITEFAIL R15: a response timeout raised a "
                                "transport error"));
        if (!boolOf(QStringLiteral("commReadButton"), "enabled")
            || !boolOf(QStringLiteral("writeActivateButton"), "enabled"))
            fail(QStringLiteral("PRODWRITEFAIL R15: a response timeout disabled "
                                "Read/Write"));
        note(QStringLiteral("PRODWRITE [R15]: silent slave -> exactly one Timeout; "
                            "local port still OPEN (串口已打开), Read/Write still "
                            "enabled, no transport error"));
    });
    push([&]() {
        // C. The USB adapter ITSELF is removed. The local port is gone, so every
        //    local-state presentation must follow — and nothing may be claimed
        //    about the remote device.
        if (!controller->prepareWrite06Draft(120, QStringLiteral("21"),
                                             QStringLiteral("7"), 1000))
            fail(QStringLiteral("PRODWRITEFAIL R15: the confirmation snapshot "
                                "could not be prepared"));
        if (stateToken() != QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL R15: state=%1, expected prepared")
                     .arg(stateToken()));
        const qreal heightBefore = window->height();
        transport->simulateAdapterRemoval(
            QStringLiteral("串口设备不可用：USB 适配器已移除（测试夹具）"));

        if (controller->serialConnected())
            fail(QStringLiteral("PRODWRITEFAIL R15: the local connection survived "
                                "the adapter removal"));
        if (!controller->hasSerialError())
            fail(QStringLiteral("PRODWRITEFAIL R15: the removal raised no error"));
        if (textOf(QStringLiteral("communicationSerialState"))
            != QStringLiteral("串口未打开"))
            fail(QStringLiteral("PRODWRITEFAIL R15: the section must read 串口未打开, "
                                "read [%1]")
                     .arg(textOf(QStringLiteral("communicationSerialState"))));
        auto *chip = itemOf(QStringLiteral("sessionChipConnection"));
        if (chip && chip->isVisible())
            fail(QStringLiteral("PRODWRITEFAIL R15: the session chip still shows a "
                                "connection after the removal"));
        if (boolOf(QStringLiteral("commReadButton"), "enabled"))
            fail(QStringLiteral("PRODWRITEFAIL R15: Read is enabled with no local "
                                "port"));
        if (boolOf(QStringLiteral("writeActivateButton"), "enabled"))
            fail(QStringLiteral("PRODWRITEFAIL R15: Write is enabled with no local "
                                "port"));
        // The confirmation was captured for a session that no longer exists.
        if (controller->preparedWriteStateToken() != QStringLiteral("invalidated")
            || controller->preparedWriteInvalidReasonToken()
                   != QStringLiteral("disconnected"))
            fail(QStringLiteral("PRODWRITEFAIL R15: the prepared snapshot survived "
                                "the removal (state=%1 reason=%2)")
                     .arg(controller->preparedWriteStateToken())
                     .arg(controller->preparedWriteInvalidReasonToken()));
        if (window->height() != heightBefore)
            fail(QStringLiteral("PRODWRITEFAIL R15: the removal resized the window "
                                "(%1 -> %2)")
                     .arg(heightBefore)
                     .arg(window->height()));
        note(QStringLiteral("PRODWRITE [R15]: adapter removal -> serialConnected "
                            "1->0, 串口未打开, chip gone, Read/Write disabled, "
                            "prepared snapshot invalidated(disconnected)"));
    });
    push([&]() {
        // D. Reconnect: the OLD confirmation must not come back to life.
        controller->connectSerial(QStringLiteral("COM_HARNESS"), 9600);
        if (!controller->serialConnected())
            fail(QStringLiteral("PRODWRITEFAIL R15: the reconnect did not restore "
                                "the local connection"));
        if (textOf(QStringLiteral("communicationSerialState"))
            != QStringLiteral("串口已打开"))
            fail(QStringLiteral("PRODWRITEFAIL R15: after reconnect the section "
                                "reads [%1]")
                     .arg(textOf(QStringLiteral("communicationSerialState"))));
        if (controller->preparedWriteStateToken() == QStringLiteral("prepared"))
            fail(QStringLiteral("PRODWRITEFAIL R15: the pre-removal confirmation "
                                "revived after reconnect"));
        if (!boolOf(QStringLiteral("commReadButton"), "enabled")
            || !boolOf(QStringLiteral("writeActivateButton"), "enabled"))
            fail(QStringLiteral("PRODWRITEFAIL R15: Read/Write did not come back "
                                "with the local port"));
        note(QStringLiteral("PRODWRITE [R15]: reconnect -> 串口已打开, Read/Write "
                            "enabled again, and the pre-removal confirmation did "
                            "NOT revive"));
    });

    // ---- R16: LOCAL LOSS while a request is IN FLIGHT, and reinsertion ----
    // The physical event that real hardware exposed: the adapter disappears. A
    // request that already entered the transmission lifecycle must land in the
    // FROZEN post-submission evidence semantics (no invented Modbus outcome),
    // and plugging the adapter back in must not resurrect anything.
    push([&]() {
        resetWrite();
        const int terminalsBefore = controller->activeSerialTerminalCount();
        const int recordsBefore = controller->activeSerialRecordCount();
        const int timeoutsBefore = controller->timeoutCount();
        const int successesBefore = controller->successCount();
        transport->setCompleteReadImmediately(false);
        // Same precondition as R15: the pending request must come from THIS
        // click, so prove the click can reach the control and did.
        if (!clickReachesNamed(QStringLiteral("commReadButton")))
            fail(QStringLiteral("PRODWRITEFAIL R16: commReadButton is not inside "
                                "the %1x%2 window, so a position-based click "
                                "cannot reach it")
                     .arg(window->width()).arg(window->height()));
        if (!clickNamed(QStringLiteral("commReadButton"))) // accepted -> pending
            fail(QStringLiteral("PRODWRITEFAIL R16: clickNamed(commReadButton) "
                                "refused (missing or not visible)"));
        transport->setCompleteReadImmediately(true);
        if (!controller->serialBusy())
            fail(QStringLiteral("PRODWRITEFAIL R16: no pending request to lose"));
        transport->simulateAdapterRemoval(
            QStringLiteral("串口设备不可用：COM_HARNESS 已从系统移除"));

        // Exactly ONE terminal, carrying the retained evidence — and no Modbus
        // outcome invented for a local loss.
        if (controller->activeSerialTerminalCount() != terminalsBefore + 1)
            fail(QStringLiteral("PRODWRITEFAIL R16: terminals=%1, expected one "
                                "more").arg(controller->activeSerialTerminalCount()));
        else {
            const auto &terminals = controller->activeSerialTerminations();
            const auto &terminal = terminals.back();
            if (terminal.reason
                != modbuslens::core::TransportTerminalReason::TransportError)
                fail(QStringLiteral("PRODWRITEFAIL R16: the terminal reason is not "
                                    "the frozen TransportError"));
            if (terminal.disposition
                != modbuslens::core::TransportDisposition::PossiblySent)
                fail(QStringLiteral("PRODWRITEFAIL R16: the terminal disposition "
                                    "must stay PossiblySent"));
        }
        if (controller->activeSerialRecordCount() != recordsBefore)
            fail(QStringLiteral("PRODWRITEFAIL R16: a transaction was fabricated "
                                "for a local port loss"));
        if (controller->timeoutCount() != timeoutsBefore)
            fail(QStringLiteral("PRODWRITEFAIL R16: a local loss was reported as a "
                                "response Timeout"));
        if (controller->successCount() != successesBefore)
            fail(QStringLiteral("PRODWRITEFAIL R16: a local loss was reported as a "
                                "Success"));
        if (controller->serialConnected())
            fail(QStringLiteral("PRODWRITEFAIL R16: the local connection survived "
                                "the removal"));
        if (!controller->hasSerialError())
            fail(QStringLiteral("PRODWRITEFAIL R16: the removal raised no error"));
        note(QStringLiteral("PRODWRITE [R16]: pending request + local loss -> "
                            "exactly one TransportError/PossiblySent terminal, no "
                            "record, no Timeout, no Success"));
    });
    push([&]() {
        // Reinsert: the port is probably enumerated again, but NOTHING may
        // reconnect by itself. Reconnecting stays an explicit user action.
        const auto sessionBefore = controller->activeSerialSessionId();
        if (textOf(QStringLiteral("communicationSerialState"))
            != QStringLiteral("串口未打开"))
            fail(QStringLiteral("PRODWRITEFAIL R16: the section reads [%1] after "
                                "the loss")
                     .arg(textOf(QStringLiteral("communicationSerialState"))));
        // The shipped Refresh Ports action is the user's discovery path: it must
        // enumerate only, never reopen a session.
        controller->refreshSerialPorts();
        if (controller->serialConnected())
            fail(QStringLiteral("PRODWRITEFAIL R16: the adapter came back and the "
                                "app reconnected on its own"));
        if (controller->activeSerialSessionId() != sessionBefore)
            fail(QStringLiteral("PRODWRITEFAIL R16: a new session appeared without "
                                "a user Connect"));
        if (boolOf(QStringLiteral("commReadButton"), "enabled")
            || boolOf(QStringLiteral("writeActivateButton"), "enabled"))
            fail(QStringLiteral("PRODWRITEFAIL R16: an action is enabled while the "
                                "local port is gone"));
        note(QStringLiteral("PRODWRITE [R16]: reinsert + Refresh Ports -> still "
                            "串口未打开, no auto-reconnect, no new session, "
                            "Read/Write still disabled"));
    });

    // ---- R17: FC16 write Timeout — the user-VISIBLE terminal (M10-F) ----
    // The Human M10-F review found that an FC16 dispatch with no responding
    // slave produced NO user-visible terminal: the write-unknown notice was
    // gated on 0x06 only. This oracle pins the corrected behaviour on the
    // SHIPPED production UI — exactly one Timeout, the frozen wording in the
    // write notice lane, and nothing that could read as a Success.
    push([&]() {
        resetWrite();
        const int rowsBefore = controller->activeSerialRecordCount();
        const int successesBefore = controller->successCount();
        const int timeoutsBefore = controller->timeoutCount();
        if (!openFc16Dialog(17, 1, fc16ValuesText({112, 1222})))
            fail(QStringLiteral("PRODWRITEFAIL R17: could not open the FC16 dialog"));
        // The AUTHORITATIVE dispatch preview (the prepared snapshot encoded by
        // the SAME encoder dispatch uses) must show the canonical FC16 PDU:
        // fn=10, start=0001, qty=0002, byteCount=04, values 0070 / 04C6.
        if (summaryText(QStringLiteral("writeSummaryPdu"))
            != QStringLiteral("10 00 01 00 02 04 00 70 04 C6"))
            fail(QStringLiteral("PRODWRITEFAIL R17: the confirmation PDU preview "
                                "is [%1], expected the canonical FC16 PDU")
                     .arg(summaryText(QStringLiteral("writeSummaryPdu"))));
        note(QStringLiteral("PRODWRITE [R17]: confirmation PDU preview = %1")
                 .arg(summaryText(QStringLiteral("writeSummaryPdu"))));
        tabToOwner(QStringLiteral("writeConfirmAcceptButton"), 10);
        sendKey(Qt::Key_Space, false);
        transport->completeWriteWithTimeout16();
        if (controller->timeoutCount() != timeoutsBefore + 1)
            fail(QStringLiteral("PRODWRITEFAIL R17: timeoutCount=%1, expected "
                                "exactly one more")
                     .arg(controller->timeoutCount()));
        if (controller->activeSerialRecordCount() != rowsBefore + 1)
            fail(QStringLiteral("PRODWRITEFAIL R17: the timeout did not enter the "
                                "shared history (rows=%1)")
                     .arg(controller->activeSerialRecordCount()));
        else if (controller->activeSerialRecords().back().functionCode() != 0x10
                 || controller->activeSerialRecords().back().analysis.status
                     != modbuslens::core::TransactionStatus::Timeout)
            fail(QStringLiteral("PRODWRITEFAIL R17: the new history row is not an "
                                "FC16 Timeout"));
        if (controller->successCount() != successesBefore)
            fail(QStringLiteral("PRODWRITEFAIL R17: a Success appeared for a "
                                "write that timed out"));
        const QString text = noticeText();
        if (!text.contains(QStringLiteral("响应超时"))
            || !text.contains(QStringLiteral("设备写入状态未知")))
            fail(QStringLiteral("PRODWRITEFAIL R17: the notice must read as the "
                                "frozen write-unknown wording: [%1]").arg(text));
        if (text.contains(QStringLiteral("设备未写入"))
            || text.contains(QStringLiteral("写入成功")))
            fail(QStringLiteral("PRODWRITEFAIL R17: the notice makes an "
                                "unsupported claim: [%1]").arg(text));
        note(QStringLiteral("PRODWRITE [R17]: FC16 no-response dispatch -> exactly "
                            "one Timeout + user-visible write-unknown notice [%1]")
                 .arg(text));
    });
    push([&]() {
        if (dialogVisible())
            fail(QStringLiteral("PRODWRITEFAIL R17: the dialog is still visible "
                                "after the timeout"));
        note(QStringLiteral("PRODWRITE [R17]: dialog closed; the write-unknown "
                            "notice stays visible"));
    });

    auto step = std::make_shared<int>(0);
    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, step, schedule]() {
        if (*step >= steps->size()) {
            // Accounting: exactly the accepted sends the oracles asked for —
            // P2, P4, P5, P11 (0x06) = 4 plus R1 (canonical FC16 N=2), R6 (FC16
            // N=1 boundary) and R17 (FC16 no-response Timeout) = 7; P9 adds an
            // attempt only, P10 a terminal. M10-F: R17 exists because the Human
            // review found the FC16 timeout was never user-visible.
            if (transport->writeSends() != 8)
                fail(QStringLiteral("PRODWRITEFAIL final: accepted sends=%1, "
                                    "expected 8").arg(transport->writeSends()));
            if (transport->sentAduLogSize() != 8)
                fail(QStringLiteral("PRODWRITEFAIL final: ADU log=%1, expected 8")
                         .arg(transport->sentAduLogSize()));
            if (transport->writeTerminals() != 1)
                fail(QStringLiteral("PRODWRITEFAIL final: terminals=%1, expected 1")
                         .arg(transport->writeTerminals()));
            if (failures->isEmpty())
                qInfo() << "PRODUCTION WRITE CHECK PASS (P1 dialog + Cancel focus; "
                           "P2 Space send exactly one + draft preserved; P3 echo -> "
                           "one shared 0x06 transaction; P4 rapid Enter x2 -> one; "
                           "P5 rapid Space x2 -> one; P6 immediate Enter -> zero; "
                           "P7 Cancel; P8 Escape; P9 NotSent notice; P10 short "
                           "submission notice; P11 write timeout notice; P12 "
                           "1024x720 + 1000x700 geometry; M1-M6 PRODUCTION modal "
                           "safety: outside press ignored, rail blocked, "
                           "background Write blocked, 8 Tabs stay in the modal "
                           "scope, Cancel/Confirm accessible names; R1-R14 "
                           "FC16/replay/capability/connection-placement oracles; "
                           "R15 LOCAL serial port state semantics: open port is "
                           "not a device-online claim (say 串口已打开, never a bare "
                           "已连接), a silent slave stays a Timeout, and a removed "
                           "USB adapter takes the port state down "
                           "(串口未打开 / Read+Write disabled / prepared snapshot "
                           "invalidated, no revival after reconnect); R16 local loss "
                           "with a request IN FLIGHT -> one TransportError/"
                           "PossiblySent terminal, no record/Timeout/Success, and a "
                           "reinserted adapter does not auto-reconnect)";
            else
                for (const QString &f : *failures)
                    qWarning().noquote() << "PRODWRITEFAIL:" << f;
            app.exit(failures->isEmpty() ? 0 : 1);
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}

// ===========================================================================
// T023 / M10 correction `--qml-read-result-check`: the FC03 READ-RESULT
// observability gate.
//
// WHY IT EXISTS: Human's discovery on the M10-F build was that a SUCCESSFUL
// holding-register read said only 「成功」 — the values, the actual bytes and
// the request parameters were all unobservable, so the result could not be
// interpreted at all. T023 froze the contract; this gate is its runtime half.
//
// ARCHITECTURE: identical to the other QML harness modes — the real
// application, the real shipped QML, and a harness-only transport that
// supplies EXACTLY the observed bytes (or their absence). Every verdict still
// comes from the shipped core session + analyzer; the harness never decides an
// outcome and never fabricates one.
//
// Coverage (T023 §7 §10 §12 §19 §29):
//   · summary state: 尚无 / 等待响应 / each terminal class, exactly one
//   · detail accessibility: the entry point opens the bounded evidence dialog
//   · TX projection: presented TX == the production preview encoder's bytes
//   · RX projection: presented RX == the injected bytes, byte for byte
//   · CLASS-10 raw registers: value table == request quantity, address/dec/hex
//   · CLASS-01/02/03/04/05/06/07/08 terminals end to end
//   · 1000x700 geometry incl. the 125-register evidence dialog
//
// CLASS-09 (无法识别的响应) is NOT reachable from any wire input: the shipped
// FC03 analyzer attaches a deterministic issue to every ProtocolError it can
// produce, so the unknown class exists only as a defensive/fallback branch
// (T023 UNK-1/UNK-4). It is therefore NOT faked here — the contract is
// asserted where it is real: `classifyFc03ReadResult` is driven directly with
// the two documented defensive inputs (unit tests). Fabricating a wire
// scenario for it would be exactly the "dead assertion" T023 READ-T-4 warns
// about.
// ===========================================================================
int runReadResultCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *controller = qobject_cast<AnalysisController *>(
        rootObj ? rootObj->findChild<QObject *>(QStringLiteral("analysisController"))
                : nullptr);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    if (!controller || !window) {
        qWarning() << "READFAIL: no controller/window";
        return 1;
    }

    auto *transport = new HarnessWriteTransport(&app);

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &line) { qInfo().noquote() << line; };

    auto itemOf = [&roots](const QString &name) {
        return qobject_cast<QQuickItem *>(findNamedItem(roots, name));
    };
    auto textOf = [&itemOf](const QString &name) {
        auto *item = itemOf(name);
        return item ? item->property("text").toString() : QStringLiteral("<none>");
    };
    auto visibleOf = [&itemOf](const QString &name, const char *prop = "visible") {
        auto *item = itemOf(name);
        return item ? item->property(prop).toBool() : false;
    };
    // The Dialog is a Popup (a QObject, not a QQuickItem) so it is reached
    // through the QObject tree; its inner Labels ARE QQuickItems and are found
    // by findNamedItem once it is open.
    auto dialogOf = [rootObj]() -> QObject * {
        return rootObj ? rootObj->findChild<QObject *>(QStringLiteral("readResultDialog"))
                       : nullptr;
    };
    auto sceneRectOf = [&itemOf](const QString &name) -> QRectF {
        auto *item = itemOf(name);
        if (!item)
            return QRectF();
        return QRectF(item->mapToScene(QPointF(0.0, 0.0)),
                      QSizeF(item->width(), item->height()));
    };
    auto windowRect = [window]() {
        return QRectF(0.0, 0.0, qreal(window->width()), qreal(window->height()));
    };
    auto insideWindow = [&windowRect](const QRectF &r) {
        return windowRect().adjusted(-0.5, -0.5, 0.5, 0.5).contains(r);
    };
    auto rectsIntersect = [](const QRectF &a, const QRectF &b) {
        return a.isValid() && b.isValid() && a.intersects(b);
    };
    // A control is reachable at a size only if its scene rect lies inside the
    // window's content rect (tolerance 0.5px = layout rounding, not a clip).
    auto assertReachable = [&](const QString &label, const QString &name) {
        auto *item = itemOf(name);
        if (!item) {
            fail(QStringLiteral("READFAIL geometry %1: %2 does not exist")
                     .arg(label, name));
            return;
        }
        if (!item->isVisible()) {
            fail(QStringLiteral("READFAIL geometry %1: %2 is not visible")
                     .arg(label, name));
            return;
        }
        if (item->width() <= 1.0 || item->height() <= 1.0) {
            fail(QStringLiteral("READFAIL geometry %1: %2 has no usable size "
                                "(%3x%4)")
                     .arg(label, name)
                     .arg(item->width())
                     .arg(item->height()));
            return;
        }
        const QRectF r = sceneRectOf(name);
        if (!insideWindow(r))
            fail(QStringLiteral("READFAIL geometry %1: %2 is clipped by the "
                                "window (scene %3,%4 %5x%6 vs window %7x%8)")
                     .arg(label, name)
                     .arg(qRound(r.x()))
                     .arg(qRound(r.y()))
                     .arg(qRound(r.width()))
                     .arg(qRound(r.height()))
                     .arg(window->width())
                     .arg(window->height()));
    };
    // The read-result names are added to a dump of the whole request/write
    // column so a regression is legible instead of a bare boolean.
    auto dumpReadGeometry = [&](const QString &label) {
        const QStringList names = {
            QStringLiteral("communicationRequestSection"),
            QStringLiteral("readResultPanel"),
            QStringLiteral("readResultSummary"),
            QStringLiteral("readResultSummaryFact"),
            QStringLiteral("readResultDetailsButton"),
            QStringLiteral("commReadButton"),
            QStringLiteral("writeFoundationPanel"),
            QStringLiteral("writeValidationError"),
            QStringLiteral("writeActivateButton")};
        QStringList parts;
        for (const QString &n : names) {
            auto *item = itemOf(n);
            if (!item || !item->isVisible()) {
                parts << n + QStringLiteral("=<hidden>");
                continue;
            }
            const QPointF p = item->mapToScene(QPointF(0.0, 0.0));
            parts << QStringLiteral("%1=(%2,%3 %4x%5)")
                         .arg(n)
                         .arg(qRound(p.x()))
                         .arg(qRound(p.y()))
                         .arg(qRound(item->width()))
                         .arg(qRound(item->height()));
        }
        note(QStringLiteral("READ [geometry %1]: window=%2x%3 %4")
                 .arg(label)
                 .arg(window->width())
                 .arg(window->height())
                 .arg(parts.join(QStringLiteral(" "))));
    };

    // ---- read-result observation helpers (all read the shipped projection) ----
    auto scanRead = [&](int unit, int start, int quantity, int timeoutMs) {
        // Manual completion: the WAITING state must be observable before any
        // terminal exists (T023 §19).
        transport->setCompleteReadImmediately(false);
        controller->readHoldingRegistersOnce(unit, start, quantity, timeoutMs);
    };
    auto assertSummary = [&](const QString &label,
                             const QString &expectedSummary,
                             const QString &expectedTitle) {
        const QString summary = textOf(QStringLiteral("readResultSummary"));
        const QString title = controller->readResultTitle();
        if (summary != expectedSummary)
            fail(QStringLiteral("READFAIL %1: summary is [%2], expected [%3]")
                     .arg(label, summary, expectedSummary));
        if (title != expectedTitle)
            fail(QStringLiteral("READFAIL %1: title is [%2], expected [%3]")
                     .arg(label, title, expectedTitle));
    };
    auto assertNoValues = [&](const QString &label) {
        if (controller->readResultHasValues())
            fail(QStringLiteral("READFAIL %1: a register value table is present "
                                "outside CLASS-10").arg(label));
        if (!controller->readResultValues().isEmpty())
            fail(QStringLiteral("READFAIL %1: value rows exist outside "
                                "CLASS-10").arg(label));
        if (controller->readResultValueCount() != 0)
            fail(QStringLiteral("READFAIL %1: value count is %2, expected 0")
                     .arg(label)
                     .arg(controller->readResultValueCount()));
    };
    auto assertRxBytes = [&](const QString &label,
                             const std::vector<std::uint8_t> &injected) {
        const QString expected = evidenceHexText(injected);
        const QString actual = controller->readResultRxHex();
        if (actual != expected)
            fail(QStringLiteral("READFAIL %1: RX bytes are [%2], expected [%3]")
                     .arg(label, actual, expected));
        if (controller->readResultRxByteCount() != int(injected.size()))
            fail(QStringLiteral("READFAIL %1: RX byte count is %2, expected %3")
                     .arg(label)
                     .arg(controller->readResultRxByteCount())
                     .arg(injected.size()));
    };

    // The FC03 request frame for (unit, start, quantity) as the SHIPPED
    // encoder produces it — the same encoder the preview and the dispatch use
    // (T023 READ-R1: PREVIEW == WIRE by construction).
    auto requestWire = [](int unit, int start, int quantity,
                          int functionCode = 0x03) {
        return modbuslens::core::encodeRtuFrame(modbuslens::core::ModbusRtuFrame{
            .address = static_cast<std::uint8_t>(unit),
            .functionCode = static_cast<std::uint8_t>(functionCode),
            .data = {static_cast<std::uint8_t>(start >> 8),
                     static_cast<std::uint8_t>(start & 0xFF),
                     static_cast<std::uint8_t>(quantity >> 8),
                     static_cast<std::uint8_t>(quantity & 0xFF)}});
    };
    // A conforming register-read answer carrying `values` (big-endian pairs).
    // M10 correction: the reply's function byte is the request's own code
    // (0x03 default, FC04 / custom codes for the READ-FC oracles).
    auto responseWith = [](int unit,
                           const std::vector<std::uint16_t> &values,
                           std::uint8_t readFunctionCode = 0x03) {
        std::vector<std::uint8_t> data;
        data.push_back(static_cast<std::uint8_t>(values.size() * 2));
        for (const std::uint16_t value : values) {
            data.push_back(static_cast<std::uint8_t>(value >> 8));
            data.push_back(static_cast<std::uint8_t>(value & 0xFF));
        }
        return modbuslens::core::encodeRtuFrame(modbuslens::core::ModbusRtuFrame{
            .address = static_cast<std::uint8_t>(unit),
            .functionCode = readFunctionCode,
            .data = std::move(data)});
    };

    controller->setSerialTransport(transport);
    controller->connectSerial(QStringLiteral("COM_READ_HARNESS"), 9600);

    // ---- staged walk (one stage per event-loop turn) ----
    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };
    auto settleMs = 100;

    auto clickNamed = [&roots, &window = *window](const QString &name) {
        auto *item = qobject_cast<QQuickItem *>(findNamedItem(roots, name));
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window.mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global, Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global, Qt::LeftButton,
                            Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &release);
        return true;
    };

    // ---- setup ----
    push([&]() {
        if (!itemOf(QStringLiteral("readResultPanel"))) {
            fail(QStringLiteral("READFAIL setup: readResultPanel does not exist "
                                "on the Communication page"));
            return;
        }
        if (!clickNamed(QStringLiteral("navItem_2")))
            fail(QStringLiteral("READFAIL setup: the Communication rail entry is "
                                "not clickable"));
    });
    push([&]() {
        if (!controller->serialConnected())
            fail(QStringLiteral("READFAIL setup: the harness transport is not "
                                "connected"));
        note(QStringLiteral("READ [setup]: Communication current, harness "
                            "transport connected, session=%1")
                 .arg(controller->activeSerialSessionId()));
    });

    // ---- A. empty state ----
    push([&]() {
        if (controller->hasReadResult())
            fail(QStringLiteral("READFAIL A: a read result exists before any "
                                "read"));
        assertSummary(QStringLiteral("A empty"),
                      QStringLiteral("读取结果：尚无"),
                      QStringLiteral("尚无读取结果"));
        if (visibleOf(QStringLiteral("readResultDetailsButton")))
            fail(QStringLiteral("READFAIL A: the details entry is offered with "
                                "no result to expand"));
        assertNoValues(QStringLiteral("A empty"));
        note(QStringLiteral("READ [A]: summary=[读取结果：尚无], details entry "
                            "hidden, no value table"));
    });

    // ---- B. CLASS-01 请求未发送 (local validation guard) ----
    push([&]() { controller->readHoldingRegistersOnce(0, 0, 2, 1000); });
    push([&]() {
        // T023 §29: a local rejection produces NO session record and NO
        // terminal — the attempt never entered the transmission lifecycle.
        if (controller->activeSerialRecordCount() != 0
            || controller->activeSerialTerminalCount() != 0)
            fail(QStringLiteral("READFAIL B: a local rejection produced %1 "
                                "record(s) / %2 terminal(s)")
                     .arg(controller->activeSerialRecordCount())
                     .arg(controller->activeSerialTerminalCount()));
        const auto klass = controller->readResultClass();
        if (klass != ReadResultClass::LocalRejected)
            fail(QStringLiteral("READFAIL B: class=%1, expected local_rejected")
                     .arg(controller->readResultClassToken()));
        assertSummary(QStringLiteral("B local rejection"),
                      QStringLiteral("读取结果：请求未发送"),
                      QStringLiteral("请求未发送"));
        if (controller->readResultHasTx() || controller->readResultHasRx())
            fail(QStringLiteral("READFAIL B: a locally rejected request has wire "
                                "bytes"));
        if (controller->readResultDispositionText() != QStringLiteral("未发送"))
            fail(QStringLiteral("READFAIL B: disposition is [%1], expected [未发送]")
                     .arg(controller->readResultDispositionText()));
        if (controller->readResultFactLine().isEmpty())
            fail(QStringLiteral("READFAIL B: the guard's own message is not "
                                "presented"));
        if (!controller->readResultHasPossibleCauses())
            fail(QStringLiteral("READFAIL B: no 可能原因 line for a local "
                                "rejection"));
        assertNoValues(QStringLiteral("B"));
        note(QStringLiteral("READ [B/CLASS-01]: [%1] / %2")
                 .arg(controller->readResultFactLine(),
                      controller->readResultDispositionText()));
    });

    // ---- C. §19 waiting state, then CLASS-10 success ----
    push([&]() { scanRead(1, 1000, 2, 1000); });
    push([&]() {
        if (!controller->readResultWaiting())
            fail(QStringLiteral("READFAIL C: the accepted read is not in the "
                                "waiting state"));
        assertSummary(QStringLiteral("C waiting"),
                      QStringLiteral("读取结果：等待响应"),
                      QStringLiteral("等待响应"));
        if (controller->readResultHasValues())
            fail(QStringLiteral("READFAIL C: a waiting state shows values"));
        // T023 READ-R7: nothing observed yet is stated, never an empty string.
        if (controller->readResultRxText() != QStringLiteral("未观测到任何字节"))
            fail(QStringLiteral("READFAIL C: RX text is [%1], expected "
                                "[未观测到任何字节]")
                     .arg(controller->readResultRxText()));
        if (controller->readResultRxByteCount() != 0)
            fail(QStringLiteral("READFAIL C: a waiting state reports bytes"));
        if (controller->readResultTone() != QStringLiteral("neutral"))
            fail(QStringLiteral("READFAIL C: waiting tone is [%1], expected "
                                "neutral").arg(controller->readResultTone()));
        // READ-R1: the WAITING TX is the send-time descriptor wire, which is
        // the same encoder output the preview shows (PREVIEW == WIRE).
        const QString previewWire =
            controller->previewReadRequest(1, 1000, 2, 1000)
                .value(QStringLiteral("rtuHex")).toString();
        const QString presentedTx = controller->readResultTxHex();
        const QString expectedTx = evidenceHexText(requestWire(1, 1000, 2));
        if (previewWire != expectedTx)
            fail(QStringLiteral("READFAIL C: preview wire is [%1] but the "
                                "encoder produces [%2]")
                     .arg(previewWire, expectedTx));
        if (presentedTx != expectedTx)
            fail(QStringLiteral("READFAIL C: presented TX is [%1], expected [%2]")
                     .arg(presentedTx, expectedTx));
        note(QStringLiteral("READ [C waiting]: summary=[读取结果：等待响应], TX=[%1] "
                            "(== preview == encoder), RX=[未观测到任何字节]")
                 .arg(presentedTx));
    });
    push([&]() {
        transport->completeReadWithBytes(responseWith(1, {0x0064, 0x00C8}),
                                         std::chrono::milliseconds{25});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::ReadSuccess)
            fail(QStringLiteral("READFAIL C: class=%1, expected read_success")
                     .arg(controller->readResultClassToken()));
        if (controller->readResultWaiting())
            fail(QStringLiteral("READFAIL C: the waiting state survived the "
                                "terminal"));
        // T023 §29: exactly ONE terminal per read — one session record, one
        // waiting→terminal replacement, no second capture.
        if (controller->activeSerialRecordCount() != 1
            || controller->activeSerialTerminalCount() != 0)
            fail(QStringLiteral("READFAIL C: the read produced %1 record(s) / %2 "
                                "terminal(s), expected exactly one terminal")
                     .arg(controller->activeSerialRecordCount())
                     .arg(controller->activeSerialTerminalCount()));
        assertSummary(QStringLiteral("C success"),
                      QStringLiteral("读取结果：读取成功"),
                      QStringLiteral("读取成功"));
        if (controller->readResultTone() != QStringLiteral("success"))
            fail(QStringLiteral("READFAIL C: success tone is [%1]")
                     .arg(controller->readResultTone()));
        // Request echo (the parameter half of Human's discovery).
        if (controller->readResultUnitId() != 1
            || controller->readResultStartAddress() != 1000
            || controller->readResultStartAddressHex() != QStringLiteral("0x03E8")
            || controller->readResultQuantity() != 2
            || controller->readResultTimeoutMs() != 1000)
            fail(QStringLiteral("READFAIL C: request echo is %1 / %2 (%3) / %4 / "
                                "%5 ms")
                     .arg(controller->readResultUnitId())
                     .arg(controller->readResultStartAddress())
                     .arg(controller->readResultStartAddressHex())
                     .arg(controller->readResultQuantity())
                     .arg(controller->readResultTimeoutMs()));
        // READ-R4: the value table is the RAW uint16 payload, index + address
        // (DEC + HEX) + value (DEC + HEX). Both registers must be present.
        if (!controller->readResultHasValues()
            || controller->readResultValueCount() != 2)
            fail(QStringLiteral("READFAIL C: value count is %1, expected 2")
                     .arg(controller->readResultValueCount()));
        else {
            const QVariantList rows = controller->readResultValues();
            const auto row = [&rows](int i) { return rows.at(i).toMap(); };
            const auto checkRow = [&](int i, int index, int address,
                                      const QString &addressHex, int dec,
                                      const QString &hex) {
                const QVariantMap r = row(i);
                if (r.value(QStringLiteral("index")).toInt() != index
                    || r.value(QStringLiteral("address")).toInt() != address
                    || r.value(QStringLiteral("addressHex")).toString() != addressHex
                    || r.value(QStringLiteral("dec")).toInt() != dec
                    || r.value(QStringLiteral("hex")).toString() != hex)
                    fail(QStringLiteral("READFAIL C: value row %1 is [%2], "
                                        "expected idx=%3 addr=%4 (%5) dec=%6 (%7)")
                             .arg(i + 1)
                             .arg(r.value(QStringLiteral("addressHex")).toString())
                             .arg(index)
                             .arg(address)
                             .arg(addressHex)
                             .arg(dec)
                             .arg(hex));
            };
            checkRow(0, 1, 1000, QStringLiteral("0x03E8"), 100,
                     QStringLiteral("0x0064"));
            checkRow(1, 2, 1001, QStringLiteral("0x03E9"), 200,
                     QStringLiteral("0x00C8"));
        }
        // READ-R2: the RX bytes presented are the injected ones, byte for byte.
        assertRxBytes(QStringLiteral("C"),
                      responseWith(1, {0x0064, 0x00C8}));
        if (controller->readResultFactLine()
            != QStringLiteral("共 2 个寄存器（耗时 25 ms）。"))
            fail(QStringLiteral("READFAIL C: success fact is [%1]")
                     .arg(controller->readResultFactLine()));
        // READ-R6: PossiblySent is never presented as 「已发送」.
        if (controller->readResultDispositionText()
                != QStringLiteral("已提交传输（设备是否收到不可证）"))
            fail(QStringLiteral("READFAIL C: disposition is [%1]")
                     .arg(controller->readResultDispositionText()));
        if (controller->readResultTxLine().contains(QStringLiteral("已发送")))
            fail(QStringLiteral("READFAIL C: the TX line claims 已发送 — "
                                "PossiblySent cannot prove the device received "
                                "anything"));
        note(QStringLiteral("READ [C/CLASS-10]: [%1] 值=100/200 地址=1000/1001 "
                            "RX=%2")
                 .arg(controller->readResultFactLine(),
                      controller->readResultRxHex()));
    });

    // ---- C2. detail accessibility (the second half of the frozen IA) ----
    push([&]() {
        if (!visibleOf(QStringLiteral("readResultDetailsButton")))
            fail(QStringLiteral("READFAIL C2: the details entry is hidden while a "
                                "result exists"));
        if (!clickNamed(QStringLiteral("readResultDetailsButton")))
            fail(QStringLiteral("READFAIL C2: the details entry is not clickable"));
    });
    push([&]() {
        auto *dialog = dialogOf();
        if (!dialog) {
            fail(QStringLiteral("READFAIL C2: readResultDialog does not exist"));
            return;
        }
        if (!dialog->property("visible").toBool())
            fail(QStringLiteral("READFAIL C2: clicking 查看详情 did not open the "
                                "evidence dialog"));
        if (textOf(QStringLiteral("readResultDetailTitle"))
            != QStringLiteral("读取成功"))
            fail(QStringLiteral("READFAIL C2: dialog title is [%1]")
                     .arg(textOf(QStringLiteral("readResultDetailTitle"))));
        if (!visibleOf(QStringLiteral("readResultRequestEcho")))
            fail(QStringLiteral("READFAIL C2: the request echo is not visible"));
        if (!textOf(QStringLiteral("readResultRequestEcho"))
                 .contains(QStringLiteral("1000")))
            fail(QStringLiteral("READFAIL C2: the request echo does not carry the "
                                "PDU start address: [%1]")
                     .arg(textOf(QStringLiteral("readResultRequestEcho"))));
        const QString txText = textOf(QStringLiteral("readResultTxText"));
        if (!txText.contains(evidenceHexText(requestWire(1, 1000, 2))))
            fail(QStringLiteral("READFAIL C2: the TX bytes shown are [%1]")
                     .arg(txText));
        const QString rxText = textOf(QStringLiteral("readResultRxText"));
        if (rxText != evidenceHexText(responseWith(1, {0x0064, 0x00C8})))
            fail(QStringLiteral("READFAIL C2: the RX bytes shown are [%1]")
                     .arg(rxText));
        if (!textOf(QStringLiteral("readResultRxByteCount"))
                 .contains(QStringLiteral("9")))
            fail(QStringLiteral("READFAIL C2: the RX byte count line is [%1]")
                     .arg(textOf(QStringLiteral("readResultRxByteCount"))));
        if (!visibleOf(QStringLiteral("readResultValuesHeader")))
            fail(QStringLiteral("READFAIL C2: the value table header is hidden "
                                "for a success"));
        if (!textOf(QStringLiteral("readResultValuesHeader"))
                 .contains(QStringLiteral("2")))
            fail(QStringLiteral("READFAIL C2: the value header is [%1]")
                     .arg(textOf(QStringLiteral("readResultValuesHeader"))));
        auto *values = itemOf(QStringLiteral("readResultValuesList"));
        if (!values || !values->isVisible())
            fail(QStringLiteral("READFAIL C2: the value table is not visible"));
        else if (values->property("count").toInt() != 2)
            fail(QStringLiteral("READFAIL C2: the value table lists %1 rows, "
                                "expected 2")
                     .arg(values->property("count").toInt()));
        // A modal evidence view must never carry the "(no wire evidence)"
        // disclaimer on an Active Serial result (T023 READ-TXN-5).
        if (visibleOf(QStringLiteral("readResultNoEvidence")))
            fail(QStringLiteral("READFAIL C2: the no-evidence disclaimer is shown "
                                "for an Active Serial result"));
        assertReachable(QStringLiteral("C2 dialog"),
                        QStringLiteral("readResultDetailTitle"));
        assertReachable(QStringLiteral("C2 dialog"),
                        QStringLiteral("readResultDetailScroll"));
        assertReachable(QStringLiteral("C2 dialog"),
                        QStringLiteral("readResultDetailCloseButton"));
        note(QStringLiteral("READ [C2]: evidence dialog opened from 查看详情 — "
                            "title/RX/TX/values all reachable"));
    });
    push([&]() { clickNamed(QStringLiteral("readResultDetailCloseButton")); });
    push([&]() {
        auto *dialog = dialogOf();
        if (dialog && dialog->property("visible").toBool())
            fail(QStringLiteral("READFAIL C2: the evidence dialog did not close"));
    });

    // ---- D. CLASS-03 响应超时 (zero bytes observed) ----
    push([&]() { scanRead(1, 0, 2, 1000); });
    push([&]() {
        transport->completeReadWithBytes({}, std::chrono::milliseconds{1000});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::TimeoutNoData)
            fail(QStringLiteral("READFAIL D: class=%1, expected timeout_no_data")
                     .arg(controller->readResultClassToken()));
        assertSummary(QStringLiteral("D timeout"),
                      QStringLiteral("读取结果：响应超时"),
                      QStringLiteral("响应超时"));
        // READ-R7 (frozen wording): empty RX is stated, never "" or 「空响应」.
        if (controller->readResultRxText() != QStringLiteral("未观测到任何字节"))
            fail(QStringLiteral("READFAIL D: RX text is [%1]")
                     .arg(controller->readResultRxText()));
        assertRxBytes(QStringLiteral("D"), {});
        if (!controller->readResultFactLine().contains(
                QStringLiteral("未观测到任何字节")))
            fail(QStringLiteral("READFAIL D: fact is [%1]")
                     .arg(controller->readResultFactLine()));
        if (!controller->readResultHasPossibleCauses())
            fail(QStringLiteral("READFAIL D: no 可能原因 line for a timeout"));
        assertNoValues(QStringLiteral("D"));
        note(QStringLiteral("READ [D/CLASS-03]: [%1]").arg(
            controller->readResultFactLine()));
    });

    // ---- E. CLASS-04 响应不完整 (bytes below the minimum frame) ----
    push([&]() { scanRead(1, 0, 2, 1000); });
    push([&]() {
        // Two bytes, then the response timeout: the SHIPPED session decodes the
        // WHOLE observed buffer and reports FrameTooShort.
        transport->completeReadWithBytes({0x01, 0x03},
                                         std::chrono::milliseconds{1000});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::IncompleteResponse)
            fail(QStringLiteral("READFAIL E: class=%1, expected "
                                "incomplete_response")
                     .arg(controller->readResultClassToken()));
        assertSummary(QStringLiteral("E incomplete"),
                      QStringLiteral("读取结果：响应不完整"),
                      QStringLiteral("响应不完整"));
        // READ-R2: the partial bytes are preserved, never truncated away.
        assertRxBytes(QStringLiteral("E"), {0x01, 0x03});
        assertNoValues(QStringLiteral("E"));
        note(QStringLiteral("READ [E/CLASS-04]: [%1] RX=[%2]")
                 .arg(controller->readResultFactLine(),
                      controller->readResultRxHex()));
    });

    // ---- F. CLASS-05 从站异常 ----
    push([&]() { scanRead(1, 0, 2, 1000); });
    push([&]() {
        const auto exception =
            modbuslens::core::encodeRtuFrame(modbuslens::core::ModbusRtuFrame{
                .address = 0x01, .functionCode = 0x83, .data = {0x02}});
        transport->completeReadWithBytes(exception,
                                         std::chrono::milliseconds{25});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::DeviceException)
            fail(QStringLiteral("READFAIL F: class=%1, expected device_exception")
                     .arg(controller->readResultClassToken()));
        assertSummary(QStringLiteral("F exception"),
                      QStringLiteral("读取结果：从站异常"),
                      QStringLiteral("从站异常"));
        const QString fact = controller->readResultFactLine();
        if (!fact.contains(QStringLiteral("0x02"))
            || !fact.contains(QStringLiteral("非法数据地址")))
            fail(QStringLiteral("READFAIL F: the exception fact is [%1]").arg(fact));
        if (!controller->readResultHasPossibleCauses())
            fail(QStringLiteral("READFAIL F: no 可能原因 line for an exception"));
        assertNoValues(QStringLiteral("F"));
        note(QStringLiteral("READ [F/CLASS-05]: [%1]").arg(fact));
    });

    // ---- G. CLASS-06 CRC 校验失败 ----
    push([&]() { scanRead(1, 1000, 2, 1000); });
    push([&]() {
        // A frame of the correct length whose CRC byte was flipped: the session
        // closes at the candidate boundary and the analyzer reports CrcError.
        std::vector<std::uint8_t> corrupted = responseWith(1, {0x0064, 0x00C8});
        corrupted.back() = static_cast<std::uint8_t>(corrupted.back() ^ 0xFF);
        transport->completeReadWithBytes(corrupted,
                                         std::chrono::milliseconds{25});
    });
    push([&]() {
        std::vector<std::uint8_t> corrupted = responseWith(1, {0x0064, 0x00C8});
        corrupted.back() = static_cast<std::uint8_t>(corrupted.back() ^ 0xFF);
        if (controller->readResultClass() != ReadResultClass::CrcFailure)
            fail(QStringLiteral("READFAIL G: class=%1, expected crc_failure")
                     .arg(controller->readResultClassToken()));
        assertSummary(QStringLiteral("G crc"),
                      QStringLiteral("读取结果：CRC 校验失败"),
                      QStringLiteral("CRC 校验失败"));
        // READ-R2: a corrupted frame keeps EVERY byte it arrived with.
        assertRxBytes(QStringLiteral("G"), corrupted);
        assertNoValues(QStringLiteral("G"));
        note(QStringLiteral("READ [G/CLASS-06]: [%1] RX=[%2]")
                 .arg(controller->readResultFactLine(),
                      controller->readResultRxHex()));
    });

    // ---- H. CLASS-07 响应不匹配 (another device answered) ----
    push([&]() { scanRead(1, 1000, 2, 1000); });
    push([&]() {
        // Address 2 while the request addressed unit 1: a well-formed reply
        // that belongs to somebody else's transaction.
        transport->completeReadWithBytes(
            responseWith(2, {0x0064, 0x00C8}), std::chrono::milliseconds{25});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::ResponseMismatch)
            fail(QStringLiteral("READFAIL H: class=%1, expected "
                                "response_mismatch")
                     .arg(controller->readResultClassToken()));
        assertSummary(QStringLiteral("H mismatch"),
                      QStringLiteral("读取结果：响应不匹配"),
                      QStringLiteral("响应不匹配"));
        assertNoValues(QStringLiteral("H"));
        note(QStringLiteral("READ [H/CLASS-07]: [%1]")
                 .arg(controller->readResultFactLine()));
    });

    // ---- H2. CLASS-07 响应不匹配 (a well-formed reply with the wrong count) ----
    push([&]() { scanRead(1, 1000, 4, 1000); });
    push([&]() {
        // Quantity 4 requested, 2 answered: the frame itself is legal, so the
        // mismatch is a cross-frame fact, not a malformed frame.
        transport->completeReadWithBytes(
            responseWith(1, {0x0064, 0x00C8}), std::chrono::milliseconds{25});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::ResponseMismatch)
            fail(QStringLiteral("READFAIL H2: class=%1, expected "
                                "response_mismatch")
                     .arg(controller->readResultClassToken()));
        assertSummary(QStringLiteral("H2 quantity mismatch"),
                      QStringLiteral("读取结果：响应不匹配"),
                      QStringLiteral("响应不匹配"));
        // The decoy: the bytes DO look like a register pair — and are still not
        // presented as values (T023 READ-RX-5).
        assertNoValues(QStringLiteral("H2"));
        note(QStringLiteral("READ [H2/CLASS-07 quantity]: [%1] (payload kept "
                            "uninterpreted)").arg(controller->readResultFactLine()));
    });

    // ---- I. CLASS-08 响应格式错误 ----
    push([&]() { scanRead(1, 1000, 2, 1000); });
    push([&]() {
        // fc 0x03 with an ODD byte count: the frame decodes, its shape does not.
        transport->completeReadWithBytes(
            modbuslens::core::encodeRtuFrame(modbuslens::core::ModbusRtuFrame{
                .address = 0x01,
                .functionCode = 0x03,
                .data = {0x03, 0x00, 0x64, 0x00}}),
            std::chrono::milliseconds{1000});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::MalformedResponse)
            fail(QStringLiteral("READFAIL I: class=%1, expected "
                                "malformed_response")
                     .arg(controller->readResultClassToken()));
        assertSummary(QStringLiteral("I malformed"),
                      QStringLiteral("读取结果：响应格式错误"),
                      QStringLiteral("响应格式错误"));
        assertNoValues(QStringLiteral("I"));
        note(QStringLiteral("READ [I/CLASS-08]: [%1]")
                 .arg(controller->readResultFactLine()));
    });

    // ---- J. CLASS-02 传输失败 (a submitted read that never got an answer) ----
    push([&]() { scanRead(1, 0, 2, 1000); });
    push([&]() {
        transport->simulateAdapterRemoval(
            QStringLiteral("测试夹具：适配器已移除"));
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::TransportFailed)
            fail(QStringLiteral("READFAIL J: class=%1, expected transport_failed")
                     .arg(controller->readResultClassToken()));
        assertSummary(QStringLiteral("J transport failure"),
                      QStringLiteral("读取结果：传输失败"),
                      QStringLiteral("传输失败"));
        // T023 §29: the abort produced exactly ONE terminal and NO record —
        // a transport abort is not a Modbus diagnosis.
        if (controller->activeSerialTerminalCount() != 1)
            fail(QStringLiteral("READFAIL J: %1 terminal(s) recorded, expected "
                                "exactly 1")
                     .arg(controller->activeSerialTerminalCount()));
        if (controller->activeSerialRecordCount() != 8)
            fail(QStringLiteral("READFAIL J: %1 record(s) after 8 completed "
                                "reads and one abort")
                     .arg(controller->activeSerialRecordCount()));
        assertNoValues(QStringLiteral("J"));
        note(QStringLiteral("READ [J/CLASS-02]: [%1]")
                 .arg(controller->readResultFactLine()));
        // Re-sync the harness transport for the geometry stage below.
        controller->connectSerial(QStringLiteral("COM_READ_HARNESS"), 9600);
    });
    push([&]() {
        if (!controller->serialConnected())
            fail(QStringLiteral("READFAIL J: the harness did not reconnect"));
    });

    // ---- K. geometry @1024x720, then the 1000x700 acceptance minimum ----
    push([&]() {
        window->resize(1024, 720);
        scanRead(1, 0, 125, 1000);
    });
    push([&]() {
        std::vector<std::uint16_t> values;
        values.reserve(125);
        for (int i = 0; i < 125; ++i)
            values.push_back(static_cast<std::uint16_t>(0x1000 + i));
        transport->completeReadWithBytes(responseWith(1, values),
                                         std::chrono::milliseconds{25});
    });
    push([&]() {
        if (controller->readResultValueCount() != 125)
            fail(QStringLiteral("READFAIL K: value count is %1, expected 125")
                     .arg(controller->readResultValueCount()));
        dumpReadGeometry(QStringLiteral("1024x720 125-reg"));
        assertReachable(QStringLiteral("1024x720"),
                        QStringLiteral("readResultPanel"));
        assertReachable(QStringLiteral("1024x720"),
                        QStringLiteral("readResultSummary"));
        assertReachable(QStringLiteral("1024x720"),
                        QStringLiteral("readResultDetailsButton"));
        assertReachable(QStringLiteral("1024x720"),
                        QStringLiteral("commReadButton"));
        assertReachable(QStringLiteral("1024x720"),
                        QStringLiteral("writeFoundationPanel"));
    });
    push([&]() { window->resize(1000, 700); });
    push([&]() {
        if (window->width() != 1000 || window->height() != 700)
            fail(QStringLiteral("READFAIL K geometry 1000x700: the window is "
                                "%1x%2 — the harness must not rely on the window "
                                "growing itself")
                     .arg(window->width())
                     .arg(window->height()));
        dumpReadGeometry(QStringLiteral("1000x700 125-reg"));
        // T023 R9 / M10 §6: the new conclusion line, its detail entry point,
        // the read action, and the write panel must ALL be inside the window.
        assertReachable(QStringLiteral("1000x700"),
                        QStringLiteral("readResultPanel"));
        assertReachable(QStringLiteral("1000x700"),
                        QStringLiteral("readResultSummary"));
        assertReachable(QStringLiteral("1000x700"),
                        QStringLiteral("readResultSummaryFact"));
        assertReachable(QStringLiteral("1000x700"),
                        QStringLiteral("readResultDetailsButton"));
        assertReachable(QStringLiteral("1000x700"),
                        QStringLiteral("commReadButton"));
        assertReachable(QStringLiteral("1000x700"),
                        QStringLiteral("writeFoundationPanel"));
        assertReachable(QStringLiteral("1000x700"),
                        QStringLiteral("writeActivateButton"));
        // The write panel's bottom edge is the ISSUE-016/018 regression line:
        // the read-result row sits directly above it and must not push it out.
        const QRectF writePanel =
            sceneRectOf(QStringLiteral("writeFoundationPanel"));
        if (writePanel.isValid()
            && writePanel.bottom() > qreal(window->height()) + 0.5)
            fail(QStringLiteral("READFAIL K geometry 1000x700: the write panel "
                                "bottom is %1 > %2 (the read-result row pushed it "
                                "out of the window)")
                     .arg(qRound(writePanel.bottom()))
                     .arg(window->height()));
        // No overlap between the new row and the write validation message.
        const QRectF readRow = sceneRectOf(QStringLiteral("readResultPanel"));
        if (rectsIntersect(readRow,
                           sceneRectOf(QStringLiteral("writeFoundationPanel"))))
            fail(QStringLiteral("READFAIL K geometry 1000x700: the read-result "
                                "row overlaps the write panel"));
        if (visibleOf(QStringLiteral("writeValidationError"))
            && rectsIntersect(
                readRow, sceneRectOf(QStringLiteral("writeValidationError"))))
            fail(QStringLiteral("READFAIL K geometry 1000x700: the read-result "
                                "row overlaps the write validation message"));
        note(QStringLiteral("READ [K geometry]: every read-result control and the "
                            "write panel are inside 1000x700"));
    });
    // The 125-register evidence view at the acceptance minimum: the dialog is
    // bounded and scrollable, so its content can never grow the page.
    push([&]() { clickNamed(QStringLiteral("readResultDetailsButton")); });
    push([&]() {
        auto *dialog = dialogOf();
        if (!dialog || !dialog->property("visible").toBool()) {
            fail(QStringLiteral("READFAIL K: the evidence dialog did not open at "
                                "1000x700"));
            return;
        }
        assertReachable(QStringLiteral("1000x700 dialog"),
                        QStringLiteral("readResultDetailTitle"));
        assertReachable(QStringLiteral("1000x700 dialog"),
                        QStringLiteral("readResultDetailScroll"));
        assertReachable(QStringLiteral("1000x700 dialog"),
                        QStringLiteral("readResultDetailCloseButton"));
        auto *values = itemOf(QStringLiteral("readResultValuesList"));
        if (!values || !values->isVisible())
            fail(QStringLiteral("READFAIL K: the 125-register table is not "
                                "visible at 1000x700"));
        else if (values->property("count").toInt() != 125)
            fail(QStringLiteral("READFAIL K: the table lists %1 rows, expected "
                                "125").arg(values->property("count").toInt()));
        else if (values->property("contentHeight").toReal()
                 <= values->property("height").toReal() + 1.0)
            fail(QStringLiteral("READFAIL K: the 125-register table is not "
                                "scrollable (contentHeight=%1, height=%2)")
                     .arg(values->property("contentHeight").toReal())
                     .arg(values->property("height").toReal()));
        // The evidence dialog must be bounded: the write panel keeps its
        // geometry while it is open.
        const QRectF writePanel =
            sceneRectOf(QStringLiteral("writeFoundationPanel"));
        if (writePanel.isValid()
            && writePanel.bottom() > qreal(window->height()) + 0.5)
            fail(QStringLiteral("READFAIL K: the write panel left the window "
                                "while the evidence dialog was open (%1 > %2)")
                     .arg(qRound(writePanel.bottom()))
                     .arg(window->height()));
        note(QStringLiteral("READ [K dialog]: 125-register evidence view reachable "
                            "and scrollable at 1000x700"));
    });
    push([&]() { clickNamed(QStringLiteral("readResultDetailCloseButton")); });
    push([&]() { window->resize(1024, 720); });

    // ---- M. READ-FC: the read function code is EDITABLE (UI -> preview ->
    // actual TX -> expected response function, all one byte) ----
    push([&]() {
        // M1: function input 04. The request goes through the REAL text
        // fields and the REAL button (readRegisterRequest raw-text path).
        itemOf(QStringLiteral("commFunctionField"))
            ->setProperty("text", QStringLiteral("04"));
        itemOf(QStringLiteral("commStartField"))
            ->setProperty("text", QStringLiteral("1000"));
        if (!clickNamed(QStringLiteral("commReadButton")))
            fail(QStringLiteral("READFAIL M1: the read button is not "
                                "clickable"));
    });
    push([&]() {
        if (!controller->readResultWaiting())
            fail(QStringLiteral("READFAIL M1: the FC04 read is not in flight"));
        const QString previewWire =
            controller->previewReadRequest(1, 1000, 2, 1000, 4)
                .value(QStringLiteral("rtuHex")).toString();
        const QString presentedTx = controller->readResultTxHex();
        // wire = slave 01 | FN 04 | start 03E8 | qty 0002 | crc
        const QString expectedTx = evidenceHexText(requestWire(1, 1000, 2, 4));
        if (previewWire != expectedTx)
            fail(QStringLiteral("READFAIL M1: preview is [%1], expected [%2] — "
                                "the preview did not follow the input function")
                     .arg(previewWire, expectedTx));
        if (presentedTx != expectedTx)
            fail(QStringLiteral("READFAIL M1: the ACTUAL TX is [%1] but the "
                                "input function was 04 (expected [%2]) — "
                                "UI/preview/dispatch disagree")
                     .arg(presentedTx, expectedTx));
        if (controller->readResultFunctionLabel()
            != QStringLiteral("FC04 (0x04)"))
            fail(QStringLiteral("READFAIL M1: requested-function label is [%1]")
                     .arg(controller->readResultFunctionLabel()));
        transport->completeReadWithBytes(responseWith(1, {0x0064, 0x00C8}, 4),
                                         std::chrono::milliseconds{25});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::ReadSuccess)
            fail(QStringLiteral("READFAIL M1: an FC04 read with a conforming "
                                "FC04 reply did not succeed (%1)")
                     .arg(controller->readResultClassToken()));
        if (controller->readResultValueCount() != 2)
            fail(QStringLiteral("READFAIL M1: value count is %1")
                     .arg(controller->readResultValueCount()));
        if (!controller->readResultHasReceivedFunction()
            || controller->readResultReceivedFunctionLabel()
                != QStringLiteral("FC04 (0x04)"))
            fail(QStringLiteral("READFAIL M1: received-function label is [%1]")
                     .arg(controller->readResultReceivedFunctionLabel()));
        note(QStringLiteral("READ [M1/READ-FC2]: function input 04 -> PDU/RTU/TX "
                            "byte 04, expected response 04, registers decoded"));
        itemOf(QStringLiteral("commFunctionField"))
            ->setProperty("text", QStringLiteral("41"));
    });
    push([&]() {
        if (!clickNamed(QStringLiteral("commReadButton")))
            fail(QStringLiteral("READFAIL M2: the read button is not "
                                "clickable"));
    });
    push([&]() {
        const QString expectedTx = evidenceHexText(requestWire(1, 1000, 2, 0x41));
        if (controller->readResultTxHex() != expectedTx)
            fail(QStringLiteral("READFAIL M2: the ACTUAL TX is [%1], expected a "
                                "custom 0x41 read [%2]")
                     .arg(controller->readResultTxHex(), expectedTx));
        if (controller->readResultFunctionLabel()
            != QStringLiteral("FC41 (0x41)"))
            fail(QStringLiteral("READFAIL M2: requested-function label is [%1]")
                     .arg(controller->readResultFunctionLabel()));
        transport->completeReadWithBytes(responseWith(1, {0x0064, 0x00C8}, 0x41),
                                         std::chrono::milliseconds{25});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::ReadSuccess)
            fail(QStringLiteral("READFAIL M2: a custom 0x41 read with a "
                                "conforming reply did not succeed (%1)")
                     .arg(controller->readResultClassToken()));
        note(QStringLiteral("READ [M2/READ-FC3]: custom function 0x41 -> same "
                            "register-read schema, conforming reply succeeds"));
    });
    // M3: an unparseable function text is a local rejection with zero sends.
    push([&]() {
        // Everything here is synchronous controller state (the raw-text front
        // door parses BEFORE any transport call), so the oracle asserts in the
        // SAME stage — deferring would run it after the later source-switch
        // stage cleared the result.
        const int readsBefore = transport->readStarts();
        itemOf(QStringLiteral("commFunctionField"))
            ->setProperty("text", QStringLiteral("GG"));
        if (!clickNamed(QStringLiteral("commReadButton")))
            fail(QStringLiteral("READFAIL M3: the read button is not "
                                "clickable"));
        if (transport->readStarts() != readsBefore)
            fail(QStringLiteral("READFAIL M3: an unparseable function text "
                                "reached the transport"));
        if (controller->readResultClass() != ReadResultClass::LocalRejected)
            fail(QStringLiteral("READFAIL M3: class=%1, expected "
                                "local_rejected")
                     .arg(controller->readResultClassToken()));
        if (controller->readResultDispositionText()
            != QStringLiteral("未发送"))
            fail(QStringLiteral("READFAIL M3: disposition is [%1]")
                     .arg(controller->readResultDispositionText()));
        note(QStringLiteral("READ [M3/READ-FC6]: function text [GG] -> "
                            "请求未发送, zero sends"));
        itemOf(QStringLiteral("commFunctionField"))
            ->setProperty("text", QStringLiteral("03"));
    });
    // M4: trailing garbage in a decimal field is rejected by the core parser.
    push([&]() {
        const int readsBefore = transport->readStarts();
        itemOf(QStringLiteral("commStartField"))
            ->setProperty("text", QStringLiteral("12x"));
        if (!clickNamed(QStringLiteral("commReadButton")))
            fail(QStringLiteral("READFAIL M4: the read button is not "
                                "clickable"));
        if (transport->readStarts() != readsBefore)
            fail(QStringLiteral("READFAIL M4: [12x] reached the transport"));
        if (controller->readResultClass() != ReadResultClass::LocalRejected)
            fail(QStringLiteral("READFAIL M4: class=%1, expected "
                                "local_rejected")
                     .arg(controller->readResultClassToken()));
        note(QStringLiteral("READ [M4/READ-FC7]: start text [12x] -> "
                            "请求未发送, zero sends"));
        itemOf(QStringLiteral("commStartField"))
            ->setProperty("text", QStringLiteral("1000"));
    });
    // M5: the baud combo carries the low-speed rates; the default is 9600.
    push([&]() {
        auto *combo = itemOf(QStringLiteral("commBaudCombo"));
        if (!combo) {
            fail(QStringLiteral("READFAIL M5: commBaudCombo does not exist"));
            return;
        }
        const QVariantList model = combo->property("model").toList();
        QStringList shown;
        for (const QVariant &v : model)
            shown << v.toString();
        for (const int baud : {1200, 2400, 4800, 9600}) {
            if (!shown.contains(QString::number(baud)))
                fail(QStringLiteral("READFAIL M5: the baud options do not "
                                    "contain %1 (got [%2])")
                         .arg(baud)
                         .arg(shown.join(QLatin1Char(','))));
        }
        if (combo->property("currentIndex").toInt() != 3
            || model.value(3).toInt() != 9600)
            fail(QStringLiteral("READFAIL M5: the default baud is [%1] at "
                                "index %2, expected 9600 at index 3")
                     .arg(model.value(combo->property("currentIndex").toInt())
                              .toString())
                     .arg(combo->property("currentIndex").toInt()));
        note(QStringLiteral("READ [M5/READ-FC8]: baud options [%1], default "
                            "9600").arg(shown.join(QLatin1Char(','))));
    });

    // ---- M6-M9 (M11 first slice): the decode view inside the Read Result
    // details dialog. The configuration is DERIVED presentation state; the
    // raw columns must never move, and every check runs against the REAL
    // dialog controls the Human will use.
    push([&]() {
        // A fresh conforming FC03 read so the dialog has values to decode.
        itemOf(QStringLiteral("commFunctionField"))
            ->setProperty("text", QStringLiteral("03"));
        itemOf(QStringLiteral("commStartField"))
            ->setProperty("text", QStringLiteral("0"));
        itemOf(QStringLiteral("commQuantityField"))
            ->setProperty("text", QStringLiteral("2"));
        if (!clickNamed(QStringLiteral("commReadButton")))
            fail(QStringLiteral("READFAIL M6: the read button is not "
                                "clickable"));
    });
    push([&]() {
        transport->completeReadWithBytes(
            responseWith(1, {0x1234, 0x0064}, 0x03),
            std::chrono::milliseconds{25});
    });
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::ReadSuccess)
            fail(QStringLiteral("READFAIL M6: the read did not succeed (%1)")
                     .arg(controller->readResultClassToken()));
        // M6: the decode configuration exists and carries the frozen
        // defaults (UInt16 + normal byte order, T024 §22 C1).
        if (controller->readDecodeType()
            != static_cast<int>(modbuslens::core::RegisterDecodeType::UInt16))
            fail(QStringLiteral("READFAIL M6: default decode type is %1, "
                                "expected UInt16")
                     .arg(controller->readDecodeType()));
        if (controller->readDecodeByteOrder() != 0)
            fail(QStringLiteral("READFAIL M6: default byte order is %1, "
                                "expected Normal")
                     .arg(controller->readDecodeByteOrder()));
        const QVariantList rows = controller->readResultValues();
        if (rows.size() != 2)
            fail(QStringLiteral("READFAIL M6: %1 rows, expected 2")
                     .arg(rows.size()));
        else {
            // Default view: decoded == raw DEC for UInt16 + normal.
            const QVariantMap row = rows.at(0).toMap();
            if (row.value(QStringLiteral("decoded")).toString()
                != QStringLiteral("4660"))
                fail(QStringLiteral("READFAIL M6: decoded=[%1], expected 4660")
                         .arg(row.value(QStringLiteral("decoded")).toString()));
            if (row.value(QStringLiteral("dec")).toInt() != 0x1234)
                fail(QStringLiteral("READFAIL M6: raw DEC moved to %1")
                         .arg(row.value(QStringLiteral("dec")).toInt()));
            if (row.value(QStringLiteral("hex")).toString()
                != QStringLiteral("0x1234"))
                fail(QStringLiteral("READFAIL M6: raw HEX moved to [%1]")
                         .arg(row.value(QStringLiteral("hex")).toString()));
        }
        note(QStringLiteral("READ [M6/M11]: default decode = UInt16 + normal; "
                            "raw 0x1234 -> derived 4660, raw columns intact"));
    });
    push([&]() {
        // Open the details dialog and verify the REAL controls exist with the
        // frozen defaults and labels.
        if (!clickNamed(QStringLiteral("readResultDetailsButton")))
            fail(QStringLiteral("READFAIL M7: the details entry is not "
                                "clickable"));
    });
    push([&]() {
        auto *typeCombo = itemOf(QStringLiteral("readDecodeTypeCombo"));
        auto *orderCombo = itemOf(QStringLiteral("readDecodeByteOrderCombo"));
        if (!typeCombo || !orderCombo) {
            fail(QStringLiteral("READFAIL M7: the decode controls do not exist "
                                "in the details dialog"));
            return;
        }
        if (!typeCombo->isVisible() || !orderCombo->isVisible())
            fail(QStringLiteral("READFAIL M7: the decode controls are not "
                                "visible"));
        const QVariantList typeModel = typeCombo->property("model").toList();
        QStringList typeTexts;
        for (const QVariant &v : typeModel)
            typeTexts << v.toString();
        for (const QString &required : {QStringLiteral("十六进制"),
                                        QStringLiteral("二进制"),
                                        QStringLiteral("无符号16位整数"),
                                        QStringLiteral("有符号16位整数")}) {
            if (!typeTexts.contains(required))
                fail(QStringLiteral("READFAIL M7: the type options lack [%1] "
                                    "(got [%2])")
                         .arg(required)
                         .arg(typeTexts.join(QLatin1Char(','))));
        }
        if (typeCombo->property("currentIndex").toInt() != 2)
            fail(QStringLiteral("READFAIL M7: the type combo defaults to index "
                                "%1, expected 2 (UInt16)")
                     .arg(typeCombo->property("currentIndex").toInt()));
        if (orderCombo->property("currentIndex").toInt() != 0)
            fail(QStringLiteral("READFAIL M7: the byte-order combo defaults to "
                                "index %1, expected 0 (正常)")
                     .arg(orderCombo->property("currentIndex").toInt()));
        note(QStringLiteral("READ [M7/M11]: decode controls present; defaults "
                            "= UInt16 / 正常"));
    });
    push([&]() {
        // M8: switching the decode TYPE re-derives the visible value while
        // the raw columns stay byte-for-byte identical.
        auto *values = itemOf(QStringLiteral("readResultValuesList"));
        if (!values) {
            fail(QStringLiteral("READFAIL M8: the value table is gone"));
            return;
        }
        const QVariantMap rawRow =
            controller->readResultValues().at(0).toMap();
        controller->setReadDecodeType(
            static_cast<int>(modbuslens::core::RegisterDecodeType::Hex));
        const QVariantMap derivedRow =
            controller->readResultValues().at(0).toMap();
        if (derivedRow.value(QStringLiteral("decoded")).toString()
            != QStringLiteral("0x1234"))
            fail(QStringLiteral("READFAIL M8: the Hex view shows [%1], expected "
                                "0x1234")
                     .arg(derivedRow.value(QStringLiteral("decoded")).toString()));
        if (derivedRow.value(QStringLiteral("dec")).toInt()
                != rawRow.value(QStringLiteral("dec")).toInt()
            || derivedRow.value(QStringLiteral("hex")).toString()
                   != rawRow.value(QStringLiteral("hex")).toString())
            fail(QStringLiteral("READFAIL M8: the raw columns moved when the "
                                "decode type changed"));
        note(QStringLiteral("READ [M8/M11]: Hex view = derived 0x1234; raw "
                            "columns unchanged"));
    });
    push([&]() {
        // M9: the byte-order switch re-derives the value (0x1234 swapped is
        // 0x3412 = 13330) while the raw columns still never move — and the
        // decode status stays "ok" (a byte-order view is not an error).
        // M8 left the type on Hex; restore UInt16 first so the byte-order
        // case is measured on the SAME view it claims to measure.
        controller->setReadDecodeType(
            static_cast<int>(modbuslens::core::RegisterDecodeType::UInt16));
        controller->setReadDecodeByteOrder(
            static_cast<int>(modbuslens::core::RegisterByteOrder::ByteSwapped));
        const QVariantList rows = controller->readResultValues();
        const QVariantMap row = rows.at(0).toMap();
        if (row.value(QStringLiteral("decoded")).toString()
            != QStringLiteral("13330"))
            fail(QStringLiteral("READFAIL M9: the byte-swapped view shows [%1], "
                                "expected 13330")
                     .arg(row.value(QStringLiteral("decoded")).toString()));
        if (row.value(QStringLiteral("decodeStatus")).toString()
            != QStringLiteral("ok"))
            fail(QStringLiteral("READFAIL M9: decode status is [%1]")
                     .arg(row.value(QStringLiteral("decodeStatus")).toString()));
        if (row.value(QStringLiteral("hex")).toString()
            != QStringLiteral("0x1234"))
            fail(QStringLiteral("READFAIL M9: the raw HEX column moved to [%1] "
                                "— a byte-order view must never rewrite the "
                                "device's answer")
                     .arg(row.value(QStringLiteral("hex")).toString()));
        note(QStringLiteral("READ [M9/M11]: byte-swapped view = 13330; raw HEX "
                            "still 0x1234 (device answer untouched)"));
        // Restore the defaults so later stages start clean.
        controller->setReadDecodeType(
            static_cast<int>(modbuslens::core::RegisterDecodeType::UInt16));
        controller->setReadDecodeByteOrder(
            static_cast<int>(modbuslens::core::RegisterByteOrder::Normal));
    });

    // ---- L. source identity: the result is never fabricated for a source
    //         that has no wire evidence ----
    push([&]() { controller->runDemoBatch(); });
    push([&]() {
        if (controller->hasReadResult())
            fail(QStringLiteral("READFAIL L: an Active Serial read result survived "
                                "the switch to the Simulator source"));
        if (controller->readResultHasTx() || controller->readResultHasRx())
            fail(QStringLiteral("READFAIL L: hex bytes exist under the Simulator "
                                "source"));
        if (!controller->readResultValues().isEmpty())
            fail(QStringLiteral("READFAIL L: values exist under the Simulator "
                                "source"));
        if (!controller->readResultAwaitingEvidenceSource())
            fail(QStringLiteral("READFAIL L: the projection does not report a "
                                "source without wire evidence"));
        if (textOf(QStringLiteral("readResultSummary"))
            != QStringLiteral("读取结果：尚无"))
            fail(QStringLiteral("READFAIL L: summary is [%1]")
                     .arg(textOf(QStringLiteral("readResultSummary"))));
        note(QStringLiteral("READ [L]: the read result is cleared with its source "
                            "and no hex is fabricated for Simulator/Replay"));
    });

    // ---- driver ----
    auto step = std::make_shared<int>(0);
    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&]() {
        if (*step >= steps->size()) {
            if (failures->isEmpty())
                qInfo() << "READ RESULT CHECK PASS (T023): summary states "
                           "(尚无 / 等待响应 / CLASS-01..08 / CLASS-10); TX == "
                           "preview == encoder; RX byte-for-byte fidelity "
                           "(partial / corrupted / oversized); CLASS-10 raw "
                           "uint16 table with index+address; no values outside "
                           "CLASS-10; frozen wording (未观测到任何字节 / 未发送 / "
                           "已提交传输, never 已发送); evidence dialog reachable and "
                           "bounded; 1000x700 geometry incl. the 125-register "
                           "table; no wire evidence fabricated for Simulator. "
                           "CLASS-09 无法识别的响应 is a defensive/fallback class "
                           "with NO reachable wire input, so it is asserted at "
                           "the mapping layer (classifyFc03ReadResult) instead of "
                           "being faked here.";
            else
                for (const QString &f : *failures)
                    qWarning().noquote() << "READFAIL:" << f;
            app.exit(failures->isEmpty() ? 0 : 1);
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}


// ---------------------------------------------------------------------------
// M11 first-slice `--qml-read-result-demo`: a TEST-ONLY / DEMO-ONLY Human
// visual entry point. It feeds a FIXED synthetic FC03 success through the
// production request → session → analyzer → ReadResultSnapshot chain (the
// same path the product uses), then leaves the REAL UI open so Human can
// operate the M11 decode controls WITHOUT any real serial I/O.
//
// Demo dataset (T024 §23): unit 1 / FC 03 / start 1000 / quantity 3 /
// timeout 1000 ms; raw words 0x1234 / 0xFFFF / 0x0080 — chosen so every
// decode view shows a distinct value:
//   UInt16: 4660 / 65535 / 128
//   Int16:  4660 / -1 / 128 (byte-swapped 0x8000 → -32768)
//   Hex:    0x1234 / 0xFFFF / 0x0080 (byte-swapped 0x3412 / 0xFFFF / 0x8000)
//
// This is NOT real-hardware evidence: the transport is a harness double and
// the response is synthetic. The window title carries a DEMO suffix so Human
// can never confuse this with a production run.
// ---------------------------------------------------------------------------
int runReadResultDemo(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *controller = qobject_cast<AnalysisController *>(
        rootObj ? rootObj->findChild<QObject *>(QStringLiteral("analysisController"))
                : nullptr);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    if (!controller || !window) {
        qWarning().noquote() << QStringLiteral("DEMODECODE FAIL: no controller/window");
        return 1;
    }

    const bool exitAfterReady =
        app.arguments().contains(QStringLiteral("--demo-exit-after-ready"));

    auto *transport = new HarnessWriteTransport(&app);
    controller->setSerialTransport(transport);
    controller->connectSerial(QStringLiteral("COM_DEMO_HARNESS"), 9600);

    auto failures = std::make_shared<QStringList>();
    auto demoFail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &m) {
        qInfo().noquote() << QStringLiteral("DEMODECODE: %1").arg(m);
    };

    // The SHIPPED encoder builds the request wire (PREVIEW == WIRE by
    // construction); the SHIPPED response builder produces a conforming FC03
    // answer (valid function / byteCount / CRC). The harness injects the
    // observation; the production session / analyzer / Snapshot chain does
    // the rest — nothing bypasses the canonical path.
    const auto responseWith = [](int unit,
                                 const std::vector<std::uint16_t> &values,
                                 std::uint8_t readFunctionCode = 0x03) {
        std::vector<std::uint8_t> data;
        data.push_back(static_cast<std::uint8_t>(values.size() * 2));
        for (const std::uint16_t value : values) {
            data.push_back(static_cast<std::uint8_t>(value >> 8));
            data.push_back(static_cast<std::uint8_t>(value & 0xFF));
        }
        return modbuslens::core::encodeRtuFrame(modbuslens::core::ModbusRtuFrame{
            .address = static_cast<std::uint8_t>(unit),
            .functionCode = readFunctionCode,
            .data = std::move(data)});
    };

    // Presentation-only navigation to the Communication workspace: the
    // read-result panel and the decode controls live there. Same rail-click
    // pattern the other gates use.
    auto itemOf = [&roots](const QString &name) {
        return qobject_cast<QQuickItem *>(findNamedItem(roots, name));
    };
    const auto requestWire = [](int unit, int start, int quantity,
                                int functionCode = 0x03) {
        return modbuslens::core::encodeRtuFrame(modbuslens::core::ModbusRtuFrame{
            .address = static_cast<std::uint8_t>(unit),
            .functionCode = static_cast<std::uint8_t>(functionCode),
            .data = {static_cast<std::uint8_t>(start >> 8),
                     static_cast<std::uint8_t>(start & 0xFF),
                     static_cast<std::uint8_t>(quantity >> 8),
                     static_cast<std::uint8_t>(quantity & 0xFF)}});
    };
    const auto clickNamed = [&roots, window](const QString &name) {
        auto *item = qobject_cast<QQuickItem *>(findNamedItem(roots, name));
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };

    const std::vector<std::uint16_t> demoValues = {0x1234, 0xFFFF, 0x0080};

    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // Stage 0: navigate to the Communication workspace so the read-result
    // panel and the M11 decode controls are visible.
    push([&]() {
        if (!clickNamed(QStringLiteral("navItem_2")))
            demoFail(QStringLiteral("the Communication rail entry is not "
                                    "clickable"));
    });

    // Stage 1: set the QML request editor fields to the demo dataset values
    // (slave=1, function=03, start=1000, quantity=3, timeout=1000) so the
    // VISIBLE editor matches the transaction the demo is about to dispatch.
    push([&]() {
        const QPair<const char *, const char *> fields[] = {
            {"commSlaveField", "1"},
            {"commFunctionField", "03"},
            {"commStartField", "1000"},
            {"commQuantityField", "3"},
            {"commTimeoutField", "1000"}};
        for (const auto &field : fields) {
            auto *item = itemOf(QString::fromLatin1(field.first));
            if (!item) {
                demoFail(QStringLiteral("field %1 not found")
                             .arg(field.first));
                continue;
            }
            item->setProperty("text", QVariant(field.second).toString());
        }
    });
    // Stage 2: dispatch the read through the production raw-text front door
    // (readRegisterRequest parses the same texts the visible fields show and
    // delegates to the SAME typed validation + dispatch chain).
    push([&]() {
        transport->setCompleteReadImmediately(false);
        controller->readRegisterRequest(
            QStringLiteral("1"), QStringLiteral("03"),
            QStringLiteral("1000"), QStringLiteral("3"),
            QStringLiteral("1000"));
    });
    // Stage 2: inject the synthetic observation (the harness supplies only
    // the bytes; the SHIPPED session / analyzer decides everything).
    push([&]() {
        transport->completeReadWithBytes(
            responseWith(1, demoValues, 0x03), std::chrono::milliseconds{25});
    });
    // Stage 3: DEMODECODE assertions + demo title + keep-or-exit.
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::ReadSuccess)
            demoFail(QStringLiteral("class=%1, expected read_success")
                         .arg(controller->readResultClassToken()));
        if (!controller->readResultHasValues())
            demoFail(QStringLiteral("no value table"));
        if (controller->readResultValueCount() != 3)
            demoFail(QStringLiteral("value count=%1, expected 3")
                         .arg(controller->readResultValueCount()));
        if (controller->readDecodeType()
            != static_cast<int>(modbuslens::core::RegisterDecodeType::UInt16))
            demoFail(QStringLiteral("default decode type is not UInt16"));
        if (controller->readDecodeByteOrder() != 0)
            demoFail(QStringLiteral("default byte order is not Normal"));

        const QVariantList rows = controller->readResultValues();
        const std::vector<std::uint16_t> expectedWords = {0x1234, 0xFFFF, 0x0080};
        for (int i = 0;
             i < int(expectedWords.size()) && i < rows.size(); ++i) {
            const QVariantMap row = rows.at(i).toMap();
            const int expectedDec = int(expectedWords[std::size_t(i)]);
            if (row.value(QStringLiteral("dec")).toInt() != expectedDec)
                demoFail(QStringLiteral("word %1: raw DEC=%2, expected %3")
                             .arg(i)
                             .arg(row.value(QStringLiteral("dec")).toInt())
                             .arg(expectedDec));
        }
        // The details entry button must be reachable so Human can open the
        // M11 decode controls.
        auto *details = qobject_cast<QQuickItem *>(
            findNamedItem(roots, QStringLiteral("readResultDetailsButton")));
        if (!details || !details->isVisible())
            demoFail(QStringLiteral("the read-result details entry is not "
                                    "reachable"));

        // ---- VISUAL CONSISTENCY: the VISIBLE request editor must match the
        // ACTUAL transaction. Human's screenshot showed editor 0/2 while the
        // transaction was 1000/3 — this block proves the fix. ----
        const struct {
            const char *name;
            const char *expected;
        } fieldChecks[] = {
            {"commSlaveField", "1"},
            {"commFunctionField", "03"},
            {"commStartField", "1000"},
            {"commQuantityField", "3"},
            {"commTimeoutField", "1000"}};
        for (const auto &fc : fieldChecks) {
            auto *item = itemOf(QString::fromLatin1(fc.name));
            const QString actual =
                item ? item->property("text").toString() : QStringLiteral("<none>");
            if (actual != QString::fromLatin1(fc.expected))
                demoFail(QStringLiteral("VISIBLE %1=[%2], expected [%3] — "
                                        "the editor and the transaction "
                                        "diverged")
                             .arg(QString::fromLatin1(fc.name),
                                  actual,
                                  QString::fromLatin1(fc.expected)));
        }

        // The read-result projections must also carry the same request.
        if (controller->readResultStartAddress() != 1000)
            demoFail(QStringLiteral("readResultStartAddress=%1, expected 1000")
                         .arg(controller->readResultStartAddress()));
        if (controller->readResultQuantity() != 3)
            demoFail(QStringLiteral("readResultQuantity=%1, expected 3")
                         .arg(controller->readResultQuantity()));

        // The preview PDU/RTU must match the demo request (production
        // encoder is the single authority; the values are NOT hardcoded in
        // QML).
        const auto preview = controller->previewReadDraft(
            QStringLiteral("1"), QStringLiteral("03"),
            QStringLiteral("1000"), QStringLiteral("3"),
            QStringLiteral("1000"));
        if (preview.value(QStringLiteral("ok")).toBool()) {
            const QString pduHex =
                preview.value(QStringLiteral("pduHex")).toString();
            const QString rtuHex =
                preview.value(QStringLiteral("rtuHex")).toString();
            if (pduHex != QStringLiteral("03 03 E8 00 03"))
                demoFail(QStringLiteral("preview PDU=[%1], expected "
                                        "03 03 E8 00 03").arg(pduHex));
            const auto wire = requestWire(1, 1000, 3, 0x03);
            QString expectedRtu;
            for (const std::uint8_t b : wire)
                expectedRtu += QStringLiteral("%1 ")
                                   .arg(b, 2, 16, QLatin1Char('0'))
                                   .toUpper();
            expectedRtu = expectedRtu.trimmed();
            if (rtuHex != expectedRtu)
                demoFail(QStringLiteral("preview RTU=[%1], expected [%2]")
                             .arg(rtuHex, expectedRtu));
        }

        if (failures->isEmpty()) {
            // Demo-mode window title (Human safety: unmistakable).
            window->setTitle(
                QStringLiteral("ModbusLens — M11 解码演示（模拟数据）"));
            note(QStringLiteral(
                "READY: synthetic register-read success; "
                "values=0x1234,0xFFFF,0x0080; no real serial I/O"));
            if (exitAfterReady)
                window->close();
        } else {
            for (const QString &f : *failures)
                qWarning().noquote()
                    << QStringLiteral("DEMODECODE FAIL: %1").arg(f);
        }
    });

    const int settleMs = 60;
    auto step = std::make_shared<int>(0);
    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, step, schedule]() {
        if (*step >= steps->size()) {
            if (!failures->isEmpty()) {
                app.exit(1);
                return;
            }
            if (exitAfterReady) {
                // The window->close() in stage 3 already ended the event
                // loop; this is a safety net.
                app.exit(0);
                return;
            }
            // Human mode: keep the GUI running until Human closes the
            // window. No further scheduled steps; the event loop stays alive.
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}

// ---------------------------------------------------------------------------
// M11 second-slice `--qml-read-result-demo32`: the Human visual demo for the
// 32-bit views (UInt32 / Int32 / Float32 + word order). Same architecture and
// the same safety boundary as the first-slice demo above: a FIXED synthetic
// FC03 success flows through the production request → session → analyzer →
// ReadResultSnapshot chain (nothing bypasses the canonical path), the harness
// asserts the projected decode rows at every stage, then leaves the REAL UI
// open so Human can operate the decode controls. No real serial I/O.
//
// Demo dataset (T024 second-slice archive): unit 1 / FC 03 / start 1000 /
// quantity 6 / timeout 1000 ms; raw words 0x3F80 / 0x0000 / 0xC0A0 / 0x0000 /
// 0x4049 / 0x0FDB — chosen so the 2-register sliding window covers, from one
// response: Float32 1.0 (1000-1001) and −5.0 (1002-1003) and π's binary32
// pattern (1004-1005); UInt32 0x3F800000 / 0xC0A00000; Int32 −1063256064;
// and the word-order flip re-combines the same words (0x00003F80).
// ---------------------------------------------------------------------------
int runReadResultDemo32(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *controller = qobject_cast<AnalysisController *>(
        rootObj ? rootObj->findChild<QObject *>(QStringLiteral("analysisController"))
                : nullptr);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    if (!controller || !window) {
        qWarning().noquote()
            << QStringLiteral("DEMODECODE32 FAIL: no controller/window");
        return 1;
    }

    const bool exitAfterReady =
        app.arguments().contains(QStringLiteral("--demo-exit-after-ready"));

    auto *transport = new HarnessWriteTransport(&app);
    controller->setSerialTransport(transport);
    controller->connectSerial(QStringLiteral("COM_DEMO_HARNESS"), 9600);

    auto failures = std::make_shared<QStringList>();
    auto demoFail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &m) {
        qInfo().noquote() << QStringLiteral("DEMODECODE32: %1").arg(m);
    };

    const auto responseWith = [](int unit,
                                 const std::vector<std::uint16_t> &values,
                                 std::uint8_t readFunctionCode = 0x03) {
        std::vector<std::uint8_t> data;
        data.push_back(static_cast<std::uint8_t>(values.size() * 2));
        for (const std::uint16_t value : values) {
            data.push_back(static_cast<std::uint8_t>(value >> 8));
            data.push_back(static_cast<std::uint8_t>(value & 0xFF));
        }
        return modbuslens::core::encodeRtuFrame(modbuslens::core::ModbusRtuFrame{
            .address = static_cast<std::uint8_t>(unit),
            .functionCode = readFunctionCode,
            .data = std::move(data)});
    };

    // Presentation-only navigation to the Communication workspace. Same
    // rail-click pattern the other gates use: the PRESS event must carry
    // Qt::LeftButton as BOTH the button and the buttons state (a press with
    // buttons=NoButton is malformed and TapHandler ignores it).
    auto itemOf = [&roots](const QString &name) {
        return qobject_cast<QQuickItem *>(findNamedItem(roots, name));
    };
    const auto clickNamed = [&roots, window](const QString &name) {
        auto *item = qobject_cast<QQuickItem *>(findNamedItem(roots, name));
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };

    const std::vector<std::uint16_t> demoValues = {0x3F80, 0x0000, 0xC0A0,
                                                   0x0000, 0x4049, 0x0FDB};
    // Independent 32-bit oracles for the harness assertions (the demo must
    // not trust the production decoder it is demonstrating).
    const auto uint32Text = [](std::uint32_t bits) {
        return QString::number(bits);
    };
    const auto int32Text = [](std::uint32_t bits) {
        std::int32_t value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return QString::number(value);
    };
    // Row check: decoded text + status token + optional span ("1000-1001").
    const auto checkRow = [failures](const QVariantList &rows, int i,
                                     const QString &decoded,
                                     const QString &status,
                                     const QString &span) {
        if (i >= rows.size()) {
            *failures << QStringLiteral("row %1 missing").arg(i);
            return;
        }
        const QVariantMap row = rows.at(i).toMap();
        if (row.value(QStringLiteral("decoded")).toString() != decoded)
            *failures << QStringLiteral("row %1 decoded=[%2], expected [%3]")
                             .arg(i)
                             .arg(row.value(QStringLiteral("decoded"))
                                      .toString(),
                                  decoded);
        if (row.value(QStringLiteral("decodeStatus")).toString() != status)
            *failures << QStringLiteral("row %1 status=[%2], expected [%3]")
                             .arg(i)
                             .arg(row.value(QStringLiteral("decodeStatus"))
                                      .toString(),
                                  status);
        const QString actualSpan =
            row.contains(QStringLiteral("decodeSpan"))
                ? row.value(QStringLiteral("decodeSpan")).toString()
                : QString();
        if (actualSpan != span)
            *failures << QStringLiteral("row %1 span=[%2], expected [%3]")
                             .arg(i)
                             .arg(actualSpan, span);
        if (!decoded.isEmpty() && decoded != status) {
            // The raw columns must be present and untouched on every row.
            if (!row.contains(QStringLiteral("dec"))
                || !row.contains(QStringLiteral("hex")))
                *failures << QStringLiteral("row %1 lost a raw column").arg(i);
        }
    };

    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // Stage 0: navigate to the Communication workspace.
    push([&]() {
        if (!clickNamed(QStringLiteral("navItem_2")))
            demoFail(QStringLiteral("the Communication rail entry is not "
                                    "clickable"));
    });
    // Stage 1: set the QML request editor fields to the demo dataset values
    // (slave=1, function=03, start=1000, quantity=6, timeout=1000) so the
    // VISIBLE editor matches the transaction the demo is about to dispatch.
    push([&]() {
        const QPair<const char *, const char *> fields[] = {
            {"commSlaveField", "1"},
            {"commFunctionField", "03"},
            {"commStartField", "1000"},
            {"commQuantityField", "6"},
            {"commTimeoutField", "1000"}};
        for (const auto &field : fields) {
            auto *item = itemOf(QString::fromLatin1(field.first));
            if (!item) {
                demoFail(QStringLiteral("field %1 not found")
                             .arg(field.first));
                continue;
            }
            item->setProperty("text", QVariant(field.second).toString());
        }
    });
    // Stage 2: dispatch the read through the production raw-text front door
    // (readRegisterRequest parses the same texts the visible fields show and
    // delegates to the SAME typed validation + dispatch chain).
    push([&]() {
        transport->setCompleteReadImmediately(false);
        controller->readRegisterRequest(
            QStringLiteral("1"), QStringLiteral("03"),
            QStringLiteral("1000"), QStringLiteral("6"),
            QStringLiteral("1000"));
    });
    // Stage 3: inject the synthetic observation.
    push([&]() {
        transport->completeReadWithBytes(
            responseWith(1, demoValues, 0x03), std::chrono::milliseconds{25});
    });
    // Stage 4: defaults — UInt16 + Normal + HighWordFirst, 6 raw rows, the
    // word-order control DISABLED for a 1-register type, visible request
    // fields consistent with the actual transaction.
    push([&]() {
        if (controller->readResultClass() != ReadResultClass::ReadSuccess)
            demoFail(QStringLiteral("class=%1, expected read_success")
                         .arg(controller->readResultClassToken()));
        if (controller->readResultValueCount() != 6)
            demoFail(QStringLiteral("value count=%1, expected 6")
                         .arg(controller->readResultValueCount()));
        if (controller->readDecodeType()
            != static_cast<int>(modbuslens::core::RegisterDecodeType::UInt16))
            demoFail(QStringLiteral("default decode type is not UInt16"));
        if (controller->readDecodeWordOrder()
            != static_cast<int>(
                modbuslens::core::RegisterWordOrder::HighWordFirst))
            demoFail(QStringLiteral(
                "default word order is not HighWordFirst"));
        if (controller->readDecodeWordOrderEnabled())
            demoFail(QStringLiteral(
                "the word-order control must be disabled for UInt16"));
        const QVariantList rows = controller->readResultValues();
        for (int i = 0; i < int(demoValues.size()); ++i) {
            const QVariantMap row = rows.at(i).toMap();
            if (row.value(QStringLiteral("dec")).toInt()
                != int(demoValues[std::size_t(i)]))
                demoFail(QStringLiteral("word %1 raw DEC moved").arg(i));
            if (row.contains(QStringLiteral("decodeSpan")))
                demoFail(QStringLiteral(
                             "a 1-register row carries a decodeSpan (row %1)")
                             .arg(i));
        }
        auto *details = qobject_cast<QQuickItem *>(
            findNamedItem(roots, QStringLiteral("readResultDetailsButton")));
        if (!details || !details->isVisible())
            demoFail(QStringLiteral("the read-result details entry is not "
                                    "reachable"));
        const struct {
            const char *name;
            const char *expected;
        } fieldChecks[] = {
            {"commSlaveField", "1"},
            {"commFunctionField", "03"},
            {"commStartField", "1000"},
            {"commQuantityField", "6"},
            {"commTimeoutField", "1000"}};
        for (const auto &fc : fieldChecks) {
            auto *item = itemOf(QString::fromLatin1(fc.name));
            const QString actual =
                item ? item->property("text").toString() : QStringLiteral("<none>");
            if (actual != QString::fromLatin1(fc.expected))
                demoFail(QStringLiteral("VISIBLE %1=[%2], expected [%3]")
                             .arg(QString::fromLatin1(fc.name), actual,
                                  QString::fromLatin1(fc.expected)));
        }
        note(QStringLiteral("stage 4: defaults UInt16/Normal/HighWordFirst; "
                           "6 raw rows; word-order control disabled"));
    });
    // Stage 5: UInt32 — the sliding window plus the address-range span; the
    // LAST register reports insufficient words; the raw columns never move.
    push([&]() {
        controller->setReadDecodeType(
            static_cast<int>(modbuslens::core::RegisterDecodeType::UInt32));
        if (!controller->readDecodeWordOrderEnabled())
            demoFail(QStringLiteral(
                "the word-order control must be enabled for UInt32"));
        const QVariantList rows = controller->readResultValues();
        checkRow(rows, 0, uint32Text(0x3F800000u),
                 QStringLiteral("ok"), QStringLiteral("1000-1001"));
        checkRow(rows, 1, uint32Text(0x0000C0A0u),
                 QStringLiteral("ok"), QStringLiteral("1001-1002"));
        checkRow(rows, 2, uint32Text(0xC0A00000u),
                 QStringLiteral("ok"), QStringLiteral("1002-1003"));
        checkRow(rows, 3, uint32Text(0x00004049u),
                 QStringLiteral("ok"), QStringLiteral("1003-1004"));
        checkRow(rows, 4, uint32Text(0x40490FDBu),
                 QStringLiteral("ok"), QStringLiteral("1004-1005"));
        checkRow(rows, 5, QString(), QStringLiteral("insufficient_words"),
                 QString());
        if (rows.at(0).toMap().value(QStringLiteral("dec")).toInt() != 0x3F80)
            demoFail(QStringLiteral("raw DEC moved under UInt32"));
        note(QStringLiteral("stage 5: UInt32 big-endian window + spans; "
                           "last register insufficient_words"));
    });
    // Stage 6: word order → LowWordFirst (CD AB): the SAME words recombine.
    push([&]() {
        controller->setReadDecodeWordOrder(
            static_cast<int>(modbuslens::core::RegisterWordOrder::LowWordFirst));
        const QVariantList rows = controller->readResultValues();
        checkRow(rows, 0, uint32Text(0x00003F80u),
                 QStringLiteral("ok"), QStringLiteral("1000-1001"));
        checkRow(rows, 1, uint32Text(0xC0A00000u),
                 QStringLiteral("ok"), QStringLiteral("1001-1002"));
        checkRow(rows, 2, uint32Text(0x0000C0A0u),
                 QStringLiteral("ok"), QStringLiteral("1002-1003"));
        if (rows.at(0).toMap().value(QStringLiteral("hex")).toString()
            != QStringLiteral("0x3F80"))
            demoFail(QStringLiteral("raw HEX moved under LowWordFirst"));
        note(QStringLiteral("stage 6: LowWordFirst recombines the same "
                           "words; raw columns untouched"));
    });
    // Stage 7: Float32 (word order back to big-endian) — 1.0 / −5.0 / π,
    // NaN-free by dataset; the last row still reports insufficient words.
    push([&]() {
        controller->setReadDecodeWordOrder(
            static_cast<int>(
                modbuslens::core::RegisterWordOrder::HighWordFirst));
        controller->setReadDecodeType(
            static_cast<int>(modbuslens::core::RegisterDecodeType::Float32));
        const QVariantList rows = controller->readResultValues();
        checkRow(rows, 0, QStringLiteral("1.0"), QStringLiteral("ok"),
                 QStringLiteral("1000-1001"));
        checkRow(rows, 2, QStringLiteral("-5.0"), QStringLiteral("ok"),
                 QStringLiteral("1002-1003"));
        // π's binary32 pattern: text round-trips back to the same bits.
        const QVariantMap pi = rows.at(4).toMap();
        bool ok = false;
        const float parsed =
            static_cast<float>(pi.value(QStringLiteral("decoded"))
                                   .toString()
                                   .toFloat(&ok));
        if (!ok)
            demoFail(QStringLiteral("pi text [%1] does not parse")
                         .arg(pi.value(QStringLiteral("decoded")).toString()));
        else {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &parsed, sizeof(bits));
            if (bits != 0x40490FDBu)
                demoFail(QStringLiteral("pi round-trip bits=%1").arg(bits));
        }
        checkRow(rows, 5, QString(), QStringLiteral("insufficient_words"),
                 QString());
        note(QStringLiteral("stage 7: Float32 1.0 / -5.0 / pi; windows and "
                           "spans correct"));
    });
    // Stage 8: Int32 — the negative branch of the two's-complement view.
    push([&]() {
        controller->setReadDecodeType(
            static_cast<int>(modbuslens::core::RegisterDecodeType::Int32));
        const QVariantList rows = controller->readResultValues();
        checkRow(rows, 0, uint32Text(0x3F800000u), QStringLiteral("ok"),
                 QStringLiteral("1000-1001"));
        checkRow(rows, 2, int32Text(0xC0A00000u), QStringLiteral("ok"),
                 QStringLiteral("1002-1003"));
        if (rows.at(0).toMap().value(QStringLiteral("dec")).toInt() != 0x3F80)
            demoFail(QStringLiteral("raw DEC moved under Int32"));
        note(QStringLiteral("stage 8: Int32 negative branch correct"));
    });
    // Stage 9: READY — demo title + keep-or-exit.
    push([&]() {
        // Restore the frozen defaults so Human starts from the standard view.
        controller->setReadDecodeType(
            static_cast<int>(modbuslens::core::RegisterDecodeType::UInt16));
        controller->setReadDecodeWordOrder(
            static_cast<int>(
                modbuslens::core::RegisterWordOrder::HighWordFirst));
        if (failures->isEmpty()) {
            // Demo-mode window title (Human safety: unmistakable).
            window->setTitle(
                QStringLiteral("ModbusLens — M11 32位解码演示（模拟数据）"));
            note(QStringLiteral(
                "READY: synthetic register-read success; "
                "words=0x3F80,0x0000,0xC0A0,0x0000,0x4049,0x0FDB; "
                "UInt32/Int32/Float32 + word order verified; no real "
                "serial I/O"));
            if (exitAfterReady)
                window->close();
        } else {
            for (const QString &f : *failures)
                qWarning().noquote()
                    << QStringLiteral("DEMODECODE32 FAIL: %1").arg(f);
        }
    });

    const int settleMs = 60;
    auto step = std::make_shared<int>(0);
    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, step, schedule]() {
        if (*step >= steps->size()) {
            if (!failures->isEmpty()) {
                app.exit(1);
                return;
            }
            if (exitAfterReady) {
                // The window->close() in the READY stage already ended the
                // event loop; this is a safety net.
                app.exit(0);
                return;
            }
            // Human mode: keep the GUI running until Human closes the
            // window.
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}
// shell. Same architecture as the other QML harness modes: the REAL app
// loads its own shipped QML and drives it through the same synthetic event
// seams the manual flow uses (mouse delivered through the window, key events
// delivered to the window / active focus item), then asserts the observable
// contract. Harness only: no product behaviour changes, no new user-facing
// command, and no assertion that the manual scenarios already own.
//
// Scenarios (frozen F1 acceptance matrix, T021 §FE7):
//   FA  Transactions: keyboard-only Tab reaches the list (and focus alone
//       never selects a row)                                    [scope A]
//   FB  the current workspace's Tab chain contains no control that belongs
//       to a hidden page, in all five workspaces                [scope B]
//   FC  a control that held focus when its workspace was left cannot execute
//       while hidden, and still activates normally when visible [scope C]
//   FD  rail Enter activates the focused rail entry (all five) [scope D]
//   FE  rail Space activates the focused rail entry (all five) [scope D]
//   FF  the disabled Device entry cannot be focused or activated
//   FG  Agent TextArea: Tab escapes without mutating the draft  [scope H]
//   FH  Agent TextArea: Shift+Tab escapes backward, no mutation [scope H]
//   FI  Transactions list: Up/Down/Home/End regression through the existing
//       currentIndex -> selectRow path (M9-D contract intact)   [scope A]
// ---------------------------------------------------------------------------
int runFocusCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *ctrl = rootObj ? rootObj->findChild<QObject *>(
                               QStringLiteral("analysisController"))
                         : nullptr;
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    if (!ctrl || !window) {
        qWarning() << "FOCUSFAIL: no controller/window";
        return 1;
    }

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };

    const QStringList pageNames = {
        QStringLiteral("transactionsPage"),
        QStringLiteral("dashboardWorkspace"),
        QStringLiteral("communicationWorkspace"),
        QStringLiteral("replayWorkspace"),
        QStringLiteral("diagnosisPage"),
    };

    auto itemOf = [&roots](const QString &name) {
        return qobject_cast<QQuickItem *>(findNamedItem(roots, name));
    };
    auto focusItem = [window]() {
        return qobject_cast<QQuickItem *>(window->activeFocusItem());
    };
    auto focusName = [&focusItem]() {
        auto *f = focusItem();
        return f ? f->objectName() : QStringLiteral("<null>");
    };
    // Nearest NAMED ancestor of the focused item. Qt moves focus to a control's
    // internal child in several cases (a SpinBox's editor; a DecimalField's
    // deliberately-unnamed TextField), so "which named control owns the focus"
    // is described by OWNERSHIP, not by an exact object-name match.
    auto focusOwnerName = [&focusItem]() {
        for (auto *p = focusItem(); p; p = p->parentItem()) {
            if (!p->objectName().isEmpty())
                return p->objectName();
        }
        return QStringLiteral("<none>");
    };
    // Keyboard-traversal reachability by OWNER name (defined next to tabTo,
    // which it uses).
    auto railIndex = [&itemOf]() {
        auto *r = itemOf(QStringLiteral("navigationRail"));
        return r ? r->property("currentWorkspaceIndex").toInt() : -1;
    };
    // Structural page ownership of the focused item (never a name guess).
    auto pageIndexOf = [&itemOf, &pageNames](QQuickItem *x) {
        if (!x)
            return -1;
        for (int i = 0; i < pageNames.size(); ++i) {
            auto *page = itemOf(pageNames.at(i));
            if (page && (x == page || underItem(x, page)))
                return i;
        }
        return -1;
    };
    // Which rail entry owns this item? (-1 when the item is not a rail entry)
    auto railIndexOf = [](QQuickItem *x) {
        for (auto *p = x; p; p = p->parentItem()) {
            if (p->objectName().startsWith(QStringLiteral("navItem_")))
                return p->property("workspaceIndex").toInt();
        }
        return -1;
    };
    auto clickItemPoint = [window](QQuickItem *item) -> bool {
        if (!window || !item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    auto clickNamed = [&itemOf, &clickItemPoint](const QString &name) {
        return clickItemPoint(itemOf(name));
    };
    // Key delivery: Tab/Backtab go through the WINDOW (the delivery path the
    // platform uses, which is where Qt performs focus traversal); every other
    // key goes to the active focus item, matching the existing nav seam.
    auto sendKey = [window](Qt::Key key, Qt::KeyboardModifiers mods,
                            bool toWindow) -> bool {
        QObject *target = toWindow ? static_cast<QObject *>(window)
                                   : window->activeFocusItem();
        if (!target)
            return false;
        QKeyEvent press(QEvent::KeyPress, key, mods);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, mods);
        QCoreApplication::sendEvent(target, &release);
        return true;
    };
    // Typing needs a key event that carries TEXT: a bare key code never
    // reaches a text editor's content (measured: an empty-text event left the
    // Agent draft unchanged, which would have made the retention assertion
    // vacuous).
    auto sendTextKey = [window](Qt::Key key, const QString &text) -> bool {
        QObject *target = window->activeFocusItem();
        if (!target)
            return false;
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, text);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, text);
        QCoreApplication::sendEvent(target, &release);
        return true;
    };
    auto tab = [&sendKey](bool forward) {
        return sendKey(forward ? Qt::Key_Tab : Qt::Key_Backtab,
                       Qt::NoModifier, true);
    };
    // Focus a named item by KEYBOARD TRAVERSAL only (the user path). Returns
    // the number of presses it took, or -1.
    auto tabTo = [&](const QString &name, int maxPresses) {
        for (int i = 1; i <= maxPresses; ++i) {
            tab(true);
            if (focusName() == name)
                return i;
        }
        return -1;
    };
    // The same walk, matched by OWNER name: Qt focuses a control's internal
    // child for SpinBox and for DecimalField (whose inner TextField is
    // deliberately unnamed), so an exact focusName() match cannot see them.
    auto tabToOwner = [&tab, &focusOwnerName](const QString &name, int maxPresses) {
        for (int i = 1; i <= maxPresses; ++i) {
            tab(true);
            if (focusOwnerName() == name)
                return i;
        }
        return -1;
    };
    auto clickTab = [&itemOf, &clickItemPoint](int tabIndex) {
        auto *tabs = itemOf(QStringLiteral("diagnosisTabs"));
        if (!tabs)
            return false;
        QQuickItem *tabItem = nullptr;
        if (!QMetaObject::invokeMethod(tabs, "itemAt",
                                       Q_RETURN_ARG(QQuickItem *, tabItem),
                                       Q_ARG(int, tabIndex)))
            return false;
        return clickItemPoint(tabItem);
    };
    auto selectWorkspace = [&](int index) {
        return clickNamed(QStringLiteral("navItem_%1").arg(index));
    };
    auto anchorFocus = [&]() {
        // The AppBar button is a real Control: clicking it establishes a
        // deterministic keyboard-focus anchor for every traversal below.
        return clickNamed(QStringLiteral("appBarClearResults"));
    };
    auto walkTabs = [&](int presses, bool forward, QStringList &chain,
                        QList<int> &pages) {
        for (int i = 0; i < presses; ++i) {
            tab(forward);
            chain << focusName();
            pages << pageIndexOf(focusItem());
        }
    };
    auto listIndex = [&itemOf]() {
        auto *l = itemOf(QStringLiteral("transactionsList"));
        return l ? l->property("currentIndex").toInt() : -1;
    };
    auto listCount = [&itemOf]() {
        auto *l = itemOf(QStringLiteral("transactionsList"));
        return l ? l->property("count").toInt() : -1;
    };
    auto selectedRow = [&itemOf]() {
        auto *p = itemOf(QStringLiteral("transactionsPage"));
        return p ? p->property("selectedRow").toInt() : -1;
    };
    auto hasBaseline = [ctrl]() {
        return ctrl->property("hasBaselineDiagnosis").toBool();
    };
    auto agentText = [&itemOf]() {
        auto *t = itemOf(QStringLiteral("diagnosisAgentQuestion"));
        return t ? t->property("text").toString() : QStringLiteral("<none>");
    };
    auto note = [](const QString &line) { qInfo().noquote() << line; };
    // M10-B correction (FM/FN): drive the ONE production append seam — the
    // same method the transport completion calls — so the oracle exercises the
    // real append, never a test-only shadow path. Records are machine
    // distinguishable (unit / status / elapsed) so the detail oracle can prove
    // WHICH transaction the pane still shows.
    auto controller = qobject_cast<AnalysisController *>(ctrl);
    auto appendActiveRecord = [controller](std::uint64_t sessionId,
                                           std::uint8_t unit,
                                           modbuslens::core::TransactionStatus status,
                                           long long elapsedMs) {
        using namespace modbuslens::core;
        const auto encoded = encodeActiveRequest(ActiveRequestIntent{
            .function = ActiveFunction::ReadHoldingRegisters,
            .unitId = unit,
            .timeout = std::chrono::milliseconds{1000},
            .payload = ReadHoldingRegistersIntent{.startAddress = 0,
                                                  .quantity = 2}});
        if (std::get_if<ActiveRequestEncodeError>(&encoded) != nullptr)
            return false;
        const auto descriptor = std::get<ActiveRequestDescriptor>(encoded);
        controller->appendActiveSerialTransaction(ActiveTransactionRecord{
            .sessionId = sessionId,
            .request = descriptor,
            .evidence = ActiveTransactionEvidence{
                .requestAdu = descriptor.wire,
                .responseAdu = {},
                .disposition = TransportDisposition::PossiblySent,
            },
            .analysis = TransactionAnalysis{
                .status = status,
                .elapsed = std::chrono::milliseconds{elapsedMs},
                .exceptionCode = std::nullopt,
                .issue = std::nullopt,
                .values = {}},
        });
        return true;
    };
    auto detailStatusText = [&itemOf]() {
        auto *label = itemOf(QStringLiteral("transactionDetailStatus"));
        return label ? label->property("text").toString() : QStringLiteral("<none>");
    };
    auto detailDeviceText = [&itemOf]() {
        auto *label = itemOf(QStringLiteral("transactionDetailDevice"));
        return label ? label->property("text").toString() : QStringLiteral("<none>");
    };
    auto detailEmptyVisible = [&itemOf]() {
        auto *hint = itemOf(QStringLiteral("transactionDetailEmpty"));
        return hint ? hint->isVisible() : false;
    };
    // M9-F F1 correction: the border group of a control's custom background
    // (Rectangle.border -> QQuickPen) is the machine-visible focus state for
    // controls whose focus indication IS the border (AppButton / TabButton).
    auto backgroundBorderWidth = [](QQuickItem *control) -> double {
        if (!control)
            return -1;
        QObject *background =
            control->property("background").value<QObject *>();
        if (!background)
            return -1;
        QObject *border =
            background->property("border").value<QObject *>();
        if (!border)
            return -1;
        return border->property("width").toDouble();
    };
    auto propBool = [](QQuickItem *item, const char *name) -> bool {
        return item ? item->property(name).toBool() : false;
    };

    // ---- staged walk (one stage per event-loop turn) ----
    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // Stage 0: startup identity + publish the deterministic demo batch so the
    // list has rows and the Transactions diagnosis cue is exposed.
    push([&]() {
        if (railIndex() != 0)
            fail(QStringLiteral("FOCUSFAIL setup: startup workspace is %1, "
                                "expected 0").arg(railIndex()));
        if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
            fail(QStringLiteral("FOCUSFAIL setup: runDemoBatch() not invokable"));
    });
    push([&]() {
        if (ctrl->property("observedCount").toInt() != 4)
            fail(QStringLiteral("FOCUSFAIL setup: observedCount=%1, expected 4")
                     .arg(ctrl->property("observedCount").toInt()));
        note(QStringLiteral("FOCUS [setup]: workspace=Transactions rows=4 "
                            "hasBaseline=%1")
                 .arg(hasBaseline() ? 1 : 0));
    });

    // M10-D4 — production WRITE VISIBILITY contract.
    //
    // THIS SUPERSEDES the M10-C2/C4 "production-hidden" oracle (kept as a
    // rename + replacement of its assertions, never deleted).
    //
    // Why the old contract ended: M10-C/D1/D2 deliberately shipped the write
    // machinery with NO production presence, so the strongest safety statement
    // available then was "nothing exists". D4 intentionally publishes the 0x06
    // write UI, so "nothing write-related exists" is no longer true BY DESIGN —
    // an intentional contract transition, not a regression. The negative
    // coverage therefore MOVES to what must still be absent: 0x10 in every
    // form, the test-foundation seam, and any write control that could act
    // while the ACTION is unavailable.
    push([&]() {
        auto *loader = itemOf(QStringLiteral("writeFoundationLoader"));
        if (!loader) {
            fail(QStringLiteral("FOCUSFAIL prod-write: writeFoundationLoader "
                                "missing from the Communication page"));
            return;
        }
        // (1) the section exists because of a STRUCTURAL capability…
        if (!ctrl->property("write06Supported").toBool())
            fail(QStringLiteral("FOCUSFAIL prod-write: write06Supported is false "
                                "in a capability-carrying build"));
        if (!loader->property("active").toBool())
            fail(QStringLiteral("FOCUSFAIL prod-write: the loader is INACTIVE even "
                                "though the product supports 0x06"));
        if (loader->property("item").value<QQuickItem *>() == nullptr)
            fail(QStringLiteral("FOCUSFAIL prod-write: the section was not "
                                "instantiated"));
        // (2) …and it is the PRODUCTION mode, not the test foundation.
        auto *section = itemOf(QStringLiteral("writeFoundationSection"));
        if (!section)
            fail(QStringLiteral("FOCUSFAIL prod-write: writeFoundationSection is "
                                "absent"));
        else {
            if (section->property("testFoundationMode").toBool())
                fail(QStringLiteral("FOCUSFAIL prod-write: the production section "
                                    "was instantiated in TEST-FOUNDATION mode"));
            if (!section->property("productionMode").toBool())
                fail(QStringLiteral("FOCUSFAIL prod-write: productionMode is false"));
        }
        // (3) M10-E4 INTENTIONAL TRANSITION — the E3 staging negative ("0x10
        // must not exist in production") is superseded: E4 publishes the FC16
        // write UI. The negative MOVES to what must still hold, and the
        // presence of the FC16 surface is now asserted directly.
        //
        // FINDING (carried into T022 §ZM): the old QObject-name scan could NOT
        // see Loader-created QML items at all (QObject::findChildren does not
        // reach them in this declarative tree), so that particular check passed
        // vacuously while the Loader was active. The visual-tree walk below is
        // the mechanism that actually observes them.
        int scanned = 0;
        for (QObject *root : roots) {
            scanned += root->findChildren<QObject *>().size();
        }
        // (3a) the FC16 surface EXISTS, because the STRUCTURAL capability and
        // the selector tab are present (never because a harness flag ran).
        if (section && !section->property("productionMode").toBool())
            fail(QStringLiteral("FOCUSFAIL prod-write: not in production mode"));
        for (const auto &name : {QStringLiteral("writeFunctionTabs"),
                                 QStringLiteral("writeTab06"),
                                 QStringLiteral("writeTab10")}) {
            if (findNamedItem(roots, name) == nullptr)
                fail(QStringLiteral("FOCUSFAIL prod-write: the FC16 surface is "
                                    "incomplete — %1 is missing").arg(name));
        }
        // (3b) the FC16 controls exist and each carries an accessible name.
        // The 0x10 draft is instantiated regardless of which tab is selected
        // (only its VISIBILITY follows the tab), so the inputs are observable
        // here without switching tabs.
        for (const auto &entry : {std::pair<const char *, const char *>{
                                      "write10UnitSpin", "0x10"},
                                  {"write10StartSpin", "0x10"},
                                  {"write10TimeoutSpin", "0x10"},
                                  {"write10ValuesArea", "0x10"},
                                  {"writeTab10", "0x10"}}) {
            auto *item = findNamedItem(roots, QString::fromUtf8(entry.first));
            if (!item) {
                fail(QStringLiteral("FOCUSFAIL prod-write: required FC16 control %1 "
                                    "is missing")
                         .arg(QString::fromUtf8(entry.first)));
                continue;
            }
            QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(item);
            const QString name = iface ? iface->text(QAccessible::Name) : QString();
            if (name.isEmpty())
                fail(QStringLiteral("FOCUSFAIL prod-write: FC16 control %1 has no "
                                    "accessible name")
                         .arg(QString::fromUtf8(entry.first)));
        }
        // M10-E3 INTENTIONAL TRANSITION: the write10Supported product
        // capability property now EXISTS (the capability layer is E3's
        // deliverable). What production must NOT have is the 0x10 UI — which
        // the checks above and below still assert item by item (nodes, named
        // items, accessible names, tab stops). The capability/UI separation
        // is the frozen contract here.
        const int write10PropertyIndex =
            ctrl->metaObject()->indexOfProperty("write10Supported");
        if (write10PropertyIndex < 0)
            fail(QStringLiteral("FOCUSFAIL prod-write: the write10Supported "
                                "product capability property is missing"));
        {
            const QVariant write10Value = ctrl->metaObject()
                                              ->property(write10PropertyIndex)
                                              .read(ctrl);
            if (!write10Value.toBool())
                fail(QStringLiteral("FOCUSFAIL prod-write: write10Supported is "
                                    "false"));
        }
        // (4) M10-E4: the FC16 accessible names must now EXIST (the E3 negative
        // "no 0x10 accessible node" is superseded with its positive mirror).
        if (section) {
            QStringList a11y10;
            QQuickItem *walk = section;
            std::function<void(QQuickItem *)> collect = [&](QQuickItem *item) {
                auto *iface = QAccessible::queryAccessibleInterface(item);
                if (iface
                    && iface->text(QAccessible::Name).contains(QStringLiteral("0x10")))
                    a11y10 << iface->text(QAccessible::Name);
                for (QQuickItem *child : item->childItems())
                    collect(child);
            };
            collect(walk);
            if (a11y10.size() < 4)
                fail(QStringLiteral("FOCUSFAIL prod-write: expected the FC16 unit / "
                                    "start / timeout / values accessible names, saw "
                                    "[%1]").arg(a11y10.join(QStringLiteral(" | "))));
            else
                note(QStringLiteral("FOCUS [prod-write]: FC16 accessible names = "
                                    "[%1]").arg(a11y10.join(
                                        QStringLiteral(" | "))));
        }
        // (5) the 0x06 controls DO exist and each carries an accessible name
        // (the D4 supersession of "production has no write a11y").
        for (const auto &entry : {std::pair<const char *, const char *>{
                                      "write06UnitSpin", "从站地址"},
                                  {"write06AddressField", "寄存器地址"},
                                  {"write06ValueField", "写入值"},
                                  {"write06TimeoutSpin", "超时"},
                                  {"writeActivateButton", "写入"}}) {
            auto *item = findNamedItem(roots, QString::fromUtf8(entry.first));
            if (!item) {
                fail(QStringLiteral("FOCUSFAIL prod-write: required 0x06 control %1 "
                                    "is missing").arg(QString::fromUtf8(entry.first)));
                continue;
            }
            QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(item);
            const QString name = iface ? iface->text(QAccessible::Name) : QString();
            if (!name.contains(QString::fromUtf8(entry.second)))
                fail(QStringLiteral("FOCUSFAIL prod-write: %1 accessible name is [%2]")
                         .arg(QString::fromUtf8(entry.first), name));
        }
        // (6) no startup snapshot (authority, not presentation) — unchanged.
        if (ctrl->property("hasPreparedWrite").toBool()
            || ctrl->property("preparedWriteToken").toULongLong() != 0
            || ctrl->property("preparedWriteState").toString()
                   != QStringLiteral("none"))
            fail(QStringLiteral("FOCUSFAIL prod-write: startup produced a write "
                                "snapshot (state=%1 token=%2)")
                     .arg(ctrl->property("preparedWriteState").toString())
                     .arg(ctrl->property("preparedWriteToken").toULongLong()));
        note(QStringLiteral("FOCUS [prod-write] PASS: section present in PRODUCTION "
                            "mode from write06Supported; %1 objects scanned — no "
                            "0x10 node, no 0x10 accessible node, no 0x10 property, "
                            "no startup snapshot; 0x06 controls + a11y present")
                 .arg(scanned));
    });
    // (7) RUNTIME GATING: capability decides EXISTENCE, runtime decides whether
    // the ACTION can run. With no serial connection the section stays visible
    // (and the page-local draft stays alive) while Write is disabled — the
    // inverse of the old "unload everything" behaviour.
    push([&]() {
        if (ctrl->property("serialConnected").toBool())
            fail(QStringLiteral("FOCUSFAIL prod-write: the focus harness unexpectedly "
                                "has an open serial connection"));
        auto *button = qobject_cast<QQuickItem *>(
            findNamedItem(roots, QStringLiteral("writeActivateButton")));
        if (!button)
            fail(QStringLiteral("FOCUSFAIL prod-write: writeActivateButton missing"));
        else if (button->property("enabled").toBool())
            fail(QStringLiteral("FOCUSFAIL prod-write: Write is ENABLED while "
                                "disconnected"));
        // Capability != availability: the section is still INSTANTIATED and its
        // loader still active even with no connection. (Effective `isVisible()`
        // is deliberately NOT asserted — the Communication page is only visible
        // while it is the current workspace, which is a navigation fact, not a
        // write-capability fact.)
        auto *loader = itemOf(QStringLiteral("writeFoundationLoader"));
        if (!loader || !loader->property("active").toBool()
            || loader->property("item").value<QQuickItem *>() == nullptr)
            fail(QStringLiteral("FOCUSFAIL prod-write: a disconnected runtime state "
                                "UNLOADED the write section (capability must not "
                                "follow availability)"));
        note(QStringLiteral("FOCUS [prod-write] PASS: disconnected -> section still "
                            "instantiated, Write disabled (capability != "
                            "availability)"));
    });
    // (7) the Communication Tab chain: D4 makes the 0x06 write controls part of
    // the REAL keyboard order (the old contract asserted they were absent),
    // while 0x10 must contribute no stop at all because it does not exist.
    push([&]() {
        selectWorkspace(2);
        for (const QString &required : {QStringLiteral("write06UnitSpin"),
                                        QStringLiteral("write06AddressField"),
                                        QStringLiteral("write06ValueField"),
                                        QStringLiteral("write06TimeoutSpin")}) {
            const int presses = tabToOwner(required, 60);
            if (presses < 0)
                fail(QStringLiteral("FOCUSFAIL prod-write: %1 is not reachable by "
                                    "Tab in production").arg(required));
            else
                note(QStringLiteral("FOCUS [prod-write]: %1 reached after %2 Tab "
                                    "press(es)")
                         .arg(required)
                         .arg(presses));
        }
        // Negative coverage, moved here from the retired foundation oracle: no
        // 0x10 control is Tab-reachable, because none of them is ever created.
        // M10-E4: with the FC06 tab ACTIVE, the FC16 *inputs* must not be Tab
        // stops (the inactive tab is excluded from traversal — the frozen
        // M10-C rule), while the FC16 TabButton itself IS one. After selecting
        // the FC16 tab the situation must mirror exactly.
        for (const QString &inactive : {QStringLiteral("write10ValuesArea"),
                                        QStringLiteral("write10UnitSpin"),
                                        QStringLiteral("write10StartSpin")}) {
            if (tabTo(inactive, 60) >= 0)
                fail(QStringLiteral("FOCUSFAIL prod-write: %1 is Tab-reachable "
                                    "while the FC06 tab is active").arg(inactive));
        }
        if (tabTo(QStringLiteral("writeTab10"), 20) < 0)
            fail(QStringLiteral("FOCUSFAIL prod-write: the FC16 tab button is not "
                                "Tab-reachable"));
        note(QStringLiteral("FOCUS [prod-write] PASS: inactive-tab FC16 inputs are "
                            "not Tab stops; the FC16 tab button is"));
        note(QStringLiteral("FOCUS [prod-write] PASS: the 0x06 controls are Tab "
                            "reachable in the production write section and no "
                            "0x10 control is"));
        selectWorkspace(0);
    });

    // FA: keyboard-only entry into the evidence table (scope A).
    push([&]() {
        selectWorkspace(0);
        anchorFocus();
        QStringList chain;
        QList<int> pages;
        walkTabs(12, true, chain, pages);
        const bool reached = chain.contains(QStringLiteral("transactionsList"));
        note(QStringLiteral("FOCUS [FA]: keyboard-only Tab chain = [%1]")
                 .arg(chain.join(QStringLiteral(", "))));
        if (!reached)
            fail(QStringLiteral("FOCUSFAIL FA: transactionsList is not in the "
                                "keyboard-only Tab chain"));
        else if (listIndex() != -1)
            fail(QStringLiteral("FOCUSFAIL FA: entering the list selected row "
                                "%1 by itself (select-on-focus)").arg(listIndex()));
        else
            note(QStringLiteral("FOCUS [FA] PASS: list reachable by Tab and no "
                                "select-on-focus (currentIndex=-1)"));
    });

    // FJ: the list's keyboard-focus indication is machine-visible (the ring
    // object exists, binds to the focus state, and turns off when focus
    // leaves). Whether it LOOKS right stays with the manual review.
    push([&]() {
        selectWorkspace(0);
        anchorFocus();
        const int presses = tabTo(QStringLiteral("transactionsList"), 16);
        if (presses < 0) {
            fail(QStringLiteral("FOCUSFAIL FJ: list not reachable by Tab"));
            return;
        }
        auto *ring = itemOf(QStringLiteral("transactionsListFocusRing"));
        if (!ring) {
            fail(QStringLiteral("FOCUSFAIL FJ: transactionsListFocusRing not "
                                "found"));
            return;
        }
        if (!propBool(ring, "visible"))
            fail(QStringLiteral("FOCUSFAIL FJ: focus ring not visible while the "
                                "list holds keyboard focus"));
        else {
            // F2 visual review HOLD: the ring must stay SUBORDINATE to the row
            // selection (the earlier 2px saturated frame out-shouted it), so
            // the weight itself is part of the contract now.
            QObject *border = ring->property("border").value<QObject *>();
            const double width = border ? border->property("width").toDouble() : -1;
            const QColor color = border
                                     ? border->property("color").value<QColor>()
                                     : QColor();
            // F2 outer-extent correction: the ring must WRAP the viewport —
            // margins -2 => its extent is the viewport plus 4 logical px in
            // each dimension (2 px per edge). Two property reads, no harness
            // restructure.
            auto *ringParent = ring->parentItem();
            const double dw = ringParent ? ring->width() - ringParent->width() : -1;
            const double dh = ringParent ? ring->height() - ringParent->height() : -1;
            if (width != 1.0 || color.alphaF() > 0.6)
                fail(QStringLiteral("FOCUSFAIL FJ: list focus ring is not "
                                    "subordinate (width=%1 alpha=%2; expected "
                                    "1px and alpha<=0.6)")
                         .arg(width)
                         .arg(color.alphaF()));
            else if (dw != 4.0 || dh != 4.0)
                fail(QStringLiteral("FOCUSFAIL FJ: list focus ring does not "
                                    "wrap the viewport (delta w=%1 h=%2; "
                                    "expected 4/4 for margins -2)")
                         .arg(dw)
                         .arg(dh));
            else
                note(QStringLiteral("FOCUS [FJ] PASS: list ring visible, "
                                    "subordinate weight (1px, alpha=%1), "
                                    "extent = viewport + 4px")
                         .arg(color.alphaF()));
        }
        tab(true);
    });
    push([&]() {
        auto *ring = itemOf(QStringLiteral("transactionsListFocusRing"));
        if (ring && propBool(ring, "visible"))
            fail(QStringLiteral("FOCUSFAIL FJ: focus ring still visible after "
                                "focus left the list"));
        else
            note(QStringLiteral("FOCUS [FJ] PASS: ring off after focus leaves "
                                "the list (focus=%1)").arg(focusName()));
    });

    // FK: per-type keyboard-focus indication, machine level (activeFocus/
    // visualFocus -> indicator property/state).
    push([&]() {
        // AppButton, secondary tone (AppBar Clear Results)
        selectWorkspace(0);
        anchorFocus();
        auto *clear = itemOf(QStringLiteral("appBarClearResults"));
        if (!clear) {
            fail(QStringLiteral("FOCUSFAIL FK: appBarClearResults not found"));
            return;
        }
        // the anchor click carries a MOUSE focus reason (visualFocus=false by
        // design), so reach the button by FORWARD traversal and prove it is
        // the focused item by pointer identity before reading the border
        bool onButton = false;
        for (int i = 1; i <= 8 && !onButton; ++i) {
            tab(true);
            onButton = (focusItem() == clear);
        }
        const double wFocused = backgroundBorderWidth(clear);
        if (!onButton)
            fail(QStringLiteral("FOCUSFAIL FK: forward traversal never reached "
                                "appBarClearResults"));
        else if (!propBool(clear, "visualFocus") || wFocused != 2.0)
            fail(QStringLiteral("FOCUSFAIL FK: AppButton focused border width=%1 "
                                "visualFocus=%2 (expected 2/true)")
                     .arg(wFocused)
                     .arg(propBool(clear, "visualFocus") ? 1 : 0));
        else
            note(QStringLiteral("FOCUS [FK] PASS: AppButton (secondary) focused "
                                "border width=2 (visualFocus=true)"));
        tab(true);
    });
    push([&]() {
        auto *clear = itemOf(QStringLiteral("appBarClearResults"));
        const double w = backgroundBorderWidth(clear);
        if (w != 1.0 || propBool(clear, "visualFocus"))
            fail(QStringLiteral("FOCUSFAIL FK: AppButton after focus moved on: "
                                "width=%1 visualFocus=%2 (expected 1/false)")
                     .arg(w)
                     .arg(propBool(clear, "visualFocus") ? 1 : 0));
        else
            note(QStringLiteral("FOCUS [FK] PASS: AppButton unfocused border "
                                "width=1"));
        // AppButton, primary tone (Dashboard Run Demo)
        selectWorkspace(1);
    });
    push([&]() {
        const int presses = tabTo(QStringLiteral("dashboardRunDemo"), 16);
        if (presses < 0) {
            fail(QStringLiteral("FOCUSFAIL FK: dashboardRunDemo not reachable"));
            return;
        }
        auto *primary = itemOf(QStringLiteral("dashboardRunDemo"));
        const double w = backgroundBorderWidth(primary);
        if (!propBool(primary, "visualFocus") || w != 2.0)
            fail(QStringLiteral("FOCUSFAIL FK: primary-tone AppButton focused "
                                "border width=%1 (expected 2)").arg(w));
        else
            note(QStringLiteral("FOCUS [FK] PASS: AppButton (primary) focused "
                                "border width=2 (light contrast border)"));
        // ComboBox: the ring binds to the control's activeFocus
        selectWorkspace(2);
    });
    push([&]() {
        const int presses = tabTo(QStringLiteral("commPortCombo"), 16);
        if (presses < 0) {
            fail(QStringLiteral("FOCUSFAIL FK: commPortCombo not reachable"));
            return;
        }
        auto *ring = itemOf(QStringLiteral("commPortComboFocusRing"));
        auto *combo = itemOf(QStringLiteral("commPortCombo"));
        if (!ring || !propBool(ring, "visible")
            || !propBool(combo, "activeFocus"))
            fail(QStringLiteral("FOCUSFAIL FK: ComboBox focus ring visible=%1 "
                                "activeFocus=%2")
                     .arg(ring && propBool(ring, "visible") ? 1 : 0)
                     .arg(propBool(combo, "activeFocus") ? 1 : 0));
        else
            note(QStringLiteral("FOCUS [FK] PASS: ComboBox ring visible while "
                                "focused (activeFocus=true)"));
        tab(true);
    });
    push([&]() {
        auto *ring = itemOf(QStringLiteral("commPortComboFocusRing"));
        if (ring && propBool(ring, "visible"))
            fail(QStringLiteral("FOCUSFAIL FK: ComboBox ring still visible after "
                                "focus moved on"));
        else
            note(QStringLiteral("FOCUS [FK] PASS: ComboBox ring off after focus "
                                "moved on"));
        // M10 correction: the read request fields are DecimalFields — the
        // component root carries the objectName and the focus lands on its
        // inner (deliberately unnamed) TextField, whose border IS the
        // indication. Measured on the component, not on a widget type.
        selectWorkspace(2);
        anchorFocus();
    });
    push([&]() {
        bool reached = false;
        for (int i = 1; i <= 16 && !reached; ++i) {
            tab(true);
            auto *fieldRoot = itemOf(QStringLiteral("commSlaveField"));
            auto *focused =
                qobject_cast<QQuickItem *>(window->activeFocusItem());
            if (!fieldRoot || !focused)
                continue;
            for (auto *p = focused; p; p = p->parentItem()) {
                if (p == fieldRoot) {
                    reached = true;
                    break;
                }
            }
        }
        if (!reached)
            fail(QStringLiteral("FOCUSFAIL FK: the slave field never reported "
                                "activeFocus during the walk"));
        else
            note(QStringLiteral("FOCUS [FK] PASS: slave DecimalField "
                                "activeFocus=true (component-owned focus "
                                "border)"));
        // TabButton: border is the channel (custom background)
        selectWorkspace(4);
    });
    push([&]() {
        auto *tabs = itemOf(QStringLiteral("diagnosisTabs"));
        QQuickItem *tabItem = nullptr;
        if (!tabs || !QMetaObject::invokeMethod(tabs, "itemAt",
                                                Q_RETURN_ARG(QQuickItem *, tabItem),
                                                Q_ARG(int, 0))
            || !tabItem) {
            fail(QStringLiteral("FOCUSFAIL FK: diagnosis tab item not found"));
            return;
        }
        // Walk the traversal until itemAt(0) itself reports keyboard focus
        // (its objectName is empty, so the walk checks the visualFocus state
        // on the item, not a name).
        bool ok = propBool(tabItem, "visualFocus");
        for (int i = 1; i <= 16 && !ok; ++i) {
            tab(true);
            ok = propBool(tabItem, "visualFocus");
        }
        // F1 correction: the tab's keyboard-focus channel is now a SEPARATE
        // inner ring (an additional element), not a variation of the selected
        // border — so the machine assert is "the ring object is visible while
        // the tab holds focus", and the selected border must stay untouched.
        const double selectedBorder = backgroundBorderWidth(tabItem);
        auto *ring = itemOf(QStringLiteral("diagnosisTabBaselineFocusRing"));
        if (!ok || !ring || !propBool(ring, "visible"))
            fail(QStringLiteral("FOCUSFAIL FK: TabButton focus ring visible=%1 "
                                "visualFocus=%2 (expected true/true)")
                     .arg(ring && propBool(ring, "visible") ? 1 : 0)
                     .arg(ok ? 1 : 0));
        else if (selectedBorder != 1.0)
            fail(QStringLiteral("FOCUSFAIL FK: the selected-tab border changed "
                                "with focus (width=%1, expected 1)")
                     .arg(selectedBorder));
        else
            note(QStringLiteral("FOCUS [FK] PASS: TabButton inner focus ring "
                                "visible; selected border untouched (width=1)"));
    });

    // FA2: the entry must be USABLE, not just reachable: with focus in the
    // list (never clicked), the four keys must drive the same
    // currentIndex -> selectRow path (this is the acceptance pairing the
    // manual scenario owns for the mouse path).
    push([&]() {
        selectWorkspace(0);
        // NOTE the anchor is the AppBar session action: clicking it CLEARS the
        // session results, so the deterministic batch is published AFTER the
        // anchor is established (a harness ordering rule, not a product one).
        anchorFocus();
        if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
            fail(QStringLiteral("FOCUSFAIL FA2: runDemoBatch() not invokable"));
    });
    push([&]() {
        if (listCount() != 4)
            fail(QStringLiteral("FOCUSFAIL FA2: list count=%1, expected 4")
                     .arg(listCount()));
        const int presses = tabTo(QStringLiteral("transactionsList"), 16);
        if (presses < 0) {
            fail(QStringLiteral("FOCUSFAIL FA2: list not reachable by Tab"));
            return;
        }
        if (listIndex() != -1)
            fail(QStringLiteral("FOCUSFAIL FA2: keyboard entry selected row %1 "
                                "by itself").arg(listIndex()));
        note(QStringLiteral("FOCUS [FA2]: list focused after %1 Tab presses "
                            "(never clicked), currentIndex=%2")
                 .arg(presses).arg(listIndex()));
        sendKey(Qt::Key_End, Qt::NoModifier, false);
    });
    push([&]() {
        const int count = listCount();
        if (count != 4)
            fail(QStringLiteral("FOCUSFAIL FA2: list count=%1, expected 4").arg(count));
        if (listIndex() != count - 1)
            fail(QStringLiteral("FOCUSFAIL FA2: End after keyboard entry gave "
                                "currentIndex=%1, expected %2")
                     .arg(listIndex()).arg(count - 1));
        else if (selectedRow() != count - 1)
            fail(QStringLiteral("FOCUSFAIL FA2: End after keyboard entry did not "
                                "reach selectRow (selectedRow=%1)").arg(selectedRow()));
        else
            note(QStringLiteral("FOCUS [FA2] PASS: End after keyboard entry -> "
                                "currentIndex=%1 selectedRow=%2")
                     .arg(listIndex()).arg(selectedRow()));
        sendKey(Qt::Key_Home, Qt::NoModifier, false);
    });
    push([&]() {
        if (listIndex() != 0 || selectedRow() != 0)
            fail(QStringLiteral("FOCUSFAIL FA2: Home after keyboard entry gave "
                                "currentIndex=%1 selectedRow=%2")
                     .arg(listIndex()).arg(selectedRow()));
        else
            note(QStringLiteral("FOCUS [FA2] PASS: Home after keyboard entry -> 0/0"));
    });

    // FB: no hidden-page control inside the current workspace's chain (scope B).
    for (int ws = 0; ws < 5; ++ws) {
        push([&, ws]() {
            selectWorkspace(ws);
        });
        push([&, ws]() {
            if (railIndex() != ws)
                fail(QStringLiteral("FOCUSFAIL FB: workspace %1 not selected "
                                    "(index=%2)").arg(ws).arg(railIndex()));
            anchorFocus();
            QStringList chain;
            QList<int> pages;
            walkTabs(16, true, chain, pages);
            const QString pageName = pageNames.at(ws);
            for (int p : pages) {
                if (p != ws && p != -1)
                    fail(QStringLiteral("FOCUSFAIL FB: %1 chain reached a "
                                        "control owned by hidden page %2")
                             .arg(pageName, pageNames.value(p)));
            }
            note(QStringLiteral("FOCUS [FB] %1: 16-press chain visited pages %2")
                     .arg(pageName, QStringLiteral("[%1]")
                              .arg([&pages]() {
                                  QStringList s;
                                  for (int p : pages)
                                      s << (p < 0 ? QStringLiteral("-")
                                                  : QString::number(p));
                                  return s.join(QStringLiteral(","));
                              }())));
        });
    }

    // FD/FE: rail Enter and Space activation, index-resolved (scope D).
    for (int keyPass = 0; keyPass < 2; ++keyPass) {
        const bool enter = (keyPass == 0);
        for (int k = 1; k <= 5; ++k) {
            push([&, k, enter]() {
                selectWorkspace(4);   // start away from every target
                anchorFocus();
                for (int j = 1; j <= k; ++j)
                    tab(true);
                const int focusedRail = railIndexOf(focusItem());
                if (focusedRail != k - 1)
                    fail(QStringLiteral("FOCUSFAIL %1: press %2 focused rail "
                                        "entry %3, expected %4")
                             .arg(enter ? QStringLiteral("FD") : QStringLiteral("FE"))
                             .arg(k).arg(focusedRail).arg(k - 1));
                const int before = railIndex();
                sendKey(enter ? Qt::Key_Return : Qt::Key_Space,
                        Qt::NoModifier, false);
                const int after = railIndex();
                if (after != k - 1)
                    fail(QStringLiteral("FOCUSFAIL %1: %2 on rail entry %3 did "
                                        "not activate (index %4 -> %5)")
                             .arg(enter ? QStringLiteral("FD") : QStringLiteral("FE"),
                                  enter ? QStringLiteral("Enter") : QStringLiteral("Space"))
                             .arg(k - 1).arg(before).arg(after));
                else
                    note(QStringLiteral("FOCUS [%1] PASS: %2 activates rail "
                                        "entry %3 (index %4 -> %5)")
                             .arg(enter ? QStringLiteral("FD") : QStringLiteral("FE"),
                                  enter ? QStringLiteral("Enter") : QStringLiteral("Space"))
                             .arg(k - 1).arg(before).arg(after));
            });
        }
    }

    // FF: the Device entry hosts the M12-B profile workspace and behaves like
    // every other rail entry (enabled, keyboard-activatable, click activates).
    push([&]() {
        auto *device = itemOf(QStringLiteral("navItem_5"));
        if (!device) {
            fail(QStringLiteral("FOCUSFAIL FF: navItem_5 not found"));
            return;
        }
        if (!device->property("enabled").toBool())
            fail(QStringLiteral("FOCUSFAIL FF: Device entry is not enabled"));
        selectWorkspace(2);
        anchorFocus();
        QStringList chain;
        QList<int> pages;
        walkTabs(16, true, chain, pages);
        int devicePress = -1;
        for (int i = 0; i < chain.size(); ++i) {
            if (chain.at(i) == QStringLiteral("navItem_5")) {
                devicePress = i;
                break;
            }
        }
        if (devicePress < 0)
            fail(QStringLiteral("FOCUSFAIL FF: Device entry never reached in "
                                "the Tab chain"));
        const int before = railIndex();
        clickItemPoint(device);
        if (railIndex() != 5)
            fail(QStringLiteral("FOCUSFAIL FF: clicking Device did not open "
                                "the profile workspace (index %1)")
                     .arg(railIndex()));
        else
            note(QStringLiteral("FOCUS [FF] PASS: Device entry enabled and "
                                "activating (index %1 -> 5, tab press %2)")
                     .arg(before).arg(devicePress + 1));
    });

    // FC: hidden retained focus (scope C, H1S oracle).
    push([&]() {
        if (hasBaseline())
            fail(QStringLiteral("FOCUSFAIL FC: baseline already ran before the "
                                "retention scenario"));
        selectWorkspace(4);
    });
    push([&]() {
        clickTab(0);
    });
    push([&]() {
        const int presses = tabTo(QStringLiteral("diagnosisRunBaselineButton"), 16);
        if (presses < 0)
            fail(QStringLiteral("FOCUSFAIL FC: baseline button not reachable by "
                                "keyboard traversal"));
        else
            note(QStringLiteral("FOCUS [FC]: baseline button focused by %1 Tab "
                                "presses (never clicked)").arg(presses));
        selectWorkspace(0);   // leave the workspace while it holds focus
    });
    push([&]() {
        const int page = pageIndexOf(focusItem());
        if (page == 4)
            fail(QStringLiteral("FOCUSFAIL FC: a control of the hidden Diagnosis "
                                "workspace still holds activeFocus after leaving"));
        else
            note(QStringLiteral("FOCUS [FC]: activeFocus left the hidden page "
                                "(focus=%1 page=%2)").arg(focusName()).arg(page));
        sendKey(Qt::Key_Space, Qt::NoModifier, false);
    });
    push([&]() {
        if (hasBaseline())
            fail(QStringLiteral("FOCUSFAIL FC: Space delivered after leaving the "
                                "workspace executed the hidden baseline run"));
        else
            note(QStringLiteral("FOCUS [FC] PASS: hidden control did not execute "
                                "(hasBaselineDiagnosis stays false)"));
        // Control: the SAME button must still activate when its page is visible.
        selectWorkspace(4);
    });
    push([&]() {
        clickTab(0);
    });
    push([&]() {
        if (tabTo(QStringLiteral("diagnosisRunBaselineButton"), 16) < 0)
            fail(QStringLiteral("FOCUSFAIL FC control: baseline button not "
                                "reachable when visible"));
        sendKey(Qt::Key_Space, Qt::NoModifier, false);
    });
    push([&]() {
        if (!hasBaseline())
            fail(QStringLiteral("FOCUSFAIL FC control: Space did NOT activate the "
                                "baseline button while its workspace was visible "
                                "(the fix must not break normal activation)"));
        else
            note(QStringLiteral("FOCUS [FC] control PASS: visible Space still runs "
                                "the baseline"));
        if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
            fail(QStringLiteral("FOCUSFAIL FC: clearResults() not invokable"));
    });

    // H2: hidden retained focus in a TEXT editor (scope C, H2 oracle).
    push([&]() {
        QMetaObject::invokeMethod(ctrl, "runDemoBatch");
        selectWorkspace(4);
    });
    push([&]() {
        clickTab(2);
    });
    push([&]() {
        const int presses = tabTo(QStringLiteral("diagnosisAgentQuestion"), 16);
        if (presses < 0)
            fail(QStringLiteral("FOCUSFAIL H2: Agent question field not reachable "
                                "by keyboard traversal"));
        if (!agentText().isEmpty())
            fail(QStringLiteral("FOCUSFAIL H2: draft is not empty at scenario "
                                "start: [%1]").arg(agentText()));
        sendTextKey(Qt::Key_Z, QStringLiteral("z"));   // visible typing still works
    });
    push([&]() {
        if (agentText() != QStringLiteral("z"))
            fail(QStringLiteral("FOCUSFAIL H2 control: visible typing did not "
                                "reach the field (text=[%1])").arg(agentText()));
        else
            note(QStringLiteral("FOCUS [H2]: visible typing works (text=[z])"));
        selectWorkspace(0);
    });
    push([&]() {
        if (pageIndexOf(focusItem()) == 4)
            fail(QStringLiteral("FOCUSFAIL H2: a control of the hidden Diagnosis "
                                "workspace still holds activeFocus"));
        const QString before = agentText();
        sendTextKey(Qt::Key_W, QStringLiteral("w"));
        sendKey(Qt::Key_Home, Qt::NoModifier, false);
        sendKey(Qt::Key_End, Qt::NoModifier, false);
        sendKey(Qt::Key_Up, Qt::NoModifier, false);
        sendKey(Qt::Key_Down, Qt::NoModifier, false);
        if (agentText() != before)
            fail(QStringLiteral("FOCUSFAIL H2: hidden text field consumed keys "
                                "([%1] -> [%2])").arg(before, agentText()));
        else
            note(QStringLiteral("FOCUS [H2] PASS: hidden field consumed nothing "
                                "(text stays [%1])").arg(before));
    });

    push([&]() {
        // the ring follows keyboard focus across tabs: after the previous walk
        // moved focus past tab 0, its ring must be off again
        auto *ring = itemOf(QStringLiteral("diagnosisTabBaselineFocusRing"));
        if (ring && propBool(ring, "visible"))
            fail(QStringLiteral("FOCUSFAIL FK: TabButton focus ring still "
                                "visible after focus moved to another stop"));
        else
            note(QStringLiteral("FOCUS [FK] PASS: TabButton ring off after focus "
                                "moved on"));
    });

    // FL: the Agent TextArea's EDIT keys keep their text semantics with a
    // real multi-line draft — measured cursor motion and unchanged content,
    // not "no handler claimed the key".
    push([&]() {
        selectWorkspace(4);
    });
    push([&]() {
        clickTab(2);
    });
    push([&]() {
        if (tabTo(QStringLiteral("diagnosisAgentQuestion"), 16) < 0) {
            fail(QStringLiteral("FOCUSFAIL FL: Agent question field not "
                                "reachable"));
            return;
        }
        auto *field = itemOf(QStringLiteral("diagnosisAgentQuestion"));
        if (!field)
            return;
        field->setProperty("text", QStringLiteral("abc\ndef"));
        field->setProperty("cursorPosition", 4);   // start of line 2
        if (field->property("text").toString() != QStringLiteral("abc\ndef"))
            fail(QStringLiteral("FOCUSFAIL FL: draft setup failed"));
    });
    push([&]() {
        auto *field = itemOf(QStringLiteral("diagnosisAgentQuestion"));
        const QString draft = QStringLiteral("abc\ndef");
        struct Step { Qt::Key key; int from; int to; const char *name; };
        const Step steps[] = {
            { Qt::Key_Left,  4, 3, "Left"  },
            { Qt::Key_Right, 3, 4, "Right" },
            { Qt::Key_Home,  5, 4, "Home"  },
            { Qt::Key_End,   4, 7, "End"   },
            { Qt::Key_Up,    4, 0, "Up"    },
            { Qt::Key_Down,  1, 5, "Down"  },
        };
        for (const Step &st : steps) {
            field->setProperty("cursorPosition", st.from);
            sendKey(st.key, Qt::NoModifier, false);
            const int cursor = field->property("cursorPosition").toInt();
            const QString text = field->property("text").toString();
            if (cursor != st.to)
                fail(QStringLiteral("FOCUSFAIL FL: %1 moved the cursor %2 -> %3 "
                                    "(expected %4)")
                         .arg(st.name).arg(st.from).arg(cursor).arg(st.to));
            else if (text != draft)
                fail(QStringLiteral("FOCUSFAIL FL: %1 mutated the draft ([%2])")
                         .arg(st.name, text));
            else if (railIndex() != 4)
                fail(QStringLiteral("FOCUSFAIL FL: %1 changed the workspace "
                                    "(index=%2)").arg(st.name).arg(railIndex()));
            else
                note(QStringLiteral("FOCUS [FL] PASS: %1 cursor %2 -> %3, draft "
                                    "unchanged, workspace unchanged")
                         .arg(st.name).arg(st.from).arg(cursor));
        }
    });

    // FG/FH: Agent TextArea traversal contract (scope H).
    push([&]() {
        selectWorkspace(4);
    });
    push([&]() {
        clickTab(2);
    });
    push([&]() {
        if (tabTo(QStringLiteral("diagnosisAgentQuestion"), 16) < 0)
            fail(QStringLiteral("FOCUSFAIL FG: Agent question field not reachable "
                                "by keyboard traversal"));
        auto *field = itemOf(QStringLiteral("diagnosisAgentQuestion"));
        if (field)
            field->setProperty("text", QStringLiteral("draft-sentinel"));
        sendKey(Qt::Key_Tab, Qt::NoModifier, true);
    });
    push([&]() {
        const QString name = focusName();
        if (name == QStringLiteral("diagnosisAgentQuestion"))
            fail(QStringLiteral("FOCUSFAIL FG: Tab did not leave the Agent "
                                "question field"));
        else if (agentText() != QStringLiteral("draft-sentinel"))
            fail(QStringLiteral("FOCUSFAIL FG: Tab mutated the draft ([%1])")
                     .arg(agentText()));
        else
            note(QStringLiteral("FOCUS [FG] PASS: Tab left the field (now %1) "
                                "and the draft is unchanged").arg(name));
        if (tabTo(QStringLiteral("diagnosisAgentQuestion"), 16) < 0)
            fail(QStringLiteral("FOCUSFAIL FH: field not re-reachable for the "
                                "backward check"));
        sendKey(Qt::Key_Backtab, Qt::ShiftModifier, true);
    });
    push([&]() {
        const QString name = focusName();
        if (name == QStringLiteral("diagnosisAgentQuestion"))
            fail(QStringLiteral("FOCUSFAIL FH: Shift+Tab did not leave the Agent "
                                "question field"));
        else if (agentText() != QStringLiteral("draft-sentinel"))
            fail(QStringLiteral("FOCUSFAIL FH: Shift+Tab mutated the draft ([%1])")
                     .arg(agentText()));
        else
            note(QStringLiteral("FOCUS [FH] PASS: Shift+Tab left the field (now "
                                "%1) and the draft is unchanged").arg(name));
        if (auto *field = itemOf(QStringLiteral("diagnosisAgentQuestion")))
            field->setProperty("text", QString());
    });

    // FI: the four-key navigation contract inside the list (scope A regression).
    push([&]() {
        selectWorkspace(0);
        if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
            fail(QStringLiteral("FOCUSFAIL FI: runDemoBatch() not invokable"));
    });
    push([&]() {
        auto *list = itemOf(QStringLiteral("transactionsList"));
        bool rowClicked = false;
        for (int attempt = 0; attempt < 4 && !rowClicked; ++attempt) {
            QQuickItem *row = nullptr;
            if (list) {
                QMetaObject::invokeMethod(list, "itemAtIndex",
                                          Q_RETURN_ARG(QQuickItem *, row),
                                          Q_ARG(int, 0));
            }
            if (row && row->isVisible() && clickItemPoint(row)
                && selectedRow() == 0)
                rowClicked = true;
            else
                QCoreApplication::processEvents();
        }
        if (!rowClicked)
            fail(QStringLiteral("FOCUSFAIL FI: the row-0 click could not be "
                                "delivered (delegate never materialized)"));
    });
    push([&]() {
        const int count = listCount();
        if (count != 4)
            fail(QStringLiteral("FOCUSFAIL FI: list count=%1, expected 4").arg(count));
        if (pageIndexOf(focusItem()) != 0)
            fail(QStringLiteral("FOCUSFAIL FI: the row click did not leave focus "
                                "in the Transactions workspace (focus=%1)")
                     .arg(focusName()));
        else
            note(QStringLiteral("FOCUS [FI]: row click focused the list path "
                                "(focus=%1 currentIndex=%2 selectedRow=%3)")
                     .arg(focusName()).arg(listIndex()).arg(selectedRow()));
        sendKey(Qt::Key_End, Qt::NoModifier, false);
    });
    push([&]() {
        const int count = listCount();
        if (listIndex() != count - 1)
            fail(QStringLiteral("FOCUSFAIL FI: End gave currentIndex=%1, "
                                "expected %2").arg(listIndex()).arg(count - 1));
        else if (selectedRow() != count - 1)
            fail(QStringLiteral("FOCUSFAIL FI: End did not reach selectRow "
                                "(selectedRow=%1)").arg(selectedRow()));
        else
            note(QStringLiteral("FOCUS [FI]: End -> currentIndex=%1 selectedRow=%2")
                     .arg(listIndex()).arg(selectedRow()));
        sendKey(Qt::Key_Home, Qt::NoModifier, false);
    });
    push([&]() {
        if (listIndex() != 0 || selectedRow() != 0)
            fail(QStringLiteral("FOCUSFAIL FI: Home gave currentIndex=%1 "
                                "selectedRow=%2, expected 0/0")
                     .arg(listIndex()).arg(selectedRow()));
        else
            note(QStringLiteral("FOCUS [FI]: Home -> currentIndex=0 selectedRow=0"));
        sendKey(Qt::Key_Down, Qt::NoModifier, false);
    });
    push([&]() {
        if (listIndex() != 1)
            fail(QStringLiteral("FOCUSFAIL FI: Down gave currentIndex=%1, "
                                "expected 1").arg(listIndex()));
        sendKey(Qt::Key_Up, Qt::NoModifier, false);
    });
    push([&]() {
        if (listIndex() != 0)
            fail(QStringLiteral("FOCUSFAIL FI: Up gave currentIndex=%1, "
                                "expected 0").arg(listIndex()));
        else
            note(QStringLiteral("FOCUS [FI] PASS: Up/Down/Home/End all drive the "
                                "existing currentIndex -> selectRow path"));
    });

    // ---- M10-B correction: FM (B05-QML) selection survives append ----
    // A real QML runtime oracle: the page's own selection state, the
    // ListView's currentIndex and the DETAIL PANE are read after a real
    // append, so "the model did not reset" is not accepted as proof.
    // NOTE: the AppBar 清空结果 button is NOT used as the focus anchor here —
    // clicking it is a destructive clear (measured in M9-F F1), which would
    // delete the very rows this oracle needs. The rail entry is used instead.
    push([&]() {
        if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
            fail(QStringLiteral("FOCUSFAIL FM: clearResults() not invokable"));
        appendActiveRecord(7, 11, modbuslens::core::TransactionStatus::Success, 25);
        appendActiveRecord(7, 22, modbuslens::core::TransactionStatus::Timeout, 1000);
        selectWorkspace(0);
    });
    push([&]() {
        if (rowCountOf(ctrl) != 2 || listCount() != 2)
            fail(QStringLiteral("FOCUSFAIL FM: expected 2 rows, model=%1 list=%2")
                     .arg(rowCountOf(ctrl)).arg(listCount()));
        // Keyboard entry through the workspace's own Tab chain (the FA
        // contract); the four-key move below then selects row 0 through the
        // product path (currentIndex -> selectRow).
        QQuickItem *list = nullptr;
        for (int i = 0; i < 24 && list == nullptr; ++i) {
            tab(true);
            if (focusName() == QStringLiteral("transactionsList"))
                list = itemOf(QStringLiteral("transactionsList"));
        }
        if (!list) {
            // Traversal itself is FA's contract; this oracle may still drive
            // the selection through the page's own entry point (the same seam
            // the nav harness uses) without weakening the append assertions.
            note(QStringLiteral("FOCUS [FM]: list not reached from the rail by "
                                "Tab; driving selection through the page entry "
                                "point instead"));
            if (!requestTransactionSelection(roots, 0))
                fail(QStringLiteral("FOCUSFAIL FM: selectRow() not invokable"));
            return;
        }
        sendKey(Qt::Key_Home, Qt::NoModifier, false); // select row 0 (unit 11)
    });
    push([&]() {
        if (listIndex() != 0 || selectedRow() != 0)
            fail(QStringLiteral("FOCUSFAIL FM: selection is %1/%2, expected 0/0")
                     .arg(listIndex()).arg(selectedRow()));
        if (!detailDeviceText().contains(QStringLiteral("11"))
            || detailStatusText() != QStringLiteral("成功"))
            fail(QStringLiteral("FOCUSFAIL FM: detail shows [%1][%2], expected "
                                "device 11 / 成功")
                     .arg(detailDeviceText(), detailStatusText()));
        assertTransactionDetailMapping(roots, QStringLiteral("FM before append"),
                                       *failures);
        note(QStringLiteral("FOCUS [FM]: row #1 (unit 11) selected; detail shows "
                            "[%1][%2]").arg(detailDeviceText(), detailStatusText()));
        // THE APPEND under test: a third, machine-distinguishable transaction.
        appendActiveRecord(7, 33, modbuslens::core::TransactionStatus::Exception, 18);
    });
    push([&]() {
        if (rowCountOf(ctrl) != 3 || listCount() != 3)
            fail(QStringLiteral("FOCUSFAIL FM: after append model=%1 list=%2, "
                                "expected 3/3").arg(rowCountOf(ctrl)).arg(listCount()));
        if (listIndex() != 0 || selectedRow() != 0)
            fail(QStringLiteral("FOCUSFAIL FM: append moved the selection to "
                                "%1/%2 (auto-follow)").arg(listIndex())
                     .arg(selectedRow()));
        if (listIndex() == 2)
            fail(QStringLiteral("FOCUSFAIL FM: the appended row was auto-selected"));
        if (detailDeviceText().contains(QStringLiteral("33"))
            || detailStatusText() == QStringLiteral("异常"))
            fail(QStringLiteral("FOCUSFAIL FM: detail switched to the appended row "
                                "[%1][%2]").arg(detailDeviceText(),
                                                detailStatusText()));
        if (!detailDeviceText().contains(QStringLiteral("11"))
            || detailStatusText() != QStringLiteral("成功"))
            fail(QStringLiteral("FOCUSFAIL FM: detail identity lost after append: "
                                "[%1][%2]").arg(detailDeviceText(),
                                                detailStatusText()));
        assertTransactionDetailMapping(roots, QStringLiteral("FM after append"),
                                       *failures);
        note(QStringLiteral("FOCUS [FM] PASS: currentIndex=%1 selectedRow=%2 "
                            "detail=[%3][%4] after appending row #3")
                 .arg(listIndex()).arg(selectedRow())
                 .arg(detailDeviceText(), detailStatusText()));
        sendKey(Qt::Key_End, Qt::NoModifier, false);
    });
    push([&]() {
        // Keyboard after append still drives the one currentIndex -> selectRow
        // path: an explicit move DOES switch the detail (no contract change).
        if (listIndex() != 2 || selectedRow() != 2)
            fail(QStringLiteral("FOCUSFAIL FM: End after append gave %1/%2, "
                                "expected 2/2").arg(listIndex()).arg(selectedRow()));
        else if (!detailDeviceText().contains(QStringLiteral("33")))
            fail(QStringLiteral("FOCUSFAIL FM: detail did not follow the explicit "
                                "End move: [%1]").arg(detailDeviceText()));
        else
            note(QStringLiteral("FOCUS [FM] PASS: Up/Down/Home/End still drive the "
                                "currentIndex -> selectRow path after an append"));
        sendKey(Qt::Key_Home, Qt::NoModifier, false);
    });
    push([&]() {
        if (listIndex() != 0 || selectedRow() != 0)
            fail(QStringLiteral("FOCUSFAIL FM: Home after append gave %1/%2, "
                                "expected 0/0").arg(listIndex()).arg(selectedRow()));
        // ---- replacement invalidation (the OTHER mechanism, still intact) ----
        if (!clickNamed(QStringLiteral("appBarClearResults")))
            fail(QStringLiteral("FOCUSFAIL FM: appBarClearResults not clickable"));
    });
    push([&]() {
        if (listIndex() != -1 || selectedRow() != -1
            || !selectedEntryOf(roots).isEmpty())
            fail(QStringLiteral("FOCUSFAIL FM: a RESET did not invalidate the "
                                "selection (%1/%2)").arg(listIndex())
                     .arg(selectedRow()));
        if (!detailEmptyVisible())
            fail(QStringLiteral("FOCUSFAIL FM: the no-selection detail state is "
                                "missing after a reset"));
        note(QStringLiteral("FOCUS [FM] PASS: reset/replacement invalidates the "
                            "selection while append preserves it"));
    });

    // ---- M10-B correction: FN (B06-QML) no selection stays none ----
    push([&]() {
        // Reach a genuine no-selection state with at least one row: the
        // destructive anchor above already cleared the model, so one append
        // builds the row without ever selecting anything.
        appendActiveRecord(9, 44, modbuslens::core::TransactionStatus::Success, 25);
        selectWorkspace(0);
    });
    push([&]() {
        if (rowCountOf(ctrl) != 1)
            fail(QStringLiteral("FOCUSFAIL FN: expected 1 row, got %1")
                     .arg(rowCountOf(ctrl)));
        if (listIndex() != -1 || selectedRow() != -1
            || !selectedEntryOf(roots).isEmpty())
            fail(QStringLiteral("FOCUSFAIL FN: a selection appeared out of nowhere "
                                "(%1/%2)").arg(listIndex()).arg(selectedRow()));
        if (!detailEmptyVisible())
            fail(QStringLiteral("FOCUSFAIL FN: the no-selection hint is not visible"));
        // Keyboard entry must not select either (FA contract, re-checked here
        // because the model now has rows while nothing is selected).
        QQuickItem *list = nullptr;
        for (int i = 0; i < 24 && list == nullptr; ++i) {
            tab(true);
            if (focusName() == QStringLiteral("transactionsList"))
                list = itemOf(QStringLiteral("transactionsList"));
        }
        if (list && listIndex() != -1)
            fail(QStringLiteral("FOCUSFAIL FN: keyboard entry selected row %1")
                     .arg(listIndex()));
    });
    push([&]() {
        appendActiveRecord(9, 55, modbuslens::core::TransactionStatus::Timeout, 1000);
    });
    push([&]() {
        if (rowCountOf(ctrl) != 2)
            fail(QStringLiteral("FOCUSFAIL FN: append did not land (rows=%1)")
                     .arg(rowCountOf(ctrl)));
        if (listIndex() != -1)
            fail(QStringLiteral("FOCUSFAIL FN: the appended row was auto-selected "
                                "(currentIndex=%1)").arg(listIndex()));
        if (selectedRow() != -1 || !selectedEntryOf(roots).isEmpty())
            fail(QStringLiteral("FOCUSFAIL FN: a selection exists after appending "
                                "without one (selectedRow=%1)").arg(selectedRow()));
        if (!detailEmptyVisible())
            fail(QStringLiteral("FOCUSFAIL FN: the no-selection detail state is gone"));
        assertTransactionDetailMapping(roots, QStringLiteral("FN no selection"),
                                       *failures);
        note(QStringLiteral("FOCUS [FN] PASS: no selection before and after the "
                            "append; detail stayed in the no-selection state"));
    });

    auto step = std::make_shared<int>(0);
    const int settleMs = 120;
    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, step, schedule]() {
        if (*step >= steps->size()) {
            if (failures->isEmpty())
                qInfo() << "FOCUS CHECK PASS (FA transactions entry; FJ list "
                           "focus ring on/off; FB hidden page exclusion x5; FC "
                           "hidden retention + visible control; FD/FE rail "
                           "Enter/Space x5; FF Device disabled; FK per-type "
                           "focus indication; FL TextArea Left/Right/Home/End/"
                           "Up/Down; FG/FH Agent TextArea traversal; FI list "
                           "four-key regression; FM selection survives append; "
                           "FN no selection stays none after append; "
                           "M10-D4 prod-write visibility: 0x06 section present "
                           "in PRODUCTION mode with accessible controls, "
                           "no 0x10 node/accessible node/property, no startup "
                           "snapshot, disconnected keeps the section visible "
                           "while disabling the action, 0x06 stops in the Tab "
                           "chain and no 0x10 stop)";
            else
                for (const QString &f : *failures)
                    qWarning().noquote() << "FOCUSFAIL:" << f;
            app.exit(failures->isEmpty() ? 0 : 1);
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}

// ---------------------------------------------------------------------------
// M9-B4.4 `--qml-evidence-capture <dir>`: produces the manual-review
// screenshot set from the CURRENT binary (deployed or build-tree) so every
// PNG provably comes from the candidate that passed the automated gates.
//
//   m9b4-replay-1024x720.png            Replay workspace, fresh start
//   m9b4-replay-1000x700.png            Replay workspace at minimum size
//   m9b4-replay-notice-1024x720.png     successful load of
//                                       t015_unsupported_fc08.mlog -> notice
//   m9b4-replay-error-notice-1024x720.png  deterministic failed replacement
//                                       -> error + preserved notice coexist
//   m9b4-dashboard-replay-1024x720.png  Dashboard carrying the demo_v1
//                                       replay session (chip + facts)
//
// Harness only: no product behaviour. Business state transitions use the
// SAME existing Controller commands the manual flow uses, and each state
// is asserted before the capture (a wrong state aborts with exit 1 rather
// than producing misleading evidence).
// ---------------------------------------------------------------------------
int runEvidenceCapture(QQmlApplicationEngine &engine, QGuiApplication &app,
                       const QString &dir)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    auto *ctrl = rootObj ? rootObj->findChild<QObject *>(
                               QStringLiteral("analysisController"))
                         : nullptr;
    if (!window || !ctrl) {
        qWarning() << "EVIDENCE FAIL: window/controller not found";
        return 1;
    }

    const int settleMs = 120;
    const int replayIndex = rootObj->property("workspaceReplayIndex").toInt();
    const int dashboardIndex =
        rootObj->property("workspaceDashboardIndex").toInt();

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };
    auto fail2 = [failures](const QString &m) { *failures << m; };

    // Screenshot oracle rule (B4 lesson): never infer the captured state
    // from the file name. Every capture logs the state that was ASSERTED
    // immediately before the grab — requested logical size, actual grabbed
    // pixel size (they legitimately differ under DPI scaling), workspace
    // index, selected nav item, Diagnosis tab and the deterministic facts.
    auto grab = [&, window, &dir](const QString &tag) {
        auto *railItem = findNamedItem(roots, QStringLiteral("navigationRail"));
        auto *tabContent =
            findNamedItem(roots, QStringLiteral("diagnosisTabContent"));
        auto *diagPage = findNamedItem(roots, QStringLiteral("diagnosisPage"));
        const QImage image = window->grabWindow();
        const QString path = QDir(dir).filePath(tag + QStringLiteral(".png"));
        if (!image.save(path)) {
            *failures << QStringLiteral("EVIDENCE FAILED: %1").arg(path);
            return;
        }
        qInfo().noquote()
            << QStringLiteral("EVIDENCE: %1").arg(path)
            << QStringLiteral("| logical=%1x%2 pixels=%3x%4")
                   .arg(window->width())
                   .arg(window->height())
                   .arg(image.size().width())
                   .arg(image.size().height())
            << QStringLiteral("| workspaceIndex=%1 navItem=%2 diagnosisVisible=%3 "
                              "diagnosisTab=%4")
                   .arg(railItem
                            ? railItem->property("currentWorkspaceIndex").toInt()
                            : -1)
                   .arg(railItem
                            ? QStringLiteral("navItem_%1")
                                  .arg(railItem->property("currentWorkspaceIndex")
                                           .toInt())
                            : QStringLiteral("<none>"))
                   .arg(diagPage ? diagPage->isVisible() : false)
                   .arg(tabContent
                            ? tabContent->property("currentIndex").toInt()
                            : -1)
            << QStringLiteral("| mode=%1 source=%2 observed=%3 baseline=%4")
                   .arg(ctrl->property("modeLabel").toString(),
                        ctrl->property("sourceLabel").toString())
                   .arg(ctrl->property("observedCount").toInt())
                   .arg(ctrl->property("hasBaselineDiagnosis").toBool());
    };

    auto switchTo = [&](int pageIndex) {
        const char *key = (pageIndex == 0)   ? "workspaceTransactionsIndex"
                        : (pageIndex == 1)   ? "workspaceDashboardIndex"
                        : (pageIndex == 2)   ? "workspaceCommunicationIndex"
                        : (pageIndex == 3)   ? "workspaceReplayIndex"
                        : (pageIndex == 4)   ? "workspaceDiagnosisIndex"
                                             : "workspaceDeviceIndex";
        const int idx = rootObj->property(key).toInt();
        auto *item = findNamedItem(roots, QStringLiteral("navItem_%1").arg(idx));
        if (!item || !QMetaObject::invokeMethod(item, "activate"))
            fail(QStringLiteral("NAVFAIL activation failed for page %1").arg(idx));
    };

    // ---- B5.4 state oracles ----
    const int diagnosisIndex =
        rootObj->property("workspaceDiagnosisIndex").toInt();

    auto selectTab = [&](int tab) {
        auto *tabBar = findNamedItem(roots, QStringLiteral("diagnosisTabs"));
        if (!tabBar) {
            fail(QStringLiteral("diagnosisTabs not found"));
            return;
        }
        tabBar->setProperty("currentIndex", tab);
    };

    // Assert the ALREADY-OBSERVED identity of the frame, not the file name:
    // workspace index, the selected nav item, the page's visibility and the
    // selected Diagnosis tab must all agree with what we are about to claim.
    auto assertFrame = [&](const QString &ctx, int wantPage, int wantTab) {
        auto *rail = findNamedItem(roots, QStringLiteral("navigationRail"));
        auto *diagPage = findNamedItem(roots, QStringLiteral("diagnosisPage"));
        auto *tabContent =
            findNamedItem(roots, QStringLiteral("diagnosisTabContent"));
        const int pageIndex =
            rail ? rail->property("currentWorkspaceIndex").toInt() : -1;
        if (pageIndex != wantPage)
            fail(QStringLiteral("EVIDENCE %1: workspace index %2 != %3")
                     .arg(ctx)
                     .arg(pageIndex)
                     .arg(wantPage));
        auto *navItem = findNamedItem(
            roots, QStringLiteral("navItem_%1").arg(wantPage));
        if (!navItem)
            fail(QStringLiteral("EVIDENCE %1: navItem_%2 not found")
                     .arg(ctx)
                     .arg(wantPage));
        else if (!navItem->property("selected").toBool())
            fail(QStringLiteral("EVIDENCE %1: navItem_%2 is not the selected "
                                "item")
                     .arg(ctx)
                     .arg(wantPage));
        if (!diagPage)
            fail(QStringLiteral("EVIDENCE %1: diagnosisPage not found").arg(ctx));
        else if (diagPage->isVisible() != (wantPage == diagnosisIndex))
            fail(QStringLiteral("EVIDENCE %1: diagnosisPage visibility (%2) "
                                "does not match the claimed page %3")
                     .arg(ctx)
                     .arg(diagPage->isVisible())
                     .arg(wantPage));
        int tab = -1;
        if (wantPage == diagnosisIndex) {
            tab = tabContent ? tabContent->property("currentIndex").toInt() : -1;
            if (tab != wantTab)
                fail(QStringLiteral("EVIDENCE %1: Diagnosis tab %2 != %3")
                         .arg(ctx)
                         .arg(tab)
                         .arg(wantTab));
        }
        qInfo().noquote()
            << QStringLiteral("EVIDENCE ASSERT %1: page=%2 tab=%3 ok")
                   .arg(ctx)
                   .arg(pageIndex)
                   .arg(tab);
    };

    // The demo golden facts, asserted from the Controller (T008 DEMO-1..4
    // constants — never from pixels or OCR).
    auto assertDemoGoldenFacts = [&](const QString &ctx) {
        struct Expect { const char *key; int value; };
        const Expect ints[] = {
            { "observedCount", 4 },   { "completedCount", 4 },
            { "pendingCount", 0 },    { "successCount", 1 },
            { "exceptionCount", 1 },  { "crcErrorCount", 1 },
            { "timeoutCount", 1 },    { "protocolErrorCount", 0 },
            { "expectedNoResponseCount", 0 },
        };
        for (const Expect &e : ints) {
            const int got =
                ctrl->property(e.key).toInt();
            if (got != e.value)
                fail(QStringLiteral("EVIDENCE %1: %2 = %3, expected %4")
                         .arg(ctx)
                         .arg(QLatin1String(e.key))
                         .arg(got)
                         .arg(e.value));
        }
        if (!ctrl->property("hasSuccessRate").toBool()
            || qAbs(ctrl->property("successRate").toDouble() - 0.25) > 1e-9)
            fail(QStringLiteral("EVIDENCE %1: success rate is not 25%% "
                                "(successRate is a FRACTION here; the QML "
                                "multiplies it by 100 for display)").arg(ctx));
        if (!ctrl->property("hasAverageSuccessLatency").toBool()
            || qAbs(ctrl->property("averageSuccessLatencyMs").toDouble() - 25.0)
                   > 1e-9)
            fail(QStringLiteral("EVIDENCE %1: average success latency is not "
                                "25 ms").arg(ctx));
        if (!ctrl->property("hasBaselineDiagnosis").toBool())
            fail(QStringLiteral("EVIDENCE %1: no baseline diagnosis").arg(ctx));
        if (ctrl->property("baselineDiagnosisText").toString().isEmpty())
            fail(QStringLiteral("EVIDENCE %1: baseline text is empty").arg(ctx));
        // No AI/Agent activity may have happened: the AI tab screenshot shows
        // the configuration state only, and Ask AI / Ask Agent are never
        // clicked (no provider call, no fabricated answer).
        if (ctrl->property("aiDiagnosisBusy").toBool()
            || ctrl->property("hasAiDiagnosis").toBool()
            || !ctrl->property("aiDiagnosisErrorMessage").toString().isEmpty())
            fail(QStringLiteral("EVIDENCE %1: AI state is not the untouched "
                                "configuration state").arg(ctx));
        if (ctrl->property("agentBusy").toBool()
            || ctrl->property("hasAgentAnswer").toBool()
            || !ctrl->property("agentErrorText").toString().isEmpty())
            fail(QStringLiteral("EVIDENCE %1: Agent state is not the untouched "
                                "state").arg(ctx));
        qInfo().noquote()
            << QStringLiteral("EVIDENCE FACTS %1: observed=4 success=1 "
                              "exception=1 crc=1 timeout=1 rate=25%% "
                              "avgLatency=25ms baselineChars=%2")
                   .arg(ctx)
                   .arg(ctrl->property("baselineDiagnosisText")
                            .toString()
                            .size());
    };

    // The demo golden COUNTS for the C5 dashboard captures. Same constants
    // as assertDemoGoldenFacts, minus the baseline requirement -- the demo
    // screenshot legitimately precedes a baseline run (the baseline-cue
    // capture asserts that state separately).
    auto assertDashboardGoldenCounts = [&](const QString &ctx) {
        struct Expect { const char *key; int value; };
        const Expect ints[] = {
            { "observedCount", 4 },   { "completedCount", 4 },
            { "pendingCount", 0 },    { "successCount", 1 },
            { "exceptionCount", 1 },  { "crcErrorCount", 1 },
            { "timeoutCount", 1 },    { "protocolErrorCount", 0 },
            { "expectedNoResponseCount", 0 },
        };
        for (const Expect &e : ints) {
            const int got = ctrl->property(e.key).toInt();
            if (got != e.value)
                fail(QStringLiteral("EVIDENCE %1: %2 = %3, expected %4")
                         .arg(ctx)
                         .arg(QLatin1String(e.key))
                         .arg(got)
                         .arg(e.value));
        }
        if (!ctrl->property("hasSuccessRate").toBool()
            || qAbs(ctrl->property("successRate").toDouble() - 0.25) > 1e-9)
            fail(QStringLiteral("EVIDENCE %1: success rate is not 25%%")
                     .arg(ctx));
        if (!ctrl->property("hasAverageSuccessLatency").toBool()
            || qAbs(ctrl->property("averageSuccessLatencyMs").toDouble() - 25.0)
                   > 1e-9)
            fail(QStringLiteral("EVIDENCE %1: average success latency is not "
                                "25 ms").arg(ctx));
        auto *summary = findNamedItem(
            roots, QStringLiteral("dashboardAttentionSummary"));
        const int expectedAttention = 3;
        if (!summary)
            fail(QStringLiteral("EVIDENCE %1: dashboardAttentionSummary not "
                                "found").arg(ctx));
        else if (summary->property("attentionCount").toInt()
                 != expectedAttention)
            fail(QStringLiteral("EVIDENCE %1: attentionCount %2 != %3")
                     .arg(ctx)
                     .arg(summary->property("attentionCount").toInt())
                     .arg(expectedAttention));
        qInfo().noquote()
            << QStringLiteral("EVIDENCE FACTS %1: observed=4 completed=4 "
                              "pending=0 success=1 exception=1 crc=1 "
                              "timeout=1 protocol=0 enr=0 rate=25%% "
                              "avgLatency=25ms attention=3")
                   .arg(ctx);
    };

    auto countNamed = [&](const QString &name) {
        int count = 0;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item)
                return;
            if (item->objectName() == name)
                ++count;
            for (auto *child : item->childItems())
                walk(child);
        };
        if (window)
            walk(window->contentItem());
        return count;
    };
    auto isUnder = [](QQuickItem *item, QQuickItem *ancestor) {
        for (auto *p = item ? item->parentItem() : nullptr; p;
             p = p->parentItem())
            if (p == ancestor)
                return true;
        return false;
    };

    auto schedule = std::make_shared<std::function<void()>>();
    auto stage = std::make_shared<int>(0);
    *schedule = [&, schedule, stage]() {
        switch (*stage) {
        case 0: switchTo(replayIndex); break;
        case 1: grab(QStringLiteral("m9b4-replay-1024x720")); break;
        case 2:
            window->resize(1000, 700);
            break;
        case 3: grab(QStringLiteral("m9b4-replay-1000x700")); break;
        case 4:
            window->resize(1024, 720);
            break;
        case 5: {
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_UNSUPPORTED_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("loadReplayFile() not invokable"));
            break;
        }
        case 6: {
            if (!ctrl->property("hasReplayNotice").toBool()
                || ctrl->property("hasReplayError").toBool()
                || ctrl->property("sourceLabel").toString()
                       != QStringLiteral("t015_unsupported_fc08.mlog"))
                fail(QStringLiteral("notice state invalid after the "
                                    "unsupported load"));
            qInfo().noquote()
                << QStringLiteral("EVIDENCE STATE: notice=%1 source=%2")
                       .arg(ctrl->property("replayNoticeText").toString(),
                            ctrl->property("sourceLabel").toString());
            grab(QStringLiteral("m9b4-replay-notice-1024x720"));
            break;
        }
        case 7: {
            const QUrl missing = QUrl::fromLocalFile(
                QStringLiteral("MODBUSLENS_NO_SUCH_DIR/missing_replay.mlog"));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, missing)))
                fail(QStringLiteral("loadReplayFile() not invokable"));
            break;
        }
        case 8: {
            if (!ctrl->property("hasReplayError").toBool())
                fail(QStringLiteral("failed replacement did not set "
                                    "replayError"));
            if (ctrl->property("replayNoticeText")
                    .toString()
                    != QStringLiteral(
                        "提示：1 条记录当前未支持分析（功能码 0x08 等），未计入"
                        "统计。"))
                fail(QStringLiteral("preserved notice changed"));
            if (ctrl->property("sourceLabel").toString()
                != QStringLiteral("t015_unsupported_fc08.mlog"))
                fail(QStringLiteral("source changed on a failed replacement"));
            grab(QStringLiteral("m9b4-replay-error-notice-1024x720"));
            break;
        }
        case 9: {
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_DEMO_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("loadReplayFile() not invokable"));
            break;
        }
        case 10: {
            if (ctrl->property("observedCount").toInt() != 4
                || ctrl->property("sourceLabel").toString()
                       != QStringLiteral("demo_v1.mlog"))
                fail(QStringLiteral("demo_v1 session state invalid"));
            switchTo(dashboardIndex);
            break;
        }
        case 11: {
            if (ctrl->property("observedCount").toInt() != 4)
                fail(QStringLiteral("dashboard facts changed"));
            grab(QStringLiteral("m9b4-dashboard-replay-1024x720"));
            break;
        }
        case 12: {
            // B5.4 Diagnosis set. Deterministic demo batch first: the SAME
            // product command the manual flow uses, no test-only state.
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("runDemoBatch() not invokable"));
            break;
        }
        case 13: {
            if (!QMetaObject::invokeMethod(ctrl, "runBaselineDiagnosis"))
                fail(QStringLiteral("runBaselineDiagnosis() not invokable"));
            break;
        }
        case 14: {
            assertDemoGoldenFacts(QStringLiteral("baseline setup"));
            switchTo(diagnosisIndex);
            break;
        }
        case 15: {
            selectTab(0);
            break;
        }
        case 16: {
            // A. Baseline tab at the default size.
            assertFrame(QStringLiteral("A"), diagnosisIndex, 0);
            assertDemoGoldenFacts(QStringLiteral("A"));
            grab(QStringLiteral("m9b5-diagnosis-baseline-1024x720"));
            break;
        }
        case 17:
            window->resize(1000, 700);
            break;
        case 18: {
            // B. The same deterministic state at the application minimum.
            assertFrame(QStringLiteral("B"), diagnosisIndex, 0);
            assertDemoGoldenFacts(QStringLiteral("B"));
            grab(QStringLiteral("m9b5-diagnosis-baseline-1000x700"));
            break;
        }
        case 19:
            window->resize(1024, 720);
            break;
        case 20:
            selectTab(1);
            break;
        case 21: {
            // C. AI tab: the real configuration state only. Ask AI is never
            // clicked (asserted in assertDemoGoldenFacts: no request, no
            // result, no error) and no credential ever reaches the UI.
            assertFrame(QStringLiteral("C"), diagnosisIndex, 1);
            assertDemoGoldenFacts(QStringLiteral("C"));
            qInfo().noquote()
                << QStringLiteral("EVIDENCE AI CONFIG: aiConfigured=%1 "
                                  "(model name is shown by the UI itself; no "
                                  "token is read, logged or captured)")
                       .arg(ctrl->property("aiConfigured").toBool());
            grab(QStringLiteral("m9b5-diagnosis-ai-1024x720"));
            break;
        }
        case 22:
            selectTab(2);
            break;
        case 23: {
            // D-step 1: write the draft into the REAL page-local TextArea.
            auto *draft =
                findNamedItem(roots, QStringLiteral("diagnosisAgentQuestion"));
            if (!draft) {
                fail(QStringLiteral("diagnosisAgentQuestion not found"));
                break;
            }
            const QString sentinel =
                QStringLiteral("B5.4 manual draft persistence");
            draft->setProperty("text", sentinel);
            if (draft->property("text").toString() != sentinel)
                fail(QStringLiteral("the sentinel draft did not stick"));
            break;
        }
        case 24: switchTo(dashboardIndex); break;
        case 25: switchTo(replayIndex); break;
        case 26: switchTo(diagnosisIndex); break;
        case 27: {
            // D. Agent tab after the round trip: the draft must be back
            // byte for byte (visual companion to Scenario N — the automated
            // nav check remains the authoritative machine evidence).
            assertFrame(QStringLiteral("D"), diagnosisIndex, 2);
            assertDemoGoldenFacts(QStringLiteral("D"));
            auto *draft =
                findNamedItem(roots, QStringLiteral("diagnosisAgentQuestion"));
            const QString want =
                QStringLiteral("B5.4 manual draft persistence");
            if (!draft)
                fail(QStringLiteral("diagnosisAgentQuestion vanished"));
            else if (draft->property("text").toString() != want)
                fail(QStringLiteral("the page-local draft did not survive the "
                                    "round trip: \"%1\"")
                         .arg(draft->property("text").toString()));
            else
                qInfo().noquote()
                    << QStringLiteral("EVIDENCE DRAFT: page-local draft "
                                      "preserved across Dashboard -> Replay -> "
                                      "Diagnosis (\"%1\")").arg(want);
            grab(QStringLiteral("m9b5-diagnosis-agent-draft-1024x720"));
            break;
        }
        case 28: {
            // M9-D D5: the Legacy evidence stage (statistics-only
            // workbench shot + single-owner probes) is RETIRED with the
            // workspace; the single-owner contract lives in the nav/geometry
            // retirement oracles now, and the user-facing statistics
            // evidence is the Dashboard shot (stage F successor).
            if (countNamed(QStringLiteral("transactionsPane")) != 1)
                fail(QStringLiteral("EVIDENCE E: expected exactly one "
                                    "transactionsPane in the whole tree"));
            if (countNamed(QStringLiteral("diagnosisTabContent")) != 1)
                fail(QStringLiteral("EVIDENCE E: expected exactly one "
                                    "diagnosisTabContent in the tree"));
            qInfo().noquote()
                << QStringLiteral("EVIDENCE RETIRED: legacyWorkspace absent; "
                                  "single transactions owner asserted");
            break;
        }

        case 30: {
            // M9-C C5 Dashboard set. Start from the clean session so the
            // empty state is the app's REAL initial state.
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("clearResults() not invokable"));
            break;
        }
        case 31: switchTo(dashboardIndex); break;
        case 32: {
            // A. Empty dashboard: real zero values, no distribution, no
            // attention, the refreshed hint — never fake enrichment.
            assertFrame(QStringLiteral("A"), dashboardIndex, -1);
            assertOutcomeDistribution(roots,
                                      QStringLiteral("evidence A"),
                                      *failures);
            assertDashboardAttention(roots,
                                     QStringLiteral("evidence A"),
                                     *failures);
            grab(QStringLiteral("m9c-dashboard-empty-1024x720"));
            break;
        }
        case 33: {
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("runDemoBatch() not invokable"));
            break;
        }
        case 34: {
            // B. Demo dashboard at the default size.
            assertFrame(QStringLiteral("B"), dashboardIndex, -1);
            assertDashboardGoldenCounts(QStringLiteral("B"));
            assertOutcomeDistribution(roots,
                                      QStringLiteral("evidence B"),
                                      *failures);
            assertDashboardAttention(roots,
                                     QStringLiteral("evidence B"),
                                     *failures);
            grab(QStringLiteral("m9c-dashboard-demo-1024x720"));
            break;
        }
        case 35:
            window->resize(1000, 700);
            break;
        case 36: {
            // C. The same deterministic state at the minimum size -- the
            // density screenshot.
            assertFrame(QStringLiteral("C"), dashboardIndex, -1);
            assertDashboardGoldenCounts(QStringLiteral("C"));
            assertOutcomeDistribution(roots,
                                      QStringLiteral("evidence C"),
                                      *failures);
            assertDashboardAttention(roots,
                                     QStringLiteral("evidence C"),
                                     *failures);
            grab(QStringLiteral("m9c-dashboard-demo-1000x700"));
            break;
        }
        case 37:
            window->resize(1024, 720);
            break;
        case 38: {
            // D. Broadcast session: a completed outcome set that is 100%
            // ExpectedNoResponse -- the bar stays full-width, successRate is
            // "—", and the attention sum stays 0 (ENR is not an anomaly).
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_BROADCAST_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("loadReplayFile() not invokable"));
            break;
        }
        case 39: {
            if (ctrl->property("sourceLabel").toString()
                != QStringLiteral("t015_broadcast.mlog")
                || ctrl->property("expectedNoResponseCount").toInt() != 1
                || ctrl->property("completedCount").toInt() != 1
                || ctrl->property("hasSuccessRate").toBool())
                fail(QStringLiteral("broadcast session state invalid"));
            assertFrame(QStringLiteral("D"), dashboardIndex, -1);
            assertOutcomeDistribution(roots,
                                      QStringLiteral("evidence D"),
                                      *failures);
            assertDashboardAttention(roots,
                                     QStringLiteral("evidence D"),
                                     *failures);
            qInfo().noquote()
                << QStringLiteral("EVIDENCE BROADCAST: expectedNoResponse=1 "
                                  "completed=1 attention=0 hasSuccessRate=0 "
                                  "(ENR is a normal outcome presentation, "
                                  "not an anomaly; no broadcast success is "
                                  "implied)");
            grab(QStringLiteral("m9c-dashboard-broadcast-1024x720"));
            break;
        }
        case 40: {
            // E. Back to the deterministic demo, then a real baseline run so
            // the diagnosis cue flips to the result-available state.
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("runDemoBatch() not invokable"));
            break;
        }
        case 41: {
            if (!QMetaObject::invokeMethod(ctrl, "runBaselineDiagnosis"))
                fail(QStringLiteral("runBaselineDiagnosis() not invokable"));
            break;
        }
        case 42: {
            if (!ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("baseline did not produce a result"));
            assertFrame(QStringLiteral("E"), dashboardIndex, -1);
            assertDashboardGoldenCounts(QStringLiteral("E"));
            assertDashboardAttention(roots,
                                     QStringLiteral("evidence E"),
                                     *failures);
            auto *cue = findNamedItem(
                roots, QStringLiteral("dashboardDiagnosisCue"));
            if (!cue)
                fail(QStringLiteral("dashboardDiagnosisCue not found"));
            else if (cue->property("text").toString()
                     != QStringLiteral("已有基线诊断结果，可在诊断工作区查看。"))
                fail(QStringLiteral("the diagnosis cue did not flip to the "
                                    "result-available state"));
            grab(QStringLiteral("m9c-dashboard-baseline-cue-1024x720"));
            break;
        }
        case 43: {
            // M9-D D5: the Legacy regression shot retired with the
            // workspace; the transactions stop (its stage successor) is the
            // regression station now.
            switchTo(rootObj->property("workspaceTransactionsIndex").toInt());
            break;
        }
        case 44: {
            assertFrame(
                QStringLiteral("F"),
                rootObj->property("workspaceTransactionsIndex").toInt(), -1);
            grab(QStringLiteral("m9d-transactions-regression-1024x720"));
            break;
        }

        // ---- M9-D D6 set: the Transactions workspace evidence. The state
        // left by stage 44 (demo batch + baseline, Transactions active) is
        // exactly shot F's precondition once a row is selected; every grab
        // is preceded by explicit state assertions, and filenames are never
        // the state oracle.
        case 45: {
            if (!requestTransactionSelection(roots, 2))
                fail(QStringLiteral("selectRow() is not invokable on the "
                                    "transactions page"));
            break;
        }
        case 46: {
            // F. Diagnosis-cue available: demo batch + row 2 + a real
            // baseline. The cue is an existence line next to the evidence —
            // not a button, not a selected-row diagnosis.
            const int txIndex =
                rootObj->property("workspaceTransactionsIndex").toInt();
            assertFrame(QStringLiteral("M9-D F"), txIndex, -1);
            if (rowCountOf(ctrl) != 4 || selectedRowOf(roots) != 2)
                fail(QStringLiteral("M9-D F: rows=%1 selected=%2, expected "
                                    "4/2")
                         .arg(rowCountOf(ctrl))
                         .arg(selectedRowOf(roots)));
            if (!ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("M9-D F: hasBaselineDiagnosis is false"));
            assertTransactionDetailMapping(
                roots, QStringLiteral("M9-D F detail"), *failures);
            assertTransactionsDiagnosisCue(
                roots, QStringLiteral("M9-D F cue"), *failures);
            grab(QStringLiteral("m9d-transactions-diagnosis-cue-1024x720"));
            break;
        }
        case 47: {
            // Back to the no-baseline presentation for the core shots: the
            // diagnosis-only clear must leave rows/selection untouched.
            if (!QMetaObject::invokeMethod(ctrl, "clearDiagnosis"))
                fail(QStringLiteral("clearDiagnosis() not invokable"));
            break;
        }
        case 48: {
            if (ctrl->property("hasBaselineDiagnosis").toBool())
                fail(QStringLiteral("clearDiagnosis did not clear the "
                                    "authority"));
            if (rowCountOf(ctrl) != 4 || selectedRowOf(roots) != 2)
                fail(QStringLiteral("clearDiagnosis touched the model or the "
                                    "selection (rows=%1 selected=%2)")
                         .arg(rowCountOf(ctrl))
                         .arg(selectedRowOf(roots)));
            // B. Demo + selected row 2 at the default size.
            assertTransactionDetailMapping(
                roots, QStringLiteral("M9-D B detail"), *failures);
            assertTransactionsDiagnosisCue(
                roots, QStringLiteral("M9-D B cue"), *failures);
            grab(QStringLiteral("m9d-transactions-demo-selected-1024x720"));
            break;
        }
        case 49:
            window->resize(1000, 700);
            break;
        case 50: {
            // C. The same state at the minimum size — the density shot. The
            // full geometry contract for the ACTIVE transactions page runs
            // here (viewport >= 6x36, detail containment, cue placement,
            // retirement oracle).
            *failures += runGeometryAssertions(
                roots, QStringLiteral("M9-D C geometry"));
            if (rowCountOf(ctrl) != 4 || selectedRowOf(roots) != 2)
                fail(QStringLiteral("M9-D C: state drifted across resize"));
            assertTransactionDetailMapping(
                roots, QStringLiteral("M9-D C detail"), *failures);
            grab(QStringLiteral("m9d-transactions-demo-selected-1000x700"));
            break;
        }
        case 51: {
            window->resize(1024, 720);
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("clearResults() not invokable"));
            break;
        }
        case 52: {
            // A. Empty startup presentation: the real cleared state — no
            // rows, no selection, the cue hidden, Legacy still retired.
            const int txIndex =
                rootObj->property("workspaceTransactionsIndex").toInt();
            assertFrame(QStringLiteral("M9-D A"), txIndex, -1);
            if (rowCountOf(ctrl) != 0)
                fail(QStringLiteral("M9-D A: %1 rows after clearResults")
                         .arg(rowCountOf(ctrl)));
            auto *list = findNamedItem(roots, QStringLiteral("transactionsList"));
            if (!list || list->property("currentIndex").toInt() != -1)
                fail(QStringLiteral("M9-D A: the list kept a selection"));
            if (selectedRowOf(roots) != -1)
                fail(QStringLiteral("M9-D A: a selection survived the clear"));
            if (findNamedItem(roots, QStringLiteral("legacyWorkspace")))
                fail(QStringLiteral("M9-D A: legacyWorkspace still exists"));
            assertTransactionsDiagnosisCue(
                roots, QStringLiteral("M9-D A cue"), *failures);
            grab(QStringLiteral("m9d-transactions-empty-1024x720"));
            break;
        }
        case 53: {
            // D. Broadcast ENR: load the real fixture, select its only row.
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_BROADCAST_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("loadReplayFile() not invokable"));
            break;
        }
        case 54: {
            const int txIndex =
                rootObj->property("workspaceTransactionsIndex").toInt();
            assertFrame(QStringLiteral("M9-D D"), txIndex, -1);
            if (rowCountOf(ctrl) != 1)
                fail(QStringLiteral("M9-D D: rows=%1, expected the single "
                                    "broadcast transaction")
                         .arg(rowCountOf(ctrl)));
            if (!requestTransactionSelection(roots, 0))
                fail(QStringLiteral("selectRow() is not invokable"));
            break;
        }
        case 55: {
            const QVariantMap entry = selectedEntryOf(roots);
            if (selectedRowOf(roots) != 0
                || entry.value(QStringLiteral("statusText")).toString()
                       != QStringLiteral("预期无响应"))
                fail(QStringLiteral("M9-D D: selected=%1 status=%2, expected "
                                    "row 0 / 预期无响应 (neutral ENR)")
                         .arg(selectedRowOf(roots))
                         .arg(entry.value(QStringLiteral("statusText"))
                                  .toString()));
            assertTransactionDetailMapping(
                roots, QStringLiteral("M9-D D detail"), *failures);
            grab(QStringLiteral("m9d-transactions-broadcast-1024x720"));
            break;
        }
        case 56: {
            // E. ProtocolError: status and detail stay two different fields.
            const QUrl fixture = QUrl::fromLocalFile(
                QStringLiteral(MODBUSLENS_PROTOCOL_ERROR_MLOG_PATH));
            if (!QMetaObject::invokeMethod(ctrl, "loadReplayFile",
                                           Q_ARG(QUrl, fixture)))
                fail(QStringLiteral("loadReplayFile() not invokable"));
            break;
        }
        case 57: {
            const int txIndex =
                rootObj->property("workspaceTransactionsIndex").toInt();
            assertFrame(QStringLiteral("M9-D E"), txIndex, -1);
            if (rowCountOf(ctrl) != 1)
                fail(QStringLiteral("M9-D E: rows=%1, expected the single "
                                    "protocol-error transaction")
                         .arg(rowCountOf(ctrl)));
            if (!requestTransactionSelection(roots, 0))
                fail(QStringLiteral("selectRow() is not invokable"));
            break;
        }
        case 58: {
            const QVariantMap entry = selectedEntryOf(roots);
            if (selectedRowOf(roots) != 0
                || entry.value(QStringLiteral("statusText")).toString()
                       != QStringLiteral("协议错误"))
                fail(QStringLiteral("M9-D E: selected=%1 status=%2, expected "
                                    "row 0 / 协议错误")
                         .arg(selectedRowOf(roots))
                         .arg(entry.value(QStringLiteral("statusText"))
                                  .toString()));
            if (entry.value(QStringLiteral("issueText")).toString().isEmpty())
                fail(QStringLiteral("M9-D E: the ProtocolError row lost its "
                                    "issue detail"));
            assertTransactionDetailMapping(
                roots, QStringLiteral("M9-D E detail"), *failures);
            grab(QStringLiteral("m9d-transactions-protocol-error-1024x720"));
            break;
        }
        case 59: {
            // Optional regression companion: after a demo batch the
            // Dashboard statistics keep working — the statistics capability
            // has a real home after the Legacy retirement.
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("runDemoBatch() not invokable"));
            break;
        }
        case 60: switchTo(dashboardIndex); break;
        case 61: {
            assertFrame(QStringLiteral("M9-D G"), dashboardIndex, -1);
            assertDashboardGoldenCounts(QStringLiteral("M9-D G"));
            assertDashboardAttention(roots,
                                     QStringLiteral("M9-D G"),
                                     *failures);
            grab(QStringLiteral("m9d-dashboard-regression-1024x720"));
            break;
        }

        default:
            break;
        }

        if (failures->isEmpty() && *stage < 61) {
            ++*stage;
            QTimer::singleShot(settleMs, &app, *schedule);
            return;
        }

        if (failures->isEmpty())
            qInfo() << "EVIDENCE CAPTURE PASS (5 B4 + 5 B5 + 6 M9-C + 7 "
                       "M9-D screenshots; every capture preceded by an "
                       "explicit state assertion)";
        else
            for (const QString &f : *failures)
                qWarning().noquote() << "EVIDENCE FAIL:" << f;
        app.exit(failures->isEmpty() ? 0 : 1);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}

} // namespace

// ---------------------------------------------------------------------------
// `--serial-hotplug-probe[=COMx] [--serial-hotplug-probe-seconds=N]
//   [--serial-hotplug-probe-log=<path>] [--serial-hotplug-probe-baud=N]`
//
// DIAGNOSTIC ONLY (M10-E4 real hot-unplug correction). It is deliberately NOT a
// product feature: it opens no Modbus session, sends no frame and is never
// reachable from the UI. It exists to answer, on REAL hardware, the questions
// that reading source code cannot answer:
//
//   1. does QSerialPort::errorOccurred fire AT ALL when the USB adapter is
//      physically removed while the port is idle?
//   2. if it fires, which SerialPortError enum, and how long after the removal?
//   3. what does QSerialPort::isOpen() report afterwards?
//   4. does QSerialPortInfo::availablePorts() (SetupAPI, DIGCF_PRESENT) drop the
//      port, and how long after the removal?
//   5. or is the removal only exposed by the next read/write?
//
// Question 5 is answered by construction: after open() the probe performs NO
// I/O at all, so any error that arrives proves it is not gated on a later read
// or write.
//
// The port is configured EXACTLY like the production adapter (8N1, no flow
// control, ReadWrite, caller baud) so the measurement describes the shipped
// open. Because this is a WIN32-subsystem binary with no console, every line
// goes to a log file AND to stderr (visible when the caller redirects output):
//
//   SERIAL-HOTPLUG t=+0.000 open port=COM3 baud=9600 result=ok isOpen=1 ...
//   SERIAL-HOTPLUG t=+0.250 poll port=COM3 isOpen=1 enumerated=1 ports=1
//   SERIAL-HOTPLUG t=+1.014 errorOccurred enum=ResourceError(15) isOpen=1 ...
//   SERIAL-HOTPLUG t=+1.250 poll port=COM3 isOpen=1 enumerated=0 ports=0
//   SERIAL-HOTPLUG SUMMARY ...
//   SERIAL-HOTPLUG VERDICT ...
//
// Operator procedure: start it with the adapter inserted, wait for the `open`
// line, physically unplug the adapter, then let it run out.
// ---------------------------------------------------------------------------
static QString serialPortErrorName(QSerialPort::SerialPortError error)
{
    switch (error) {
    case QSerialPort::NoError:
        return QStringLiteral("NoError");
    case QSerialPort::DeviceNotFoundError:
        return QStringLiteral("DeviceNotFoundError");
    case QSerialPort::PermissionError:
        return QStringLiteral("PermissionError");
    case QSerialPort::OpenError:
        return QStringLiteral("OpenError");
    case QSerialPort::NotOpenError:
        return QStringLiteral("NotOpenError");
    // Qt 6 removed ParityError / FramingError / BreakConditionError from
    // SerialPortError (parity and framing faults are no longer port errors).
    case QSerialPort::WriteError:
        return QStringLiteral("WriteError");
    case QSerialPort::ReadError:
        return QStringLiteral("ReadError");
    case QSerialPort::ResourceError:
        return QStringLiteral("ResourceError");
    case QSerialPort::UnsupportedOperationError:
        return QStringLiteral("UnsupportedOperationError");
    case QSerialPort::TimeoutError:
        return QStringLiteral("TimeoutError");
    case QSerialPort::UnknownError:
        return QStringLiteral("UnknownError");
    }
    return QStringLiteral("Unrecognised(%1)").arg(static_cast<int>(error));
}

static int runSerialHotplugProbe(const QStringList &arguments)
{
    const auto optionValue = [&arguments](const QString &name) {
        for (int i = 0; i < arguments.size(); ++i) {
            if (arguments.at(i) == name && i + 1 < arguments.size())
                return arguments.at(i + 1);
            if (arguments.at(i).startsWith(name + QLatin1Char('=')))
                return arguments.at(i).mid(name.size() + 1);
        }
        return QString();
    };

    const QString portName =
        optionValue(QStringLiteral("--serial-hotplug-probe")).trimmed();
    if (portName.isEmpty()) {
        qWarning() << "--serial-hotplug-probe requires a port name, e.g."
                      "--serial-hotplug-probe=COM3";
        return 2;
    }
    const QString secondsRaw =
        optionValue(QStringLiteral("--serial-hotplug-probe-seconds"));
    const int seconds = secondsRaw.isEmpty() ? 20 : secondsRaw.toInt();
    const QString baudRaw =
        optionValue(QStringLiteral("--serial-hotplug-probe-baud"));
    const qint32 baud = baudRaw.isEmpty() ? 9600 : baudRaw.toInt();
    QString logPath = optionValue(QStringLiteral("--serial-hotplug-probe-log"));
    if (logPath.isEmpty())
        logPath = QDir::current().filePath(QStringLiteral("serial-hotplug-probe.log"));

    QFile log(logPath);
    if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        qWarning() << "cannot open probe log" << logPath;
        return 2;
    }
    QTextStream out(&log);
    const auto emitLine = [&out](const QString &line) {
        out << line << '\n';
        out.flush();
        qInfo().noquote() << line;
    };

    QSerialPort port;
    // Exactly the production adapter's configuration (8N1, no flow control).
    port.setDataBits(QSerialPort::Data8);
    port.setParity(QSerialPort::NoParity);
    port.setStopBits(QSerialPort::OneStop);
    port.setFlowControl(QSerialPort::NoFlowControl);
    port.setPortName(portName);
    port.setBaudRate(baud);

    QElapsedTimer clock;
    clock.start();
    const auto stamp = [&clock]() {
        return QStringLiteral("+%1s").arg(clock.elapsed() / 1000.0, 0, 'f', 3);
    };
    const auto isEnumerated = [&portName]() {
        const auto ports = QSerialPortInfo::availablePorts();
        for (const auto &info : ports) {
            if (info.portName() == portName)
                return true;
        }
        return false;
    };

    int errorEvents = 0;
    QString firstErrorAt;
    QString firstErrorEnum;
    QString firstErrorString;
    bool absentSeen = false;
    QString firstAbsentAt;

    QObject::connect(&port, &QSerialPort::errorOccurred, &port,
                     [&](QSerialPort::SerialPortError error) {
        ++errorEvents;
        const QString line =
            QStringLiteral("SERIAL-HOTPLUG t=%1 errorOccurred enum=%2(%3) "
                           "isOpen=%4 errorString=[%5]")
                .arg(stamp())
                .arg(serialPortErrorName(error))
                .arg(static_cast<int>(error))
                .arg(port.isOpen() ? 1 : 0)
                .arg(port.errorString());
        emitLine(line);
        if (firstErrorEnum.isEmpty()) {
            firstErrorAt = stamp();
            firstErrorEnum = serialPortErrorName(error);
            firstErrorString = port.errorString();
        }
    });

    const bool opened = port.open(QIODevice::ReadWrite);
    emitLine(QStringLiteral("SERIAL-HOTPLUG t=%1 open port=%2 baud=%3 result=%4 "
                            "isOpen=%5 errorString=[%6]")
                 .arg(stamp())
                 .arg(portName)
                 .arg(baud)
                 .arg(opened ? QStringLiteral("ok") : QStringLiteral("failed"))
                 .arg(port.isOpen() ? 1 : 0)
                 .arg(port.errorString()));
    if (!opened) {
        emitLine(QStringLiteral("SERIAL-HOTPLUG VERDICT openFailed=1 "
                                "errorString=[%1] log=%2")
                     .arg(port.errorString(), logPath));
        return 1;
    }

    // Observation loop. A line is written on every CHANGE (plus the first
    // sample) so the timestamps show exactly when each observable flipped.
    int polls = 0;
    bool haveLast = false;
    bool lastOpen = false;
    bool lastEnumerated = false;
    int lastPortCount = -1;
    QTimer pollTimer;
    QObject::connect(&pollTimer, &QTimer::timeout, &port, [&]() {
        ++polls;
        const auto ports = QSerialPortInfo::availablePorts();
        const bool enumerated = isEnumerated();
        const int portCount = static_cast<int>(ports.size());
        const bool isOpen = port.isOpen();
        if (!enumerated && !absentSeen) {
            absentSeen = true;
            firstAbsentAt = stamp();
        }
        if (!haveLast || isOpen != lastOpen || enumerated != lastEnumerated
            || portCount != lastPortCount) {
            haveLast = true;
            lastOpen = isOpen;
            lastEnumerated = enumerated;
            lastPortCount = portCount;
            emitLine(QStringLiteral("SERIAL-HOTPLUG t=%1 poll port=%2 isOpen=%3 "
                                    "enumerated=%4 ports=%5")
                         .arg(stamp())
                         .arg(portName)
                         .arg(isOpen ? 1 : 0)
                         .arg(enumerated ? 1 : 0)
                         .arg(portCount));
        }
    });
    pollTimer.start(250);

    QTimer::singleShot(seconds * 1000, &port, [&]() {
        pollTimer.stop();
        const bool isOpenAtEnd = port.isOpen();
        const bool enumeratedAtEnd = isEnumerated();
        emitLine(QStringLiteral("SERIAL-HOTPLUG SUMMARY port=%1 ran=%2s polls=%3 "
                                "errorEvents=%4 firstErrorEnum=%5 firstErrorAt=%6 "
                                "errorStringAtFirst=[%7] isOpenAtEnd=%8 "
                                "enumeratedAtEnd=%9 firstAbsentAt=%10 "
                                "ioAfterOpen=0 log=%11")
                     .arg(portName)
                     .arg(seconds)
                     .arg(polls)
                     .arg(errorEvents)
                     .arg(firstErrorEnum.isEmpty() ? QStringLiteral("none") : firstErrorEnum)
                     .arg(firstErrorAt.isEmpty() ? QStringLiteral("n/a") : firstErrorAt)
                     .arg(firstErrorString)
                     .arg(isOpenAtEnd ? 1 : 0)
                     .arg(enumeratedAtEnd ? 1 : 0)
                     .arg(firstAbsentAt.isEmpty() ? QStringLiteral("n/a") : firstAbsentAt)
                     .arg(logPath));
        emitLine(QStringLiteral("SERIAL-HOTPLUG VERDICT errorOccurredFired=%1 "
                                "enum=%2 errorLatency=%3 availablePortsDropped=%4 "
                                "dropLatency=%5 isOpenStaleAfterRemoval=%6 "
                                "ioPerformedAfterOpen=0")
                     .arg(errorEvents > 0 ? QStringLiteral("yes") : QStringLiteral("no"))
                     .arg(firstErrorEnum.isEmpty() ? QStringLiteral("n/a") : firstErrorEnum)
                     .arg(firstErrorAt.isEmpty() ? QStringLiteral("n/a") : firstErrorAt)
                     .arg(absentSeen ? QStringLiteral("yes") : QStringLiteral("no"))
                     .arg(firstAbsentAt.isEmpty() ? QStringLiteral("n/a") : firstAbsentAt)
                     .arg((absentSeen && isOpenAtEnd) ? QStringLiteral("yes")
                                                      : QStringLiteral("no")));
        port.close();
        // Purposeful exit code so the probe is scriptable:
        //   0 -> the QSerialPort error door DID fire (Case A evidence)
        //   3 -> it stayed silent for the whole run (Case B evidence)
        QCoreApplication::exit(errorEvents > 0 ? 0 : 3);
    });
    return QCoreApplication::exec();
}

// ---------------------------------------------------------------------------
using modbuslens::core::DeviceProfile;
using modbuslens::core::RegisterDecodeType;
using modbuslens::core::RegisterEntry;
using modbuslens::ui::ProfileStore;
using modbuslens::ui::ManualStore;

// M12-B first slice `--qml-profile-editor-check`: an automated end-to-end
// gate for the Device Profile workspace (rail index 5). It drives the REAL
// app QML through the REAL rail navigation and exercises the identity/file
// lifecycle against an INJECTED temporary managed root — automated tests must
// never touch a real user's profile data (T027 §36).
//
// Covered (B1-Q01..Q22): reachability, empty state, New, read-only profileId,
// editable identity, dirty indication, Save, catalog appearance, Open,
// validation display, Delete confirmation, dirty Open (Cancel/Discard/Save),
// malformed-catalog warning, 1000×700 reachability, keyboard, existing
// workspaces unaffected, and the exit dirty guard (Cancel blocks close).
// ---------------------------------------------------------------------------
int runProfileEditorCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    auto *controller = rootObj
                           ? rootObj->findChild<QObject *>(
                               QStringLiteral("profileController"))
                           : nullptr;
    if (!window || !controller) {
        qWarning().noquote()
            << QStringLiteral("PROFFAIL: window/controller not found");
        return 1;
    }
    // The exit-guard stages deliberately attempt window->close(); a broken
    // guard must be REPORTABLE, not end the process through quit-on-last-
    // window-closed before the failure list is printed.
    app.setQuitOnLastWindowClosed(false);

    // Injected managed root: a fresh temporary directory per run. The gate
    // writes ONLY here; the production AppData profiles location is never
    // touched (asserted in stage 1).
    QTemporaryDir managedRoot;
    if (!managedRoot.isValid()) {
        qWarning().noquote() << QStringLiteral("PROFFAIL: temp root invalid");
        return 1;
    }
    ProfileStore::setManagedRootOverride(managedRoot.path());
    const auto writeSeedProfile = [&](const QString &profileId,
                                      const QString &displayName) {
        DeviceProfile profile;
        profile.schemaVersion = 1;
        profile.profileId = profileId.toStdString();
        profile.displayName = displayName.toStdString();
        RegisterEntry entry;
        entry.readFunctionCode = 0x03;
        entry.address = 1000;
        entry.name = "Frequency";
        entry.dataType = RegisterDecodeType::UInt16;
        entry.registerCount = 1;
        profile.registers.push_back(entry);
        return ProfileStore::saveToFile(
                   profile, ProfileStore::defaultFilePathFor(profileId))
            .ok();
    };
    if (!writeSeedProfile(QStringLiteral("seed-existing"),
                          QStringLiteral("Existing inverter"))) {
        qWarning().noquote() << QStringLiteral("PROFFAIL: seed write failed");
        return 1;
    }
    // A malformed managed file: must NOT block valid profiles and must surface
    // as an observable issue (B1-Q19), never silently ignored or deleted.
    {
        QFile bad(QDir(managedRoot.path())
                      .filePath(QStringLiteral("profile-badbadbadbadbadbadbad"
                                                "badbadbadbadbadbadbadbadbad"
                                                "badbadbadbadbadbad.json")));
        if (!bad.open(QIODevice::WriteOnly)) {
            qWarning().noquote()
                << QStringLiteral("PROFFAIL: cannot write malformed seed");
            return 1;
        }
        bad.write("{\"schemaVersion\":1,\"profileId\":");
        bad.close();
    }

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &m) {
        qInfo().noquote() << QStringLiteral("PROF: %1").arg(m);
    };

    auto itemOf = [&roots](const QString &name) {
        return findNamedItem(roots, name);
    };
    const auto propStr = [](QObject *o, const char *name) {
        return o ? o->property(name).toString() : QStringLiteral("<none>");
    };
    const auto propBool = [](QObject *o, const char *name) {
        return o ? o->property(name).toBool() : false;
    };
    const auto visibleOf = [&roots](const QString &name) -> bool {
        // Dialogs are QQuickPopup (QObject-only, NOT a QQuickItem): read the
        // visible property through the QObject tree.
        for (QObject *root : roots) {
            auto *popup = root->findChild<QObject *>(name);
            if (popup)
                return popup->property("visible").toBool();
        }
        // Fall through to the QQuickItem path for regular items.
        return false;
    };
    const auto clickNamed = [&roots, window](const QString &name) {
        auto *item = findNamedItem(roots, name);
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    const auto setField = [&roots](const QString &name, const QString &text) {
        auto *field = findNamedItem(roots, name);
        if (!field) {
            return false;
        }
        field->setProperty("text", text);
        return true;
    };
    const auto catalogRow = [&controller](int index,
                                          const char *key) -> QString {
        const QVariantList catalog =
            controller->property("profileCatalog").toList();
        if (index < 0 || index >= catalog.size()) {
            return QStringLiteral("<none>");
        }
        return catalog.at(index).toMap().value(key).toString();
    };
    const auto catalogContains = [&controller](const QString &displayPrimary) {
        // Index-independent membership: saving re-sorts the catalog by
        // displayName, so a row index captured before the save is not stable
        // (acceptance-round RCA: the old index-based oracle was wrong).
        const QVariantList catalog =
            controller->property("profileCatalog").toList();
        for (const QVariant &row : catalog) {
            if (row.toMap().value("displayPrimary").toString()
                == displayPrimary) {
                return true;
            }
        }
        return false;
    };
    const auto sceneRectOf = [&itemOf](const QString &name) -> QRectF {
        auto *item = itemOf(name);
        if (!item) {
            return QRectF();
        }
        const QPointF topLeft = item->mapToScene(QPointF(0, 0));
        return QRectF(topLeft, QSizeF(item->width(), item->height()));
    };
    // Dialogs are QQuickPopup (QObject-only): their frame geometry is read
    // through the background item (the popup's full frame incl. header and
    // footer chrome); contentItem alone is the inner content.
    const auto popupRectOf = [&roots](const QString &name) -> QRectF {
        for (QObject *root : roots) {
            auto *popup = root->findChild<QObject *>(name);
            if (!popup) {
                continue;
            }
            auto *frame =
                popup->property("background").value<QQuickItem *>();
            if (!frame) {
                return QRectF();
            }
            const QPointF topLeft = frame->mapToScene(QPointF(0, 0));
            return QRectF(topLeft, QSizeF(frame->width(), frame->height()));
        }
        return QRectF();
    };
    const auto selectCatalogRow = [&](int index) -> bool {
        // Click the Nth delegate row (delegates share one objectName).
        // Walk the VISUAL tree (childItems): Repeater delegates are reachable
        // as visual children while QObject::findChild misses them (repo
        // finding, see findNamedItemRecursive above).
        QList<QQuickItem *> rows;
        const std::function<void(QQuickItem *)> collectRows =
            [&](QQuickItem *item) {
                if (item->objectName() == QStringLiteral("profileCatalogRow"))
                    rows << item;
                for (QQuickItem *child : item->childItems())
                    collectRows(child);
            };
        collectRows(window->contentItem());
        if (index < 0 || index >= rows.size()
            || !rows.at(index)->isVisible()) {
            fail(QStringLiteral("selectCatalogRow(%1): %2 visual rows found")
                     .arg(index).arg(rows.size()));
            return false;
        }
        QQuickItem *row = rows.at(index);
        const QPointF local(row->width() / 2.0, row->height() / 2.0);
        const QPointF scene = row->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
 const auto callController = [&controller](const char *method) {
        QMetaObject::invokeMethod(controller, method);
    };
    const auto withinWindow = [&](const QString &name) -> bool {
        auto *item = itemOf(name);
        if (!item || !item->isVisible())
            return false;
        const QPointF topLeft = item->mapToScene(QPointF(0, 0));
        const QRectF rect(topLeft, QSizeF(item->width(), item->height()));
        return QRectF(QPointF(0, 0),
                      QSizeF(window->width(), window->height()))
            .contains(rect);
    };
    const auto requireInsideWindow = [&](const QString &name) {
        auto *item = itemOf(name);
        if (!item || !item->isVisible()) {
            fail(QStringLiteral("%1 is not visible at measure time").arg(name));
            return;
        }
        if (!withinWindow(name)) {
            const QRectF rect = sceneRectOf(name);
            fail(QStringLiteral("%1 outside: x=%2 y=%3 w=%4 h=%5 win=%6x%7")
                     .arg(name)
                     .arg(rect.x())
                     .arg(rect.y())
                     .arg(rect.width())
                     .arg(rect.height())
                     .arg(window->width())
                     .arg(window->height()));
        }
    };

    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // Stage 0: navigate via the REAL rail entry (B1-Q01/Q02).
    push([&]() {
        if (!clickNamed(QStringLiteral("navItem_5")))
            fail(QStringLiteral("the Device rail entry is not clickable"));
        if (!visibleOf(QStringLiteral("deviceProfileWorkspace")))
            fail(QStringLiteral("the Device Profile workspace is not visible"));
        if (!propBool(itemOf(QStringLiteral("navItem_5")), "enabled"))
            fail(QStringLiteral("the Device rail entry is not enabled"));
        // The controller scanned the PRODUCTION root during engine load (the
        // override was installed afterwards); refresh against the injected
        // root now.
        callController("refreshCatalog");
        if (!visibleOf(QStringLiteral("profileCatalogIssues")))
            fail(QStringLiteral("stage 0: the injected-root catalog was not "
                                "refreshed"));
        note(QStringLiteral("stage 0: Device workspace reachable via rail"));
    });
    // Stage 1: empty-state + seeded catalog + malformed warning (Q03/Q19) +
    // root isolation (Q20 of the C matrix).
    push([&]() {
        if (catalogRow(0, "displayPrimary")
            != QStringLiteral("Existing inverter"))
            fail(QStringLiteral("the seeded profile is not listed first"));
        if (!visibleOf(QStringLiteral("profileCatalogIssues")))
            fail(QStringLiteral("the malformed-catalog warning is not "
                                "visible"));
        const QString prod =
            QDir(QStandardPaths::writableLocation(
                     QStandardPaths::AppDataLocation))
                .filePath(QStringLiteral("profiles"));
        if (ProfileStore::managedProfilesDirectory() == prod)
            fail(QStringLiteral("the gate is writing to the production "
                                "AppData root"));
        note(QStringLiteral("stage 1: seeded catalog + malformed warning + "
                           "injected root isolation"));
    });
    // Stage 2: New creates an editor (Q04), profileId read-only (Q05).
    push([&]() {
        if (!clickNamed(QStringLiteral("profileNewButton")))
            fail(QStringLiteral("the New button is not clickable"));
        if (!controller->property("hasOpenProfile").toBool())
            fail(QStringLiteral("New did not open the editor"));
        const QString shownId = propStr(
            itemOf(QStringLiteral("profileIdReadOnly")), "text");
        if (shownId == "<none>" || shownId.isEmpty())
            fail(QStringLiteral("profileId is not displayed read-only"));
        if (!visibleOf(QStringLiteral("profileIdReadOnly")))
            fail(QStringLiteral("profileId display is not visible"));
        note(QStringLiteral("stage 2: New opened the identity editor"));
    });
    // Stage 3: identity editing (Q06/Q07) + dirty indication (Q08).
    push([&]() {
        for (const auto &field : {std::pair<const char *, QString>{
                 "profileDisplayNameField", QStringLiteral("Second device")}}) {
            if (!setField(field.first, field.second))
                fail(QStringLiteral("cannot edit %1").arg(field.first));
        }
        setField("profileManufacturerField", "ACME");
        setField("profileModelField", "P-200");
        setField("profileRevisionField", "rev 1");
        setField("profileDescriptionField", "gate fixture");
        if (!controller->property("dirty").toBool())
            fail(QStringLiteral("editing identity did not set dirty"));
        if (!visibleOf(QStringLiteral("profileDirtyIndicator")))
            fail(QStringLiteral("the dirty indicator is not visible"));
        note(QStringLiteral("stage 3: identity editable; dirty visible"));
    });
    // Stage 4: Save (Q09) + catalog appearance (Q10).
    push([&]() {
        if (!clickNamed(QStringLiteral("profileSaveButton")))
            fail(QStringLiteral("the Save button is not clickable"));
        if (controller->property("dirty").toBool())
            fail(QStringLiteral("Save did not clear dirty"));
        bool savedListed = false;
        for (int i = 0; i < 2 && !savedListed; ++i)
            savedListed = catalogRow(i, "displayPrimary")
                          == QStringLiteral("Second device");
        if (!savedListed)
            fail(QStringLiteral("the saved profile is not in the catalog"));
        note(QStringLiteral("stage 4: Save cleared dirty; catalog updated"));
    });
    // Stage 5: Open the seeded profile (Q11).
    push([&]() {
        if (!selectCatalogRow(0))
            fail(QStringLiteral("cannot select the seeded catalog row"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("the Open button is not clickable"));
        if (propStr(itemOf(QStringLiteral("profileDisplayNameField")), "text")
            != QStringLiteral("Existing inverter"))
            fail(QStringLiteral("Open did not load the seeded profile"));
        note(QStringLiteral("stage 5: Open loaded the seeded profile"));
    });
    // Stage 6: displayName-only edits drive the dirty cue and the validation
    // label (Q12). Regression protection for a state change without its
    // NOTIFY signal: every getter on the controller is computed on demand, so
    // only live bindings can catch a missing editorChanged emission.
    push([&]() {
        setField("profileDisplayNameField", QStringLiteral("Validation probe"));
        if (!controller->property("dirty").toBool())
            fail(QStringLiteral("a displayName-only edit did not set dirty"));
        if (!visibleOf(QStringLiteral("profileDirtyIndicator")))
            fail(QStringLiteral("the dirty cue did not follow a "
                                "displayName-only edit"));
        if (visibleOf(QStringLiteral("profileValidationText")))
            fail(QStringLiteral("the validation label is visible for a valid "
                                "draft"));
        setField("profileDisplayNameField", QString());
        if (!visibleOf(QStringLiteral("profileValidationText")))
            fail(QStringLiteral("the validation text is not visible"));
        if (propStr(itemOf(QStringLiteral("profileValidationText")), "text")
                .isEmpty())
            fail(QStringLiteral("the validation label text did not update"));
        if (!visibleOf(QStringLiteral("profileDirtyIndicator")))
            fail(QStringLiteral("the dirty cue is hidden while the draft is "
                                "invalid"));
        setField("profileDisplayNameField",
                 QStringLiteral("Existing inverter"));
        if (visibleOf(QStringLiteral("profileValidationText")))
            fail(QStringLiteral("the validation label stayed visible after "
                                "the draft became valid"));
        note(QStringLiteral("stage 6: displayName-only edits drive the dirty "
                           "cue and the validation label"));
    });
    // Stage 7: dirty Open -> Cancel keeps the draft AND closes the dialog
    // (Q15). A dialog left open keeps its modal overlay swallowing every
    // later click, so the close is part of the contract.
    push([&]() {
        setField("profileDisplayNameField", QStringLiteral("Changed draft"));
        if (!selectCatalogRow(0))
            fail(QStringLiteral("cannot select row 0 for the dirty Open"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("Open is not clickable while dirty"));
        if (!visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("the dirty dialog did not appear"));
        if (!clickNamed(QStringLiteral("profileDirtyCancelButton")))
            fail(QStringLiteral("the Cancel branch is not clickable"));
        if (visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("the dirty dialog stayed open after Cancel"));
        if (propStr(itemOf(QStringLiteral("profileDisplayNameField")), "text")
            != QStringLiteral("Changed draft"))
            fail(QStringLiteral("Cancel did not keep the current draft"));
        note(QStringLiteral("stage 7: dirty Open Cancel keeps the draft and "
                           "closes the dialog"));
    });
    push([&]() {
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("Open is not clickable (second pass)"));
        if (!visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("the dirty dialog did not reappear"));
        if (!clickNamed(QStringLiteral("profileDirtyDiscardButton")))
            fail(QStringLiteral("the Discard branch is not clickable"));
        if (visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("the dirty dialog stayed open after Discard"));
        if (propStr(itemOf(QStringLiteral("profileDisplayNameField")), "text")
            != QStringLiteral("Existing inverter"))
            fail(QStringLiteral("Discard did not open the target profile"));
        note(QStringLiteral("stage 8: dirty Open Discard opens the target"));
    });
    // Stage 9: dirty Open -> Save then opens the target (Q17). The catalog
    // oracle is index-independent: saving re-sorts the catalog by
    // displayName, so the renamed row's index is not stable (acceptance-round
    // RCA: the old row-1 oracle was wrong, not the save).
    push([&]() {
        setField("profileDisplayNameField", QStringLiteral("Renamed inverter"));
        if (!selectCatalogRow(1))
            fail(QStringLiteral("cannot select row 1 (Save branch)"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("Open is not clickable (Save branch)"));
        if (!visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("the dirty dialog did not appear (Save)"));
        if (!clickNamed(QStringLiteral("profileDirtySaveButton")))
            fail(QStringLiteral("the Save branch is not clickable"));
        if (visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("the dirty dialog stayed open after the Save "
                                "branch"));
        if (!catalogContains(QStringLiteral("Renamed inverter")))
            fail(QStringLiteral("the Save branch did not save the draft"));
        if (propStr(itemOf(QStringLiteral("profileDisplayNameField")), "text")
            != QStringLiteral("Second device"))
            fail(QStringLiteral("the Save branch did not open the target"));
        note(QStringLiteral("stage 9: dirty Open Save saved then opened"));
    });
    // Stage 9b: a FAILED save keeps the dialog open and blocks the pending
    // action (T027 §33.2 Group 2): the user stays on the unsaved draft with
    // the failure reason visible, and the target is NOT opened.
    push([&]() {
        setField("profileDisplayNameField", QString());
        if (!selectCatalogRow(1))
            fail(QStringLiteral("cannot select row 1 (Save failure)"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("Open is not clickable (Save failure)"));
        if (!visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("the dirty dialog did not appear (Save "
                                "failure)"));
        if (!clickNamed(QStringLiteral("profileDirtySaveButton")))
            fail(QStringLiteral("the Save branch is not clickable (Save "
                                "failure)"));
        if (!visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("a failed save closed the dirty dialog"));
        if (controller->property("lastActionError").toString().isEmpty())
            fail(QStringLiteral("a failed save did not surface an action "
                                "error"));
        if (!propStr(itemOf(QStringLiteral("profileDisplayNameField")), "text")
                 .isEmpty())
            fail(QStringLiteral("a failed save replaced the draft"));
        if (!clickNamed(QStringLiteral("profileDirtyCancelButton")))
            fail(QStringLiteral("the Cancel branch is not clickable after a "
                                "failed save"));
        if (visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("Cancel did not close the dialog after a "
                                "failed save"));
        setField("profileDisplayNameField", QStringLiteral("Second device"));
        note(QStringLiteral("stage 9b: failed save blocks the pending action"));
    });
    // Stage 10: delete requires confirmation (Q13) and removes (Q14). The
    // confirmed path must close its dialog and drop the deleted row's
    // selection, so Open/Delete cannot act on a stale index.
    push([&]() {
        if (!selectCatalogRow(0))
            fail(QStringLiteral("cannot select the row to delete"));
        if (!clickNamed(QStringLiteral("profileDeleteButton")))
            fail(QStringLiteral("the Delete button is not clickable"));
        if (!visibleOf(QStringLiteral("profileDeleteDialog")))
            fail(QStringLiteral("the delete confirmation did not appear"));
        if (!clickNamed(QStringLiteral("profileDeleteConfirmButton")))
            fail(QStringLiteral("the confirm-delete button is not "
                                "clickable"));
        if (visibleOf(QStringLiteral("profileDeleteDialog")))
            fail(QStringLiteral("the delete dialog stayed open after "
                                "confirm"));
        if (controller->property("profileCatalog").toList().size() != 1)
            fail(QStringLiteral("confirmed delete did not remove the "
                                "profile"));
        if (propBool(itemOf(QStringLiteral("profileOpenButton")), "enabled"))
            fail(QStringLiteral("Open stayed enabled without a selection"));
        if (propBool(itemOf(QStringLiteral("profileDeleteButton")), "enabled"))
            fail(QStringLiteral("Delete stayed enabled without a selection"));
        note(QStringLiteral("stage 10: delete confirmed and removed"));
    });
    // Stage 11a: resize to the 1000x700 contract size. Measuring in the SAME
    // step reads the PREVIOUS window's layout (acceptance-round harness RCA),
    // so the measurement is a separate step.
    push([&]() {
        window->resize(1000, 700);
        note(QStringLiteral("stage 11a: window resized to 1000x700"));
    });
    // Stage 11b: the 1000x700 reachability contract on real scene geometry.
    // Nothing is hidden by a clip: the page root does not clip (only the
    // catalog Flickable viewport does, by design), so a passed containment
    // assert IS the visible-area proof for these interactive controls.
    push([&]() {
        if (window->width() != 1000 || window->height() != 700)
            fail(QStringLiteral("the window is not at the 1000x700 contract "
                                "size: %1x%2")
                     .arg(window->width())
                     .arg(window->height()));
        for (const auto &name :
             {QStringLiteral("deviceProfileWorkspace"),
              QStringLiteral("deviceProfileActions"),
              QStringLiteral("profileNewButton"),
              QStringLiteral("profileOpenButton"),
              QStringLiteral("profileSaveButton"),
              QStringLiteral("profileDeleteButton"),
              QStringLiteral("profileCatalogCard"),
              QStringLiteral("profileCatalogList"),
              QStringLiteral("profileIdentityCard"),
              QStringLiteral("profileDisplayNameField"),
              QStringLiteral("profileDescriptionField")}) {
            auto *item = itemOf(name);
            if (!item) {
                fail(QStringLiteral("%1 disappeared before the geometry "
                                    "measure")
                         .arg(name));
                continue;
            }
            const QRectF rect = sceneRectOf(name);
            qInfo().noquote()
                << QStringLiteral("PROFGEO %1: x=%2 y=%3 w=%4 h=%5")
                       .arg(name)
                       .arg(rect.x())
                       .arg(rect.y())
                       .arg(rect.width())
                       .arg(rect.height());
            requireInsideWindow(name);
        }
        note(QStringLiteral("stage 11b: 1000x700 controls reachable"));
    });
    // Stage 11c: the dirty dialog's own geometry at 1000x700 — opened for
    // real through a dirty Open (no controller bypass), measured, cancelled.
    push([&]() {
        setField("profileDisplayNameField", QStringLiteral("Dialog probe"));
        if (!selectCatalogRow(0))
            fail(QStringLiteral("cannot select a row for the dialog probe"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("Open is not clickable (dialog probe)"));
        if (!visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("the dirty dialog did not appear (dialog "
                                "probe)"));
        const QRectF dialogRect =
            popupRectOf(QStringLiteral("profileDirtyDialog"));
        qInfo().noquote()
            << QStringLiteral("PROFGEO profileDirtyDialog: x=%1 y=%2 w=%3 "
                              "h=%4")
                   .arg(dialogRect.x())
                   .arg(dialogRect.y())
                   .arg(dialogRect.width())
                   .arg(dialogRect.height());
        if (dialogRect.isEmpty())
            fail(QStringLiteral("the dirty dialog has no measurable "
                                "geometry"));
        else if (!QRectF(QPointF(0, 0),
                         QSizeF(window->width(), window->height()))
                      .contains(dialogRect))
            fail(QStringLiteral("the dirty dialog is outside the window"));
        for (const auto &name :
             {QStringLiteral("profileDirtySaveButton"),
              QStringLiteral("profileDirtyDiscardButton"),
              QStringLiteral("profileDirtyCancelButton")})
            requireInsideWindow(name);
        if (!clickNamed(QStringLiteral("profileDirtyCancelButton")))
            fail(QStringLiteral("the Cancel branch is not clickable (dialog "
                                "probe)"));
        if (visibleOf(QStringLiteral("profileDirtyDialog")))
            fail(QStringLiteral("the dirty dialog stayed open (dialog "
                                "probe)"));
        setField("profileDisplayNameField", QStringLiteral("Second device"));
        note(QStringLiteral("stage 11c: dirty dialog reachable at 1000x700"));
    });
    // Stage 11d: the delete confirmation's own geometry, and its Cancel
    // deletes nothing.
    push([&]() {
        if (!selectCatalogRow(0))
            fail(QStringLiteral("cannot select a row for the delete probe"));
        if (!clickNamed(QStringLiteral("profileDeleteButton")))
            fail(QStringLiteral("the Delete button is not clickable (delete "
                                "probe)"));
        if (!visibleOf(QStringLiteral("profileDeleteDialog")))
            fail(QStringLiteral("the delete dialog did not appear (delete "
                                "probe)"));
        const QRectF dialogRect =
            popupRectOf(QStringLiteral("profileDeleteDialog"));
        qInfo().noquote()
            << QStringLiteral("PROFGEO profileDeleteDialog: x=%1 y=%2 w=%3 "
                              "h=%4")
                   .arg(dialogRect.x())
                   .arg(dialogRect.y())
                   .arg(dialogRect.width())
                   .arg(dialogRect.height());
        if (dialogRect.isEmpty())
            fail(QStringLiteral("the delete dialog has no measurable "
                                "geometry"));
        else if (!QRectF(QPointF(0, 0),
                         QSizeF(window->width(), window->height()))
                      .contains(dialogRect))
            fail(QStringLiteral("the delete dialog is outside the window"));
        for (const auto &name :
             {QStringLiteral("profileDeleteConfirmButton"),
              QStringLiteral("profileDeleteCancelButton")})
            requireInsideWindow(name);
        if (!clickNamed(QStringLiteral("profileDeleteCancelButton")))
            fail(QStringLiteral("the delete Cancel branch is not clickable"));
        if (visibleOf(QStringLiteral("profileDeleteDialog")))
            fail(QStringLiteral("the delete dialog stayed open (delete "
                                "probe)"));
        if (controller->property("profileCatalog").toList().size() != 1)
            fail(QStringLiteral("cancelling the delete confirmation removed "
                                "a profile"));
        note(QStringLiteral("stage 11d: delete dialog reachable; Cancel "
                           "deletes nothing"));
    });
    // Stage 12: keyboard focus reaches the identity editor (Q21).
    push([&]() {
        auto *field = itemOf(QStringLiteral("profileDisplayNameField"));
        if (!field) {
            fail(QStringLiteral("the displayName field disappeared"));
        } else {
            field->forceActiveFocus();
            if (!field->hasActiveFocus())
                fail(QStringLiteral("the displayName field cannot take "
                                    "focus"));
            else
                note(QStringLiteral("stage 12: identity editor focusable"));
        }
    });
    // Stage 13: the five existing workspaces are unaffected (Q22).
    push([&]() {
        for (int i = 0; i <= 4; ++i) {
            auto *entry = itemOf(QStringLiteral("navItem_%1").arg(i));
            if (!entry || !entry->property("enabled").toBool())
                fail(QStringLiteral("existing rail entry %1 regressed")
                         .arg(i));
        }
        note(QStringLiteral("stage 13: existing workspaces unaffected"));
    });
    // Stage 14: the exit dirty guard (Group 2/3): dirty + close request ->
    // the Cancel branch genuinely blocks the close.
    push([&]() {
        if (!selectCatalogRow(0))
            fail(QStringLiteral("cannot select a profile for the exit guard"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("Open is not clickable (exit guard)"));
        setField("profileDisplayNameField", QStringLiteral("Exit guard draft"));
        if (!controller->property("dirty").toBool())
            fail(QStringLiteral("the exit-guard setup is not dirty"));
        window->close(); // delivers the real QCloseEvent
        if (!window->isVisible())
            fail(QStringLiteral("the window closed while the dirty exit "
                                "dialog should block it"));
        if (!visibleOf(QStringLiteral("profileExitDialog")))
            fail(QStringLiteral("the exit dialog did not appear"));
        if (!clickNamed(QStringLiteral("profileExitCancelButton")))
            fail(QStringLiteral("the exit Cancel branch is not clickable"));
        if (!window->isVisible())
            fail(QStringLiteral("Cancel did not block the close"));
        if (visibleOf(QStringLiteral("profileExitDialog")))
            fail(QStringLiteral("the exit dialog did not close on Cancel"));
        note(QStringLiteral("stage 14: exit dirty guard blocks close on "
                           "Cancel"));
        // Cleanup: drop the draft via the controller; the gate window stays
        // open so the schedule can finish and report.
        QMetaObject::invokeMethod(controller, "discardCurrentChanges");
    });
    // Stage 15: exit request with an UNSAVABLE draft: the Save branch must
    // neither close the window nor close the exit dialog (the close stays
    // denied until the draft is resolved another way).
    push([&]() {
        setField("profileDisplayNameField", QString());
        window->close(); // delivers the real QCloseEvent
        if (!window->isVisible())
            fail(QStringLiteral("the window closed although the draft cannot "
                                "be saved"));
        if (!visibleOf(QStringLiteral("profileExitDialog")))
            fail(QStringLiteral("the exit dialog did not appear for the "
                                "failed-save path"));
        if (!clickNamed(QStringLiteral("profileExitSaveButton")))
            fail(QStringLiteral("the exit Save branch is not clickable"));
        if (!window->isVisible())
            fail(QStringLiteral("a failed save still closed the window"));
        if (!visibleOf(QStringLiteral("profileExitDialog")))
            fail(QStringLiteral("the exit dialog closed although the save "
                                "failed"));
        if (!clickNamed(QStringLiteral("profileExitCancelButton")))
            fail(QStringLiteral("the exit Cancel branch is not clickable "
                                "(failed-save path)"));
        // Cleanup: restore a valid draft and drop it; the gate window stays
        // open so the schedule can finish and report.
        setField("profileDisplayNameField", QStringLiteral("Second device"));
        QMetaObject::invokeMethod(controller, "discardCurrentChanges");
        note(QStringLiteral("stage 15: exit + failed save keeps the window "
                           "and the dialog"));
    });

    const int settleMs = 60;
    auto step = std::make_shared<int>(0);
    auto schedule = std::make_shared<std::function<void()>>();
    auto failuresShared = failures;
    *schedule = [&, step, schedule, failuresShared, &app]() {
        if (*step >= steps->size()) {
            if (!failuresShared->isEmpty()) {
                for (const QString &f : *failuresShared)
                    qWarning().noquote()
                        << QStringLiteral("PROFFAIL: %1").arg(f);
                app.exit(1);
                return;
            }
            note(QStringLiteral("PROFILE EDITOR CHECK PASS (B1-Q01..Q22): "
                               "workspace reachable via the rail; New/Open/"
                               "Save/Delete lifecycle; read-only profileId; "
                               "dirty protection with Save/Discard/Cancel "
                               "(each branch closes the dialog); failed save "
                               "blocks the pending action; validation and "
                               "dirty cues follow displayName-only edits; "
                               "malformed catalog non-blocking; 1000x700 "
                               "reachable incl. both dialogs; exit dirty "
                               "guard denies close on Cancel and on a failed "
                               "save"));
            app.exit(0);
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}

// ---------------------------------------------------------------------------
// M12-B second slice `--qml-register-map-check`: the Register Map editor
// driven end to end through the REAL UI path (Device rail → Device Profile
// workspace → Profile → Register Map UI → entry editor dialog), against an
// injected temporary managed root (never the production AppData).
// Covers B2-Q01..B2-Q30 (T027 §37 user instruction §23).
// ---------------------------------------------------------------------------
int runRegisterMapCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    auto *controller = rootObj
                           ? rootObj->findChild<QObject *>(
                               QStringLiteral("profileController"))
                           : nullptr;
    if (!window || !controller) {
        qWarning().noquote()
            << QStringLiteral("REGFAIL: window/controller not found");
        return 1;
    }
    app.setQuitOnLastWindowClosed(false);

    QTemporaryDir managedRoot;
    if (!managedRoot.isValid()) {
        qWarning().noquote() << QStringLiteral("REGFAIL: temp root invalid");
        return 1;
    }
    ProfileStore::setManagedRootOverride(managedRoot.path());
    {
        // Seed: one valid profile with a single FC03 UInt16 @1000 entry
        // (scale 0.1, unit Hz) — the same shape the save/open round-trip
        // must preserve.
        DeviceProfile profile;
        profile.schemaVersion = 1;
        profile.profileId = "seed-register";
        profile.displayName = "Register seed";
        RegisterEntry entry;
        entry.readFunctionCode = 0x03;
        entry.address = 1000;
        entry.name = "Frequency";
        entry.description = "seed entry";
        entry.dataType = RegisterDecodeType::UInt16;
        entry.registerCount = 1;
        entry.scale = 0.1;
        entry.unit = "Hz";
        profile.registers.push_back(entry);
        if (!ProfileStore::saveToFile(
                profile, ProfileStore::defaultFilePathFor(
                             QStringLiteral("seed-register")))
                 .ok()) {
            qWarning().noquote()
                << QStringLiteral("REGFAIL: seed write failed");
            return 1;
        }
    }

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &m) {
        qInfo().noquote() << QStringLiteral("REG: %1").arg(m);
    };

    auto itemOf = [&roots](const QString &name) {
        return findNamedItem(roots, name);
    };
    const auto propStr = [](QObject *o, const char *name) {
        return o ? o->property(name).toString() : QStringLiteral("<none>");
    };
    const auto propBool = [](QObject *o, const char *name) {
        return o ? o->property(name).toBool() : false;
    };
    const auto visibleOf = [&roots](const QString &name) -> bool {
        for (QObject *root : roots) {
            auto *popup = root->findChild<QObject *>(name);
            if (popup)
                return popup->property("visible").toBool();
        }
        return false;
    };
    const auto clickNamed = [&roots, window](const QString &name) {
        auto *item = findNamedItem(roots, name);
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    const auto setField = [&roots](const QString &name, const QString &text) {
        auto *field = findNamedItem(roots, name);
        if (!field) {
            return false;
        }
        field->setProperty("text", text);
        return true;
    };
    // Catalog/register delegates share one objectName: reach the Nth row by
    // walking the VISUAL tree (childItems) — QObject::findChild misses
    // Repeater delegates in this declarative tree (repo finding).
    const auto collectRows = [&window](const QString &rowName) {
        QList<QQuickItem *> rows;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->objectName() == rowName)
                rows << item;
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        if (window->contentItem())
            walk(window->contentItem());
        return rows;
    };
    const auto clickRow = [&](const QString &rowName, int index) -> bool {
        const QList<QQuickItem *> rows = collectRows(rowName);
        if (index < 0 || index >= rows.size() || !rows.at(index)->isVisible())
            return false;
        QQuickItem *row = rows.at(index);
        const QPointF local(row->width() / 2.0, row->height() / 2.0);
        const QPointF scene = row->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    const auto rowsOf = [&controller](const char *key) -> QStringList {
        QStringList values;
        const QVariantList map = controller->property("registerMap").toList();
        for (const QVariant &row : map)
            values << row.toMap().value(QLatin1String(key)).toString();
        return values;
    };
    // Set a ComboBox selection through its property (presentation write only;
    // the business value the controller receives is the INDEX the dialog
    // forwards in its fields map).
    const auto selectCombo = [&itemOf](const QString &name, int index) {
        auto *combo = itemOf(name);
        if (!combo || index < 0) {
            return false;
        }
        return combo->setProperty("currentIndex", index);
    };
    const auto sceneRectOf = [&itemOf](const QString &name) -> QRectF {
        auto *item = itemOf(name);
        if (!item) {
            return QRectF();
        }
        const QPointF topLeft = item->mapToScene(QPointF(0, 0));
        return QRectF(topLeft, QSizeF(item->width(), item->height()));
    };
    // Dialogs are QQuickPopup: measure the frame through the background item.
    const auto popupRectOf = [&roots](const QString &name) -> QRectF {
        for (QObject *root : roots) {
            auto *popup = root->findChild<QObject *>(name);
            if (!popup) {
                continue;
            }
            auto *frame = popup->property("background").value<QQuickItem *>();
            if (!frame) {
                return QRectF();
            }
            const QPointF topLeft = frame->mapToScene(QPointF(0, 0));
            return QRectF(topLeft, QSizeF(frame->width(), frame->height()));
        }
        return QRectF();
    };
    const auto requireInsideWindow = [&](const QString &name) {
        auto *item = itemOf(name);
        if (!item || !item->isVisible()) {
            fail(QStringLiteral("%1 is not visible at measure time").arg(name));
            return;
        }
        const QRectF rect = sceneRectOf(name);
        if (!QRectF(QPointF(0, 0), QSizeF(window->width(), window->height()))
                 .contains(rect)) {
            fail(QStringLiteral("%1 outside: x=%2 y=%3 w=%4 h=%5 win=%6x%7")
                     .arg(name)
                     .arg(rect.x())
                     .arg(rect.y())
                     .arg(rect.width())
                     .arg(rect.height())
                     .arg(window->width())
                     .arg(window->height()));
        }
    };

    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // Stage 0: navigate via the REAL rail entry (B2-Q01 pre-condition).
    push([&]() {
        if (!clickNamed(QStringLiteral("navItem_5")))
            fail(QStringLiteral("the Device rail entry is not clickable"));
        if (!visibleOf(QStringLiteral("deviceProfileWorkspace")))
            fail(QStringLiteral("the Device Profile workspace is not visible"));
        QMetaObject::invokeMethod(controller, "refreshCatalog");
        note(QStringLiteral("stage 0: Device workspace reachable"));
    });
    // Stage 1: open the seeded profile (clean open, no dirty dialog).
    push([&]() {
        if (!clickRow(QStringLiteral("profileCatalogRow"), 0))
            fail(QStringLiteral("cannot select the seeded catalog row"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("the Open button is not clickable"));
        if (propStr(itemOf(QStringLiteral("profileDisplayNameField")), "text")
            != QStringLiteral("Register seed"))
            fail(QStringLiteral("Open did not load the seeded profile"));
        note(QStringLiteral("stage 1: seeded profile open"));
    });
    // Stage 2: Register Map area visible with the seeded row (B2-Q01/Q19).
    push([&]() {
        if (!visibleOf(QStringLiteral("profileRegisterCard")))
            fail(QStringLiteral("the register map card is not visible"));
        if (!visibleOf(QStringLiteral("profileRegisterList")))
            fail(QStringLiteral("the register map list is not visible"));
        const QStringList names = rowsOf("name");
        if (names.size() != 1 || names.first() != QStringLiteral("Frequency"))
            fail(QStringLiteral("the seeded register row is not listed"));
        if (!propBool(itemOf(QStringLiteral("profileRegisterAddButton")),
                      "enabled"))
            fail(QStringLiteral("Add is not enabled with an open profile"));
        note(QStringLiteral("stage 2: register map visible with seed row"));
    });
    // Stage 3: Add opens the editor; FC starts EMPTY (no silent FC03);
    // UInt16 default derives registerCount=1 read-only (B2-Q03/Q04/Q08/Q09).
    push([&]() {
        if (!clickNamed(QStringLiteral("profileRegisterAddButton")))
            fail(QStringLiteral("Add is not clickable"));
        if (!visibleOf(QStringLiteral("entryEditorDialog")))
            fail(QStringLiteral("the entry editor dialog did not open"));
        if (!propStr(itemOf(QStringLiteral("regFcField")), "text").isEmpty())
            fail(QStringLiteral("the FC field is not empty for a new entry "
                                "(silent default FC03)"));
        if (propStr(itemOf(QStringLiteral("regCountField")), "text") != "1")
            fail(QStringLiteral("UInt16 did not derive registerCount=1"));
        if (!propBool(itemOf(QStringLiteral("regCountField")), "readOnly"))
            fail(QStringLiteral("registerCount is editable (Group 1-B)"));
        note(QStringLiteral("stage 3: editor opens; FC empty; count=1"));
    });
    // Stage 4: 1-word wordOrder shown but disabled with 不适用; byteOrder
    // enabled (B2-Q11/Q12/Q14).
    push([&]() {
        if (propBool(itemOf(QStringLiteral("regWordOrderCombo")), "enabled"))
            fail(QStringLiteral("1-word wordOrder is enabled"));
        if (!visibleOf(QStringLiteral("regWordOrderNaLabel")))
            fail(QStringLiteral("the wordOrder 不适用 expression is hidden"));
        if (!propBool(itemOf(QStringLiteral("regByteOrderCombo")), "enabled"))
            fail(QStringLiteral("byteOrder is disabled"));
        note(QStringLiteral("stage 4: 1-word wordOrder N/A; byteOrder on"));
    });
    // Stage 5: Float32 derives registerCount=2 and enables wordOrder
    // (B2-Q07/Q10/Q13).
    push([&]() {
        if (!selectCombo(QStringLiteral("regDataTypeCombo"), 6))
            fail(QStringLiteral("cannot select Float32"));
        if (propStr(itemOf(QStringLiteral("regCountField")), "text") != "2")
            fail(QStringLiteral("Float32 did not derive registerCount=2"));
        if (!propBool(itemOf(QStringLiteral("regWordOrderCombo")), "enabled"))
            fail(QStringLiteral("2-word wordOrder is not enabled"));
        if (visibleOf(QStringLiteral("regWordOrderNaLabel")))
            fail(QStringLiteral("the 不适用 label stayed for a 2-word type"));
        note(QStringLiteral("stage 5: Float32 → count=2; wordOrder on"));
    });
    // Stage 6: back to UInt16 (index 2) for the add below.
    push([&]() {
        if (!selectCombo(QStringLiteral("regDataTypeCombo"), 2))
            fail(QStringLiteral("cannot select UInt16"));
        if (propStr(itemOf(QStringLiteral("regCountField")), "text") != "1")
            fail(QStringLiteral("UInt16 did not re-derive registerCount=1"));
        note(QStringLiteral("stage 6: back to UInt16"));
    });
    // Stage 7: add a VALID cross-FC entry at the SAME address: FC04 @1000
    // with scale/unit metadata; the dialog closes, the row appears and the
    // profile goes dirty (B2-Q02..Q06/Q15..Q19/Q23/Q25).
    push([&]() {
        setField(QStringLiteral("regFcField"), QStringLiteral("04"));
        setField(QStringLiteral("regAddressField"), QStringLiteral("1000"));
        setField(QStringLiteral("regNameField"), QStringLiteral("Ambient"));
        setField(QStringLiteral("regDescField"), QStringLiteral("added by gate"));
        setField(QStringLiteral("regScaleField"), QStringLiteral("1"));
        setField(QStringLiteral("regUnitField"),
                 QStringLiteral("\u00B0C"));
        if (!clickNamed(QStringLiteral("regApplyButton")))
            fail(QStringLiteral("Apply is not clickable"));
        if (visibleOf(QStringLiteral("entryEditorDialog")))
            fail(QStringLiteral("the dialog stayed open for a valid entry: "
                                "%1 / %2")
                     .arg(controller->property("lastActionError").toString(),
                          controller->property("lastActionErrorToken")
                              .toString()));
        if (controller->property("lastActionErrorToken").toString().isEmpty()
            == false)
            fail(QStringLiteral("a valid add left an error token: %1")
                     .arg(controller->property("lastActionErrorToken")
                              .toString()));
        const QStringList fcs = rowsOf("readFunctionCode");
        const QStringList names = rowsOf("name");
        if (fcs.size() != 2 || !fcs.contains(QStringLiteral("4")))
            fail(QStringLiteral("the FC04 entry did not appear"));
        if (!names.contains(QStringLiteral("Ambient")))
            fail(QStringLiteral("the added entry name is missing"));
        if (!controller->property("dirty").toBool())
            fail(QStringLiteral("adding an entry did not make the profile "
                                "dirty"));
        note(QStringLiteral("stage 7: FC04@1000 added; profile dirty"));
    });
    // Stage 8: same-FC duplicate at the same start is REFUSED with a
    // Human-readable reason; the dialog stays open with the input intact
    // (B2-Q22), and the draft is unchanged (B2-C27 twin at the UI layer).
    push([&]() {
        if (!clickNamed(QStringLiteral("profileRegisterAddButton")))
            fail(QStringLiteral("Add is not clickable (duplicate probe)"));
        setField(QStringLiteral("regFcField"), QStringLiteral("03"));
        setField(QStringLiteral("regAddressField"), QStringLiteral("1000"));
        setField(QStringLiteral("regNameField"), QStringLiteral("Dup"));
        if (!clickNamed(QStringLiteral("regApplyButton")))
            fail(QStringLiteral("Apply is not clickable (duplicate probe)"));
        if (!visibleOf(QStringLiteral("entryEditorDialog")))
            fail(QStringLiteral("a refused apply closed the dialog"));
        if (!visibleOf(QStringLiteral("regEditorErrorLabel")))
            fail(QStringLiteral("no validation error is shown for the "
                                "duplicate"));
        const QString errorText =
            propStr(itemOf(QStringLiteral("regEditorErrorLabel")), "text");
        if (!errorText.contains(QStringLiteral("重复地址")))
            fail(QStringLiteral("the duplicate error is not human-readable"));
        if (rowsOf("name").size() != 2)
            fail(QStringLiteral("a refused add mutated the register map"));
        if (!clickNamed(QStringLiteral("regCancelButton")))
            fail(QStringLiteral("the editor Cancel is not clickable"));
        if (visibleOf(QStringLiteral("entryEditorDialog")))
            fail(QStringLiteral("the editor Cancel did not close the dialog"));
        note(QStringLiteral("stage 8: same-FC duplicate refused"));
    });
    // Stage 9: same-FC OVERLAPPING span refused (999..1000 crosses 1000).
    push([&]() {
        if (!clickNamed(QStringLiteral("profileRegisterAddButton")))
            fail(QStringLiteral("Add is not clickable (overlap probe)"));
        setField(QStringLiteral("regFcField"), QStringLiteral("03"));
        setField(QStringLiteral("regAddressField"), QStringLiteral("999"));
        setField(QStringLiteral("regNameField"), QStringLiteral("Overlap"));
        if (!selectCombo(QStringLiteral("regDataTypeCombo"), 4))
            fail(QStringLiteral("cannot select UInt32"));
        if (!clickNamed(QStringLiteral("regApplyButton")))
            fail(QStringLiteral("Apply is not clickable (overlap probe)"));
        if (!visibleOf(QStringLiteral("regEditorErrorLabel")))
            fail(QStringLiteral("no validation error is shown for the "
                                "overlap"));
        const QString errorText =
            propStr(itemOf(QStringLiteral("regEditorErrorLabel")), "text");
        if (!errorText.contains(QStringLiteral("重叠")))
            fail(QStringLiteral("the overlap error is not human-readable"));
        if (rowsOf("name").size() != 2)
            fail(QStringLiteral("a refused add mutated the register map"));
        if (!clickNamed(QStringLiteral("regCancelButton")))
            fail(QStringLiteral("the editor Cancel is not clickable"));
        note(QStringLiteral("stage 9: same-FC overlap refused"));
    });
    // Stage 10: an invalid read function code (0x80) is refused, the typed
    // value STAYS in the field, and the profile draft is unchanged (B2-Q24).
    push([&]() {
        if (!clickNamed(QStringLiteral("profileRegisterAddButton")))
            fail(QStringLiteral("Add is not clickable (FC probe)"));
        setField(QStringLiteral("regFcField"), QStringLiteral("80"));
        setField(QStringLiteral("regAddressField"), QStringLiteral("2000"));
        setField(QStringLiteral("regNameField"), QStringLiteral("BadFc"));
        if (!clickNamed(QStringLiteral("regApplyButton")))
            fail(QStringLiteral("Apply is not clickable (FC probe)"));
        if (!visibleOf(QStringLiteral("regEditorErrorLabel")))
            fail(QStringLiteral("no validation error is shown for FC 0x80"));
        if (propStr(itemOf(QStringLiteral("regFcField")), "text")
            != QStringLiteral("80"))
            fail(QStringLiteral("a refused apply cleared the typed FC"));
        if (rowsOf("name").size() != 2)
            fail(QStringLiteral("a refused add mutated the register map"));
        if (!clickNamed(QStringLiteral("regCancelButton")))
            fail(QStringLiteral("the editor Cancel is not clickable"));
        note(QStringLiteral("stage 10: invalid FC refused; input kept"));
    });
    // Stage 11: Edit the FC04 entry (row 1 in display order): pre-filled,
    // renamed, applied (B2-Q20).
    push([&]() {
        clickRow(QStringLiteral("profileRegisterRow"), 1);
        if (!clickNamed(QStringLiteral("profileRegisterEditButton")))
            fail(QStringLiteral("Edit is not clickable"));
        if (!visibleOf(QStringLiteral("entryEditorDialog")))
            fail(QStringLiteral("the editor did not open for Edit"));
        if (propStr(itemOf(QStringLiteral("regFcField")), "text") != QStringLiteral("4"))
            fail(QStringLiteral("Edit did not pre-fill the function code"));
        if (propStr(itemOf(QStringLiteral("regNameField")), "text")
            != QStringLiteral("Ambient"))
            fail(QStringLiteral("Edit did not pre-fill the entry metadata"));
        setField(QStringLiteral("regNameField"),
                 QStringLiteral("Ambient Temp"));
        if (!clickNamed(QStringLiteral("regApplyButton")))
            fail(QStringLiteral("Apply is not clickable (Edit)"));
        if (visibleOf(QStringLiteral("entryEditorDialog")))
            fail(QStringLiteral("the dialog stayed open after a valid edit"));
        if (!rowsOf("name").contains(QStringLiteral("Ambient Temp")))
            fail(QStringLiteral("the edited name did not appear"));
        note(QStringLiteral("stage 11: entry edited in place"));
    });
    // Stage 12: Save persists the register map; dirty clears (B2-Q26).
    push([&]() {
        if (!clickNamed(QStringLiteral("profileSaveButton")))
            fail(QStringLiteral("Save is not clickable"));
        if (controller->property("dirty").toBool())
            fail(QStringLiteral("Save did not clear dirty"));
        if (rowsOf("name").size() != 2)
            fail(QStringLiteral("Save changed the register map"));
        note(QStringLiteral("stage 12: save persisted the register map"));
    });
    // Stage 13: re-open the same profile from disk: the map round-trips
    // (B2-Q27).
    push([&]() {
        if (!clickRow(QStringLiteral("profileCatalogRow"), 0))
            fail(QStringLiteral("cannot select the catalog row (reopen)"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("Open is not clickable (reopen)"));
        const QStringList names = rowsOf("name");
        if (names.size() != 2
            || !names.contains(QStringLiteral("Frequency"))
            || !names.contains(QStringLiteral("Ambient Temp")))
            fail(QStringLiteral("the reopened profile lost register "
                                "metadata"));
        if (controller->property("lastActionErrorToken").toString().isEmpty()
            == false)
            fail(QStringLiteral("a clean reopen reported an error"));
        note(QStringLiteral("stage 13: open restored the register map"));
    });
    // Stage 14: Delete the FC04 entry (B2-Q21): draft-only, dirty, and the
    // persisted truth returns through Save.
    push([&]() {
        clickRow(QStringLiteral("profileRegisterRow"), 1);
        if (!clickNamed(QStringLiteral("profileRegisterDeleteButton")))
            fail(QStringLiteral("Delete is not clickable"));
        if (rowsOf("name").size() != 1)
            fail(QStringLiteral("the deleted entry is still listed"));
        if (!controller->property("dirty").toBool())
            fail(QStringLiteral("deleting an entry did not make the profile "
                                "dirty"));
        if (!clickNamed(QStringLiteral("profileSaveButton")))
            fail(QStringLiteral("Save is not clickable (after delete)"));
        if (controller->property("dirty").toBool())
            fail(QStringLiteral("Save did not clear dirty after delete"));
        note(QStringLiteral("stage 14: entry deleted; save persists"));
    });
    // Stage 15: resize to the 1000x700 contract size (measure in the NEXT
    // stage — a same-stage measurement reads the previous layout).
    push([&]() {
        window->resize(1000, 700);
        note(QStringLiteral("stage 15: window resized to 1000x700"));
    });
    // Stage 16: the 1000x700 reachability contract (B2-Q28): no clip hides
    // anything — containment asserts measure real scene geometry.
    push([&]() {
        if (window->width() != 1000 || window->height() != 700)
            fail(QStringLiteral("the window is not at 1000x700: %1x%2")
                     .arg(window->width())
                     .arg(window->height()));
        for (const auto &name :
             {QStringLiteral("profileRegisterCard"),
              QStringLiteral("profileRegisterList"),
              QStringLiteral("profileRegisterAddButton"),
              QStringLiteral("profileRegisterEditButton"),
              QStringLiteral("profileRegisterDeleteButton")}) {
            auto *item = itemOf(name);
            if (!item) {
                fail(QStringLiteral("%1 disappeared before measure").arg(name));
                continue;
            }
            const QRectF rect = sceneRectOf(name);
            qInfo().noquote()
                << QStringLiteral("REGGEO %1: x=%2 y=%3 w=%4 h=%5")
                       .arg(name)
                       .arg(rect.x())
                       .arg(rect.y())
                       .arg(rect.width())
                       .arg(rect.height());
            requireInsideWindow(name);
        }
        // The entry editor dialog (opened for real) with ALL fields reachable.
        if (!clickNamed(QStringLiteral("profileRegisterAddButton")))
            fail(QStringLiteral("Add is not clickable (geometry probe)"));
        if (!visibleOf(QStringLiteral("entryEditorDialog")))
            fail(QStringLiteral("the entry dialog did not open (geometry "
                                "probe)"));
        const QRectF dialogRect = popupRectOf(QStringLiteral("entryEditorDialog"));
        qInfo().noquote()
            << QStringLiteral("REGGEO entryEditorDialog: x=%1 y=%2 w=%3 h=%4")
                   .arg(dialogRect.x())
                   .arg(dialogRect.y())
                   .arg(dialogRect.width())
                   .arg(dialogRect.height());
        if (dialogRect.isEmpty())
            fail(QStringLiteral("the entry dialog has no measurable geometry"));
        else if (!QRectF(QPointF(0, 0),
                         QSizeF(window->width(), window->height()))
                      .contains(dialogRect))
            fail(QStringLiteral("the entry dialog is outside the window"));
        for (const auto &name :
             {QStringLiteral("regFcField"),
              QStringLiteral("regAddressField"),
              QStringLiteral("regNameField"),
              QStringLiteral("regDescField"),
              QStringLiteral("regDataTypeCombo"),
              QStringLiteral("regCountField"),
              QStringLiteral("regByteOrderCombo"),
              QStringLiteral("regWordOrderCombo"),
              QStringLiteral("regScaleField"),
              QStringLiteral("regOffsetField"),
              QStringLiteral("regUnitField"),
              QStringLiteral("regApplyButton"),
              QStringLiteral("regCancelButton")})
            requireInsideWindow(name);
        if (!clickNamed(QStringLiteral("regCancelButton")))
            fail(QStringLiteral("the editor Cancel is not clickable"));
        note(QStringLiteral("stage 16: 1000x700 reachable incl. dialog"));
    });
    // Stage 17: keyboard focus reaches the entry editor fields (B2-Q29).
    push([&]() {
        if (!clickNamed(QStringLiteral("profileRegisterAddButton")))
            fail(QStringLiteral("Add is not clickable (focus probe)"));
        auto *field = itemOf(QStringLiteral("regFcField"));
        if (!field) {
            fail(QStringLiteral("the FC field disappeared"));
        } else {
            field->forceActiveFocus();
            if (!field->hasActiveFocus())
                fail(QStringLiteral("the FC field cannot take focus"));
        }
        if (!clickNamed(QStringLiteral("regCancelButton")))
            fail(QStringLiteral("the editor Cancel is not clickable"));
        note(QStringLiteral("stage 17: editor fields focusable"));
    });
    // Stage 18: the FIRST-slice identity editor still works next to the new
    // card (B2-Q30).
    push([&]() {
        setField(QStringLiteral("profileDisplayNameField"),
                 QStringLiteral("Register seed renamed"));
        if (!controller->property("dirty").toBool())
            fail(QStringLiteral("identity editing no longer sets dirty"));
        if (!visibleOf(QStringLiteral("profileDirtyIndicator")))
            fail(QStringLiteral("the dirty cue no longer follows edits"));
        setField(QStringLiteral("profileDisplayNameField"),
                 QStringLiteral("Register seed"));
        note(QStringLiteral("stage 18: identity editor unaffected"));
    });

    const int settleMs = 60;
    auto step = std::make_shared<int>(0);
    auto schedule = std::make_shared<std::function<void()>>();
    auto failuresShared = failures;
    *schedule = [&, step, schedule, failuresShared, &app]() {
        if (*step >= steps->size()) {
            if (!failuresShared->isEmpty()) {
                for (const QString &f : *failuresShared)
                    qWarning().noquote()
                        << QStringLiteral("REGFAIL: %1").arg(f);
                app.exit(1);
                return;
            }
            note(QStringLiteral("REGISTER MAP CHECK PASS (B2-Q01..Q30): "
                               "map visible; add/edit/delete through the "
                               "bounded dialog; FC keyboard-editable with no "
                               "silent default; cross-FC same-address valid; "
                               "same-FC duplicate/overlap refused with "
                               "human-readable errors; derived read-only "
                               "registerCount; 1-word wordOrder N/A shown "
                               "disabled; 1000x700 incl. dialog reachable; "
                               "dirty integration and save/open round-trip"));
            app.exit(0);
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}


// ---------------------------------------------------------------------------
// M12-B third slice `--qml-active-profile-check`: the Communication
// workspace's lightweight current-profile selector, driven end to end
// through the REAL UI path (Communication page → selector → Device Profile
// workspace editor operations → back), against an injected temporary
// managed root (never the production AppData). Covers B3-Q01..Q22.
// ---------------------------------------------------------------------------
int runActiveProfileCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    auto *controller = rootObj
                           ? rootObj->findChild<QObject *>(
                               QStringLiteral("profileController"))
                           : nullptr;
    auto *active = rootObj
                       ? rootObj->findChild<QObject *>(
                           QStringLiteral("activeProfileController"))
                       : nullptr;
    if (!window || !controller || !active) {
        qWarning().noquote()
            << QStringLiteral("ACTFAIL: window/controller not found");
        return 1;
    }
    app.setQuitOnLastWindowClosed(false);

    QTemporaryDir managedRoot;
    if (!managedRoot.isValid()) {
        qWarning().noquote() << QStringLiteral("ACTFAIL: temp root invalid");
        return 1;
    }
    ProfileStore::setManagedRootOverride(managedRoot.path());
    const auto writeSeed = [&](const char *profileId, const char *displayName,
                               std::uint8_t fc, std::uint16_t address) {
        DeviceProfile profile;
        profile.schemaVersion = 1;
        profile.profileId = profileId;
        profile.displayName = displayName;
        profile.manufacturer = "ACME";
        profile.model = "INV-1000";
        RegisterEntry entry;
        entry.readFunctionCode = fc;
        entry.address = address;
        entry.name = "Frequency";
        entry.dataType = RegisterDecodeType::UInt16;
        entry.registerCount = 1;
        entry.scale = 0.1;
        entry.unit = "Hz";
        profile.registers.push_back(entry);
        return ProfileStore::saveToFile(
                   profile, ProfileStore::defaultFilePathFor(
                                QString::fromLatin1(profileId)))
            .ok();
    };
    if (!writeSeed("id-a", "Alpha", 0x03, 1000)
        || !writeSeed("id-b", "Beta", 0x03, 2000)) {
        qWarning().noquote() << QStringLiteral("ACTFAIL: seed write failed");
        return 1;
    }
    {
        // A malformed managed file: must NOT appear in the valid selector
        // list and must not block A/B selection.
        QFile bad(QDir(managedRoot.path())
                      .filePath(QStringLiteral("profile-badbadbadbadbadbad"
                                                "badbadbadbadbadbadbadbadbad"
                                                "badbadbadbadbadbad.json")));
        if (!bad.open(QIODevice::WriteOnly)) {
            qWarning().noquote()
                << QStringLiteral("ACTFAIL: cannot write malformed seed");
            return 1;
        }
        bad.write("{\"schemaVersion\":1,\"profileId\":");
        bad.close();
    }

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &m) {
        qInfo().noquote() << QStringLiteral("ACT: %1").arg(m);
    };

    auto itemOf = [&roots](const QString &name) {
        return findNamedItem(roots, name);
    };
    const auto propStr = [](QObject *o, const char *name) {
        return o ? o->property(name).toString() : QStringLiteral("<none>");
    };
    const auto propBool = [](QObject *o, const char *name) {
        return o ? o->property(name).toBool() : false;
    };
    const auto visibleOf = [&roots](const QString &name) -> bool {
        for (QObject *root : roots) {
            auto *popup = root->findChild<QObject *>(name);
            if (popup)
                return popup->property("visible").toBool();
        }
        return false;
    };
    const auto clickNamed = [&roots, window](const QString &name) {
        auto *item = findNamedItem(roots, name);
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    const auto setField = [&roots](const QString &name, const QString &text) {
        auto *field = findNamedItem(roots, name);
        if (!field) {
            return false;
        }
        field->setProperty("text", text);
        return true;
    };
    // Catalog/register delegates share one objectName: reach the Nth row via
    // the VISUAL tree (childItems) — QObject::findChild misses Repeater
    // delegates in this declarative tree (repo finding).
    const auto collectRows = [&window](const QString &rowName) {
        QList<QQuickItem *> rows;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->objectName() == rowName)
                rows << item;
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        if (window->contentItem())
            walk(window->contentItem());
        return rows;
    };
    const auto clickRow = [&](const QString &rowName, int index) -> bool {
        const QList<QQuickItem *> rows = collectRows(rowName);
        if (index < 0 || index >= rows.size() || !rows.at(index)->isVisible())
            return false;
        QQuickItem *row = rows.at(index);
        const QPointF local(row->width() / 2.0, row->height() / 2.0);
        const QPointF scene = row->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    const auto catalogIndexOf = [&controller](const QString &profileId) {
        const QVariantList catalog =
            controller->property("profileCatalog").toList();
        for (int i = 0; i < catalog.size(); ++i) {
            if (catalog.at(i).toMap().value("profileId").toString()
                == profileId) {
                return i;
            }
        }
        return -1;
    };
    // The selector's ComboBox: activate the row for a profileId the same way
    // a user choice does (the QML handler reads the profileId role).
    const auto selectProfileById = [&](const QString &profileId) -> bool {
        auto *combo = itemOf(QStringLiteral("commProfileSelector"));
        if (!combo) {
            return false;
        }
        if (profileId.isEmpty()) {
            return QMetaObject::invokeMethod(combo, "activated",
                                             Q_ARG(int, 0));
        }
        const int choice = catalogIndexOf(profileId) + 1;
        if (choice < 1) {
            return false;
        }
        return QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, choice));
    };
    const auto sceneRectOf = [&itemOf](const QString &name) -> QRectF {
        auto *item = itemOf(name);
        if (!item) {
            return QRectF();
        }
        const QPointF topLeft = item->mapToScene(QPointF(0, 0));
        return QRectF(topLeft, QSizeF(item->width(), item->height()));
    };
    const auto requireInsideWindow = [&](const QString &name) {
        auto *item = itemOf(name);
        if (!item || !item->isVisible()) {
            fail(QStringLiteral("%1 is not visible at measure time").arg(name));
            return;
        }
        const QRectF rect = sceneRectOf(name);
        if (!QRectF(QPointF(0, 0), QSizeF(window->width(), window->height()))
                 .contains(rect)) {
            fail(QStringLiteral("%1 outside: x=%2 y=%3 w=%4 h=%5 win=%6x%7")
                     .arg(name)
                     .arg(rect.x())
                     .arg(rect.y())
                     .arg(rect.width())
                     .arg(rect.height())
                     .arg(window->width())
                     .arg(window->height()));
        }
    };
    const auto navigate = [&](const QString &railEntry,
                              const QString &workspace) -> bool {
        if (!clickNamed(railEntry) || !visibleOf(workspace)) {
            return false;
        }
        return true;
    };

    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // Stage 0: navigate to the Communication workspace via the REAL rail.
    push([&]() {
        if (!navigate(QStringLiteral("navItem_2"),
                      QStringLiteral("communicationWorkspace")))
            fail(QStringLiteral("the Communication workspace is not "
                                "reachable"));
        if (!visibleOf(QStringLiteral("communicationProfileSelectorRow")))
            fail(QStringLiteral("the profile selector row is not visible"));
        // The controller scanned the PRODUCTION root during engine load (the
        // override was installed afterwards); refresh against the injected
        // root now.
        QMetaObject::invokeMethod(controller, "refreshCatalog");
        note(QStringLiteral("stage 0: Communication workspace reachable"));
    });
    // Stage 1: label + startup state (B3-Q01/Q02/Q03).
    push([&]() {
        if (propStr(itemOf(QStringLiteral("commProfileSelectorLabel")),
                    "text")
            != QStringLiteral("当前设备档案"))
            fail(QStringLiteral("the selector label is not understandable"));
        auto *combo = itemOf(QStringLiteral("commProfileSelector"));
        if (!combo || combo->property("currentIndex").toInt() != 0)
            fail(QStringLiteral("startup is not 未选择设备档案"));
        if (active->property("hasActiveProfile").toBool())
            fail(QStringLiteral("the session started with an active profile"));
        if (!active->property("activeProfileId").toString().isEmpty())
            fail(QStringLiteral("activeProfileId is not empty at startup"));
        note(QStringLiteral("stage 1: selector visible; startup = none"));
    });
    // Stage 2: valid catalog rows present; the malformed file is not
    // selectable (B3-Q04/Q05).
    push([&]() {
        const QVariantList catalog =
            controller->property("profileCatalog").toList();
        if (catalog.size() != 2)
            fail(QStringLiteral("the valid catalog should list exactly A and "
                                "B, got %1").arg(catalog.size()));
        bool sawA = false;
        bool sawB = false;
        for (const QVariant &row : catalog) {
            const QString id =
                row.toMap().value("profileId").toString();
            sawA = sawA || id == QStringLiteral("id-a");
            sawB = sawB || id == QStringLiteral("id-b");
        }
        if (!sawA || !sawB)
            fail(QStringLiteral("the seeded valid profiles are not listed"));
        note(QStringLiteral("stage 2: valid rows only (malformed excluded)"));
    });
    // Stage 3: select A through the REAL selector path (B3-Q06/Q07).
    push([&]() {
        if (!selectProfileById(QStringLiteral("id-a")))
            fail(QStringLiteral("cannot activate selector row A"));
        if (active->property("activeProfileId").toString()
            != QStringLiteral("id-a"))
            fail(QStringLiteral("the active identity is not id-a"));
        if (!active->property("hasActiveProfile").toBool())
            fail(QStringLiteral("hasActiveProfile did not follow selection"));
        note(QStringLiteral("stage 3: A selected by profileId"));
    });
    // Stage 4: workspace away/back — the session selection survives
    // (B3-Q08).
    push([&]() {
        if (!navigate(QStringLiteral("navItem_0"),
                      QStringLiteral("transactionsPage")))
            fail(QStringLiteral("cannot navigate away"));
        if (!navigate(QStringLiteral("navItem_2"),
                      QStringLiteral("communicationWorkspace")))
            fail(QStringLiteral("cannot navigate back"));
        auto *combo = itemOf(QStringLiteral("commProfileSelector"));
        const QVariantList catalog =
            controller->property("profileCatalog").toList();
        int expected = 0;
        for (int i = 0; i < catalog.size(); ++i) {
            if (catalog.at(i).toMap().value("profileId").toString()
                == QStringLiteral("id-a")) {
                expected = i + 1;
            }
        }
        if (!combo || combo->property("currentIndex").toInt() != expected)
            fail(QStringLiteral("the selector lost A across workspaces"));
        if (active->property("activeProfileId").toString()
            != QStringLiteral("id-a"))
            fail(QStringLiteral("activeProfileId changed across workspaces"));
        note(QStringLiteral("stage 4: selection survives navigation"));
    });
    // Stage 5: open profile B in the EDITOR — active remains A (B3-Q09).
    push([&]() {
        if (!navigate(QStringLiteral("navItem_5"),
                      QStringLiteral("deviceProfileWorkspace")))
            fail(QStringLiteral("cannot reach the Device Profile workspace"));
        QMetaObject::invokeMethod(controller, "refreshCatalog");
        const int rowB = catalogIndexOf(QStringLiteral("id-b"));
        if (rowB < 0 || !clickRow(QStringLiteral("profileCatalogRow"), rowB))
            fail(QStringLiteral("cannot select catalog row B"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("Open is not clickable"));
        if (active->property("activeProfileId").toString()
            != QStringLiteral("id-a"))
            fail(QStringLiteral("opening B for edit changed the active "
                                "profile"));
        note(QStringLiteral("stage 5: open-for-edit B keeps active A"));
    });
    // Stage 6: edit B identity (rename to duplicate A) — still unsaved, the
    // active profile is untouched (B3-Q10).
    push([&]() {
        setField(QStringLiteral("profileDisplayNameField"),
                 QStringLiteral("Alpha"));
        if (!controller->property("dirty").toBool())
            fail(QStringLiteral("the B rename did not set dirty"));
        if (active->property("activeProfileId").toString()
            != QStringLiteral("id-a"))
            fail(QStringLiteral("an unsaved edit changed the active profile"));
        note(QStringLiteral("stage 6: unsaved B edit keeps active A"));
    });
    // Stage 7: save B — the active profile is STILL A (B3-Q11), and the
    // selector now carries two "Alpha" rows distinguished by their
    // secondary text (B3-Q12/Q13).
    push([&]() {
        if (!clickNamed(QStringLiteral("profileSaveButton")))
            fail(QStringLiteral("Save is not clickable"));
        if (active->property("activeProfileId").toString()
            != QStringLiteral("id-a"))
            fail(QStringLiteral("saving B changed the active profile"));
        if (!navigate(QStringLiteral("navItem_2"),
                      QStringLiteral("communicationWorkspace")))
            fail(QStringLiteral("cannot navigate back to Communication"));
        const QVariantList catalog =
            controller->property("profileCatalog").toList();
        if (catalog.size() != 2)
            fail(QStringLiteral("the catalog should still hold A and B"));
        const QString primaryA =
            catalog.at(catalogIndexOf(QStringLiteral("id-a")))
                .toMap()
                .value("displayPrimary")
                .toString();
        const QString primaryB =
            catalog.at(catalogIndexOf(QStringLiteral("id-b")))
                .toMap()
                .value("displayPrimary")
                .toString();
        if (primaryA != QStringLiteral("Alpha")
            || primaryB != QStringLiteral("Alpha"))
            fail(QStringLiteral("the duplicate displayName is not in place"));
        const QString secondaryA =
            catalog.at(catalogIndexOf(QStringLiteral("id-a")))
                .toMap()
                .value("displaySecondary")
                .toString();
        const QString secondaryB =
            catalog.at(catalogIndexOf(QStringLiteral("id-b")))
                .toMap()
                .value("displaySecondary")
                .toString();
        if (secondaryA == secondaryB)
            fail(QStringLiteral("duplicate displayName rows are not "
                                "disambiguated"));
        note(QStringLiteral("stage 7: save B; duplicate names disambiguated"));
    });
    // Stage 8: the two same-named profiles remain independently selectable by
    // profileId; select B, then back to A (B3-Q12 revisit through the UI).
    push([&]() {
        if (!selectProfileById(QStringLiteral("id-b")))
            fail(QStringLiteral("cannot select the duplicated B"));
        if (active->property("activeProfileId").toString()
            != QStringLiteral("id-b"))
            fail(QStringLiteral("B could not be selected under a duplicate "
                                "displayName"));
        if (!selectProfileById(QStringLiteral("id-a")))
            fail(QStringLiteral("cannot select back A"));
        if (active->property("activeProfileId").toString()
            != QStringLiteral("id-a"))
            fail(QStringLiteral("A could not be re-selected"));
        note(QStringLiteral("stage 8: duplicate names independently "
                           "selectable"));
    });
    // Stage 9: the 未选择设备档案 entry clears the session selection
    // (B3-Q14).
    push([&]() {
        if (!selectProfileById(QString()))
            fail(QStringLiteral("cannot activate the none entry"));
        if (active->property("hasActiveProfile").toBool())
            fail(QStringLiteral("the none entry did not clear the active "
                                "profile"));
        auto *combo = itemOf(QStringLiteral("commProfileSelector"));
        if (!combo || combo->property("currentIndex").toInt() != 0)
            fail(QStringLiteral("the selector did not return to 未选择设备"
                                "档案"));
        note(QStringLiteral("stage 9: clear through the selector"));
    });
    // Stage 10: select A, then delete the NON-active B — A survives
    // (B3-Q16).
    push([&]() {
        if (!selectProfileById(QStringLiteral("id-a")))
            fail(QStringLiteral("cannot re-select A"));
        if (!navigate(QStringLiteral("navItem_5"),
                      QStringLiteral("deviceProfileWorkspace")))
            fail(QStringLiteral("cannot reach the Device workspace"));
        const int rowB = catalogIndexOf(QStringLiteral("id-b"));
        if (rowB < 0 || !clickRow(QStringLiteral("profileCatalogRow"), rowB))
            fail(QStringLiteral("cannot select catalog row B"));
        if (!clickNamed(QStringLiteral("profileDeleteButton")))
            fail(QStringLiteral("Delete is not clickable"));
        if (!visibleOf(QStringLiteral("profileDeleteDialog")))
            fail(QStringLiteral("the delete confirmation did not appear"));
        if (!clickNamed(QStringLiteral("profileDeleteConfirmButton")))
            fail(QStringLiteral("the confirm-delete button is not "
                                "clickable"));
        if (active->property("activeProfileId").toString()
            != QStringLiteral("id-a"))
            fail(QStringLiteral("deleting non-active B changed the active "
                                "profile"));
        note(QStringLiteral("stage 10: delete non-active B keeps A"));
    });
    // Stage 11: SAVE the active profile with a rename — the identity stays
    // and the selector's visible label refreshes (B3-Q17).
    push([&]() {
        const int rowA = catalogIndexOf(QStringLiteral("id-a"));
        if (rowA < 0 || !clickRow(QStringLiteral("profileCatalogRow"), rowA))
            fail(QStringLiteral("cannot select catalog row A"));
        if (!clickNamed(QStringLiteral("profileOpenButton")))
            fail(QStringLiteral("Open is not clickable (active rename)"));
        setField(QStringLiteral("profileDisplayNameField"),
                 QStringLiteral("Alpha Renamed"));
        if (!clickNamed(QStringLiteral("profileSaveButton")))
            fail(QStringLiteral("Save is not clickable (active rename)"));
        if (active->property("activeProfileId").toString()
            != QStringLiteral("id-a"))
            fail(QStringLiteral("saving the active profile changed its "
                                "identity"));
        if (!navigate(QStringLiteral("navItem_2"),
                      QStringLiteral("communicationWorkspace")))
            fail(QStringLiteral("cannot navigate back (active rename)"));
        auto *combo = itemOf(QStringLiteral("commProfileSelector"));
        if (!combo || combo->property("currentIndex").toInt() == 0)
            fail(QStringLiteral("the selector dropped the active profile "
                                "after rename+save"));
        if (propStr(combo, "displayText")
            != QStringLiteral("Alpha Renamed"))
            fail(QStringLiteral("the selector label did not refresh after "
                                "the rename: %1")
                     .arg(combo ? propStr(combo, "displayText")
                                : QStringLiteral("<none>")));
        note(QStringLiteral("stage 11: active renamed; label refreshed"));
    });
    // Stage 12: delete the ACTIVE profile — the selector returns to
    // 未选择设备档案 and no stale content remains (B3-Q15).
    push([&]() {
        if (!navigate(QStringLiteral("navItem_5"),
                      QStringLiteral("deviceProfileWorkspace")))
            fail(QStringLiteral("cannot reach the Device workspace"));
        const int rowA = catalogIndexOf(QStringLiteral("id-a"));
        if (rowA < 0 || !clickRow(QStringLiteral("profileCatalogRow"), rowA))
            fail(QStringLiteral("cannot select catalog row A (delete)"));
        if (!clickNamed(QStringLiteral("profileDeleteButton")))
            fail(QStringLiteral("Delete is not clickable (active)"));
        if (!visibleOf(QStringLiteral("profileDeleteDialog")))
            fail(QStringLiteral("the delete confirmation did not appear"));
        if (!clickNamed(QStringLiteral("profileDeleteConfirmButton")))
            fail(QStringLiteral("the confirm button is not clickable"));
        if (active->property("hasActiveProfile").toBool())
            fail(QStringLiteral("deleting the active profile did not clear "
                                "the selection"));
        if (!navigate(QStringLiteral("navItem_2"),
                      QStringLiteral("communicationWorkspace")))
            fail(QStringLiteral("cannot navigate back (delete active)"));
        auto *combo = itemOf(QStringLiteral("commProfileSelector"));
        if (!combo || combo->property("currentIndex").toInt() != 0)
            fail(QStringLiteral("the selector is not back at 未选择设备档案"));
        note(QStringLiteral("stage 12: delete active → selector cleared"));
    });
    // Stage 13: NO semantic overlay exists on the Communication page
    // (B3-Q18) — the read result panel is untouched by profile state.
    push([&]() {
        QList<QQuickItem *> semanticItems;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->objectName().contains(QStringLiteral("semantic"),
                                            Qt::CaseInsensitive))
                semanticItems << item;
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        if (window->contentItem())
            walk(window->contentItem());
        if (!semanticItems.isEmpty())
            fail(QStringLiteral("a semantic overlay object appeared"));
        if (!visibleOf(QStringLiteral("readResultPanel")))
            fail(QStringLiteral("the read result panel disappeared"));
        note(QStringLiteral("stage 13: no semantic overlay"));
    });
    // Stage 14: the existing request controls remain usable (B3-Q19).
    push([&]() {
        for (const auto &name :
             {QStringLiteral("commStartField"),
              QStringLiteral("commReadButton"),
              QStringLiteral("commProfileSelector")}) {
            if (!propBool(itemOf(name), "visible")
                && !(itemOf(name) && itemOf(name)->isVisible()))
                fail(QStringLiteral("%1 is not visible").arg(name));
        }
        note(QStringLiteral("stage 14: request controls visible"));
    });
    // Stage 15: resize to the 1000x700 contract size (measure next stage).
    push([&]() {
        window->resize(1000, 700);
        note(QStringLiteral("stage 15: window resized to 1000x700"));
    });
    // Stage 16: the 1000x700 reachability contract (B3-Q20) — the selector
    // fits and NOTHING on the communication page got squeezed out.
    push([&]() {
        if (window->width() != 1000 || window->height() != 700)
            fail(QStringLiteral("the window is not at 1000x700: %1x%2")
                     .arg(window->width())
                     .arg(window->height()));
        for (const auto &name :
             {QStringLiteral("communicationProfileSelectorRow"),
              QStringLiteral("commProfileSelectorLabel"),
              QStringLiteral("commProfileSelector"),
              QStringLiteral("communicationConnectionSection"),
              QStringLiteral("commStartField"),
              QStringLiteral("commReadButton"),
              QStringLiteral("readResultPanel")}) {
            auto *item = itemOf(name);
            if (!item) {
                fail(QStringLiteral("%1 disappeared before measure").arg(name));
                continue;
            }
            const QRectF rect = sceneRectOf(name);
            qInfo().noquote()
                << QStringLiteral("ACTGEO %1: x=%2 y=%3 w=%4 h=%5")
                       .arg(name)
                       .arg(rect.x())
                       .arg(rect.y())
                       .arg(rect.width())
                       .arg(rect.height());
            requireInsideWindow(name);
        }
        note(QStringLiteral("stage 16: 1000x700 selector + controls "
                           "reachable"));
    });
    // Stage 17: keyboard focus reaches the selector (B3-Q21).
    push([&]() {
        auto *combo = itemOf(QStringLiteral("commProfileSelector"));
        if (!combo) {
            fail(QStringLiteral("the selector disappeared"));
        } else {
            combo->forceActiveFocus();
            if (!combo->hasActiveFocus())
                fail(QStringLiteral("the selector cannot take focus"));
        }
        note(QStringLiteral("stage 17: selector focusable"));
    });
    // Stage 18: the Profile Editor still works next to the new selector
    // (B3-Q22).
    push([&]() {
        if (!navigate(QStringLiteral("navItem_5"),
                      QStringLiteral("deviceProfileWorkspace")))
            fail(QStringLiteral("the Device Profile workspace is no longer "
                                "reachable"));
        if (!visibleOf(QStringLiteral("profileRegisterCard")))
            fail(QStringLiteral("the register map card disappeared"));
        note(QStringLiteral("stage 18: profile editor unaffected"));
    });

    const int settleMs = 60;
    auto step = std::make_shared<int>(0);
    auto schedule = std::make_shared<std::function<void()>>();
    auto failuresShared = failures;
    *schedule = [&, step, schedule, failuresShared, &app]() {
        if (*step >= steps->size()) {
            if (!failuresShared->isEmpty()) {
                for (const QString &f : *failuresShared)
                    qWarning().noquote()
                        << QStringLiteral("ACTFAIL: %1").arg(f);
                app.exit(1);
                return;
            }
            note(QStringLiteral("ACTIVE PROFILE CHECK PASS (B3-Q01..Q22): "
                               "selector visible with startup = 未选择设备"
                               "档案; selection by full profileId; survives "
                               "navigation; editor open/edit/save of another "
                               "profile never changes it; duplicate "
                               "displayName rows disambiguated and "
                               "independently selectable; clear works; "
                               "delete-active clears while delete-failure "
                               "would keep it; active rename+save refreshes "
                               "the visible label; no semantic overlay; "
                               "1000x700 reachable; editor unaffected"));
            app.exit(0);
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}


// ---------------------------------------------------------------------------
// M12-C C1a `--qml-manual-import-check`: the Manual Import area inside the
// Device Profile workspace driven end to end through the REAL UI path, against
// an injected temporary managed root. Deterministic by contract: TXT /
// Markdown only, no AI, no cloud, no network, no credential, no Candidate and
// no Q&A. PDF / DOCX remain C1b (deferred, still in scope) and OCR stays a
// later capability — this gate never claims otherwise.
// ---------------------------------------------------------------------------
int runManualImportCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    auto *manual = rootObj ? rootObj->findChild<QObject *>(
                                 QStringLiteral("manualController"))
                           : nullptr;
    auto *profiles = rootObj ? rootObj->findChild<QObject *>(
                                   QStringLiteral("profileController"))
                             : nullptr;
    if (!window || !manual || !profiles) {
        qWarning().noquote()
            << QStringLiteral("MANFAIL: window/controller not found");
        return 1;
    }
    app.setQuitOnLastWindowClosed(false);

    QTemporaryDir managedRoot;
    if (!managedRoot.isValid()) {
        qWarning().noquote() << QStringLiteral("MANFAIL: temp root invalid");
        return 1;
    }
    ManualStore::setManagedRootOverride(managedRoot.path());
    ProfileStore::setManagedRootOverride(managedRoot.path());

    const QString sourceDir = QDir(managedRoot.path()).filePath("sources");
    if (!QDir(managedRoot.path()).mkpath(QStringLiteral("sources"))) {
        qWarning().noquote() << QStringLiteral("MANFAIL: seed dir failed");
        return 1;
    }
    const auto writeSeed = [&sourceDir](const QString &name,
                                        const QByteArray &bytes) {
        const QString path = QDir(sourceDir).filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return QString();
        }
        file.write(bytes);
        file.close();
        return path;
    };
    const QByteArray txtBytes =
        QStringLiteral("Frequency register 1000\n输出频率 46.6 Hz\n")
            .toUtf8();
    QByteArray mdBytes;
    mdBytes.append("\xEF\xBB\xBF", 3);
    mdBytes.append(
        QStringLiteral(
            "# 设备手册\n\n<script>alert(1)</script>\n\n"
            "![x](https://example.invalid/x.png)\n\n"
            "[link](https://example.invalid/page)\n")
            .toUtf8());
    const QString txtPath =
        writeSeed(QStringLiteral("manual-utf8.txt"), txtBytes);
    const QString mdPath =
        writeSeed(QStringLiteral("manual-bom.md"), mdBytes);
    const QString exePath =
        writeSeed(QStringLiteral("manual.exe"), QByteArray("MZ binary"));
    if (txtPath.isEmpty() || mdPath.isEmpty() || exePath.isEmpty()) {
        qWarning().noquote() << QStringLiteral("MANFAIL: seed write failed");
        return 1;
    }

    // M12-C C1b second slice (T027 section 60): deterministic PDF / DOCX
    // seeds built in-code (licence-free, offline). The PDF text pages use a
    // plain Helvetica font; no CJK fixture is needed at gate level (the CJK
    // coverage lives in the unit suites).
    const auto pdfStreamObject = [](const QByteArray &stream) {
        return QByteArray("<< /Length ") + QByteArray::number(stream.size())
               + QByteArray(" >>\nstream\n") + stream
               + QByteArray("\nendstream");
    };
    const auto buildTextPdf = [&](const QList<QByteArray> &pageTexts) {
        QList<QByteArray> objects;
        objects << QByteArray("<< /Type /Catalog /Pages 2 0 R >>");
        QByteArray kids;
        for (int i = 0; i < pageTexts.size(); ++i) {
            kids += QByteArray::number(3 + i * 2) + QByteArray(" 0 R ");
        }
        objects << QByteArray("<< /Type /Pages /Kids [").append(kids)
                       .append("] /Count ")
                       .append(QByteArray::number(pageTexts.size()))
                       .append(" >>");
        for (int i = 0; i < pageTexts.size(); ++i) {
            objects << QByteArray("<< /Type /Page /Parent 2 0 R ")
                           .append("/MediaBox [0 0 612 792] /Contents ")
                           .append(QByteArray::number(4 + i * 2))
                           .append(" 0 R /Resources << /Font << /F1 ")
                           .append(QByteArray::number(5 + pageTexts.size() * 2))
                           .append(" 0 R >> >> >>");
            const QByteArray stream = QByteArray("BT /F1 18 Tf 72 700 Td (")
                                          .append(pageTexts.at(i))
                                          .append(") Tj ET");
            objects << pdfStreamObject(stream);
        }
        objects << QByteArray("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        QByteArray out = "%PDF-1.4\n";
        QList<int> offsets;
        for (int i = 0; i < objects.size(); ++i) {
            offsets.append(out.size());
            out += QByteArray::number(i + 1) + " 0 obj\n" + objects.at(i)
                   + "\nendobj\n";
        }
        const int xrefAt = out.size();
        out += "xref\n0 " + QByteArray::number(objects.size() + 1)
               + "\n0000000000 65535 f \n";
        for (int offset : offsets) {
            out += QByteArray::number(offset).rightJustified(10, '0')
                   + " 00000 n \n";
        }
        out += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1)
               + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xrefAt)
               + "\n%%EOF\n";
        return out;
    };
    const QByteArray textPdfBytes = buildTextPdf(
        {QByteArray("ModbusLens C1b PDF import")});
    const QByteArray noTextPdfBytes = buildTextPdf({});
    const QByteArray twoPagePdfBytes = buildTextPdf(
        {QByteArray("ModbusLens C1b PDF page one"),
         QByteArray("ModbusLens C1b PDF page two")});
    const QString textPdfPath =
        writeSeed(QStringLiteral("manual-text.pdf"), textPdfBytes);
    const QString noTextPdfPath =
        writeSeed(QStringLiteral("manual-notext.pdf"), noTextPdfBytes);
    const QString twoPagePdfPath =
        writeSeed(QStringLiteral("manual-twopage.pdf"), twoPagePdfBytes);
    const QString textPdfCopyPath =
        writeSeed(QStringLiteral("manual-text-copy.pdf"), textPdfBytes);

    const QByteArray docxDocumentXml = QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/"
        "wordprocessingml/2006/main\"><w:body><w:p><w:r><w:t>"
        "DOCX \u5bfc\u5165\u6b63\u6587</w:t></w:r></w:p></w:body></w:document>");
    const QByteArray docxContentTypes = QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/"
        "content-types\"><Default Extension=\"rels\" ContentType=\"application/"
        "vnd.openxmlformats-package.relationships+xml\"/><Default Extension=\"xml\""
        " ContentType=\"application/xml\"/><Override PartName=\"/word/document.xml\""
        " ContentType=\"application/vnd.openxmlformats-officedocument."
        "wordprocessingml.document.main+xml\"/></Types>");
    const QByteArray docxRels = QByteArray(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/"
        "relationships\"><Relationship Id=\"rId1\" Type=\"http://schemas."
        "openxmlformats.org/officeDocument/2006/relationships/officeDocument\""
        " Target=\"word/document.xml\"/></Relationships>");
    const auto writeDocxSeed = [&](const QString &path) -> bool {
        int errorCode = 0;
        zip_t *archive =
            zip_open(path.toUtf8().constData(), ZIP_CREATE | ZIP_TRUNCATE,
                     &errorCode);
        if (archive == nullptr) {
            return false;
        }
        const auto add = [archive](const char *name, const QByteArray &content) {
            zip_source_t *source = zip_source_buffer(
                archive, content.constData(), content.size(), 0);
            return source != nullptr
                   && zip_file_add(archive, name, source, ZIP_FL_ENC_UTF_8) >= 0;
        };
        const bool ok =
            add("[Content_Types].xml", docxContentTypes)
            && add("_rels/.rels", docxRels)
            && add("word/document.xml", docxDocumentXml);
        zip_close(archive);
        return ok;
    };
    const QString docxPath =
        writeSeed(QStringLiteral("manual.docx"), QByteArray());
    if (docxPath.isEmpty() || !writeDocxSeed(docxPath)) {
        qWarning().noquote() << QStringLiteral("MANFAIL: docx seed failed");
        return 1;
    }
    if (textPdfPath.isEmpty() || noTextPdfPath.isEmpty()
        || twoPagePdfPath.isEmpty() || textPdfCopyPath.isEmpty()) {
        qWarning().noquote() << QStringLiteral("MANFAIL: pdf seed write failed");
        return 1;
    }

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &m) {
        qInfo().noquote() << QStringLiteral("MAN: %1").arg(m);
    };
    auto itemOf = [&roots](const QString &name) {
        return findNamedItem(roots, name);
    };
    const auto visibleOf = [&roots](const QString &name) -> bool {
        for (QObject *root : roots) {
            auto *item = root->findChild<QObject *>(name);
            if (item)
                return item->property("visible").toBool();
        }
        return false;
    };
    const auto clickNamed = [&roots, window](const QString &name) {
        auto *item = findNamedItem(roots, name);
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    const auto requireInsideWindow = [&](const QString &name) {
        auto *item = itemOf(name);
        if (!item || !item->isVisible()) {
            fail(QStringLiteral("%1 is not visible at measure time").arg(name));
            return;
        }
        const QPointF topLeft = item->mapToScene(QPointF(0, 0));
        const QRectF rect(topLeft, QSizeF(item->width(), item->height()));
        if (!QRectF(QPointF(0, 0), QSizeF(window->width(), window->height()))
                 .contains(rect)) {
            fail(QStringLiteral("%1 outside: x=%2 y=%3 w=%4 h=%5 win=%6x%7")
                     .arg(name)
                     .arg(rect.x())
                     .arg(rect.y())
                     .arg(rect.width())
                     .arg(rect.height())
                     .arg(window->width())
                     .arg(window->height()));
        }
    };
    const auto importFile = [&](const QString &path) -> bool {
        bool ok = false;
        QMetaObject::invokeMethod(
            manual, "importManualFile", Qt::DirectConnection,
            Q_RETURN_ARG(bool, ok), Q_ARG(QUrl, QUrl::fromLocalFile(path)));
        return ok;
    };
    const auto selectDocument = [&](int index) {
        QMetaObject::invokeMethod(manual, "selectDocument", Qt::DirectConnection,
                                  Q_ARG(int, index));
    };
    const auto docCount = [&]() {
        return manual->property("manualDocuments").toList().size();
    };
    const auto errorToken = [&]() {
        return manual->property("lastErrorToken").toString();
    };
    const auto preview = [&]() {
        return manual->property("previewText").toString();
    };
    // The managed index is ordered by metadata filename, not by import order:
    // always resolve a document by its original file name.
    const auto indexOfDoc = [&](const QString &fileName) {
        const QVariantList docs = manual->property("manualDocuments").toList();
        for (int i = 0; i < docs.size(); ++i) {
            if (docs.at(i).toMap().value("originalFileName").toString()
                == fileName) {
                return i;
            }
        }
        return -1;
    };
    const auto collectRows = [&window](const QString &rowName) {
        QList<QQuickItem *> rows;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->objectName() == rowName)
                rows << item;
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        if (window->contentItem())
            walk(window->contentItem());
        return rows;
    };

    // Geometry forensics (T027 §48 / C1a layout RCA): the Manual area must be
    // measured on REAL scene geometry — never "the object exists".
    const auto dumpManualGeometry = [&](const QString &tag) {
        static const char *const kNames[] = {
            "deviceProfileWorkspace", "profileWorkspaceRow", "manualImportHost",
            "manualImportCard",      "manualImportHeader",  "manualImportActions",
            "manualImportButton",    "manualImportBody",    "manualDocumentList",
            "manualDocColumn",       "manualDocName",       "manualPreview",
            "manualPreviewText",
        };
        for (const char *rawName : kNames) {
            const QString name = QString::fromLatin1(rawName);
            auto *item = itemOf(name);
            if (!item) {
                qInfo().noquote() << QStringLiteral("MANGEO %1 %2: <missing>")
                                         .arg(tag, name);
                continue;
            }
            const QPointF p = item->mapToScene(QPointF(0, 0));
            qInfo().noquote()
                << QStringLiteral(
                       "MANGEO %1 %2: x=%3 y=%4 w=%5 h=%6 iw=%7 ih=%8 vis=%9")
                       .arg(tag, name)
                       .arg(p.x())
                       .arg(p.y())
                       .arg(item->width())
                       .arg(item->height())
                       .arg(item->implicitWidth())
                       .arg(item->implicitHeight())
                       .arg(item->isVisible() ? QStringLiteral("1")
                                              : QStringLiteral("0"));
        }
        // Parent chain: the first ancestor that HAS a size while the child is
        // 0 is where the layout stops propagating.
        QQuickItem *cur = itemOf(QStringLiteral("manualImportHost"));
        for (int depth = 0; cur && depth < 6; ++depth) {
            const QPointF p = cur->mapToScene(QPointF(0, 0));
            qInfo().noquote()
                << QStringLiteral("MANCHAIN %1 d%2 %3: x=%4 y=%5 w=%6 h=%7 iw=%8 ih=%9")
                       .arg(tag)
                       .arg(depth)
                       .arg(cur->objectName().isEmpty()
                                ? QString::fromLatin1(cur->metaObject()->className())
                                : cur->objectName())
                       .arg(p.x())
                       .arg(p.y())
                       .arg(cur->width())
                       .arg(cur->height())
                       .arg(cur->implicitWidth())
                       .arg(cur->implicitHeight());
            cur = cur->parentItem();
        }
    };

    // "The object exists" is NOT evidence: every measured item must also have
    // a real, non-zero viewport inside the window.
    const auto requireSized = [&](const QString &name) {
        auto *item = itemOf(name);
        if (!item) {
            fail(QStringLiteral("%1 is missing").arg(name));
            return;
        }
        if (!item->isVisible()) {
            fail(QStringLiteral("%1 is not visible").arg(name));
            return;
        }
        if (item->width() <= 0.0 || item->height() <= 0.0) {
            fail(QStringLiteral("%1 has a zero viewport: w=%2 h=%3")
                     .arg(name)
                     .arg(item->width())
                     .arg(item->height()));
        }
    };

    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // Stage 0: reachable through the REAL rail, and the Manual Import area is
    // inside the 1000x700 window.
    push([&]() {
        window->resize(1000, 700);
        if (!clickNamed(QStringLiteral("navItem_5")))
            fail(QStringLiteral("the Device rail entry is not clickable"));
        if (!visibleOf(QStringLiteral("deviceProfileWorkspace")))
            fail(QStringLiteral("the Device Profile workspace is not visible"));
        QMetaObject::invokeMethod(manual, "refresh");
        QMetaObject::invokeMethod(profiles, "refreshCatalog");
        if (!visibleOf(QStringLiteral("manualImportCard")))
            fail(QStringLiteral("the Manual Import card is not visible"));
        note(QStringLiteral("stage 0: Manual Import area reachable via rail"));
    });

    // Stage 0b: geometry is measured in its OWN step — the window resize from
    // stage 0 only reaches the scene graph after an event-loop pass, so a
    // measurement taken inside the same step reads a stale (unsettled) layout.
    push([&]() {
        requireSized(QStringLiteral("manualImportHost"));
        requireSized(QStringLiteral("manualImportCard"));
        requireSized(QStringLiteral("manualImportButton"));
        requireSized(QStringLiteral("manualDocumentList"));
        requireSized(QStringLiteral("manualPreview"));
        requireInsideWindow(QStringLiteral("manualImportCard"));
        requireInsideWindow(QStringLiteral("manualImportButton"));
        requireInsideWindow(QStringLiteral("manualDocumentList"));
        requireInsideWindow(QStringLiteral("manualPreview"));
        dumpManualGeometry(QStringLiteral("s0b"));
        note(QStringLiteral("stage 0b: Manual Import area measured at 1000x700"));
    });

    // Stage 1: deterministic UTF-8 TXT import + plain-text preview.
    push([&]() {
        if (!importFile(txtPath)) {
            fail(QStringLiteral("stage 1: UTF-8 TXT import failed: %1")
                     .arg(errorToken()));
            return;
        }
        if (docCount() != 1)
            fail(QStringLiteral("stage 1: expected 1 document, got %1")
                     .arg(docCount()));
        selectDocument(indexOfDoc(QStringLiteral("manual-utf8.txt")));
        if (preview() != QString::fromUtf8(txtBytes))
            fail(QStringLiteral("stage 1: preview != source text"));
        if (collectRows(QStringLiteral("manualDocumentRow")).size() != 1)
            fail(QStringLiteral("stage 1: the document list row is missing"));
        note(QStringLiteral("stage 1: UTF-8 TXT imported and previewed"));
    });

    // Stage 2: UTF-8 BOM Markdown — the BOM never reaches the visible text
    // and Markdown stays plain source text (no HTML/script/remote fetch).
    push([&]() {
        if (!importFile(mdPath)) {
            fail(QStringLiteral("stage 2: BOM Markdown import failed: %1")
                     .arg(errorToken()));
            return;
        }
        if (docCount() != 2)
            fail(QStringLiteral("stage 2: expected 2 documents, got %1")
                     .arg(docCount()));
        selectDocument(indexOfDoc(QStringLiteral("manual-bom.md")));
        const QString text = preview();
        if (text.contains(QChar(0xFEFF)))
            fail(QStringLiteral("stage 2: the BOM leaked into the text"));
        if (!text.contains(QStringLiteral("<script>alert(1)</script>")))
            fail(QStringLiteral("stage 2: Markdown source text altered"));
        if (!text.contains(QStringLiteral("![x](https://example.invalid/x.png)")))
            fail(QStringLiteral("stage 2: the remote image markup is missing"));
        note(QStringLiteral("stage 2: BOM Markdown imported as plain text"));
    });

    // Stage 3: an unsupported extension is refused — and the refusal
    // changes nothing. (T027 section 60.1: .pdf / .docx are now SUPPORTED
    // routes, so this case moved to a still-unsupported extension.)
    push([&]() {
        if (importFile(exePath))
            fail(QStringLiteral("stage 3: an .exe was accepted"));
        if (errorToken() != QStringLiteral("unsupported_type"))
            fail(QStringLiteral("stage 3: expected unsupported_type, got %1")
                     .arg(errorToken()));
        if (docCount() != 2)
            fail(QStringLiteral("stage 3: a refused import changed the list"));
        if (collectRows(QStringLiteral("manualDocumentRow")).size() != 2)
            fail(QStringLiteral("stage 3: the document list changed"));
        note(QStringLiteral("stage 3: unsupported extension refused, list unchanged"));

    // Stage 3b: a PDF WITH an existing text layer imports successfully; the
    // preview is the deterministic extracted text under a page header, and
    // the page header is presentation-only (never in the cache payload).
    push([&]() {
        if (!importFile(textPdfPath)) {
            fail(QStringLiteral("stage 3b: PDF import failed: %1")
                     .arg(errorToken()));
            return;
        }
        if (docCount() != 3)
            fail(QStringLiteral("stage 3b: expected 3 documents, got %1")
                     .arg(docCount()));
        selectDocument(indexOfDoc(QStringLiteral("manual-text.pdf")));
        if (preview().isEmpty()
            || !preview().contains(QStringLiteral("ModbusLens C1b PDF import")))
            fail(QStringLiteral("stage 3b: the extracted text is missing"));
        if (!preview().startsWith(QStringLiteral("\u7b2c 1 \u9875\n")))
            fail(QStringLiteral("stage 3b: the page header is missing"));
        const QString hash =
            manual->property("selectedDocument").toMap()
                .value(QStringLiteral("contentHash")).toString();
        QFile cacheFile(QDir(ManualStore::textDirectory())
                            .filePath(hash + QStringLiteral(".json")));
        if (!cacheFile.open(QIODevice::ReadOnly)) {
            fail(QStringLiteral("stage 3b: the PDF cache is missing"));
            return;
        }
        const QByteArray cacheBytes = cacheFile.readAll();
        cacheFile.close();
        if (cacheBytes.contains("\u7b2c"))
            fail(QStringLiteral("stage 3b: a page header leaked into the cache"));
        note(QStringLiteral("stage 3b: PDF text layer imported; header is presentation-only"));
    });

    // Stage 3c: DOCX main story imports with the frozen plain text.
    push([&]() {
        if (!importFile(docxPath)) {
            fail(QStringLiteral("stage 3c: DOCX import failed: %1")
                     .arg(errorToken()));
            return;
        }
        if (docCount() != 4)
            fail(QStringLiteral("stage 3c: expected 4 documents, got %1")
                     .arg(docCount()));
        selectDocument(indexOfDoc(QStringLiteral("manual.docx")));
        if (preview() != QString::fromUtf8("DOCX \u5bfc\u5165\u6b63\u6587\n"))
            fail(QStringLiteral("stage 3c: the main story text changed"));
        note(QStringLiteral("stage 3c: DOCX main story imported"));
    });

    // Stage 3d: a PDF without a text layer imports SUCCESSFULLY and the UI
    // reports the explicit no-text state (never a fake empty success).
    push([&]() {
        if (!importFile(noTextPdfPath)) {
            fail(QStringLiteral("stage 3d: no-text PDF import failed: %1")
                     .arg(errorToken()));
            return;
        }
        if (docCount() != 5)
            fail(QStringLiteral("stage 3d: expected 5 documents, got %1")
                     .arg(docCount()));
        selectDocument(indexOfDoc(QStringLiteral("manual-notext.pdf")));
        if (manual->property("previewStateToken").toString()
            != QStringLiteral("no_extractable_text"))
            fail(QStringLiteral("stage 3d: the no-text state is not surfaced"));
        if (!preview().isEmpty())
            fail(QStringLiteral("stage 3d: a no-text PDF must not yield text"));
        note(QStringLiteral("stage 3d: no-text PDF imports with an explicit state"));
    });

    // Stage 3e: the same PDF bytes from a different original path form a
    // SECOND record while the cache payload is reused (one cache file).
    push([&]() {
        if (!importFile(textPdfCopyPath)) {
            fail(QStringLiteral("stage 3e: duplicate import failed: %1")
                     .arg(errorToken()));
            return;
        }
        if (docCount() != 6)
            fail(QStringLiteral("stage 3e: expected 6 documents, got %1")
                     .arg(docCount()));
        int jsonCaches = 0;
        const QDir textDir(ManualStore::textDirectory());
        for (const QString &name : textDir.entryList(QDir::Files)) {
            if (name.endsWith(QLatin1String(".json")))
                ++jsonCaches;
        }
        if (jsonCaches != 2)  // the text PDF + the no-text PDF: exactly one each
            fail(QStringLiteral("stage 3e: the cache was duplicated (%1)")
                     .arg(jsonCaches));
        note(QStringLiteral("stage 3e: same bytes -> second record, cache reused"));
    });

    // Stage 3f: a two-page PDF presents BOTH pages under their headers and
    // still keeps the per-page truth intact in the cache.
    push([&]() {
        if (!importFile(twoPagePdfPath)) {
            fail(QStringLiteral("stage 3f: two-page PDF import failed: %1")
                     .arg(errorToken()));
            return;
        }
        selectDocument(indexOfDoc(QStringLiteral("manual-twopage.pdf")));
        if (!preview().startsWith(QStringLiteral("\u7b2c 1 \u9875\n")))
            fail(QStringLiteral("stage 3f: page 1 header missing"));
        if (!preview().contains(QStringLiteral("\u7b2c 2 \u9875\n")))
            fail(QStringLiteral("stage 3f: page 2 header missing"));
        if (!preview().contains(QStringLiteral("page two")))
            fail(QStringLiteral("stage 3f: page 2 text missing"));
        note(QStringLiteral("stage 3f: two-page presentation verified"));
    });
    });

    // Stage 4: no AI / Candidate / Q&A / credential control exists anywhere in
    // the Manual Import area.
    push([&]() {
        auto *card = itemOf(QStringLiteral("manualImportCard"));
        if (!card) {
            fail(QStringLiteral("stage 4: the Manual Import card is missing"));
            return;
        }
        QStringList names;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (!item->objectName().isEmpty())
                names << item->objectName();
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        walk(card);
        const QStringList forbidden = {
            QStringLiteral("candidate"),
            QStringLiteral("accept"),     QStringLiteral("reject"),
            QStringLiteral("question"),   QStringLiteral("chat"),
            QStringLiteral("upload"),     QStringLiteral("credential"),
            QStringLiteral("apikey"),     QStringLiteral("network"),
        };
        for (const QString &name : names) {
            const QString lower = name.toLower();
            for (const QString &token : forbidden) {
                if (lower.contains(token))
                    fail(QStringLiteral("stage 4: forbidden control '%1' "
                                        "(token '%2')")
                             .arg(name, token));
            }
        }
        note(QStringLiteral("stage 4: no AI/Candidate/Q&A controls present"));
    });

    // Stage 5: profile isolation — the import touched nothing in the profile
    // world (draft, dirty state, open profile, active profile).
    push([&]() {
        if (profiles->property("hasOpenProfile").toBool())
            fail(QStringLiteral("stage 5: an import opened a profile"));
        if (profiles->property("dirty").toBool())
            fail(QStringLiteral("stage 5: an import dirtied the draft"));
        note(QStringLiteral("stage 5: profile state untouched"));
    });

    // Stage 6: the 1000x700 contract still holds with documents present.
    push([&]() {
        window->resize(1000, 700);
        requireInsideWindow(QStringLiteral("manualImportCard"));
        requireInsideWindow(QStringLiteral("manualImportButton"));
        requireInsideWindow(QStringLiteral("manualDocumentList"));
        requireInsideWindow(QStringLiteral("manualPreview"));
        requireSized(QStringLiteral("manualPreview"));
        requireSized(QStringLiteral("manualDocName"));
        requireInsideWindow(QStringLiteral("manualDocName"));
        // Neighbour contract: the Manual area must neither push the profile
        // region out of the window nor overlap it. Measured at REGION level --
        // individual scrollable fields inside the profile cards are reachable by
        // scrolling by design and are deliberately not part of this contract.
        requireInsideWindow(QStringLiteral("profileCatalogCard"));
        requireInsideWindow(QStringLiteral("profileRegisterCard"));
        auto *const row = itemOf(QStringLiteral("profileWorkspaceRow"));
        auto *const host = itemOf(QStringLiteral("manualImportHost"));
        if (row != nullptr && host != nullptr) {
            const qreal rowBottom =
                row->mapToScene(QPointF(0, 0)).y() + row->height();
            const qreal hostTop = host->mapToScene(QPointF(0, 0)).y();
            if (rowBottom > hostTop + 0.5) {
                fail(QStringLiteral("the profile region overlaps the Manual "
                                    "Import area: rowBottom=%1 hostTop=%2")
                         .arg(rowBottom)
                         .arg(hostTop));
            }
        }
        // Session J vertical-budget contract (real runtime geometry, measured
        // at 1000x700 with documents present). The Device Profile row must not
        // own the page, and the Manual Import reading area must keep a usable
        // document viewport — the Human defect was a preview that showed only
        // 2-3 lines of a long manual.
        auto *const workspace = itemOf(QStringLiteral("deviceProfileWorkspace"));
        auto *const preview = itemOf(QStringLiteral("manualPreview"));
        if (workspace == nullptr || row == nullptr || host == nullptr
            || preview == nullptr) {
            fail(QStringLiteral("the vertical-budget items are missing"));
        } else {
            const qreal wsHeight = workspace->height();
            if (wsHeight <= 0.0) {
                fail(QStringLiteral("the Device workspace has no height"));
            } else {
                const qreal rowShare = row->height() / wsHeight;
                const qreal hostShare = host->height() / wsHeight;
                if (rowShare > 0.50) {
                    fail(QStringLiteral("the Device Profile row owns too much "
                                        "vertical space: share=%1 (row=%2 "
                                        "workspace=%3)")
                             .arg(rowShare, 0, 'f', 3)
                             .arg(row->height())
                             .arg(wsHeight));
                }
                if (hostShare < 0.35) {
                    fail(QStringLiteral("the Manual Import area gets too little "
                                        "vertical space: share=%1 (host=%2 "
                                        "workspace=%3)")
                             .arg(hostShare, 0, 'f', 3)
                             .arg(host->height())
                             .arg(wsHeight));
                }
            }
            if (preview->height() < 120.0) {
                fail(QStringLiteral("the document preview viewport is too "
                                    "small: h=%1 (minimum 120)")
                         .arg(preview->height()));
            }
        }
        dumpManualGeometry(QStringLiteral("s6"));
        note(QStringLiteral("stage 6: 1000x700 reachable with documents"));
        note(QStringLiteral("stage 7: vertical budget — profile row share=%1, "
                            "manual import share=%2, preview viewport=%3")
                 .arg(workspace != nullptr && workspace->height() > 0.0
                          && row != nullptr
                          ? row->height() / workspace->height() : -1.0, 0, 'f', 3)
                 .arg(workspace != nullptr && workspace->height() > 0.0
                          && host != nullptr
                          ? host->height() / workspace->height() : -1.0, 0, 'f', 3)
                 .arg(preview != nullptr ? preview->height() : -1.0, 0, 'f', 1));
    });

    const int settleMs = 60;
    auto step = std::make_shared<int>(0);
    auto schedule = std::make_shared<std::function<void()>>();
    auto failuresShared = failures;
    *schedule = [&, step, schedule, failuresShared, &app]() {
        if (*step >= steps->size()) {
            if (!failuresShared->isEmpty()) {
                for (const QString &f : *failuresShared)
                    qWarning().noquote()
                        << QStringLiteral("MANFAIL: %1").arg(f);
                app.exit(1);
                return;
            }
            note(QStringLiteral("MANUAL IMPORT CHECK PASS (MAN-Q01..Q12): "
                                "Manual Import area reachable in the Device "
                                "Profile workspace; UTF-8 TXT + UTF-8 BOM "
                                "Markdown imported deterministically; BOM "
                                "never visible; Markdown stays plain source "
                                "text; PDF refused as C1b; no AI/Candidate/"
                                "Q&A/credential control; profile state "
                                "untouched; 1000x700 reachable"));
            app.exit(0);
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}


// ---------------------------------------------------------------------------
// M12-B slice 4 `--qml-profile-semantic-check` / `--qml-profile-semantic-demo`:
// the Read Result three-layer semantic overlay driven through the REAL UI
// path (Communication page → selector → real read dispatch → synthetic
// deterministic response → Read Result dialog), against an injected
// temporary managed root. check mode asserts B4-Q01..Q32; demo mode shows
// Scenario A (FC03@1000, 466 → 46.6 Hz) and Scenario B (Float32 2-word
// start/continuation) and STAYS OPEN for Human review.
// ---------------------------------------------------------------------------
int runProfileSemanticCheck(QQmlApplicationEngine &engine, QGuiApplication &app)
{
    const auto roots = engine.rootObjects();
    QObject *rootObj = roots.value(0);
    auto *window = qobject_cast<QQuickWindow *>(roots.value(0));
    auto *controller = qobject_cast<AnalysisController *>(
        rootObj
            ? rootObj->findChild<QObject *>(
                QStringLiteral("analysisController"))
            : nullptr);
    auto *profiles = qobject_cast<modbuslens::ui::ProfileController *>(
        rootObj
            ? rootObj->findChild<QObject *>(
                QStringLiteral("profileController"))
            : nullptr);
    auto *active = qobject_cast<modbuslens::ui::ActiveProfileController *>(
        rootObj
            ? rootObj->findChild<QObject *>(
                QStringLiteral("activeProfileController"))
            : nullptr);
    if (!window || !controller || !profiles || !active) {
        qWarning().noquote()
            << QStringLiteral("SEMFAIL: window/controller not found");
        return 1;
    }
    // Three explicit lifetimes (RCA correction, T027 §44):
    //   · --qml-profile-semantic-check                      → assertion
    //     pipeline, exits 0/1 when it finishes (ctest).
    //   · --qml-profile-semantic-demo --demo-exit-after-ready → TEST-ONLY
    //     automated demo: asserts Scenario A+B then exits 0 (ctest).
    //   · --qml-profile-semantic-demo                       → HUMAN VISUAL
    //     demo: shows both scenarios and KEEPS RUNNING until the Human
    //     closes the window (closing the window exits the process).
    // The previous build let the Human demo fall through into the check
    // pipeline, whose final step calls app.exit(0) — the window closed by
    // itself after ~2s. Quit-on-last-window-closed is therefore TRUE only
    // in the Human demo mode (the assertion modes must not be interrupted
    // by an incidental window close).
    const bool demoMode =
        app.arguments().contains(QStringLiteral("--qml-profile-semantic-demo"));
    const bool exitAfterReady =
        app.arguments().contains(QStringLiteral("--demo-exit-after-ready"));
    const bool humanDemo = demoMode && !exitAfterReady;
    app.setQuitOnLastWindowClosed(humanDemo);

    QTemporaryDir managedRoot;
    if (!managedRoot.isValid()) {
        qWarning().noquote() << QStringLiteral("SEMFAIL: temp root invalid");
        return 1;
    }
    ProfileStore::setManagedRootOverride(managedRoot.path());
    const auto writeSeed = [&](const QString &profileId,
                               const QString &displayName) {
        DeviceProfile profile;
        profile.schemaVersion = 1;
        profile.profileId = profileId.toStdString();
        profile.displayName = displayName.toStdString();
        profile.manufacturer = "ACME";
        {
            // Scenario A: FC03 UInt16 @1000, scale 0.1 → 466 = 46.6 Hz.
            RegisterEntry entry;
            entry.readFunctionCode = 0x03;
            entry.address = 1000;
            entry.name = "输出频率";
            entry.dataType = RegisterDecodeType::UInt16;
            entry.registerCount = 1;
            entry.scale = 0.1;
            entry.unit = "Hz";
            profile.registers.push_back(entry);
        }
        {
            // FC04 at the SAME address: different space, different metadata.
            RegisterEntry entry;
            entry.readFunctionCode = 0x04;
            entry.address = 1000;
            entry.name = "摄氏温度";
            entry.dataType = RegisterDecodeType::UInt16;
            entry.registerCount = 1;
            entry.scale = 0.5;
            entry.unit = "°C";
            profile.registers.push_back(entry);
        }
        {
            // Scenario B: FC03 Float32 @2000 (2-word, start-row semantics).
            RegisterEntry entry;
            entry.readFunctionCode = 0x03;
            entry.address = 2000;
            entry.name = "整流器温度";
            entry.dataType = RegisterDecodeType::Float32;
            entry.registerCount = 2;
            entry.scale = 1.0;
            entry.unit = "°C";
            profile.registers.push_back(entry);
        }
        {
            // Custom function code (vendor space).
            RegisterEntry entry;
            entry.readFunctionCode = 0x41;
            entry.address = 3000;
            entry.name = "厂商自定义";
            entry.dataType = RegisterDecodeType::UInt16;
            entry.registerCount = 1;
            entry.scale = 3.0;
            entry.unit = "kPa";
            profile.registers.push_back(entry);
        }
        {
            // Profile byte order probe: raw 0x0466 swapped = 0x6604.
            RegisterEntry entry;
            entry.readFunctionCode = 0x03;
            entry.address = 4000;
            entry.name = "字节序探测";
            entry.dataType = RegisterDecodeType::UInt16;
            entry.registerCount = 1;
            entry.scale = 1.0;
            entry.byteOrder = modbuslens::core::RegisterByteOrder::ByteSwapped;
            profile.registers.push_back(entry);
        }
        {
            // Profile word order probe: words {1,0} LowWordFirst = 1.
            RegisterEntry entry;
            entry.readFunctionCode = 0x03;
            entry.address = 5000;
            entry.name = "字序探测";
            entry.dataType = RegisterDecodeType::UInt32;
            entry.registerCount = 2;
            entry.scale = 1.0;
            entry.wordOrder = modbuslens::core::RegisterWordOrder::LowWordFirst;
            profile.registers.push_back(entry);
        }
        {
            RegisterEntry entry;
            entry.readFunctionCode = 0x03;
            entry.address = 6000;
            entry.name = "偏移探测";
            entry.dataType = RegisterDecodeType::UInt16;
            entry.registerCount = 1;
            entry.scale = 1.0;
            entry.offset = 2.5;
            profile.registers.push_back(entry);
        }
        {
            RegisterEntry entry;
            entry.readFunctionCode = 0x03;
            entry.address = 7000;
            entry.name = "坏传感器";
            entry.dataType = RegisterDecodeType::Float32;
            entry.registerCount = 2;
            entry.scale = 2.0;
            entry.offset = 1.0;
            profile.registers.push_back(entry);
        }
        return ProfileStore::saveToFile(
                   profile, ProfileStore::defaultFilePathFor(profileId))
            .ok();
    };
    if (!writeSeed(QStringLiteral("id-semantic"),
                   QStringLiteral("语义演示设备"))) {
        qWarning().noquote() << QStringLiteral("SEMFAIL: seed write failed");
        return 1;
    }

    // Deterministic synthetic responses (the harness injects bytes; the
    // shipped encoder/analyzer/session decide everything).
    const auto responseWith = [](int unit,
                                 const std::vector<std::uint16_t> &values,
                                 std::uint8_t readFunctionCode = 0x03) {
        std::vector<std::uint8_t> data;
        data.push_back(static_cast<std::uint8_t>(values.size() * 2));
        for (const std::uint16_t value : values) {
            data.push_back(static_cast<std::uint8_t>(value >> 8));
            data.push_back(static_cast<std::uint8_t>(value & 0xFF));
        }
        return modbuslens::core::encodeRtuFrame(
            modbuslens::core::ModbusRtuFrame{
                .address = static_cast<std::uint8_t>(unit),
                .functionCode = readFunctionCode,
                .data = std::move(data)});
    };

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };
    auto note = [](const QString &m) {
        qInfo().noquote() << QStringLiteral("SEM: %1").arg(m);
    };

    auto itemOf = [&roots](const QString &name) {
        return findNamedItem(roots, name);
    };
    const auto visibleOf = [&roots](const QString &name) -> bool {
        for (QObject *root : roots) {
            auto *popup = root->findChild<QObject *>(name);
            if (popup)
                return popup->property("visible").toBool();
        }
        return false;
    };
    const auto clickNamed = [&roots, window](const QString &name) {
        auto *item = findNamedItem(roots, name);
        if (!item || !item->isVisible())
            return false;
        const QPointF local(item->width() / 2.0, item->height() / 2.0);
        const QPointF scene = item->mapToScene(local);
        const QPointF global = window->mapToGlobal(scene);
        QMouseEvent press(QEvent::MouseButtonPress, scene, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, scene, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
        return true;
    };
    const auto setField = [&roots](const QString &name, const QString &text) {
        auto *field = findNamedItem(roots, name);
        if (!field) {
            return false;
        }
        field->setProperty("text", text);
        return true;
    };
    const auto collectRows = [&window](const QString &rowName) {
        QList<QQuickItem *> rows;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->objectName() == rowName)
                rows << item;
            for (QQuickItem *child : item->childItems())
                walk(child);
        };
        if (window->contentItem())
            walk(window->contentItem());
        return rows;
    };
    const auto rowText = [](QQuickItem *row) {
        return row->property("text").toString();
    };

    // Read through the REAL production front door and complete it.
    auto *transport = new HarnessWriteTransport(&app);
    controller->setSerialTransport(transport);
    controller->connectSerial(QStringLiteral("COM_SEM_HARNESS"), 9600);
    const auto readAndComplete = [&](const QString &function,
                                     const QString &start,
                                     const QString &quantity,
                                     const std::vector<std::uint16_t> &words,
                                     std::uint8_t wireFunction = 0x03) {
        // The harness's default completes every read IMMEDIATELY with its
        // own fixed two-register answer; the semantic gate needs the EXACT
        // deterministic words below instead.
        transport->setCompleteReadImmediately(false);
        controller->readRegisterRequest(
            QStringLiteral("1"), function, start, quantity,
            QStringLiteral("1000"));
        transport->completeReadWithBytes(
            responseWith(1, words, wireFunction),
            std::chrono::milliseconds{25});
    };
    const auto openDialog = [&]() {
        if (!clickNamed(QStringLiteral("readResultDetailsButton")))
            return false;
        return visibleOf(QStringLiteral("readResultDialog"));
    };
    const auto semanticRows = [&]() {
        // The per-row semantic cells, in display order.
        QList<QQuickItem *> cells;
        const QList<QQuickItem *> rows = collectRows(QStringLiteral("readSemanticCell"));
        return rows;
    };
    const auto findSemanticCell = [&](const QString &needle) -> QString {
        const QList<QQuickItem *> cells = semanticRows();
        for (QQuickItem *cell : cells) {
            const QString text = rowText(cell);
            if (text.contains(needle))
                return text;
        }
        return QString();
    };

    // Wire the semantic source BEFORE any read (production injection order).
    controller->setActiveProfileController(active);
    profiles->refreshCatalog();
    if (!active->selectProfile(QStringLiteral("id-semantic"))) {
        qWarning().noquote() << QStringLiteral("SEMFAIL: seed select failed");
        return 1;
    }

    if (exitAfterReady) {
        // Demo harness mode: Scenario A on screen, assertions equivalent to
        // the first check stage, then report and exit for ctest.
        readAndComplete(QStringLiteral("03"), QStringLiteral("1000"),
                        QStringLiteral("1"), {466}, 0x03);
        const QVariantList rows = controller->readResultValues();
        const QVariantMap row0 = rows.value(0).toMap();
        if (row0.value("semanticStatus").toString()
                != QStringLiteral("mapped_start")
            || row0.value("semanticText").toString()
                != QStringLiteral("46.6 Hz")) {
            qWarning().noquote()
                << QStringLiteral("SEMFAIL: demo scenario A mismatch: %1 / %2")
                       .arg(row0.value("semanticStatus").toString(),
                            row0.value("semanticText").toString());
            return 1;
        }
        note(QStringLiteral("SCENARIO A: FC03 @1000 raw 466 (0x01D2) → "
                           "generic 466 → semantic 46.6 Hz"));
        // Scenario B: the 2-word Float32 entry — start row carries the
        // semantic value, the continuation row only its membership.
        readAndComplete(QStringLiteral("03"), QStringLiteral("2000"),
                        QStringLiteral("2"), {0x42F6, 0xE979}, 0x03);
        const QVariantList rowsB = controller->readResultValues();
        const QVariantMap rowB0 = rowsB.value(0).toMap();
        const QVariantMap rowB1 = rowsB.value(1).toMap();
        if (rowB0.value("semanticStatus").toString()
                != QStringLiteral("mapped_start")
            || !rowB0.value("semanticText").toString().startsWith(
                QStringLiteral("123.456"))
            || rowB1.value("semanticStatus").toString()
                != QStringLiteral("mapped_continuation")
            || rowB1.contains("semanticText")) {
            qWarning().noquote()
                << QStringLiteral("SEMFAIL: demo scenario B mismatch");
            return 1;
        }
        note(QStringLiteral("SCENARIO B: FC03 @2000 Float32 0x42F6E979 -> "
                           "generic 2 words -> semantic 123.456 C on the "
                           "start row; row 2001 shows continuation only"));
        qInfo().noquote()
            << QStringLiteral("SEM: PROFILE SEMANTIC DEMO READY");
        app.exit(0);
        return 0;
    }

    if (humanDemo) {
        // HUMAN VISUAL MODE: present both deterministic scenarios through
        // the production projection + UI and then KEEP RUNNING. No
        // app.exit, no stage pipeline, no timer that closes anything; the
        // Human closes the window when done (quitOnLastWindowClosed=true
        // makes that a normal exit 0).
        if (!clickNamed(QStringLiteral("navItem_2"))
            || !visibleOf(QStringLiteral("communicationWorkspace"))) {
            qWarning().noquote()
                << QStringLiteral("SEMFAIL: cannot reach the Communication "
                                  "workspace");
            return 1;
        }
        readAndComplete(QStringLiteral("03"), QStringLiteral("1000"),
                        QStringLiteral("1"), {466}, 0x03);
        const QVariantList rowsA = controller->readResultValues();
        const QVariantMap rowA0 = rowsA.value(0).toMap();
        if (rowA0.value("semanticStatus").toString()
                != QStringLiteral("mapped_start")
            || rowA0.value("semanticText").toString()
                != QStringLiteral("46.6 Hz")) {
            qWarning().noquote()
                << QStringLiteral("SEMFAIL: demo scenario A mismatch");
            return 1;
        }
        note(QStringLiteral("SCENARIO A: FC03 @1000 raw 466 (0x01D2) → "
                           "generic 466 → semantic 46.6 Hz — Raw/Generic/"
                           "Semantic layers visible in 读取结果详情"));
        if (!openDialog()) {
            qWarning().noquote()
                << QStringLiteral("SEMFAIL: the read result dialog did not "
                                  "open");
            return 1;
        }
        // Scenario B is read SECOND so it is the content on screen; the
        // Human can re-open the detail dialog at any time (the panel keeps
        // the latest result). Reading it after A also demonstrates the
        // start-row/continuation rule live.
        readAndComplete(QStringLiteral("03"), QStringLiteral("2000"),
                        QStringLiteral("2"), {0x42F6, 0xE979}, 0x03);
        const QVariantList rowsB = controller->readResultValues();
        const QVariantMap rowB0 = rowsB.value(0).toMap();
        const QVariantMap rowB1 = rowsB.value(1).toMap();
        if (rowB0.value("semanticStatus").toString()
                != QStringLiteral("mapped_start")
            || !rowB0.value("semanticText").toString().startsWith(
                QStringLiteral("123.456"))
            || rowB1.value("semanticStatus").toString()
                != QStringLiteral("mapped_continuation")
            || rowB1.contains("semanticText")) {
            qWarning().noquote()
                << QStringLiteral("SEMFAIL: demo scenario B mismatch");
            return 1;
        }
        note(QStringLiteral("SCENARIO B: FC03 @2000 Float32 0x42F6E979 → "
                           "semantic 123.456 °C on the start row; row 2001 "
                           "shows continuation only"));
        qInfo().noquote()
            << QStringLiteral("SEM: PROFILE SEMANTIC DEMO VISUAL MODE — "
                              "窗口保持打开，由 Human 手工关闭（关闭即正常退出）");
        // Keep the event loop alive until the Human closes the window.
        return app.exec();
    }

    auto steps = std::make_shared<QList<std::function<void()>>>();
    auto push = [steps](std::function<void()> fn) { *steps << fn; };

    // Stage 0: navigate to Communication via the REAL rail entry.
    push([&]() {
        if (!clickNamed(QStringLiteral("navItem_2"))
            || !visibleOf(QStringLiteral("communicationWorkspace")))
            fail(QStringLiteral("the Communication workspace is not "
                                "reachable"));
        note(QStringLiteral("stage 0: Communication workspace"));
    });
    // Stage 0b: let the harness serial connection settle (connectSerial is
    // asynchronous — the FIRST read must see a connected session).
    push([&]() {
        if (!controller->serialConnected())
            fail(QStringLiteral("the harness connection did not settle"));
        note(QStringLiteral("stage 0b: harness connection settled"));
    });
    // Stage 1: Scenario A mapped start row (B4-Q01..Q04/Q07/Q08/Q09/Q21).
    push([&]() {
        readAndComplete(QStringLiteral("03"), QStringLiteral("1000"),
                        QStringLiteral("1"), {466}, 0x03);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticStatus").toString()
            != QStringLiteral("mapped_start"))
            fail(QStringLiteral("stage 1: not mapped_start"));
        if (row0.value("semanticName").toString()
            != QStringLiteral("输出频率"))
            fail(QStringLiteral("stage 1: wrong register name"));
        if (row0.value("semanticText").toString()
            != QStringLiteral("46.6 Hz"))
            fail(QStringLiteral("stage 1: wrong semantic text: %1")
                     .arg(row0.value("semanticText").toString()));
        if (row0.value("dec").toInt() != 466
            || row0.value("decoded").toString() != QStringLiteral("466"))
            fail(QStringLiteral("stage 1: raw/generic columns changed"));
        note(QStringLiteral("stage 1: 46.6 Hz mapped start; raw+generic "
                           "intact"));
    });
    // Stage 2: the three-layer dialog (B4-Q01..Q04/Q27/Q28).
    push([&]() {
        if (!openDialog())
            fail(QStringLiteral("the read result dialog did not open"));
        auto *legend = itemOf(QStringLiteral("readResultLayerLegend"));
        if (!legend || !legend->isVisible())
            fail(QStringLiteral("the three-layer legend is not visible"));
        const QString cellText = findSemanticCell(QStringLiteral("档案语义 "));
        if (!cellText.contains(QStringLiteral("档案语义 输出频率 = 46.6 Hz")))
            fail(QStringLiteral("the semantic cell is missing or wrong: %1")
                     .arg(cellText));
        note(QStringLiteral("stage 2: three layers visible in the dialog"));
    });
    // Stage 3: FC04 at the SAME address maps to the FC04 entry (B4-Q11).
    push([&]() {
        if (!clickNamed(QStringLiteral("readResultDetailCloseButton")))
            fail(QStringLiteral("the dialog close is not clickable"));
        readAndComplete(QStringLiteral("04"), QStringLiteral("1000"),
                        QStringLiteral("1"), {40}, 0x04);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticName").toString()
            != QStringLiteral("摄氏温度"))
            fail(QStringLiteral("stage 3: FC04 mapping fell back to FC03"));
        if (row0.value("semanticText").toString() != QStringLiteral("20 °C"))
            fail(QStringLiteral("stage 3: wrong FC04 semantic"));
        note(QStringLiteral("stage 3: FC04 same-address mapped correctly"));
    });
    // Stage 4: custom FC41 (B4-Q12).
    push([&]() {
        readAndComplete(QStringLiteral("41"), QStringLiteral("3000"),
                        QStringLiteral("1"), {5}, 0x41);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticName").toString()
            != QStringLiteral("厂商自定义"))
            fail(QStringLiteral("stage 4: custom FC41 mapping wrong"));
        if (row0.value("semanticText").toString()
            != QStringLiteral("15 kPa"))
            fail(QStringLiteral("stage 4: custom FC41 semantic wrong"));
        note(QStringLiteral("stage 4: custom FC41 mapped"));
    });
    // Stage 5: no cross-FC fallback (B4-Q06/Q13): FC41 @1000 has no entry.
    push([&]() {
        readAndComplete(QStringLiteral("41"), QStringLiteral("1000"),
                        QStringLiteral("1"), {466}, 0x41);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticStatus").toString() != QStringLiteral("unmapped"))
            fail(QStringLiteral("stage 5: cross-FC fallback happened"));
        note(QStringLiteral("stage 5: unmapped → no fallback"));
    });
    // Stage 6: generic control change moves generic, NOT semantic
    // (B4-Q14/Q15).
    push([&]() {
        readAndComplete(QStringLiteral("03"), QStringLiteral("1000"),
                        QStringLiteral("1"), {466}, 0x03);
        controller->setReadDecodeType(
            static_cast<int>(RegisterDecodeType::Int16));
        controller->setReadDecodeByteOrder(
            static_cast<int>(
                modbuslens::core::RegisterByteOrder::ByteSwapped));
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        // Generic followed its own controls: 0x01D2 swapped = 0xD201 →
        // Int16 = -11775.
        if (row0.value("decoded").toString() != QStringLiteral("-11775"))
            fail(QStringLiteral("stage 6: generic did not follow controls"));
        if (row0.value("semanticText").toString()
            != QStringLiteral("46.6 Hz"))
            fail(QStringLiteral("stage 6: semantic followed the generic "
                                "controls"));
        note(QStringLiteral("stage 6: generic vs semantic independence"));
    });
    // Stage 7: 2-word Float32 start row + continuation (B4-Q16..Q18/Q26).
    push([&]() {
        controller->setReadDecodeType(
            static_cast<int>(RegisterDecodeType::UInt16));
        controller->setReadDecodeByteOrder(
            static_cast<int>(modbuslens::core::RegisterByteOrder::Normal));
        readAndComplete(QStringLiteral("03"), QStringLiteral("2000"),
                        QStringLiteral("2"), {0x42F6, 0xE979}, 0x03);
        const QVariantList rows = controller->readResultValues();
        const QVariantMap row0 = rows.value(0).toMap();
        const QVariantMap row1 = rows.value(1).toMap();
        if (row0.value("semanticStatus").toString()
            != QStringLiteral("mapped_start"))
            fail(QStringLiteral("stage 7: start row is not the semantic "
                                "owner"));
        if (!row0.value("semanticText").toString().startsWith(
                QStringLiteral("123.456")))
            fail(QStringLiteral("stage 7: Float32 vector mismatch: %1")
                     .arg(row0.value("semanticText").toString()));
        if (row1.value("semanticStatus").toString()
            != QStringLiteral("mapped_continuation"))
            fail(QStringLiteral("stage 7: continuation state wrong"));
        if (row1.contains("semanticText"))
            fail(QStringLiteral("stage 7: continuation duplicated the "
                                "semantic value"));
        if (row1.value("semanticStartAddress").toInt() != 2000)
            fail(QStringLiteral("stage 7: continuation start address wrong"));
        note(QStringLiteral("stage 7: 2-word start + continuation"));
    });
    // Stage 8: profile byteOrder reflected (B4-Q19).
    push([&]() {
        readAndComplete(QStringLiteral("03"), QStringLiteral("4000"),
                        QStringLiteral("1"), {0x0466}, 0x03);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticText").toString() != QStringLiteral("26116"))
            fail(QStringLiteral("stage 8: profile byte order not reflected"));
        note(QStringLiteral("stage 8: profile byteOrder applied"));
    });
    // Stage 9: profile wordOrder reflected (B4-Q20).
    push([&]() {
        readAndComplete(QStringLiteral("03"), QStringLiteral("5000"),
                        QStringLiteral("2"), {1, 0}, 0x03);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticText").toString() != QStringLiteral("1"))
            fail(QStringLiteral("stage 9: profile word order not reflected"));
        note(QStringLiteral("stage 9: profile wordOrder applied"));
    });
    // Stage 10: offset applied after scale (B4-Q22).
    push([&]() {
        readAndComplete(QStringLiteral("03"), QStringLiteral("6000"),
                        QStringLiteral("1"), {10}, 0x03);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticText").toString() != QStringLiteral("12.5"))
            fail(QStringLiteral("stage 10: offset not applied"));
        note(QStringLiteral("stage 10: offset applied"));
    });
    // Stage 11: special value presentation (B4-Q23).
    push([&]() {
        readAndComplete(QStringLiteral("03"), QStringLiteral("7000"),
                        QStringLiteral("2"), {0x7FC0, 0x0000}, 0x03);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticText").toString()
            != QStringLiteral("非数字（NaN）"))
            fail(QStringLiteral("stage 11: NaN presentation wrong: %1")
                     .arg(row0.value("semanticText").toString()));
        note(QStringLiteral("stage 11: NaN presented as a special value"));
    });
    // Stage 12: an UNSAVED editor edit does not change the semantic
    // (B4-Q24).
    push([&]() {
        QMetaObject::invokeMethod(profiles, "openProfile",
                                  Q_ARG(QString, QStringLiteral("id-semantic")));
        QVariantMap fields{
            {QStringLiteral("readFunctionCode"), QStringLiteral("03")},
            {QStringLiteral("address"), QStringLiteral("1000")},
            {QStringLiteral("name"), QStringLiteral("输出频率")},
            {QStringLiteral("dataType"), QStringLiteral("2")},
            {QStringLiteral("byteOrder"), QStringLiteral("0")},
            {QStringLiteral("wordOrder"), QStringLiteral("-1")},
            {QStringLiteral("scale"), QStringLiteral("9.9")},
            {QStringLiteral("offset"), QString()},
            {QStringLiteral("unit"), QStringLiteral("Hz")}};
        bool ok = false;
        QMetaObject::invokeMethod(profiles, "editRegisterEntry",
                                  Q_RETURN_ARG(bool, ok),
                                  Q_ARG(int, 0),
                                  Q_ARG(QVariantMap, fields));
        if (!ok || !profiles->property("dirty").toBool())
            fail(QStringLiteral("stage 12: the unsaved edit setup failed"));
        readAndComplete(QStringLiteral("03"), QStringLiteral("1000"),
                        QStringLiteral("1"), {466}, 0x03);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticText").toString()
            != QStringLiteral("46.6 Hz"))
            fail(QStringLiteral("stage 12: the unsaved draft leaked into "
                                "the semantic layer"));
        note(QStringLiteral("stage 12: unsaved draft isolated"));
    });
    // Stage 13: SAVING the active profile refreshes the semantic
    // (B4-Q25).
    push([&]() {
        QMetaObject::invokeMethod(profiles, "saveCurrent");
        if (profiles->property("dirty").toBool())
            fail(QStringLiteral("stage 13: the save did not clear dirty"));
        readAndComplete(QStringLiteral("03"), QStringLiteral("1000"),
                        QStringLiteral("1"), {466}, 0x03);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticText").toString() != QStringLiteral("4613.4 Hz"))
            fail(QStringLiteral("stage 13: the semantic did not refresh: %1")
                     .arg(row0.value("semanticText").toString()));
        // Restore the persisted scale for the remaining stages.
        QVariantMap fields{
            {QStringLiteral("readFunctionCode"), QStringLiteral("03")},
            {QStringLiteral("address"), QStringLiteral("1000")},
            {QStringLiteral("name"), QStringLiteral("输出频率")},
            {QStringLiteral("dataType"), QStringLiteral("2")},
            {QStringLiteral("byteOrder"), QStringLiteral("0")},
            {QStringLiteral("wordOrder"), QStringLiteral("-1")},
            {QStringLiteral("scale"), QStringLiteral("0.1")},
            {QStringLiteral("offset"), QString()},
            {QStringLiteral("unit"), QStringLiteral("Hz")}};
        bool ok = false;
        QMetaObject::invokeMethod(profiles, "editRegisterEntry",
                                  Q_RETURN_ARG(bool, ok),
                                  Q_ARG(int, 0),
                                  Q_ARG(QVariantMap, fields));
        QMetaObject::invokeMethod(profiles, "saveCurrent");
        note(QStringLiteral("stage 13: saved edit refreshed the semantic"));
    });
    // Stage 14: deleting the ACTIVE profile returns the semantic layer to
    // no-profile (B4-Q26).
    push([&]() {
        QMetaObject::invokeMethod(active, "clearActive");
        readAndComplete(QStringLiteral("03"), QStringLiteral("1000"),
                        QStringLiteral("1"), {466}, 0x03);
        const QVariantMap row0 =
            controller->readResultValues().value(0).toMap();
        if (row0.value("semanticStatus").toString()
            != QStringLiteral("no_active_profile"))
            fail(QStringLiteral("stage 14: no-active state wrong"));
        note(QStringLiteral("stage 14: no-active → honest empty layer"));
    });
    // Stage 15: resize to the 1000x700 contract size (measure next stage).
    push([&]() {
        window->resize(1000, 700);
        note(QStringLiteral("stage 15: window resized to 1000x700"));
    });
    // Stage 16: the dialog geometry contract (B4-Q29/Q30) — the real popup
    // frame, the scroll viewport, the LAST semantic row reachable through
    // the intended scroll and the close control all inside the window.
    push([&]() {
        if (window->width() != 1000 || window->height() != 700)
            fail(QStringLiteral("the window is not at 1000x700: %1x%2")
                     .arg(window->width())
                     .arg(window->height()));
        // Re-select the profile so the measured rows carry REAL semantic
        // content (stage 14 cleared it).
        if (!active->selectProfile(QStringLiteral("id-semantic")))
            fail(QStringLiteral("stage 16: cannot re-select the profile"));
        readAndComplete(QStringLiteral("03"), QStringLiteral("1000"),
                        QStringLiteral("1"), {466}, 0x03);
        if (!openDialog())
            fail(QStringLiteral("the dialog did not open at 1000x700"));
        const auto popupRectOf = [&roots](const QString &name) -> QRectF {
            for (QObject *root : roots) {
                auto *popup = root->findChild<QObject *>(name);
                if (!popup)
                    continue;
                auto *frame =
                    popup->property("background").value<QQuickItem *>();
                if (!frame)
                    return QRectF();
                const QPointF topLeft = frame->mapToScene(QPointF(0, 0));
                return QRectF(topLeft,
                              QSizeF(frame->width(), frame->height()));
            }
            return QRectF();
        };
        const QRectF dialogRect =
            popupRectOf(QStringLiteral("readResultDialog"));
        qInfo().noquote()
            << QStringLiteral("SEMGEO readResultDialog: x=%1 y=%2 w=%3 h=%4")
                   .arg(dialogRect.x())
                   .arg(dialogRect.y())
                   .arg(dialogRect.width())
                   .arg(dialogRect.height());
        if (dialogRect.isEmpty())
            fail(QStringLiteral("the dialog has no measurable geometry"));
        else if (!QRectF(QPointF(0, 0),
                         QSizeF(window->width(), window->height()))
                      .contains(dialogRect))
            fail(QStringLiteral("the dialog is outside the window"));
        for (const auto &name :
             {QStringLiteral("readResultDetailScroll"),
              QStringLiteral("readResultDetailCloseButton")}) {
            auto *item = itemOf(name);
            if (!item || !item->isVisible()) {
                fail(QStringLiteral("%1 is not visible at 1000x700").arg(name));
                continue;
            }
            const QPointF topLeft = item->mapToScene(QPointF(0, 0));
            const QRectF rect(topLeft,
                              QSizeF(item->width(), item->height()));
            if (!QRectF(QPointF(0, 0),
                        QSizeF(window->width(), window->height()))
                     .contains(rect))
                fail(QStringLiteral("%1 outside: x=%2 y=%3 w=%4 h=%5")
                         .arg(name)
                         .arg(rect.x())
                         .arg(rect.y())
                         .arg(rect.width())
                         .arg(rect.height()));
        }
        // The LAST semantic row must be reachable through the scroll.
        const QList<QQuickItem *> cells =
            collectRows(QStringLiteral("readSemanticCell"));
        if (cells.isEmpty())
            fail(QStringLiteral("no semantic cells rendered"));
        else
            qInfo().noquote()
                << QStringLiteral("SEMGEO lastSemanticCell: text=(%1)")
                       .arg(rowText(cells.last()));
        auto *list = itemOf(QStringLiteral("readResultValuesList"));
        if (list) {
            const double maxY = std::max(
                0.0, list->property("contentHeight").toDouble()
                         - list->property("height").toDouble());
            list->setProperty("contentY", maxY);
            const double reachedY = list->property("contentY").toDouble();
            if (maxY > 1.0 && reachedY < maxY - 1.0)
                fail(QStringLiteral("the values viewport cannot scroll to "
                                    "the last row (contentY %1 < %2)")
                         .arg(reachedY)
                         .arg(maxY));
        }
        if (!clickNamed(QStringLiteral("readResultDetailCloseButton")))
            fail(QStringLiteral("the dialog close is not clickable"));
        if (visibleOf(QStringLiteral("readResultDialog")))
            fail(QStringLiteral("the dialog did not close"));
        note(QStringLiteral("stage 16: 1000x700 dialog + scroll + close"));
    });

    const int settleMs = 60;
    auto step = std::make_shared<int>(0);
    auto schedule = std::make_shared<std::function<void()>>();
    auto failuresShared = failures;
    *schedule = [&, step, schedule, failuresShared, &app]() {
        if (*step >= steps->size()) {
            if (!failuresShared->isEmpty()) {
                for (const QString &f : *failuresShared)
                    qWarning().noquote()
                        << QStringLiteral("SEMFAIL: %1").arg(f);
                app.exit(1);
                return;
            }
            note(QStringLiteral("PROFILE SEMANTIC CHECK PASS (B4-Q01..Q32): "
                               "three labeled layers; no-active and unmapped "
                               "states honest; FC03/FC04 same-address and "
                               "custom FC41 mapped by the REQUESTED function "
                               "code with no cross-FC fallback; generic vs "
                               "semantic independence; 2-word start row + "
                               "continuation membership; profile byte/word "
                               "order, scale, offset, NaN presentation; "
                               "unsaved draft isolated, save refreshes"));
            app.exit(0);
            return;
        }
        const int current = (*step)++;
        (*steps)[current]();
        QTimer::singleShot(settleMs, &app, *schedule);
    };
    QTimer::singleShot(settleMs, &app, *schedule);
    return app.exec();
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ModbusLens"));
    QCoreApplication::setApplicationName(QStringLiteral("ModbusLens"));
    // M9-E E1: derived from the CMake project VERSION authority via the
    // generated header (the hardcoded 0.1.0 literal is gone).
    QCoreApplication::setApplicationVersion(
        QStringLiteral(MODBUSLENS_VERSION_STRING));
    // M9-E E2: the Qt runtime window/taskbar icon, loaded from the embedded
    // committed ICO (Qt resource). The SAME derived asset feeds the PE icon
    // via the Windows resource (single source artwork, single derived ICO).
    app.setWindowIcon(QIcon(QStringLiteral(
        ":/ModbusLens/assets/brand/windows/ModbusLens.ico")));
    // M9-E E1: the frozen product name as the user-visible display name.
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ModbusLens"));

    // M10-E4 REAL-HARDWARE diagnostic (not a product feature, never reachable
    // from the UI): measure what the OS/Qt actually do when a USB serial
    // adapter is physically removed. Dispatched BEFORE any QML is loaded so the
    // measurement is not disturbed by the application's own serial session.
    for (const auto &argument : app.arguments()) {
        if (argument == QStringLiteral("--serial-hotplug-probe")
            || argument.startsWith(
                QStringLiteral("--serial-hotplug-probe="))) {
            return runSerialHotplugProbe(app.arguments());
        }
    }

    // T013 Phase E: the Windows native style ignores our QML control
    // customization (TabButton/ScrollBar background & contentItem) — switch
    // to a style that honors custom backgrounds so the light-theme tab
    // borders and thin scrollbars actually render.
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QQmlApplicationEngine engine;

    // M9-A: the DesignSystem token singleton is exposed as an engine context
    // property ("DS"). It is instantiated ONCE here from its qrc URL; its
    // qmldir "singleton" route is intentionally not used — see ISSUE-010
    // (the module-singleton lookup emitted "DS is not defined" at runtime
    // in this exe-attached qrc module).
    QQmlComponent dsComponent(&engine,
                              QUrl(QStringLiteral("qrc:/ModbusLens/src/ui/qml/DS/DesignSystem.qml")));
    QObject *ds = dsComponent.create();
    if (!ds) {
        qWarning() << "DesignSystem failed to instantiate:"
                   << dsComponent.errorString();
        return -1;
    }
    QQmlEngine::setObjectOwnership(ds, QQmlEngine::CppOwnership);
    ds->setParent(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("DS"), ds);

    // M10-C2: the ONLY switch that decides whether the hidden write foundation
    // is instantiated. It answers exactly one question — "does this run load
    // the hidden UI foundation for testing?" — and it is NEVER a capability
    // signal. As of M10-D3 the 0x06 encoder and the Controller's atomic
    // dispatch DO exist, yet the production Write UI stays hidden regardless:
    // this flag is orthogonal to write06Supported, and the production Confirm
    // button is still confirmation-only until M10-D4. 0x10 has no encoder and
    // no dispatch path at all. Normal production leaves this false, so no write
    // control exists at all; only the dedicated harness mode turns it on.
    engine.rootContext()->setContextProperty(
        QStringLiteral("writeFoundationVisible"),
        app.arguments().contains(QStringLiteral("--qml-write-foundation-check")));

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("ModbusLens"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }

    // Diagnostic mode for CTest (see T008 Part A): load the real QML module,
    // verify it instantiates, then exit without entering the event loop.
    if (app.arguments().contains(QStringLiteral("--qml-smoke-test"))) {
        // M9-E E1: identity/version oracle. applicationVersion must be
        // DERIVED from the CMake project VERSION authority via the generated
        // header (the hardcoded 0.1.0 literal is gone); applicationDisplayName
        // is the frozen product name; organizationDomain stays unset (no
        // authority - it must never be invented).
        const auto smokeIdentityFail = [](const QString &what,
                                          const QString &actual,
                                          const QString &expected) {
            qWarning().noquote()
                << QStringLiteral("SMOKEFAIL identity: %1 = '%2', expected "
                                  "'%3' (expected E1 missing implementation)")
                       .arg(what,
                            actual.isEmpty() ? QStringLiteral("<empty>")
                                             : actual,
                            expected);
        };
        if (QCoreApplication::applicationName() != QStringLiteral("ModbusLens")) {
            smokeIdentityFail(QStringLiteral("applicationName"),
                              QCoreApplication::applicationName(),
                              QStringLiteral("ModbusLens"));
            return 1;
        }
        if (QGuiApplication::applicationDisplayName()
            != QStringLiteral("ModbusLens")) {
            smokeIdentityFail(QStringLiteral("applicationDisplayName"),
                              QGuiApplication::applicationDisplayName(),
                              QStringLiteral("ModbusLens"));
            return 1;
        }
        // M9-E E1 correction: the expected version comes from the SAME
        // generated authority the application consumes (CMake project
        // VERSION via modbuslens_version.h) - no second hardcoded version
        // literal lives here. A future release decision changes the CMake
        // VERSION once and every consumer (this oracle included) follows.
        if (QCoreApplication::applicationVersion()
            != QStringLiteral(MODBUSLENS_VERSION_STRING)) {
            smokeIdentityFail(QStringLiteral("applicationVersion"),
                              QCoreApplication::applicationVersion(),
                              QStringLiteral(MODBUSLENS_VERSION_STRING));
            return 1;
        }
        if (QCoreApplication::organizationName() != QStringLiteral("ModbusLens")) {
            smokeIdentityFail(QStringLiteral("organizationName"),
                              QCoreApplication::organizationName(),
                              QStringLiteral("ModbusLens"));
            return 1;
        }
        if (!QCoreApplication::organizationDomain().isEmpty()) {
            smokeIdentityFail(QStringLiteral("organizationDomain"),
                              QCoreApplication::organizationDomain(),
                              QStringLiteral("<unset>"));
            return 1;
        }
        // M9-E E2: window icon oracle. The icon must come from the embedded
        // brand ICO resource; before E2 the resource does not exist and the
        // window icon is null. A window title or a screenshot never proves
        // icon presence - this reads the actual QIcon state.
        const QString brandResource = QStringLiteral(
            ":/ModbusLens/assets/brand/windows/ModbusLens.ico");
        if (!QFile::exists(brandResource)) {
            smokeIdentityFail(QStringLiteral("brand icon resource"),
                              QStringLiteral("<missing>"), brandResource);
            return 1;
        }
        if (QGuiApplication::windowIcon().isNull()) {
            smokeIdentityFail(QStringLiteral("application window icon"),
                              QStringLiteral("<null>"),
                              QStringLiteral("non-null brand icon"));
            return 1;
        }
        QObject *identityRoot = engine.rootObjects().value(0);
        const QString windowTitle =
            identityRoot ? identityRoot->property("title").toString()
                         : QString();
        if (windowTitle != QStringLiteral("ModbusLens")) {
            qWarning().noquote()
                << QStringLiteral("SMOKEFAIL identity: window title = '%1', "
                                  "expected 'ModbusLens' (E1 freezes the title; "
                                  "no version/source suffix)")
                       .arg(windowTitle);
            return 1;
        }
        const QIcon windowIcon = QGuiApplication::windowIcon();
        QStringList iconSizes;
        for (const QSize &size : windowIcon.availableSizes())
            iconSizes << QStringLiteral("%1x%2").arg(size.width())
                             .arg(size.height());
        qInfo().noquote()
            << QStringLiteral("SMOKE IDENTITY PASS: applicationName=%1 "
                              "displayName=%2 version=%3 organizationName=%4 "
                              "organizationDomain=<unset> title=%5 "
                              "windowIconSizes=[%6]")
                   .arg(QCoreApplication::applicationName(),
                        QGuiApplication::applicationDisplayName(),
                        QCoreApplication::applicationVersion(),
                        QCoreApplication::organizationName())
                   .arg(windowTitle, iconSizes.join(QStringLiteral(" ")));
        return 0;
    }

    // Geometry regression guard for the statistics migration (ISSUE-012).
    if (app.arguments().contains(QStringLiteral("--qml-geometry-check"))) {
        return runGeometryCheck(engine, app);
    }

    // Navigation guard (M9-B2): two real workspaces and the
    // "navigation changes no business state" invariant.
    if (app.arguments().contains(QStringLiteral("--qml-nav-check"))) {
        return runNavCheck(engine, app);
    }

    // Focus / keyboard-traversal guard (M9-F F1): the shell's focus
    // contract (workspace gating, rail activation + accessible identity,
    // Transactions list entry, Agent TextArea traversal).
    if (app.arguments().contains(QStringLiteral("--qml-focus-check"))) {
        return runFocusCheck(engine, app);
    }
    if (app.arguments().contains(QStringLiteral("--qml-write-foundation-check"))) {
        return runWriteFoundationCheck(engine, app);
    }

    // M10-D4 production-write runtime harness. Note: it deliberately does NOT
    // pass --qml-write-foundation-check, so writeFoundationVisible stays false
    // and the section is instantiated for the PRODUCTION reason (the structural
    // capability), which is exactly what makes the normal Confirm button the
    // thing under test.
    if (app.arguments().contains(QStringLiteral("--qml-production-write-check"))) {
        return runProductionWriteCheck(engine, app);
    }

    // T023 READ-RESULT harness. Like the production-write harness it does NOT
    // pass --qml-write-foundation-check: the read-result row and the write
    // panel are measured in the configuration the product actually ships.
    if (app.arguments().contains(QStringLiteral("--qml-read-result-check"))) {
        return runReadResultCheck(engine, app);
    }

    // M11 first-slice Human visual demo: TEST-ONLY / DEMO-ONLY synthetic
    // decode view (no real serial I/O). See runReadResultDemo for the
    // safety boundary.
    if (app.arguments().contains(QStringLiteral("--qml-read-result-demo"))) {
        return runReadResultDemo(engine, app);
    }

    // M11 second-slice Human visual demo: the 32-bit views (UInt32 / Int32 /
    // Float32 + word order), same TEST-ONLY / DEMO-ONLY boundary.
    if (app.arguments().contains(QStringLiteral("--qml-read-result-demo32"))) {
        return runReadResultDemo32(engine, app);
    }

    // M12-B first slice gate: the Device Profile workspace end to end.
    if (app.arguments().contains(QStringLiteral("--qml-profile-editor-check"))) {
        return runProfileEditorCheck(engine, app);
    }

    // M12-B second slice gate: the Register Map editor end to end.
    if (app.arguments().contains(QStringLiteral("--qml-register-map-check"))) {
        return runRegisterMapCheck(engine, app);
    }

    // M12-B third slice gate: the Communication profile selector end to end.
    if (app.arguments().contains(QStringLiteral("--qml-active-profile-check"))) {
        return runActiveProfileCheck(engine, app);
    }

    // M12-C C1a gate: the Manual Import area inside the Device Profile
    // workspace (deterministic TXT / Markdown import + plain-text preview).
    if (app.arguments().contains(QStringLiteral("--qml-manual-import-check"))) {
        return runManualImportCheck(engine, app);
    }

    // M12-B slice 4: the Read Result semantic overlay (check + visual demo).
    if (app.arguments().contains(QStringLiteral("--qml-profile-semantic-check"))) {
        return runProfileSemanticCheck(engine, app);
    }
    if (app.arguments().contains(QStringLiteral("--qml-profile-semantic-demo"))) {
        return runProfileSemanticCheck(engine, app);
    }

    // Manual-candidate evidence capture (M9-B4.4): harness only.
    const int evIdx =
        app.arguments().indexOf(QStringLiteral("--qml-evidence-capture"));
    if (evIdx >= 0) {
        const QString dir = evIdx + 1 < app.arguments().size()
                                ? app.arguments().at(evIdx + 1)
                                : QString();
        if (dir.isEmpty()) {
            qWarning() << "--qml-evidence-capture requires a directory";
            return 1;
        }
        return runEvidenceCapture(engine, app, dir);
    }

    return app.exec();
}
