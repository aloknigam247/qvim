#include <QSignalSpy>
#include <QtTest>

#include "GridModel.h"
#include "IntegrationHelpers.h"
#include "NvimConnector.h"
#include "ResizeCoalescer.h"

using namespace qvim;
using namespace qvim::test;

class TestResizeSmoothness : public QObject {
    Q_OBJECT
private slots:
    void burstResizeProducesFewRpcsAndConvergesToFinal() {
        NvimConnector conn;
        QVERIFY(conn.start(locateNvim()));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        // Tighten the debounce so the test runs quickly but still exercises
        // the coalescer's "swallow burst → emit final" behaviour.
        conn.resizeCoalescer()->setIntervalMs(15);

        QSignalSpy sizeSpy(conn.grid(), &GridModel::sizeChanged);
        sizeSpy.clear();

        // Simulate 30 rapid drag-edge requests across an interpolated path.
        const int finalCols = 110;
        const int finalRows = 32;
        for (int i = 1; i <= 30; ++i) {
            const int c = 80 + ((finalCols - 80) * i) / 30;
            const int r = 24 + ((finalRows - 24) * i) / 30;
            conn.requestResize(c, r);
            QTest::qWait(1);
        }

        // Wait for debounce + nvim round-trip + a few extra flushes.
        for (int i = 0; i < 10; ++i) {
            if (conn.grid()->cols() == finalCols && conn.grid()->rows() == finalRows) break;
            waitForFlush(&conn, 500);
        }

        QCOMPARE(conn.grid()->cols(), finalCols);
        QCOMPARE(conn.grid()->rows(), finalRows);

        // sizeChanged also fires on hl/active changes — but at minimum we
        // must NOT have one fire per request. 5 is a comfortable bound:
        // a few are expected during attach/flush plus the single coalesced
        // resize round-trip.
        QVERIFY2(sizeSpy.count() <= 5,
                 qPrintable(QStringLiteral("expected <= 5 sizeChanged fires, got %1")
                                .arg(sizeSpy.count())));
    }
};

QTEST_GUILESS_MAIN(TestResizeSmoothness)
#include "test_resize_smoothness.moc"
