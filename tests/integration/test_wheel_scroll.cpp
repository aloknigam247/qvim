// Integration: horizontal wheel events must reach nvim and pan the view in the
// matching direction.
//
// This pins what tests/unit/test_input_handler.cpp cannot: the wiring in
// GridItem::wheelEvent. Passing angleDelta().y() twice, swapping the x/y
// arguments, or negating x would leave every unit test green — only an
// end-to-end directional assertion catches it.
//
// It also covers the pre-existing bug fixed alongside issue #1: modifiers used
// to be folded into the action string ("S-up"), which nvim_input_mouse rejects
// with "invalid button or action", silently dropping every modified wheel event.
//
// Sign convention under test: Qt reports positive angleDelta().x() when the
// user scrolls LEFT (it negates the Windows WM_MOUSEHWHEEL delta), and nvim's
// action "left" pans the viewport left, decreasing winsaveview().leftcol.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QWheelEvent>
#include <QtTest>

#include "GridItem.h"
#include "IntegrationHelpers.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

namespace {

QQuickWindow* loadMainQml(QQmlApplicationEngine& engine, NvimConnector* conn) {
    engine.rootContext()->setContextProperty(QStringLiteral("$connector"), conn);
    engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QQuickWindow*>(engine.rootObjects().first());
}

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

GridItem* findGridItem(QQuickItem* root) {
    if (!root) return nullptr;
    if (auto* g = qobject_cast<GridItem*>(root)) return g;
    const auto kids = root->childItems();
    for (QQuickItem* c : kids) {
        if (auto* g = findGridItem(c)) return g;
    }
    return nullptr;
}

} // namespace

class TestWheelScroll : public QObject {
    Q_OBJECT

private:
    NvimConnector*        m_conn   = nullptr;
    QQuickWindow*         m_window = nullptr;
    GridItem*             m_grid   = nullptr;

    // Round-trip nvim's current horizontal scroll offset through a g: var.
    // Returns -1 if the value never arrives.
    int leftcol() {
        m_conn->execLua(QStringLiteral(
            "vim.g.qvim_test_leftcol = vim.fn.winsaveview().leftcol"));
        std::optional<QVariant> got;
        bool done = false;
        m_conn->getVar(QStringLiteral("qvim_test_leftcol"),
                       [&](std::optional<QVariant> v) { got = v; done = true; });
        if (!waitUntil([&] { return done; }, 3000)) return -1;
        if (!got.has_value()) return -1;
        return got->toInt();
    }

    // Deliver a real QWheelEvent to the window so it routes through Qt's
    // hit-test into GridItem::wheelEvent, exactly like a user gesture.
    void sendWheel(int dx, int dy, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        const qreal cw = m_grid->cellWidth();
        const qreal ch = m_grid->cellHeight();
        const QPointF local(cw * 4 + cw / 2.0, ch * 2 + ch / 2.0);
        const QPointF scenePos = m_grid->mapToScene(local);
        const QPointF globalPos = m_window->mapToGlobal(scenePos.toPoint());
        QWheelEvent ev(scenePos, globalPos, QPoint(0, 0), QPoint(dx, dy),
                       Qt::NoButton, mods, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(m_window, &ev);
        for (int i = 0; i < 3; ++i) waitForFlush(m_conn, 300);
    }

private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void horizontalWheelPansView() {
        NvimConnector conn;
        m_conn = &conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        m_window = loadMainQml(engine, &conn);
        QVERIFY2(m_window, "Main.qml failed to load");
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));
        QVERIFY(waitUntil([&] { return m_window->isVisible(); }, 5000));
        QVERIFY(QTest::qWaitForWindowExposed(m_window));

        m_grid = findGridItem(m_window->contentItem());
        QVERIFY2(m_grid, "Could not find GridItem in QML tree");
        QVERIFY(m_grid->cellWidth() > 0.0 && m_grid->cellHeight() > 0.0);

