#include <QtTest>
#include "IntegrationHelpers.h"

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
        for (int i = 0; i < 5; ++i) waitForFlush(&conn, 1000);

        QVERIFY(conn.tabline()->rowCount() >= initial + 1);
    }
};

QTEST_GUILESS_MAIN(TestTabline)
#include "test_tabline.moc"
