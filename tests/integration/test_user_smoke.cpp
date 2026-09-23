// User-POV smoke harness: loads the real Main.qml in a QQuickWindow with the
// production NvimConnector, then injects key events through Qt's focus chain
// (NOT directly via NvimConnector::input) so the test exercises the exact
// path a user's keystroke takes — including focus management and grid layout.
//
// Specifically reproduces the bug Alok hit: after pressing ':', the very next
// keystroke was lost because focus was invalidated on a grid layout event. The
// test drives ':<command><CR>' through the focus chain and asserts nvim
// actually received it, so a focus regression fails hard.

#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSharedPointer>
#include <QSignalSpy>
#include <QtTest>

#include "IntegrationHelpers.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

namespace {

QQuickWindow *loadMainQml(QQmlApplicationEngine &engine, NvimConnector *conn) {
    engine.rootContext()->setContextProperty(QStringLiteral("$connector"), conn);
    engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
    if(engine.rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QQuickWindow *>(engine.rootObjects().first());
}

// Polls a predicate up to timeoutMs, processing events between checks. Returns
// true on first success, false on timeout. Used in lieu of QSignalSpy::wait
// for predicates that depend on accumulated nvim state across multiple
// flush()es (e.g. cmdline content updating).
template <typename F>
bool waitUntil(F &&predicate, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while(!predicate()) {
        if(t.elapsed() >= timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return true;
}

} // namespace

class TestUserSmoke : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Tests run under QT_QPA_PLATFORM=minimal (the only headless platform
        // vcpkg's Qt deploys here). Synthetic key events still route through
        // QWindowSystemInterface so QTest::keyClick targeting QQuickWindow
        // works. Force the QML software renderer so QQuickWindow::grabWindow()
        // produces real pixels without a GPU surface.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    // Core reproduction of the "keystroke lost after ':'" focus bug. Drives a
    // full ':let ...<CR>' through Qt's focus chain (QTest::keyClick targets the
    // window's activeFocusItem), then asserts nvim actually applied the command.
    // A focus regression after the colon makes the trailing keys vanish and the
    // variable never gets set — a hard failure here, not a soft focus check.
    void keypressAfterColonReachesNvim() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        QQuickWindow *window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        QVERIFY2(window->activeFocusItem() != nullptr,
                 "No active focus item after attach — Shell didn't force focus");

        QTest::keyClick(window, Qt::Key_Colon);
        for(char c: QByteArrayLiteral("let g:smoke_after_colon = 7")) {
            QTest::keyClick(window, c);
        }
        QTest::keyClick(window, Qt::Key_Return);

        const bool applied = waitUntil([&] {
            const auto v = evalSync(conn, QStringLiteral("get(g:, 'smoke_after_colon', 0)"), 500);
            return v && v->toInt() == 7;
        }, 5000);
        QVERIFY2(applied,
                 "keystrokes after ':' never reached nvim — focus was lost after the colon");
        QVERIFY2(window->activeFocusItem() != nullptr, "Focus lost after command entry");
    }
};

QTEST_MAIN(TestUserSmoke)
#include "test_user_smoke.moc"
