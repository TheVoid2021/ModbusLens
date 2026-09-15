#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QCoreApplication>
#include <QQuickStyle>

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

    return app.exec();
}