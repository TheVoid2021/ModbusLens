#include <QMainWindow>
#include <QtTest>

namespace {

class SmokeTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultMainWindowIsEmpty();
    void windowTitleIsSet();
};

void SmokeTest::defaultMainWindowIsEmpty()
{
    QMainWindow window;
    QVERIFY(window.centralWidget() == nullptr);
}

void SmokeTest::windowTitleIsSet()
{
    QMainWindow window;
    window.setWindowTitle(QStringLiteral("ModbusLens"));
    QCOMPARE(window.windowTitle(), QStringLiteral("ModbusLens"));
}

} // namespace

QTEST_MAIN(SmokeTest)
#include "test_smoke.moc"