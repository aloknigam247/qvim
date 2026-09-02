// User-POV smoke test for the grid smooth-scroll animation. Loads the real
// Main.qml with the production NvimConnector, fills the buffer with more lines
// than fit on screen, then drives a single-line viewport scroll through Qt's
// focus chain (Ctrl-E) exactly as a keystroke would. The animatable scroll
// seeds GridItem's ease, so the production-path scrollAnimOffset() must go
// non-zero and then settle back to exactly zero within the ease duration.
//
// Reading scrollAnimOffset() (a Q_INVOKABLE on the live GridItem) proves the
// real render item — not just the model — picked up the scroll and is easing
// it. A shape that isn't animatable would leave the offset pinned at zero, so a
// non-zero reading also asserts the scroll produced the animatable region.

#include <cmath>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest>

#include "GridItem.h"
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

template <typename F>
bool waitUntil(F &&predicate, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while(!predicate()) {
        if(t.elapsed() >= timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

} // namespace

class TestSmoothScroll : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void scrollEasesThenSettles() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        QQuickWindow *window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));
        QVERIFY2(window->activeFocusItem() != nullptr, "No active focus item after attach");

        GridItem *grid = window->findChild<GridItem *>();
        QVERIFY2(grid, "No GridItem in the window tree");

        // Fill the buffer well beyond one screen so Ctrl-E has room to scroll.
        conn.command(QStringLiteral("call setline(1, map(range(1, 500), 'string(v:val)'))"));
        for(int i = 0; i < 5; ++i) waitForFlush(&conn, 200);

        QCOMPARE(grid->scrollAnimOffset(), 0.0);

        // Ctrl-E scrolls the viewport down one line: a full-width, top-anchored
        // single-row grid_scroll — the animatable shape.
        QTest::keyClick(window, Qt::Key_E, Qt::ControlModifier);

        bool wentNonZero =
            waitUntil([&] { return std::abs(grid->scrollAnimOffset()) > 0.0; }, 2000);
        QVERIFY2(wentNonZero, "scrollAnimOffset stayed at zero — the scroll never eased");

        bool settled = waitUntil([&] { return grid->scrollAnimOffset() == 0.0; }, 2000);
        QVERIFY2(settled, "scrollAnimOffset never returned to zero — the ease never finished");
    }
};

QTEST_MAIN(TestSmoothScroll)
#include "test_smooth_scroll.moc"
