#include "IntegrationHelpers.h"
#include <QtTest>

using namespace qvim;
using namespace qvim::test;

class TestResize : public QObject {
    Q_OBJECT
private slots:
    void resizeReachesGridModel() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(40, 10));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.tryResize(60, 15);
        for(int i = 0; i < 5; ++i) waitForFlush(&conn, 1000);

        QCOMPARE(conn.grid()->cols(), 60);
        QCOMPARE(conn.grid()->rows(), 15);
    }
};

QTEST_GUILESS_MAIN(TestResize)
#include "test_resize.moc"
