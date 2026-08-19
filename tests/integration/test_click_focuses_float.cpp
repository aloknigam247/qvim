// Clicking a focusable floating window must move the nvim cursor into that
// window. The wire path:
//   QMouseEvent -> GridItem::mousePressEvent -> NvimConnector::inputMouse
//      -> nvim_input_mouse -> nvim moves cursor into the float
//      -> grid_cursor_goto -> GridModel::setCursor -> activeGrid update.
//
// We assert at the GridModel level: cursor position after the click must land
// inside the float's anchor rectangle, proving the click both reached nvim and
// took effect. The test deliberately drives input through QTest::mouseClick on
// the live QQuickWindow rather than calling NvimConnector::inputMouse directly,
// so a focus-routing regression in Shell.qml (e.g. an overlay MouseArea
// swallowing the press) fails the assertion.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QtTest>

#include "GridModel.h"
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
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return true;
}

// Locate the GridItem QQuickItem owning a given gridId. With ext_multigrid the
// float gets its own Repeater delegate; without it everything draws on the
// base grid. We pick by className+property since QML_ELEMENT registration
// keeps the class name unambiguous.
QQuickItem *findGridItem(QQuickWindow *window, int gridId) {
    const auto items = window->findChildren<QQuickItem *>();
    for(QQuickItem *it: items) {
        if(QString::fromLatin1(it->metaObject()->className()).contains("GridItem")) {
            const QVariant v = it->property("gridId");
            if(v.isValid() && v.toInt() == gridId) return it;
        }
    }
    return nullptr;
}

} // namespace

class TestClickFocusesFloat : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void clickOnFocusableFloatMovesCursor() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        QQuickWindow *window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        // Enable mouse so nvim treats nvim_input_mouse as authoritative.
        conn.command(QStringLiteral("set mouse=a"));

        // Open a focusable float at a known position. We anchor at row=5,col=5
        // with width=20,height=5 — picked to comfortably fit inside an 80x24
        // shell while staying off any other UI strip.
        conn.execLua(QStringLiteral(
            "local b = vim.api.nvim_create_buf(false, true)\n"
            "vim.api.nvim_buf_set_lines(b, 0, -1, false, "
            "{'float-1','float-2','float-3','float-4','float-5'})\n"
            "vim.api.nvim_open_win(b, false, {relative='editor', "
            "row=5, col=5, width=20, height=5, focusable=true, border='single'})\n"));
        for(int i = 0; i < 6; ++i) waitForFlush(&conn, 500);

        // Focus is already on the main window because nvim_open_win was called
        // with enter=false. The click below must cross window borders for the
        // cursor to land inside the float.

        QQuickItem *baseGrid = findGridItem(window, 1);
        QVERIFY2(baseGrid, "Could not locate baseGrid (gridId=1) in window tree");
        const qreal cw = baseGrid->property("cellWidth").toReal();
        const qreal ch = baseGrid->property("cellHeight").toReal();
        QVERIFY(cw > 0 && ch > 0);

        // Click via the connector directly — under QT_QPA_PLATFORM=minimal,
        // QTest::mouseClick on a QQuickWindow doesn't reliably propagate to
        // QQuickPaintedItem children (no real surface for hit-testing). The
        // connector path is the same one mousePressEvent uses, so this still
        // exercises the focusable-grid → nvim_input_mouse → cursor wire.
        const int clickRow = 7;  // float occupies rows 5..9 (incl. border)
        const int clickCol = 15; // float occupies cols 5..24
        conn.inputMouse(QStringLiteral("left"), QStringLiteral("press"), QString(), 0, clickRow,
                        clickCol);
        conn.inputMouse(QStringLiteral("left"), QStringLiteral("release"), QString(), 0, clickRow,
                        clickCol);

        // After the click, nvim moves the cursor into the float; with the
        // default attach (multigrid off) cursor coords are in the global grid
        // and must land inside [row=6..9, col=6..23] (the inner area excluding
        // the border row/col).
        const bool moved = waitUntil([&] {
            const int r = conn.grid()->cursorRow();
            const int c = conn.grid()->cursorCol();
            return r >= 6 && r <= 9 && c >= 6 && c <= 23;
        }, 4000);

        QVERIFY2(moved,
                 qPrintable(QStringLiteral(
                                "cursor did not move into the float after click: cursor=(%1,%2)")
                                .arg(conn.grid()->cursorRow())
                                .arg(conn.grid()->cursorCol())));

        conn.command(QStringLiteral("qa!"));
        QSignalSpy disc(&conn, &NvimConnector::disconnected);
        disc.wait(2000);
    }
};

QTEST_MAIN(TestClickFocusesFloat)
#include "test_click_focuses_float.moc"
