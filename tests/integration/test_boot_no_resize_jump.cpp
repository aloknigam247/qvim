// Regression guard for the post-T6 visible resize jump.
//
// T6 (perf(boot): fire nvim_ui_attach early) issues nvim_ui_attach(80, 24)
// in main.cpp BEFORE the QQmlApplicationEngine is constructed, so the nvim
// handshake overlaps with QML cold compile. The user-visible side effect was
// that the window appeared at the 80x24 placeholder size and then visibly
// jumped to the real window geometry once Main.qml's Component.onCompleted
// issued an nvim_ui_try_resize.
//
// The fix keeps the perf win but hides the window (visible: false in Main.qml)
// until the first redraw flush arrives at the post-resize grid size. This test
// verifies that ordering:
//   1. Immediately after loadFromModule, window is NOT visible.
//   2. After the grid reaches the requested size and a flush arrives, window
//      becomes visible.
//   3. The grid 1 size at that moment is the real geometry, NOT 80x24.

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QtTest>

#include "IntegrationHelpers.h"
#include "GridModel.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

namespace {

template <typename F>
bool waitUntil(F&& predicate, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while (!predicate()) {
        if (t.elapsed() >= timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return true;
}

} // namespace

class TestBootNoResizeJump : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void windowHiddenUntilResizeFlush() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        // Mirror main.cpp's early nvim_ui_attach(80, 24): the whole point of
        // this fix is to keep the window hidden during the 80x24 phase.
        QVERIFY(conn.attachUi(80, 24));

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("$connector"), &conn);
        engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QQuickWindow* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);

        // Step 1: window must NOT be visible immediately after load. This is
        // the load-bearing assertion — if Main.qml ever flips back to
        // visible: true at construction time, the resize jump returns.
        QVERIFY2(!window->isVisible(),
                 "Window was visible immediately after loadFromModule — "
                 "Main.qml must start with visible: false to avoid the "
                 "80x24 -> real-size jump.");

        // Drive the event loop until the connector has fully attached and the
        // post-attach tryResize has propagated to grid 1.
        QVERIFY(waitUntil([&]() { return conn.attached(); }, 5000));

        // Step 2: wait for window to become visible (gated on the
        // matching-size flush in Main.qml).
        QVERIFY2(waitUntil([&]() { return window->isVisible(); }, 5000),
                 "Window never became visible — flush listener or safety "
                 "timer in Main.qml didn't fire.");

        // Step 3: at the moment of becoming visible, grid 1 must be at the
        // window's real geometry, not the 80x24 placeholder. We can't
        // intercept the exact frame of the visibility flip in a portable
        // way, but we can assert grid 1 has grown past 80x24 by the time
        // visible is true. Any reasonable 1200x780 window with a typical
        // monospace cell exceeds 80 columns.
        GridModel* grid = conn.grid();
        QVERIFY(grid);
        const int cols = grid->gridCols(1);
        const int rows = grid->gridRows(1);
        QVERIFY2(cols > 80 || rows > 24,
                 qPrintable(QStringLiteral(
                     "Grid 1 was still at 80x24 when window became visible "
                     "(cols=%1, rows=%2) — resize didn't complete before show.")
                     .arg(cols).arg(rows)));
    }
};

QTEST_MAIN(TestBootNoResizeJump)
#include "test_boot_no_resize_jump.moc"