        // A long line with 'nowrap' gives room to pan in both directions.
        conn.command(QStringLiteral("set mouse=a"));
        conn.command(QStringLiteral("set nowrap"));
        conn.command(QStringLiteral("set mousescroll=ver:3,hor:6"));
        conn.command(QStringLiteral("call setline(1, repeat('abcdefghij', 40))"));
        conn.command(QStringLiteral("normal! 100|"));
        conn.command(QStringLiteral("normal! 50zl"));
        for (int i = 0; i < 3; ++i) waitForFlush(&conn, 500);

        const int base = leftcol();
        QVERIFY2(base > 0, qPrintable(QStringLiteral(
            "expected a non-zero starting leftcol after 50zl, got %1").arg(base)));

        // Scroll left: Qt positive x -> nvim action "left" -> leftcol decreases.
        sendWheel(120, 0);
        const int afterLeft = leftcol();
        QVERIFY2(afterLeft < base, qPrintable(QStringLiteral(
            "horizontal wheel (angleDelta.x=+120) did not pan the view left: "
            "leftcol %1 -> %2 (expected a decrease)").arg(base).arg(afterLeft)));

        // Scroll right: negative x -> "right" -> leftcol increases.
        sendWheel(-120, 0);
        const int afterRight = leftcol();
        QVERIFY2(afterRight > afterLeft, qPrintable(QStringLiteral(
            "horizontal wheel (angleDelta.x=-120) did not pan the view right: "
            "leftcol %1 -> %2 (expected an increase)").arg(afterLeft).arg(afterRight)));

        // Negative check: a purely vertical wheel must not move the view
        // horizontally. Without this, an implementation that panned on every
        // wheel event would still pass the two assertions above.
        const int beforeVertical = afterRight;
        sendWheel(0, 120);
        const int afterVertical = leftcol();
        QCOMPARE(afterVertical, beforeVertical);
    }

    // The modifier must travel in nvim_input_mouse's `modifier` argument. When
    // it was concatenated into the action ("S-left"), nvim rejected the event
    // and leftcol never moved.
    void modifiedWheelStillReachesNvim() {
        NvimConnector conn;
        m_conn = &conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        m_window = loadMainQml(engine, &conn);
        QVERIFY2(m_window, "Main.qml failed to load");
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));
        QVERIFY(waitUntil([&] { return m_window->isVisible(); }, 5000));
        QVERIFY(QTest::qWaitForWindowExposed(m_window));

        m_grid = findGridItem(m_window->contentItem());
        QVERIFY2(m_grid, "Could not find GridItem in QML tree");

        conn.command(QStringLiteral("set mouse=a"));
        conn.command(QStringLiteral("set nowrap"));
        conn.command(QStringLiteral("set mousescroll=ver:3,hor:6"));
        conn.command(QStringLiteral("call setline(1, repeat('abcdefghij', 40))"));
        conn.command(QStringLiteral("normal! 100|"));
        conn.command(QStringLiteral("normal! 50zl"));
        // Map the modified horizontal wheel to a plain horizontal scroll so the
        // assertion depends only on the event being *delivered*, not on any
        // default binding for <S-ScrollWheelLeft>.
        conn.command(QStringLiteral("nnoremap <S-ScrollWheelLeft> 6zh"));
        for (int i = 0; i < 3; ++i) waitForFlush(&conn, 500);

        const int base = leftcol();
        QVERIFY(base > 0);

        sendWheel(120, 0, Qt::ShiftModifier);
        const int after = leftcol();
        QVERIFY2(after < base, qPrintable(QStringLiteral(
            "Shift + horizontal wheel never reached nvim: leftcol %1 -> %2. "
            "The modifier is probably being folded into the action string, "
            "which nvim rejects as 'invalid button or action'.")
            .arg(base).arg(after)));
    }
};

QTEST_MAIN(TestWheelScroll)
#include "test_wheel_scroll.moc"
