// Verifies that when the window becomes visible, its pixel dimensions are exact
// multiples of the cell size (zero padding). This is the post-snap assertion —
// Main.qml snaps window.width/height before flipping visible=true.

#include <cmath>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest>

#include "CellMetrics.h"
#include "GridModel.h"
#include "IntegrationHelpers.h"
#include "NvimConnector.h"
#include "ResizeCoalescer.h"

using namespace qvim;
using namespace qvim::test;

namespace {

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

class TestWindowSnap : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void windowSizeIsExactMultipleOfCellSize() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("$connector"), &conn);
        engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);

        // Wait for the full startup sequence to complete.
        QVERIFY(waitUntil([&]() { return conn.attached(); }, 5000));
        QVERIFY(waitUntil([&]() { return window->isVisible(); }, 5000));

        // Allow a couple more event loop ticks for layout to settle.
        QTest::qWait(100);

        // Compute expected cell metrics independently using the same system font
        // that GridItem uses (--clean → no guifont set).
        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSizeF(14.0);
        const CellMetrics cm = computeCellMetrics(QFontMetricsF(font), 0);
        QVERIFY(cm.cellWidth > 0);
        QVERIFY(cm.cellHeight > 0);

        GridModel *grid = conn.grid();
        QVERIFY(grid);
        const int cols = grid->gridCols(1);
        const int rows = grid->gridRows(1);
        QVERIFY(cols > 0);
        QVERIFY(rows > 0);

        // Determine chrome height (tabline/cmdline) by finding the Shell item.
        // Shell is anchored between tabline and cmdline, so its height is the
        // grid area. Chrome = window.height - shell.height.
        qreal chromeH = 0;
        QQuickItem *shell = window->contentItem()->findChild<QQuickItem *>(QStringLiteral("shell"));
        if(shell) chromeH = window->height() - shell->height();

        const qreal expectedW = cols * cm.cellWidth;
        const qreal expectedH = rows * cm.cellHeight + chromeH;
        const qreal winW = static_cast<qreal>(window->width());
        const qreal winH = static_cast<qreal>(window->height());

        // Dump diagnostics so failures are immediately actionable.
        qDebug() << "Window size:" << winW << "x" << winH;
        qDebug() << "Grid:" << cols << "cols x" << rows << "rows";
        qDebug() << "Cell:" << cm.cellWidth << "x" << cm.cellHeight;
        qDebug() << "Chrome height:" << chromeH;
        qDebug() << "Expected:" << expectedW << "x" << expectedH;
        qDebug() << "Delta W:" << (winW - expectedW) << " Delta H:" << (winH - expectedH);

        QVERIFY2(std::abs(winW - expectedW) < 1.0,
                 qPrintable(QStringLiteral(
                                "Width mismatch: window=%1, expected cols(%2)*cw(%3)=%4, delta=%5")
                                .arg(winW)
                                .arg(cols)
                                .arg(cm.cellWidth)
                                .arg(expectedW)
                                .arg(winW - expectedW)));

        QVERIFY2(
            std::abs(winH - expectedH) < 1.0,
            qPrintable(
                QStringLiteral(
                    "Height mismatch: window=%1, expected rows(%2)*ch(%3)+chrome(%4)=%5, delta=%6")
                    .arg(winH)
                    .arg(rows)
                    .arg(cm.cellHeight)
                    .arg(chromeH)
                    .arg(expectedH)
                    .arg(winH - expectedH)));
    }

    void nvimSetColumnsAndLinesResizesWindow() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("$connector"), &conn);
        engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QQuickWindow *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);

        QVERIFY(waitUntil([&]() { return conn.attached(); }, 5000));
        QVERIFY(waitUntil([&]() { return window->isVisible(); }, 5000));
        QTest::qWait(100);

        QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSizeF(14.0);
        const CellMetrics cm = computeCellMetrics(QFontMetricsF(font), 0);
        QVERIFY(cm.cellWidth > 0);
        QVERIFY(cm.cellHeight > 0);

        GridModel *grid = conn.grid();
        QVERIFY(grid);

        qreal chromeH = 0;
        QQuickItem *shell = window->contentItem()->findChild<QQuickItem *>(QStringLiteral("shell"));
        if(shell) chromeH = window->height() - shell->height();

        const int bootCols = grid->gridCols(1);
        const int bootRows = grid->gridRows(1);
        QVERIFY(bootCols > 20);
        QVERIFY(bootRows > 10);

        // Pick a target guaranteed to differ from the boot grid (well above
        // nvim's 12-col / 3-line minimums), so the test can't coincidentally
        // pass on main where the window never moves.
        const int targetCols = bootCols - 7;
        const int targetRows = bootRows - 4;
        const qreal expectedW = targetCols * cm.cellWidth;
        const qreal expectedH = targetRows * cm.cellHeight + chromeH;
        // Precondition: on main the window stays at the boot size, so the target
        // pixel size must differ — otherwise the assertion below couldn't fail.
        QVERIFY(std::abs(static_cast<qreal>(window->width()) - expectedW) >= 1.0 ||
                std::abs(static_cast<qreal>(window->height()) - expectedH) >= 1.0);

        conn.command(QStringLiteral("set columns=%1").arg(targetCols));
        conn.command(QStringLiteral("set lines=%1").arg(targetRows));

        QVERIFY(waitUntil([&]() {
            return grid->gridCols(1) == targetCols && grid->gridRows(1) == targetRows;
        }, 5000));

        // Hard asserts: grid reports the requested size AND the window client
        // area is exactly targetCols*cellWidth x targetRows*cellHeight (+chrome).
        QCOMPARE(grid->gridCols(1), targetCols);
        QCOMPARE(grid->gridRows(1), targetRows);

        const qreal winW = static_cast<qreal>(window->width());
        const qreal winH = static_cast<qreal>(window->height());
        qDebug() << "nvim-resize: window" << winW << "x" << winH << "expected" << expectedW << "x"
                 << expectedH;
        QVERIFY2(std::abs(winW - expectedW) < 1.0,
                 qPrintable(QStringLiteral("Width mismatch: window=%1 expected=%2 delta=%3")
                                .arg(winW)
                                .arg(expectedW)
                                .arg(winW - expectedW)));
        QVERIFY2(std::abs(winH - expectedH) < 1.0,
                 qPrintable(QStringLiteral("Height mismatch: window=%1 expected=%2 delta=%3")
                                .arg(winH)
                                .arg(expectedH)
                                .arg(winH - expectedH)));

        // No-oscillation / no-echo: after settling, the geometryChange from the
        // window resize must not bounce a redundant nvim_ui_try_resize back
        // (syncAfterDirectResize suppresses it), the grid must stay put, and the
        // window must still fit exactly.
        QSignalSpy resizeSpy(conn.resizeCoalescer(), &ResizeCoalescer::resizeRequested);
        QTest::qWait(200);
        QCOMPARE(grid->gridCols(1), targetCols);
        QCOMPARE(grid->gridRows(1), targetRows);
        QVERIFY2(std::abs(static_cast<qreal>(window->width()) - expectedW) < 1.0,
                 "window width drifted after settle");
        QVERIFY2(std::abs(static_cast<qreal>(window->height()) - expectedH) < 1.0,
                 "window height drifted after settle");
        QCOMPARE(resizeSpy.count(), 0);
    }
};

QTEST_MAIN(TestWindowSnap)
#include "test_window_snap.moc"
