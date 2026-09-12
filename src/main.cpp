#include <QGuiApplication>
#include <QQmlApplicationEngine>
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