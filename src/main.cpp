#include <QAbstractItemModel>
#include <QGuiApplication>
#include <QDir>
#include <QImage>
#include <QFile>
#include <QIcon>
#include <QKeyEvent>

// M9-E E1: generated version interface (configure_file output, build tree only).
#include "modbuslens_version.h"
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QCoreApplication>
#include <QQuickStyle>
#include <QStringList>
#include <QTimer>

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
            struct DraftSpec {
                const char *objectName;
                const char *property;
                int value;
            };
            const DraftSpec specs[] = {
                { "commSlaveSpin", "value", 7 },
                { "commStartSpin", "value", 10 },
                { "commQuantitySpin", "value", 3 },
                { "commTimeoutSpin", "value", 2500 },
                { "commBaudCombo", "currentIndex", 2 },
            };
            for (const auto &spec : specs) {
                auto *item = findNamedItem(
                    roots, QString::fromLatin1(spec.objectName));
                if (!item) {
                    fail(QStringLiteral("NAVFAIL draft control %1 not found")
                             .arg(QLatin1String(spec.objectName)));
                    continue;
                }
                item->setProperty(spec.property, spec.value);
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
                                         : QStringLiteral("value");
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
                const QString prop = (it.key() == QStringLiteral("commBaudCombo")
                                      || it.key() == QStringLiteral("commPortCombo"))
                                         ? QStringLiteral("currentIndex")
                                         : QStringLiteral("value");
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
            else if (device->property("enabled").toBool())
                fail(QStringLiteral("NAVFAIL scenario S2: navItem_5 (Device) "
                                    "must stay disabled"));
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
                                  "Diagnosis(4) Device(5, disabled)");
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
                                    "activation path is not invokable — the "
                                    "guard would be vacuous"));
            if (rail->property("currentWorkspaceIndex").toInt() != before)
                fail(QStringLiteral("NAVFAIL scenario S7: the Device activation "
                                    "changed currentWorkspaceIndex (%1 -> %2)")
                         .arg(before)
                         .arg(rail->property("currentWorkspaceIndex").toInt()));
            endScenario(QStringLiteral("S"));
            qInfo().noquote()
                << QStringLiteral("NAV [scenario S]: legacy retirement verified "
                                  "(startup=Transactions at 0 / compact rail / "
                                  "no legacy runtime objects / child 0 owns the "
                                  "presentation / five-workspace round trip "
                                  "neutral / Device activation inert)");
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
// M9-F F1 `--qml-focus-check`: keyboard focus / traversal guard for the
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
        // SpinBox: the STYLE draws the indication; the machine asserts the
        // focus state that drives it (zero source diff on this type).
        selectWorkspace(2);
        anchorFocus();
    });
    push([&]() {
        bool reached = false;
        for (int i = 1; i <= 16 && !reached; ++i) {
            tab(true);
            auto *spin = itemOf(QStringLiteral("commSlaveSpin"));
            if (spin && propBool(spin, "activeFocus"))
                reached = true;
        }
        if (!reached)
            fail(QStringLiteral("FOCUSFAIL FK: SpinBox never reported "
                                "activeFocus during the walk"));
        else
            note(QStringLiteral("FOCUS [FK] PASS: SpinBox activeFocus=true "
                                "(style-owned indication, zero source diff)"));
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

    // FF: the disabled Device entry stays unreachable and inert.
    push([&]() {
        auto *device = itemOf(QStringLiteral("navItem_5"));
        if (!device) {
            fail(QStringLiteral("FOCUSFAIL FF: navItem_5 not found"));
            return;
        }
        if (device->property("enabled").toBool())
            fail(QStringLiteral("FOCUSFAIL FF: Device entry is enabled"));
        if (device->property("activeFocusOnTab").toBool())
            fail(QStringLiteral("FOCUSFAIL FF: Device entry is a Tab stop"));
        selectWorkspace(2);
        anchorFocus();
        QStringList chain;
        QList<int> pages;
        walkTabs(16, true, chain, pages);
        for (int i = 0; i < chain.size(); ++i) {
            if (chain.at(i) == QStringLiteral("navItem_5"))
                fail(QStringLiteral("FOCUSFAIL FF: Device entry appeared in the "
                                    "Tab chain at press %1").arg(i + 1));
        }
        const int before = railIndex();
        clickItemPoint(device);
        if (railIndex() != before)
            fail(QStringLiteral("FOCUSFAIL FF: clicking Device changed the "
                                "workspace index %1 -> %2")
                     .arg(before).arg(railIndex()));
        else
            note(QStringLiteral("FOCUS [FF] PASS: Device disabled, not a Tab "
                                "stop, inert on click (index stays %1)").arg(before));
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
                           "four-key regression)";
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