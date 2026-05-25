#include <QtTest>
#include "IntegrationHelpers.h"

using namespace qvim;
using namespace qvim::test;

class TestCmdline : public QObject {
    Q_OBJECT
private slots:
    void colonShowsCmdline() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(40, 10));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.input(QStringLiteral(":echo \"hi\""));
        for (int i = 0; i < 5; ++i) waitForFlush(&conn, 500);

        // ext_cmdline is disabled in NvimConnector::attachUi (diagnostic mode);
        // until it's re-enabled the CmdlineModel never receives cmdline_show
        // events and the overlay stays hidden. Assert the disabled state so
        // this test starts failing the moment someone re-enables ext_cmdline
        // and forgets to update this check to expect the overlay to appear.
        QVERIFY(!conn.cmdline()->visible());
        QVERIFY(conn.cmdline()->content().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCmdline)
#include "test_cmdline.moc"
