#include <QtTest>
#include "IntegrationHelpers.h"

using namespace qvim;
using namespace qvim::test;

class TestCmdline : public QObject {
    Q_OBJECT
private slots:
    void colonShowsCmdline() {
        NvimConnector conn;
        QVERIFY(conn.start(locateNvim()));
        QVERIFY(conn.attachUi(40, 10));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.input(QStringLiteral(":echo \"hi\""));
        for (int i = 0; i < 10; ++i) {
            if (conn.cmdline()->visible()) break;
            waitForFlush(&conn, 1000);
        }
        QVERIFY(conn.cmdline()->visible());
        QVERIFY(conn.cmdline()->content().contains(QStringLiteral("echo")));
    }
};

QTEST_GUILESS_MAIN(TestCmdline)
#include "test_cmdline.moc"
