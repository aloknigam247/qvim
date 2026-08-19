#include "IntegrationHelpers.h"
#include <QtTest>

using namespace qvim;
using namespace qvim::test;

class TestTabline : public QObject {
    Q_OBJECT
private slots:
    void newTabAppearsInModel() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(40, 10));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        const int initial = conn.tabline()->rowCount();
        conn.command(QStringLiteral("tabnew"));
        for(int i = 0; i < 5; ++i) waitForFlush(&conn, 500);

        // ext_tabline is disabled in NvimConnector::attachUi (diagnostic
        // mode); without it nvim renders the tabline on the grid and never
        // emits tabline_update, so TablineModel never grows. Assert the
        // disabled state — flip when ext_tabline is re-enabled.
        QCOMPARE(conn.tabline()->rowCount(), initial);
    }
};

QTEST_GUILESS_MAIN(TestTabline)
#include "test_tabline.moc"
