#include <QGuiApplication>
#include <QDir>
#include <QImage>
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

// M9-B3: which workspace page is currently visible (drives which page's
// geometry gets asserted — hidden pages are never asserted).
enum class ActivePage { Legacy, Dashboard, Communication };

ActivePage activePage(const QList<QObject *> &roots)
{
    if (auto *page = findNamedItem(roots, QStringLiteral("communicationWorkspace"));
        page && page->isVisible())
        return ActivePage::Communication;
    if (auto *page = findNamedItem(roots, QStringLiteral("dashboardWorkspace"));
        page && page->isVisible())
        return ActivePage::Dashboard;
    return ActivePage::Legacy;
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
    const bool legacyVisible = (page == ActivePage::Legacy);
    const bool statsVisible = (page != ActivePage::Communication);
    QString suffix = (page == ActivePage::Legacy) ? QStringLiteral("legacy")
                                                  : QStringLiteral("dashboard");
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
    row1 = findNamedItem(roots, suffixed(QStringLiteral("statisticsRow1")));
    auto *row2 = findNamedItem(roots, suffixed(QStringLiteral("statisticsRow2")));
    const auto row1Count = row1 ? row1->childItems().size() : -1;
    const auto row2Count = row2 ? row2->childItems().size() : -1;

    auto *header = findNamedItem(roots, suffixed(QStringLiteral("statisticsHeader")));
    auto *panel = findNamedItem(roots, suffixed(QStringLiteral("statisticsPanel")));
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

    } // end statsVisible

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

    auto *diag = findNamedItem(roots, QStringLiteral("diagnosisWorkspace"));
    if (legacyVisible && panel && diag
        && diag->y() + 1e-6 < panel->y() + panel->height())
        fail(QStringLiteral("diagnosisWorkspace y=%1 invades statisticsPanel "
                            "(y=%2 h=%3)")
                 .arg(diag->y())
                 .arg(panel->y())
                 .arg(panel->height()));

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
    const int legacyIndex =
        rootObj->property("workspaceLegacyIndex").toInt();
    const int dashboardIndex =
        rootObj->property("workspaceDashboardIndex").toInt();

    const int index = rail->property("currentWorkspaceIndex").toInt();
    if (index < 0 || index > 5)
        fail(QStringLiteral("NAV currentWorkspaceIndex %1 out of range 0..5")
                 .arg(index));
    const int communicationIndex =
        rootObj->property("workspaceCommunicationIndex").toInt();
    if (index != legacyIndex && index != dashboardIndex
        && index != communicationIndex)
        fail(QStringLiteral("NAV currentWorkspaceIndex %1 is not one of the "
                            "real workspaces (legacy=%2 dashboard=%3 "
                            "communication=%4)")
                 .arg(index)
                 .arg(legacyIndex)
                 .arg(dashboardIndex)
                 .arg(communicationIndex));

    // Visibility must follow the selection (page-independent form: this
    // guard runs at EVERY workspace now).
    auto *legacy = findNamedItem(roots, QStringLiteral("legacyWorkspace"));
    auto *dashboard = findNamedItem(roots, QStringLiteral("dashboardWorkspace"));
    auto *communication =
        findNamedItem(roots, QStringLiteral("communicationWorkspace"));
    if (!legacy)
        fail(QStringLiteral("NAV legacyWorkspace not found"));
    else if (legacy->isVisible() != (index == legacyIndex))
        fail(QStringLiteral("NAV legacyWorkspace visibility (%1) does not "
                            "follow the selection %2")
                 .arg(legacy->isVisible())
                 .arg(index));
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

    auto *item0 = findNamedItem(roots, QStringLiteral("navItem_0"));
    auto *item1 = findNamedItem(roots, QStringLiteral("navItem_1"));
    if (!item0 || !item1) {
        fail(QStringLiteral("NAV navItem_0/navItem_1 not found"));
        return failures;
    }

    // M9-B3 matrix: 工作台 / 总览 / 通信 are REAL workspaces (enabled); the
    // remaining three stay disabled until their own extraction steps.
    if (!item0->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_0 (workbench) must be enabled"));
    if (!item1->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_1 (dashboard) must be enabled"));
    auto *item2 = findNamedItem(roots, QStringLiteral("navItem_2"));
    if (!item2 || !item2->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_2 (communication) must be enabled"));
    for (int i = 3; i <= 5; ++i) {
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
    QString suffix = (page == ActivePage::Legacy) ? QStringLiteral("legacy")
                                                  : QStringLiteral("dashboard");
    auto suffixed = [&suffix](const QString &base) {
        return base + QLatin1Char('_') + suffix;
    };

    QStringList names = {
        QStringLiteral("appBar"),          QStringLiteral("navigationRail"),
        QStringLiteral("workspaceHost"),   QStringLiteral("legacyWorkspace"),
        QStringLiteral("dashboardWorkspace"),
        QStringLiteral("communicationWorkspace"),
    };
    if (statsVisible) {
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
    } else {
        names << QStringLiteral("communicationContentLayout")
              << QStringLiteral("communicationHeader")
              << QStringLiteral("communicationConnectionHeader")
              << QStringLiteral("communicationConnectionSection")
              << QStringLiteral("communicationRequestHeader")
              << QStringLiteral("communicationRequestSection")
              << QStringLiteral("communicationSerialError");
    }
    names << QStringLiteral("diagnosisWorkspace");
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
        int pageIndex; // 0 legacy / 1 dashboard / 2 communication
        bool resizeToMin;
        QString tag;
        QString label;
    };
    const QVector<MeasureStep> steps = {
        { 0, false, QStringLiteral("m9b3-legacy-1024x720"),
          QStringLiteral("DEFAULT legacy") },
        { 1, false, QStringLiteral("m9b3-dashboard-1024x720"),
          QStringLiteral("DEFAULT dashboard") },
        { 2, false, QStringLiteral("m9b3-communication-1024x720"),
          QStringLiteral("DEFAULT communication") },
        { 2, true, QStringLiteral("m9b3-communication-1000x700"),
          QStringLiteral("MIN 1000x700 communication") },
        { 1, false, QStringLiteral("m9b3-dashboard-1000x700"),
          QStringLiteral("MIN 1000x700 dashboard") },
        { 0, false, QStringLiteral("m9b3-legacy-1000x700"),
          QStringLiteral("MIN 1000x700 legacy") },
    };

    const int settleMs = 100;
    const int maxAttempts = 5;
    auto stepIndex = std::make_shared<int>(0);
    auto currentPageIndex = std::make_shared<int>(0);
    auto failures = std::make_shared<QStringList>();
    auto attempt = std::make_shared<int>(0);

    auto finish = [&app](const QStringList &fails) {
        if (fails.isEmpty())
            qInfo() << "GEOMETRY CHECK PASS"
                       "(legacy + dashboard + communication at default size "
                       "and 1000x700)";
        else
            for (const QString &f : fails)
                qWarning().noquote() << "GEOFAIL:" << f;
        app.exit(fails.isEmpty() ? 0 : 1);
    };

    auto switchWorkspace = [&](int pageIndex, QStringList &fails) {
        QObject *rootObj = roots.value(0);
        const char *key = (pageIndex == 0) ? "workspaceLegacyIndex"
                        : (pageIndex == 1) ? "workspaceDashboardIndex"
                                           : "workspaceCommunicationIndex";
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

    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, schedule, failures, attempt, stepIndex, currentPageIndex,
                 transitionDone, pendingPre]() {
        const MeasureStep &step = steps.at(*stepIndex);
        QStringList pre;
        if (!*transitionDone) {
            const bool needSwitch = (step.pageIndex != *currentPageIndex);
            if (needSwitch || step.resizeToMin) {
                if (needSwitch) {
                    switchWorkspace(step.pageIndex, pre);
                    *currentPageIndex = step.pageIndex;
                }
                if (step.resizeToMin && window)
                    window->resize(1000, 700);
                *transitionDone = true;
                *pendingPre = pre;  // measured on the re-entry below
                QTimer::singleShot(settleMs, &app, *schedule);
                return;
            }
            *transitionDone = true;
        }
        pre = *pendingPre;

        qInfo().noquote() << dumpGeometryTable(roots, step.label);
        *failures = pre;
        *failures += runGeometryAssertions(roots, step.label);
        *failures += runShellNavAssertions(roots, step.label);
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
                             QQuickItem **legacyPageOut,
                             QQuickItem **dashboardPageOut)
{
    QStringList failures;
    auto fail = [&failures, &contextLabel](const QString &message) {
        failures << contextLabel + QStringLiteral(": ") + message;
    };

    auto *rail = findNamedItem(roots, QStringLiteral("navigationRail"));
    auto *legacy = findNamedItem(roots, QStringLiteral("legacyWorkspace"));
    auto *dashboard = findNamedItem(roots, QStringLiteral("dashboardWorkspace"));
    if (!rail)
        fail(QStringLiteral("NAVFAIL navigationRail not found"));
    if (!legacy)
        fail(QStringLiteral("NAVFAIL legacyWorkspace not found"));
    if (!dashboard)
        fail(QStringLiteral("NAVFAIL dashboardWorkspace not found"));
    if (legacyPageOut)
        *legacyPageOut = legacy;
    if (dashboardPageOut)
        *dashboardPageOut = dashboard;
    if (!rail || !legacy || !dashboard)
        return failures;

    QObject *rootObj = roots.value(0);
    const int legacyIndex = rootObj->property("workspaceLegacyIndex").toInt();
    const int dashboardIndex =
        rootObj->property("workspaceDashboardIndex").toInt();
    const int communicationIndex =
        rootObj->property("workspaceCommunicationIndex").toInt();
    const int index = rail->property("currentWorkspaceIndex").toInt();

    auto *communication =
        findNamedItem(roots, QStringLiteral("communicationWorkspace"));
    if (!communication)
        fail(QStringLiteral("NAVFAIL communicationWorkspace not found"));

    const bool realWorkspace = (index == legacyIndex)
                            || (index == dashboardIndex)
                            || (index == communicationIndex);
    if (!realWorkspace)
        fail(QStringLiteral("NAVFAIL selection %1 is not a real workspace "
                            "(legacy=%2 dashboard=%3 communication=%4)")
                 .arg(index)
                 .arg(legacyIndex)
                 .arg(dashboardIndex)
                 .arg(communicationIndex));
    if (legacy->isVisible() != (index == legacyIndex))
        fail(QStringLiteral("NAVFAIL legacyWorkspace visibility does not "
                            "follow selection %1")
                 .arg(index));
    if (dashboard->isVisible() != (index == dashboardIndex))
        fail(QStringLiteral("NAVFAIL dashboardWorkspace visibility does not "
                            "follow selection %1")
                 .arg(index));
    if (communication && communication->isVisible() != (index == communicationIndex))
        fail(QStringLiteral("NAVFAIL communicationWorkspace visibility does "
                            "not follow selection %1")
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
        return values;
    };

    auto failures = std::make_shared<QStringList>();
    auto fail = [failures](const QString &m) { *failures << m; };

    const int settleMs = 100;
    constexpr int kLastStage = 25;

    // Shared state across stages.
    auto legacyPtr = std::make_shared<QQuickItem *>(nullptr);
    auto dashboardPtr = std::make_shared<QQuickItem *>(nullptr);
    auto communicationPtr = std::make_shared<QQuickItem *>(nullptr);
    auto snapshot0 = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioA = std::make_shared<QMap<QString, QVariant>>();
    auto scenarioB = std::make_shared<QMap<QString, QVariant>>();
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

    auto switchTo = [&](int pageIndex) {
        const char *key = (pageIndex == 0) ? "workspaceLegacyIndex"
                        : (pageIndex == 1) ? "workspaceDashboardIndex"
                                           : "workspaceCommunicationIndex";
        const int idx = rootObj->property(key).toInt();
        auto *item = findNamedItem(
            roots, QStringLiteral("navItem_%1").arg(idx));
        if (!item || !QMetaObject::invokeMethod(item, "activate"))
            fail(QStringLiteral("NAVFAIL activation failed for page %1")
                     .arg(pageIndex));
    };

    auto verifyStructureAndIdentity = [&](const QString &ctx) {
        *failures += runNavAssertions(roots, ctx, nullptr, nullptr);
        auto *legacy = findNamedItem(roots, QStringLiteral("legacyWorkspace"));
        auto *dashboard =
            findNamedItem(roots, QStringLiteral("dashboardWorkspace"));
        auto *communication =
            findNamedItem(roots, QStringLiteral("communicationWorkspace"));
        if (legacy != *legacyPtr)
            fail(QStringLiteral("NAVFAIL %1: legacy page identity changed")
                     .arg(ctx));
        if (dashboard != *dashboardPtr)
            fail(QStringLiteral("NAVFAIL %1: dashboard page identity changed")
                     .arg(ctx));
        if (communication != *communicationPtr)
            fail(QStringLiteral("NAVFAIL %1: communication page identity changed")
                     .arg(ctx));
    };

    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, schedule, failures, legacyPtr, dashboardPtr,
                 communicationPtr, snapshot0, scenarioA, scenarioB, draftValues,
                 gPre, stage, ctrl, takeSnapshot, compareAgainst, switchTo,
                 verifyStructureAndIdentity]() {
        auto *rail = findNamedItem(roots, QStringLiteral("navigationRail"));
        const int legacyIndex = rootObj->property("workspaceLegacyIndex").toInt();
        const int dashboardIndex =
            rootObj->property("workspaceDashboardIndex").toInt();
        const int communicationIndex =
            rootObj->property("workspaceCommunicationIndex").toInt();

        switch (*stage) {
        // ---- Structural phase: Legacy -> Dashboard -> Communication ->
        // Dashboard -> Legacy (Scenario E and H live in these switches) ----
        case 0: {
            if (!ctrl) {
                fail(QStringLiteral("NAVFAIL analysisController not found"));
                break;
            }
            *failures += runNavAssertions(roots, QStringLiteral("initial"),
                                          legacyPtr.get(), dashboardPtr.get());
            *communicationPtr = findNamedItem(
                roots, QStringLiteral("communicationWorkspace"));
            if (!*communicationPtr)
                fail(QStringLiteral("NAVFAIL communicationWorkspace not found"));
            *snapshot0 = takeSnapshot(ctrl);
            qInfo().noquote()
                << QStringLiteral("NAV [initial]: index=%1 legacy=%2x%3 "
                                  "dashboard=%4x%5 communication=%6x%7")
                       .arg(rail ? rail->property("currentWorkspaceIndex").toInt()
                                 : -1)
                       .arg((*legacyPtr) ? (*legacyPtr)->width() : -1)
                       .arg((*legacyPtr) ? (*legacyPtr)->height() : -1)
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
                << QStringLiteral("NAV [dashboard]: index=%1 legacyVisible=%2 "
                                  "dashboardVisible=%3 communicationVisible=%4")
                       .arg(rail->property("currentWorkspaceIndex").toInt())
                       .arg((*legacyPtr)->isVisible())
                       .arg((*dashboardPtr)->isVisible())
                       .arg((*communicationPtr)->isVisible());
            break;
        }
        case 3: switchTo(2); break;
        case 4: {
            verifyStructureAndIdentity(QStringLiteral("communication"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("to communication"));
            qInfo().noquote()
                << QStringLiteral("NAV [communication]: index=%1 "
                                  "legacyVisible=%2 dashboardVisible=%3 "
                                  "communicationVisible=%4")
                       .arg(rail->property("currentWorkspaceIndex").toInt())
                       .arg((*legacyPtr)->isVisible())
                       .arg((*dashboardPtr)->isVisible())
                       .arg((*communicationPtr)->isVisible());
            break;
        }
        case 5: switchTo(1); break;
        case 6: {
            verifyStructureAndIdentity(QStringLiteral("dashboard again"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("communication -> dashboard"));
            break;
        }
        case 7: switchTo(0); break;
        case 8: {
            verifyStructureAndIdentity(QStringLiteral("workbench return"));
            compareAgainst(*snapshot0, takeSnapshot(ctrl),
                           QStringLiteral("back to workbench"));
            qInfo().noquote()
                << QStringLiteral("NAV [workbench]: index=%1 round-trip trace "
                                  "complete")
                       .arg(rail->property("currentWorkspaceIndex").toInt());
            break;
        }

        // ---- Scenario A: deterministic batch survives navigation ----
        case 9: {
            if (!QMetaObject::invokeMethod(ctrl, "runDemoBatch"))
                fail(QStringLiteral("NAVFAIL runDemoBatch() not invokable"));
            break;
        }
        case 10: {
            *scenarioA = takeSnapshot(ctrl);
            if (scenarioA->value(QStringLiteral("observedCount")).toInt() != 4)
                fail(QStringLiteral("NAVFAIL scenario A: expected the "
                                    "deterministic 4-transaction batch"));
            switchTo(1);
            break;
        }
        case 11: {
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
        case 12: {
            compareAgainst(*scenarioA, takeSnapshot(ctrl),
                           QStringLiteral("scenario A return"));
            if (!QMetaObject::invokeMethod(ctrl, "runBaselineDiagnosis"))
                fail(QStringLiteral("NAVFAIL runBaselineDiagnosis() not "
                                    "invokable"));
            break;
        }

        // ---- Scenario B: diagnosis survives navigation ----
        case 13: {
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
        case 14: {
            if (ctrl->property("hasBaselineDiagnosis")
                    != scenarioB->value(QStringLiteral("hasBaselineDiagnosis"))
                || ctrl->property("baselineDiagnosisText")
                    != scenarioB->value(QStringLiteral("baselineDiagnosisText")))
                fail(QStringLiteral("NAVFAIL scenario B: diagnosis changed "
                                    "across navigation"));
            switchTo(0);
            break;
        }
        case 15: {
            if (ctrl->property("hasBaselineDiagnosis")
                    != scenarioB->value(QStringLiteral("hasBaselineDiagnosis")))
                fail(QStringLiteral("NAVFAIL scenario B (return): diagnosis "
                                    "changed"));
            if (!QMetaObject::invokeMethod(ctrl, "clearResults"))
                fail(QStringLiteral("NAVFAIL clearResults() not invokable"));
            break;
        }

        // ---- Scenario D: clear is reflected by every view ----
        case 16: {
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
        case 17: {
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
        case 18: {
            if (ctrl->property("observedCount").toInt() != 0)
                fail(QStringLiteral("NAVFAIL scenario D (return): workbench "
                                    "not cleared"));
            break;
        }

        // ---- Scenario F: page-local drafts survive navigation ----
        case 19: {
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
        case 20: {
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
        case 21: {
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
        case 22: switchTo(2); break;
        case 23: {
            if (ctrl->property("serialConnected").toBool()
                || !ctrl->property("hasSerialError").toBool())
                fail(QStringLiteral("NAVFAIL scenario G': connect-failure "
                                    "state did not survive navigation"));
            break;
        }

        // ---- Scenario F verification after the round trip ----
        case 24: {
            switchTo(0);
            break;
        }
        case 25: {
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

        default:
            break;
        }

        if (failures->isEmpty() && *stage < kLastStage) {
            ++*stage;
            QTimer::singleShot(settleMs, &app, *schedule);
            return;
        }

        if (failures->isEmpty())
            qInfo() << "NAV CHECK PASS (three workspaces; identity stable; "
                       "navigation changed no business values; scenarios "
                       "A/B/D/E/F/G'/H asserted)";
        else
            for (const QString &f : *failures)
                qWarning().noquote() << "GEOFAIL:" << f;
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
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

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

    return app.exec();
}