#include "IntegrationHelpers.h"
#include <QtTest>

using namespace qvim;
using namespace qvim::test;

class TestAttachAndRender : public QObject {
    Q_OBJECT
private slots:
    void attachProducesGrid() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));
        QCOMPARE(conn.grid()->cols(), 80);
        QCOMPARE(conn.grid()->rows(), 24);
    }
};

QTEST_GUILESS_MAIN(TestAttachAndRender)
#include "test_attach_and_render.moc"
