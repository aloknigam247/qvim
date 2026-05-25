#include <QtTest>
#include "IntegrationHelpers.h"

using namespace qvim;
using namespace qvim::test;

class TestPopupmenu : public QObject {
    Q_OBJECT
private slots:
    void completionShowsItems() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(40, 10));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        // Force the popupmenu to show even when a single match would otherwise auto-insert.
        conn.command(QStringLiteral("set completeopt=menuone,noinsert"));
        // Multiple words starting with "alp" so completion has options to show.
        conn.input(QStringLiteral("ialpha alpine alphabet alp"));
        for (int i = 0; i < 5; ++i) waitForFlush(&conn, 1000);
        conn.input(QStringLiteral("<C-x><C-n>"));
        for (int i = 0; i < 10; ++i) {
            if (conn.popupmenu()->visible() && conn.popupmenu()->rowCount() > 0) break;
            waitForFlush(&conn, 1000);
        }
        QVERIFY(conn.popupmenu()->visible());
        QVERIFY(conn.popupmenu()->rowCount() > 0);
    }
};

QTEST_GUILESS_MAIN(TestPopupmenu)
#include "test_popupmenu.moc"
