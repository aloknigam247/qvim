// Integration: enter visual-line mode over multiple rows and assert there are
// no horizontal seams of default-bg pixels inside the highlighted block. The
// regression: when cell width/height were fractional, adjacent row rects fell
// on sub-pixel boundaries and antialiasing bled the editor background through
// as a thin horizontal stripe between selected rows. Snapping cell metrics to
// integer pixels eliminates the seam.

#include <QColor>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSharedPointer>
#include <QtTest>

#include "GridItem.h"
#include "HighlightTable.h"
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
    for (QQuickItem* c : root->childItems()) {
        if (auto* g = findGridItem(c)) return g;
    }
    return nullptr;
}

QImage grabItem(QQuickItem* item, int timeoutMs = 3000) {
    QSharedPointer<QQuickItemGrabResult> result = item->grabToImage();
    if (!result) return {};
    QElapsedTimer t;
    t.start();
    while (result->image().isNull() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return result->image();
}

int channelDelta(QRgb a, QRgb b) {
    return std::max({
        std::abs(qRed(a)   - qRed(b)),
        std::abs(qGreen(a) - qGreen(b)),
        std::abs(qBlue(a)  - qBlue(b)),
    });
}

} // namespace

class TestVisualSelectionNoSeams : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void selectionBlockHasNoHorizontalDefaultBgStripes() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        QQmlApplicationEngine engine;
        QQuickWindow* window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        // Seed enough rows that visual-line selection covers >=3 rows. The
        // contents matter less than the bg colour on the selected lines.
        conn.command(QStringLiteral("call setline(1, 'aaaaaaaaaaaaaaaaaaaa')"));
        conn.command(QStringLiteral("call setline(2, 'bbbbbbbbbbbbbbbbbbbb')"));
        conn.command(QStringLiteral("call setline(3, 'cccccccccccccccccccc')"));
        conn.command(QStringLiteral("call setline(4, 'dddddddddddddddddddd')"));
        conn.command(QStringLiteral("call cursor(1, 1)"));
        QVERIFY(waitForFlush(&conn));

        // Enter visual-line mode and extend selection down 2 rows so we cover
        // rows 1..3 (3 rows total). Use raw input so the keystrokes go to nvim
        // regardless of which QML item currently owns focus.
        conn.input(QStringLiteral("V"));
        conn.input(QStringLiteral("jj"));
        for (int i = 0; i < 5; ++i) waitForFlush(&conn, 200);

        QVERIFY2(waitUntil([&] {
                     return conn.modeInfo()->currentName()
                         .contains(QStringLiteral("visual"), Qt::CaseInsensitive);
                 }, 3000),
                 "nvim did not enter visual mode");

        GridItem* gridItem = findGridItem(window->contentItem());
        QVERIFY2(gridItem, "Could not find GridItem in QML tree");

        // Process a couple more event loop turns so the grid_line + flush
        // events for the visual highlight definitely reach the renderer.
        for (int i = 0; i < 5; ++i) waitForFlush(&conn, 200);

        const qreal cw = gridItem->cellWidth();
        const qreal ch = gridItem->cellHeight();
        QVERIFY(cw > 0.0 && ch > 0.0);

        // Integer-snap invariant — the whole point of this fix.
        QCOMPARE(cw, std::round(cw));
        QCOMPARE(ch, std::round(ch));

        const QImage img = grabItem(gridItem);
        QVERIFY2(!img.isNull(), "grabToImage returned null");
        QVERIFY(img.width() > 0 && img.height() > 0);

        const QColor defaultBg = conn.highlights()->defaultBg();
        const QRgb   defBgRgb  = defaultBg.rgb();

        // Walk a vertical strip in the middle of the selected text. For
        // visual-line, every column of rows 0..2 is part of the selection,
        // so a column 5 cells in from the left should be selection-coloured
        // for the full height of those three rows. We allow a small channel
        // tolerance (cursor blink, AA) but require any "default-bg" pixel
        // count on the strip to be small — a horizontal seam between rows
        // would produce a full row-width worth of default-bg pixels at
        // exactly y = ch and y = 2*ch, which is what we're guarding against.
        const int sampleX = static_cast<int>(cw * 5 + cw / 2.0);
        const int yTop    = 0;
        const int yBot    = static_cast<int>(ch * 3);
        QVERIFY(sampleX < img.width());
        QVERIFY(yBot    <= img.height());

        int defBgHits = 0;
        for (int y = yTop; y < yBot; ++y) {
            const QRgb px = img.pixel(sampleX, y);
            if (channelDelta(px, defBgRgb) <= 2) ++defBgHits;
        }

        // If the seam exists, at each row boundary we'd see at least one
        // pixel row of default-bg — i.e. >= 2 default-bg hits for two
        // boundaries. Budget 1 to absorb AA jitter at the very top edge.
        QVERIFY2(defBgHits <= 1,
            qPrintable(QStringLiteral(
                "Found %1 default-bg pixels along the selected column strip "
                "(y=[%2,%3), x=%4). A horizontal seam between selected rows "
                "indicates fractional cell metrics. cellWidth=%5 cellHeight=%6")
                .arg(defBgHits).arg(yTop).arg(yBot).arg(sampleX).arg(cw).arg(ch)));
    }
};

QTEST_MAIN(TestVisualSelectionNoSeams)
#include "test_visual_selection_no_seams.moc"
