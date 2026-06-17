// Verifies that when the window becomes visible, its pixel dimensions are exact
// multiples of the cell size (zero padding). This is the post-snap assertion —
// Main.qml snaps window.width/height before flipping visible=true.

#include <QFontDatabase>
#include <QFontMetricsF>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest>
#include <cmath>

#include "CellMetrics.h"
#include "GridModel.h"
#include "IntegrationHelpers.h"
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
        QQuickWindow* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
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

        GridModel* grid = conn.grid();
        QVERIFY(grid);
        const int cols = grid->gridCols(1);
        const int rows = grid->gridRows(1);
        QVERIFY(cols > 0);
        QVERIFY(rows > 0);

        // Determine chrome height (tabline/cmdline) by finding the Shell item.
        // Shell is anchored between tabline and cmdline, so its height is the
        // grid area. Chrome = window.height - shell.height.
        qreal chromeH = 0;
        QQuickItem* shell = window->contentItem()->findChild<QQuickItem*>(
            QStringLiteral("shell"));
        if (shell)
            chromeH = window->height() - shell->height();

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
        qDebug() << "Delta W:" << (winW - expectedW)
                 << " Delta H:" << (winH - expectedH);

        QVERIFY2(std::abs(winW - expectedW) < 1.0,
                 qPrintable(QStringLiteral(
                     "Width mismatch: window=%1, expected cols(%2)*cw(%3)=%4, delta=%5")
                     .arg(winW).arg(cols).arg(cm.cellWidth).arg(expectedW)
                     .arg(winW - expectedW)));

        QVERIFY2(std::abs(winH - expectedH) < 1.0,
                 qPrintable(QStringLiteral(
                     "Height mismatch: window=%1, expected rows(%2)*ch(%3)+chrome(%4)=%5, delta=%6")
                     .arg(winH).arg(rows).arg(cm.cellHeight).arg(chromeH)
                     .arg(expectedH).arg(winH - expectedH)));
    }
};

QTEST_MAIN(TestWindowSnap)
#include "test_window_snap.moc"
