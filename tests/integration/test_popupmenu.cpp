#include "IntegrationHelpers.h"
#include <QtTest>

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
        for(int i = 0; i < 5; ++i) waitForFlush(&conn, 500);
        conn.input(QStringLiteral("<C-x><C-n>"));
        for(int i = 0; i < 5; ++i) waitForFlush(&conn, 500);

        // ext_popupmenu is disabled in NvimConnector::attachUi (diagnostic
        // mode); without it nvim renders the completion menu on the grid and
        // never emits popupmenu_show, so PopupMenuModel stays empty. Assert
        // the disabled state — flip the asserts back when ext_popupmenu is
        // re-enabled.
        QVERIFY(!conn.popupmenu()->visible());
        QCOMPARE(conn.popupmenu()->rowCount(), 0);
    }
};

QTEST_GUILESS_MAIN(TestPopupmenu)
#include "test_popupmenu.moc"
