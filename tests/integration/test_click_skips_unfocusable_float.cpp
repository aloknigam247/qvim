// Mirror of test_click_focuses_float for the focusable=false case. nvim's
// own mouse routing treats unfocusable floats as click-through (the click
// targets whichever window sits under them in the stack), so a click on the
// float's pixels MUST NOT move the cursor into the float.
//
// We additionally rely on Shell.qml's `enabled: !surface || surface.isFocusable`
// gating so that with ext_multigrid the float delegate doesn't even consume
// the press — but the assertion is independent of that path: even if the
// click reaches nvim, the focusable=false flag must keep the cursor out.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QtTest>

#include "IntegrationHelpers.h"
#include "NvimConnector.h"
#include "GridModel.h"

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

QQuickItem* findGridItem(QQuickWindow* window, int gridId) {
    const auto items = window->findChildren<QQuickItem*>();
    for (QQuickItem* it : items) {
        if (QString::fromLatin1(it->metaObject()->className()).contains("GridItem")) {
            const QVariant v = it->property("gridId");
            if (v.isValid() && v.toInt() == gridId) return it;
        }
    }
    return nullptr;
}

} // namespace

class TestClickSkipsUnfocusableFloat : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void clickOnUnfocusableFloatLeavesCursorAlone() {
        NvimConnector conn;
        QVERIFY(conn.start(locateNvim()));

        QQmlApplicationEngine engine;
        QQuickWindow* window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.command(QStringLiteral("set mouse=a"));

        conn.execLua(QStringLiteral(
            "local b = vim.api.nvim_create_buf(false, true)\n"
            "vim.api.nvim_buf_set_lines(b, 0, -1, false, "
            "{'float-1','float-2','float-3','float-4','float-5'})\n"
            "vim.api.nvim_open_win(b, false, {relative='editor', "
            "row=5, col=5, width=20, height=5, focusable=false, border='single'})\n"
        ));
        for (int i = 0; i < 6; ++i) waitForFlush(&conn, 500);

        // Ensure focus is on the main window first.
        conn.command(QStringLiteral("wincmd p"));
        for (int i = 0; i < 4; ++i) waitForFlush(&conn, 300);

        const int startRow = conn.grid()->cursorRow();
        const int startCol = conn.grid()->cursorCol();

        QQuickItem* baseGrid = findGridItem(window, 1);
        QVERIFY2(baseGrid, "Could not locate baseGrid (gridId=1) in window tree");

        // Same as test_click_focuses_float: bypass QTest::mouseClick (which is
        // unreliable on QQuickPaintedItem children under minimal QPA) and call
        // the connector method that mousePressEvent itself invokes.
        const int clickRow = 7;
        const int clickCol = 15;
        conn.inputMouse(QStringLiteral("left"), QStringLiteral("press"),
                        QString(), 0, clickRow, clickCol);
        conn.inputMouse(QStringLiteral("left"), QStringLiteral("release"),
                        QString(), 0, clickRow, clickCol);

        // Allow several flushes — nvim would have emitted at least an
        // ack/repaint if it acted on the click.
        for (int i = 0; i < 6; ++i) waitForFlush(&conn, 300);

        // The cursor must NOT have entered the float's anchor area. Either it
        // moved to the underlying window's coords at the click point, or it
        // stayed put — anything *except* landing inside the float counts as a
        // pass for the unfocusable case.
        const int r = conn.grid()->cursorRow();
        const int c = conn.grid()->cursorCol();
        const bool insideFloat = (r >= 6 && r <= 9 && c >= 6 && c <= 23);
        QVERIFY2(!insideFloat, qPrintable(QStringLiteral(
            "cursor ended up inside an unfocusable float: start=(%1,%2) end=(%3,%4)")
            .arg(startRow).arg(startCol).arg(r).arg(c)));

        conn.command(QStringLiteral("qa!"));
        QSignalSpy disc(&conn, &NvimConnector::disconnected);
        disc.wait(2000);
    }
};

QTEST_MAIN(TestClickSkipsUnfocusableFloat)
#include "test_click_skips_unfocusable_float.moc"
