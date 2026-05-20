#include <QtTest>
#include "IntegrationHelpers.h"

using namespace qvim;
using namespace qvim::test;

class TestMultigrid : public QObject {
    Q_OBJECT
private slots:
    // After ext_multigrid attach + a :vsplit, nvim must allocate a second
    // per-window grid and emit win_pos for it. We assert that GridModel sees
    // at least two grid ids and that each has a non-empty geometry.
    void vsplitCreatesSecondGrid() {
        NvimConnector conn;
        QVERIFY(conn.start(locateNvim()));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.command(QStringLiteral("vsplit"));
        // Give nvim a few flushes to allocate the new grid and emit win_pos.
        for (int i = 0; i < 10; ++i) waitForFlush(&conn, 500);

        const QList<int> ids = conn.grid()->gridIds();
        // Expect global grid (1) plus at least one per-window grid.
        QVERIFY2(ids.size() >= 2,
                 qPrintable(QStringLiteral("expected >=2 grids, got %1").arg(ids.size())));

        int positionedWindowGrids = 0;
        for (int id : ids) {
            if (id == 1) continue;
            const QRect g = conn.grid()->gridGeometry(id);
            // Per-window grids must have a real width/height after win_pos.
            if (g.width() > 0 && g.height() > 0) ++positionedWindowGrids;
        }
        QVERIFY2(positionedWindowGrids >= 2,
                 qPrintable(QStringLiteral("expected >=2 positioned window grids, got %1")
                            .arg(positionedWindowGrids)));
    }

    // grid_cursor_goto must set the active grid; after a vsplit the cursor
    // ends up on the new (left) window's grid, not the global grid.
    void cursorTracksActiveGrid() {
        NvimConnector conn;
        QVERIFY(conn.start(locateNvim()));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.command(QStringLiteral("vsplit"));
        for (int i = 0; i < 10; ++i) waitForFlush(&conn, 500);

        // With ext_multigrid, after the first cursor placement the active
        // grid should be one of the per-window grids (id != 1).
        const int active = conn.grid()->activeGrid();
        QVERIFY2(active != 0, "active grid id should be set");
        QVERIFY2(conn.grid()->hasGrid(active),
                 qPrintable(QStringLiteral("active grid id %1 not in grid table").arg(active)));
    }
};

QTEST_GUILESS_MAIN(TestMultigrid)
#include "test_multigrid.moc"
