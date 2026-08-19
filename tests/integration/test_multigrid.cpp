#include "IntegrationHelpers.h"
#include <QtTest>

using namespace qvim;
using namespace qvim::test;

class TestMultigrid : public QObject {
    Q_OBJECT
private slots:
    // ext_multigrid is disabled in NvimConnector::attachUi (diagnostic mode),
    // so a :vsplit splits within the global grid (id=1) without allocating
    // per-window grids. Assert the disabled state — this test starts failing
    // the moment ext_multigrid is re-enabled, which is the cue to update it
    // to assert the multi-grid behaviour it was originally written for.
    void vsplitDoesNotAllocateExtraGrid() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.command(QStringLiteral("vsplit"));
        for(int i = 0; i < 5; ++i) waitForFlush(&conn, 500);

        const QList<int> ids = conn.grid()->gridIds();
        QVERIFY2(ids.size() == 1,
                 qPrintable(QStringLiteral("expected only the global grid, got %1 grids "
                                           "— ext_multigrid may have been re-enabled")
                                .arg(ids.size())));
    }

    // Cursor lives on the global grid (id=1) when ext_multigrid is off.
    void cursorStaysOnGlobalGrid() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.command(QStringLiteral("vsplit"));
        for(int i = 0; i < 5; ++i) waitForFlush(&conn, 500);

        QCOMPARE(conn.grid()->activeGrid(), 1);
    }
};

QTEST_GUILESS_MAIN(TestMultigrid)
#include "test_multigrid.moc"
