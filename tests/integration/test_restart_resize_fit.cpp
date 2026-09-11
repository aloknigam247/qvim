// Regression guard: a cell-metric change must never leave the grid overshot.
//
// Reported symptom: after :restart the nvim grid ends up TALLER than the qvim
// window (last rows / statusline pushed off the bottom) and stays there.
//
// Root cause (in GridItem::maybeResizeUi): on :restart the freshly spawned nvim
// reports its built-in default guifont (small cells) BEFORE re-sourcing the
// user config's larger font. GridItem applies the small cells and fires a
// resize that OVERSHOOTS the row count to fill the window; the config font then
// re-applies (large cells) and GridItem recomputes the correct, smaller size.
// That corrective resize used to be gated behind a guard that compared the
// freshly-computed size against the GRID MODEL — but while the overshoot resize
// is still in flight the model has not caught up, so it still reads the
// pre-change (correct) size. The guard saw "computed == model", skipped the
// corrective resize, and the overshoot stuck permanently.
//
// This reproduces that exact guard failure deterministically, without relying
// on RPC race timing. ResizeCoalescer batches within a single event-loop tick,
// so applying the small-then-large cell change in ONE tick (no event
// processing between the two) is equivalent to the corrective change arriving
// while the model is still stale: the grid model still reads the fitted size
// when the large-font maybeResizeUi runs. With the bug, the guard skips the
// corrective resize and the coalescer keeps the overshoot; with the fix, the
// corrective resize is always forwarded and the coalescer emits the fitted
// size. Real nvim confirms the resulting grid size end-to-end.

#include <algorithm>

#include <QQuickItem>
#include <QtTest>

#include "GridItem.h"
#include "GridModel.h"
#include "IntegrationHelpers.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

namespace {

void spin(int ms) {
    QElapsedTimer t;
    t.start();
    while(t.elapsed() < ms) { QCoreApplication::processEvents(QEventLoop::AllEvents, 25); }
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

} // namespace

class TestRestartResizeFit : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void correctiveResizeSurvivesStaleModelGuard() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        constexpr qreal kLargePt = 18.0;
        constexpr qreal kSmallPt = 14.0;
        constexpr qreal kWidth = 1000.0;
        constexpr qreal kHeight = 800.0;

        // A fresh GridItem's metrics are computed at the default 14pt, so its
        // cellHeight is the "small font" height. Used only to assert the two
        // font sizes really produce different row counts — otherwise the race
        // below would be a no-op that passes even with the bug present.
        GridItem probe;
        const qreal chSmall = probe.cellHeight();
        QVERIFY(chSmall > 0.0);

        GridItem grid;
        grid.setConnector(&conn);
        grid.setGridId(1);
        grid.setFontSize(kLargePt);
        const qreal chLarge = grid.cellHeight();
        QVERIFY2(chLarge > chSmall, "large font must yield a taller cell than the small font");

        // Baseline: fit the window at the large font. Setting geometry fires
        // maybeResizeUi -> requestResize; drive the loop until nvim settles the
        // grid to the fitted size.
        grid.setWidth(kWidth);
        grid.setHeight(kHeight);
        const int colsLarge = std::max(10, static_cast<int>(kWidth / grid.cellWidth()));
        const int rowsLarge = std::max(3, static_cast<int>(kHeight / grid.cellHeight()));
        QVERIFY2(waitUntil(
                     [&]() {
            return conn.grid()->gridCols(1) == colsLarge && conn.grid()->gridRows(1) == rowsLarge;
        }, 8000),
                 "grid never settled to the fitted large-font size");

        // The small font fills the same window with strictly more rows: that is
        // the overshoot the bug leaves stuck. Guard the test's own validity.
        const int rowsSmall = std::max(3, static_cast<int>(kHeight / chSmall));
        QVERIFY2(rowsSmall > rowsLarge, "precondition: small font must overshoot the row count");

        // The race, in ONE event-loop tick (no processEvents between the two
        // calls, mirroring the corrective change landing before the model
        // catches up): small font -> requestResize(overshoot); large font ->
        // maybeResizeUi recomputes the fitted size while the model still reads
        // the fitted size. The buggy guard sees "computed == model" and drops
        // the corrective resize, leaving the coalescer holding the overshoot.
        grid.setFontSize(kSmallPt);
        grid.setFontSize(kLargePt);

        // Let the coalescer fire and nvim round-trip the resulting resize. The
        // overshoot is a stable steady state (nothing self-corrects it), so a
        // fixed settle is deterministic, not racy.
        spin(1500);

        // With the fix the corrective resize reaches the coalescer, so nvim
        // stays at the fitted size. With the bug nvim is driven to the
        // overshoot (rowsSmall) and stays there — grid taller than the window.
        QCOMPARE(conn.grid()->gridRows(1), rowsLarge);
        QCOMPARE(conn.grid()->gridCols(1), colsLarge);
    }
};

QTEST_MAIN(TestRestartResizeFit)
#include "test_restart_resize_fit.moc"
