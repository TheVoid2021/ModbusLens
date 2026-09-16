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

QStringList runGeometryAssertions(const QList<QObject *> &roots,
                                  const QString &contextLabel)
{
    QStringList failures;

    const QStringList cardNames = {
        QStringLiteral("statCard_0"),   QStringLiteral("statCard_1"),
        QStringLiteral("statCard_2"),   QStringLiteral("statCard_rate"),
        QStringLiteral("statCard_latency"),
        QStringLiteral("statusCard_0"), QStringLiteral("statusCard_1"),
        QStringLiteral("statusCard_2"), QStringLiteral("statusCard_3"),
        QStringLiteral("statusCard_4"), QStringLiteral("statusCard_5"),
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

    auto *row1 = findNamedItem(roots, QStringLiteral("statisticsRow1"));
    auto *row2 = findNamedItem(roots, QStringLiteral("statisticsRow2"));
    const auto row1Count = row1 ? row1->childItems().size() : -1;
    const auto row2Count = row2 ? row2->childItems().size() : -1;

    auto *header = findNamedItem(roots, QStringLiteral("statisticsHeader"));
    auto *panel = findNamedItem(roots, QStringLiteral("statisticsPanel"));
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

    auto *diag = findNamedItem(roots, QStringLiteral("diagnosisWorkspace"));
    if (panel && diag && diag->y() + 1e-6 < panel->y() + panel->height())
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

    const int index = rail->property("currentWorkspaceIndex").toInt();
    if (index < 0 || index > 5)
        fail(QStringLiteral("NAV currentWorkspaceIndex %1 out of range 0..5")
                 .arg(index));
    if (index != 0)
        fail(QStringLiteral("NAV expected initial workspace 0 (legacy), got %1")
                 .arg(index));

    auto *legacy = findNamedItem(roots, QStringLiteral("legacyWorkspace"));
    if (!legacy)
        fail(QStringLiteral("NAV legacyWorkspace not found"));
    else if (!legacy->isVisible())
        fail(QStringLiteral("NAV legacyWorkspace is not visible at index 0"));

    auto *item0 = findNamedItem(roots, QStringLiteral("navItem_0"));
    auto *item1 = findNamedItem(roots, QStringLiteral("navItem_1"));
    if (!item0 || !item1) {
        fail(QStringLiteral("NAV navItem_0/navItem_1 not found"));
        return failures;
    }

    if (!item0->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_0 (real workspace) must be enabled"));
    if (item1->property("enabled").toBool())
        fail(QStringLiteral("NAV navItem_1 (future workspace) must be disabled"));

    // A disabled entry must not be able to change the selection, even when
    // its activation path is invoked directly (same path as click/keys).
    // The invoke result is checked too: a silently unresolvable activate()
    // would make this guard vacuous.
    if (!QMetaObject::invokeMethod(item1, "activate"))
        fail(QStringLiteral("NAV navItem_1.activate() is not invokable — the "
                            "disabled-entry guard would be vacuous"));
    if (rail->property("currentWorkspaceIndex").toInt() != 0)
        fail(QStringLiteral("NAV disabled navItem_1 changed "
                            "currentWorkspaceIndex"));

    // Re-activating the already-selected entry is a no-op.
    if (!QMetaObject::invokeMethod(item0, "activate"))
        fail(QStringLiteral("NAV navItem_0.activate() is not invokable"));
    if (rail->property("currentWorkspaceIndex").toInt() != 0)
        fail(QStringLiteral("NAV re-activating navItem_0 changed the index"));

    return failures;
}

QString dumpGeometryTable(const QList<QObject *> &roots, const QString &contextLabel)
{
    const QStringList names = {
        QStringLiteral("appBar"),        QStringLiteral("navigationRail"),
        QStringLiteral("workspaceHost"), QStringLiteral("legacyWorkspace"),
        QStringLiteral("statisticsPanel"), QStringLiteral("statisticsRow1"),
        QStringLiteral("statisticsRow2"),  QStringLiteral("statCard_0"),
        QStringLiteral("statCard_1"),      QStringLiteral("statCard_2"),
        QStringLiteral("statCard_rate"),   QStringLiteral("statCard_latency"),
        QStringLiteral("statusCard_0"),    QStringLiteral("statusCard_1"),
        QStringLiteral("statusCard_2"),    QStringLiteral("statusCard_3"),
        QStringLiteral("statusCard_4"),    QStringLiteral("statusCard_5"),
        QStringLiteral("diagnosisWorkspace"),
    };
    QStringList lines;
    lines << QStringLiteral("GEOMETRY [%1]:").arg(contextLabel);
    for (const QString &name : names) {
        auto *item = findNamedItem(roots, name);
        if (!item) {
            lines << QStringLiteral("  %1: MISSING").arg(name);
            continue;
        }
        lines << QStringLiteral("  %1: x=%2 y=%3 w=%4 h=%5 implicit=%6x%7")
                     .arg(name)
                     .arg(item->x())
                     .arg(item->y())
                     .arg(item->width())
                     .arg(item->height())
                     .arg(item->implicitWidth())
                     .arg(item->implicitHeight());
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

    // Delegate creation / layout polish are asynchronous: give the event
    // loop real settle time between attempts instead of raw singleShot(0)
    // turns, and retry "not found" results (up to 5 x 100ms) before
    // declaring a real failure.
    const int settleMs = 100;
    const int maxAttempts = 5;
    int passIndex = 0;
    auto failures = std::make_shared<QStringList>();
    auto attempt = std::make_shared<int>(0);

    auto finish = [&app](const QStringList &fails) {
        if (fails.isEmpty())
            qInfo() << "GEOMETRY CHECK PASS"
                       "(default size + 1000x700 minimum)";
        else
            for (const QString &f : fails)
                qWarning().noquote() << "GEOFAIL:" << f;
        app.exit(fails.isEmpty() ? 0 : 1);
    };

    auto schedule = std::make_shared<std::function<void()>>();
    *schedule = [&, schedule, failures, attempt]() {
        const QString context =
            passIndex == 0 ? QStringLiteral("DEFAULT") : QStringLiteral("MIN 1000x700");
        qInfo().noquote() << dumpGeometryTable(roots, context);
        *failures = runGeometryAssertions(roots, context);
        *failures += runShellNavAssertions(roots, context);
        const bool missingItems =
            failures->join(u' ').contains(QStringLiteral("not found"));
        if (!failures->isEmpty() && missingItems && *attempt < maxAttempts) {
            ++*attempt;
            QTimer::singleShot(settleMs, &app, *schedule);
            return;
        }
        *attempt = 0;
        if (failures->isEmpty())
            dumpGrab(passIndex == 0 ? QStringLiteral("geometry-1024x720")
                                    : QStringLiteral("geometry-1000x700"));
        if (passIndex == 0 && failures->isEmpty() && window) {
            ++passIndex;
            window->resize(1000, 700);
            QTimer::singleShot(settleMs, &app, *schedule);
            return;
        }
        finish(*failures);
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

    return app.exec();
}