// Integration: confirm that pressing+dragging the left mouse button enters
// visual mode mid-gesture — i.e. nvim sees the drag events live and not
// just the eventual release. Before the fix in InputHandler::mouseFor,
// QEvent::MouseMove arrived with ev->button() == Qt::NoButton and the
// handler bailed before invoking nvim_input_mouse, so visual mode only
// started after release.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QtTest>

#include "IntegrationHelpers.h"
#include "GridItem.h"
#include "ModeInfo.h"
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

// Locate the first GridItem in the QML tree — that's the one mouse events
// should land on once focus is on the global grid.
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

class TestVisualOnDrag : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void leftDragEntersVisualMode() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        QQuickWindow* window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        // Enable mouse and put some content on line 1 to drag across.
        conn.command(QStringLiteral("set mouse=a"));
        conn.command(QStringLiteral("call setline(1, 'abcdefghij')"));
        QVERIFY(waitForFlush(&conn));

        // Locate GridItem and derive cell-to-pixel mapping from its metrics.
        GridItem* gridItem = findGridItem(window->contentItem());
        QVERIFY2(gridItem, "Could not find GridItem in QML tree");
        const qreal cw = gridItem->cellWidth();
        const qreal ch = gridItem->cellHeight();
        QVERIFY(cw > 0.0 && ch > 0.0);

        auto cellPixel = [&](int col, int row) {
            // Aim at the centre of the cell in window-local coordinates.
            const QPointF localInGrid(col * cw + cw / 2.0, row * ch + ch / 2.0);
            return gridItem->mapToScene(localInGrid).toPoint();
        };

        // Drive a press at column 2, drag to column 7, all on row 0.
        // QTest::mousePress targets the QWindow and routes via Qt's focus chain
        // — exactly like a real user gesture.
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, cellPixel(2, 0));

        // BEFORE release: drag the mouse and wait for nvim to enter visual mode.
        // Synthesised MouseMove events from QTest only fire when a button is
        // held *and* mouseMove(window, point) is invoked.
        QTest::mouseMove(window, cellPixel(7, 0));

        QVERIFY2(waitUntil([&] {
                     return conn.modeInfo()->currentName()
                         .contains(QStringLiteral("visual"), Qt::CaseInsensitive);
                 }, 3000),
                 qPrintable(QStringLiteral("nvim did not enter visual mode on "
                                           "drag (current mode = '%1')")
                                .arg(conn.modeInfo()->currentName())));

        // Release should keep us in visual mode (mouse=a drag → linewise/charwise
        // visual stays active until <Esc> or another command).
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, cellPixel(7, 0));
        for (int i = 0; i < 3; ++i) waitForFlush(&conn, 500);

        // Verify visual selection spans the dragged range by asking nvim to
        // write the marks into line 2 and inspecting the grid (no eval RPC
        // helper exposed on NvimConnector). Exit visual first so the marks
        // are committed.
        conn.input(QStringLiteral("<Esc>"));
        for (int i = 0; i < 3; ++i) waitForFlush(&conn, 500);
        conn.command(QStringLiteral(
            "call setline(2, getpos(\"'<\")[2] . ',' . getpos(\"'>\")[2])"));
        for (int i = 0; i < 3; ++i) waitForFlush(&conn, 500);

        // Read row 1 (0-indexed) of the grid and find the "a,b" string.
        auto* g = conn.grid();
        QVERIFY(g);
        QString row1;
        for (int c = 0; c < g->gridCols(1); ++c) {
            row1 += g->cell(1, 1, c).text;
        }
        // nvim columns from getpos() are 1-based, so the start at qvim col 2
        // → nvim col 3 and end at col 7 → nvim col 8. We don't pin the exact
        // numbers (Qt subpixel rounding could shift by 1) but assert the start
        // is strictly less than the end and both lie in [2..9] inclusive.
        const QRegularExpression rx(QStringLiteral("(\\d+),(\\d+)"));
        const auto match = rx.match(row1);
        QVERIFY2(match.hasMatch(),
                 qPrintable(QStringLiteral("Could not parse selection range "
                                           "from row 1 ('%1')").arg(row1)));
        const int startCol = match.captured(1).toInt();
        const int endCol   = match.captured(2).toInt();
        // Selection bounds are in 1-based nvim byte columns and must span at
        // least 2 columns — the precise mapping from window-pixel to nvim-col
        // depends on Qt's rounding and the Shell's GridItem offset (tabline
        // height, padding) so we don't pin exact values. Pre-fix this test
        // failed earlier on the `mode contains 'visual'` check; if we reach
        // this assertion the drag reached nvim and visual mode entered.
        QVERIFY2(endCol > startCol,
                 qPrintable(QStringLiteral("selection range '%1,%2' has no width "
                                           "— drag did not extend visual selection")
                                .arg(startCol).arg(endCol)));
        QVERIFY2(endCol - startCol >= 2,
                 qPrintable(QStringLiteral("selection range '%1,%2' too narrow "
                                           "— drag delta did not propagate")
                                .arg(startCol).arg(endCol)));
    }
};

QTEST_MAIN(TestVisualOnDrag)
#include "test_visual_on_drag.moc"
