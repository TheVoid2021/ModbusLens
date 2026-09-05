#include <QApplication>
#include <QMainWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ModbusLens"));
    QCoreApplication::setApplicationName(QStringLiteral("ModbusLens"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QMainWindow window;
    window.setWindowTitle(QStringLiteral("ModbusLens"));
    window.resize(1024, 720);
    window.show();

    return app.exec();
}