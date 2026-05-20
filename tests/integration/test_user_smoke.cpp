// User-POV smoke harness: loads the real Main.qml in a QQuickWindow with the
// production NvimConnector, then injects key events through Qt's focus chain
// (NOT directly via NvimConnector::input) so the test exercises the exact
// path a user's keystroke takes — including focus management, the Repeater
// rebuild behaviour, and the cmdline/grid layout reflow.
//
// Specifically reproduces the bug Alok hit: after pressing ':', the very next
// keystroke was lost because the sub-grid Repeater was destroying delegates
// on every grid_resize, invalidating activeFocusItem. With GridSurfaceProxy
// in place the delegate persists and focus survives.

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
#include "CmdlineModel.h"

using namespace qvim;
using namespace qvim::test;

namespace {

QQuickWindow* loadMainQml(QQmlApplicationEngine& engine, NvimConnector* conn) {
    engine.rootContext()->setContextProperty(QStringLiteral("$connector"), conn);
    engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QQuickWindow*>(engine.rootObjects().first());
}

// Polls a predicate up to timeoutMs, processing events between checks. Returns
// true on first success, false on timeout. Used in lieu of QSignalSpy::wait
// for predicates that depend on accumulated nvim state across multiple
// flush()es (e.g. cmdline content updating).
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

// Synchronously renders the QML subtree rooted at `item` into a QImage via
// the scene graph software backend. Works under minimal QPA where
// QQuickWindow::grabWindow() returns null because no real surface exists.
QImage grabItem(QQuickItem* item, int timeoutMs = 2000) {
    QSharedPointer<QQuickItemGrabResult> result = item->grabToImage();
    if (!result) return {};
    QElapsedTimer t;
    t.start();
    while (result->image().isNull() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return result->image();
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

    // Core reproduction: ':' opens cmdline, then the very next key MUST be
    // routed by Qt's focus chain through GridItem and reach nvim. If focus
    // was lost on the cmdline_show reflow this assertion fails.
    void keypressAfterColonReachesNvim() {
        NvimConnector conn;
        QVERIFY(conn.start(locateNvim()));

        QQmlApplicationEngine engine;
        QQuickWindow* window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        // baseGrid should hold active focus after Shell's Component.onCompleted.
        QVERIFY2(window->activeFocusItem() != nullptr,
                 "No active focus item after attach — Shell didn't force focus");

        // Send ':' through the window's focus chain.
        QTest::keyClick(window, Qt::Key_Colon);

        // cmdline_show is asynchronous: nvim → msgpack → CmdlineModel.
        QVERIFY2(waitUntil([&] { return conn.cmdline()->visible(); }, 3000),
                 "Cmdline didn't open after ':'");

        // After cmdline_show, the layout shrinks Shell and may trigger
        // nvim_ui_try_resize → grid_resize → win_pos. With GridSurfaceProxy in
        // place the Repeater delegates rebind in-place, so focus must survive.
        QVERIFY2(window->activeFocusItem() != nullptr,
                 "Focus lost after cmdline_show — Repeater destroyed the delegate");

        // The key that was being LOST before the fix.
        QTest::keyClick(window, Qt::Key_Q);

        // Verify nvim actually received it: cmdline content should now contain "q".
        QVERIFY2(waitUntil([&] {
                     return conn.cmdline()->content().contains(QLatin1Char('q'));
                 }, 3000),
                 "'q' did not reach nvim after ':' — focus chain is broken");
    }

    // Visual confirmation: after ':' the bottom strip of the rendered window
    // must look different from the grid area above it — i.e. the Cmdline
    // overlay actually drew. A model-level "cmdline.visible=true" can be true
    // while the QML hides the strip with opacity=0, wrong anchor, z-order
    // collision, or a black-on-black render bug; only pixels catch those.
    void cmdlineIsVisibleOnScreenAfterColon() {
        NvimConnector conn;
        QVERIFY(conn.start(locateNvim()));

        QQmlApplicationEngine engine;
        QQuickWindow* window = loadMainQml(engine, &conn);
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        // Locate the Cmdline QQuickItem in the tree. It's a top-level child of
        // the Window's contentItem in Main.qml.
        QQuickItem* cmdlineItem = nullptr;
        for (QQuickItem* c : window->contentItem()->childItems()) {
            if (QString::fromLatin1(c->metaObject()->className()).contains("Cmdline")) {
                cmdlineItem = c;
                break;
            }
        }
        QVERIFY2(cmdlineItem, "Could not find Cmdline QML item in window tree");

        // Baseline: cmdline is hidden, so its height should be 0 and a grab
        // should produce either a null/empty image. We don't assert on the
        // baseline shape, just record it for comparison.
        QVERIFY2(!cmdlineItem->isVisible(),
                 "Cmdline shouldn't be visible before any ':' is pressed");

        QTest::keyClick(window, Qt::Key_Colon);
        QVERIFY(waitUntil([&] { return conn.cmdline()->visible(); }, 3000));

        // The QML binding `Cmdline.visible: $connector.cmdline.visible` should
        // now be true at the item level — this is the user-visible check that
        // a model flip actually propagated to the rendered scene.
        QVERIFY2(waitUntil([&] { return cmdlineItem->isVisible(); }, 1000),
                 "Cmdline QML item never became visible despite model "
                 "reporting visible=true (binding broken or item hidden by "
                 "z-order / opacity)");
        QVERIFY2(cmdlineItem->height() >= 20.0 && cmdlineItem->width() > 0.0,
                 qPrintable(QStringLiteral("Cmdline item has zero/invalid size "
                                           "(%1 x %2) after cmdline_show")
                                .arg(cmdlineItem->width())
                                .arg(cmdlineItem->height())));

        // Pixel-level confirmation: render the Cmdline subtree and assert it
        // produced non-uniform output (i.e. the prompt char ':' actually drew
        // and the strip isn't a flat black-on-black render bug).
        const QImage frame = grabItem(cmdlineItem);
        QVERIFY2(!frame.isNull() && frame.width() > 0 && frame.height() > 0,
                 "grabItem returned an empty image");

        QSet<QRgb> colors;
        for (int y = 0; y < frame.height(); y += 2) {
            for (int x = 0; x < frame.width(); x += 2) {
                colors.insert(frame.pixel(x, y));
                if (colors.size() > 3) break;
            }
            if (colors.size() > 3) break;
        }
        QVERIFY2(colors.size() > 1,
                 qPrintable(QStringLiteral("Cmdline rendered as a single colour "
                                           "(0x%1) — prompt char didn't draw")
                                .arg(*colors.constBegin(), 8, 16, QLatin1Char('0'))));
    }

    // Defence-in-depth: typing a multi-char command after ':' all reaches nvim.
    void multiKeyCommandReachesNvim() {
        NvimConnector conn;
        QVERIFY(conn.start(locateNvim()));

        QQmlApplicationEngine engine;
        QQuickWindow* window = loadMainQml(engine, &conn);
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        QTest::keyClick(window, Qt::Key_Colon);
        QVERIFY(waitUntil([&] { return conn.cmdline()->visible(); }, 3000));

        for (Qt::Key k : {Qt::Key_E, Qt::Key_C, Qt::Key_H, Qt::Key_O}) {
            QTest::keyClick(window, k);
        }

        QVERIFY2(waitUntil([&] {
                     return conn.cmdline()->content() == QStringLiteral("echo");
                 }, 3000),
                 qPrintable(QStringLiteral("Cmdline content was '%1' (expected 'echo')")
                                .arg(conn.cmdline()->content())));
    }
};

QTEST_MAIN(TestUserSmoke)
#include "test_user_smoke.moc"
