#include <QtTest>
#include "IntegrationHelpers.h"

using namespace qvim;
using namespace qvim::test;

class TestInsertAndQuit : public QObject {
    Q_OBJECT
private slots:
    void typeAbcThenAssert() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(40, 10));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        conn.input(QStringLiteral("iabc<Esc>"));
        // Allow several redraw flushes; the last flush should carry the final state.
        for (int i = 0; i < 5; ++i) waitForFlush(&conn, 1000);

        const QString dump = conn.grid()->dumpAscii();
        QVERIFY2(dump.startsWith(QStringLiteral("abc")),
                 qPrintable(QStringLiteral("expected row 0 starts with 'abc', got first row: '%1'")
                                .arg(dump.left(40))));
    }

    void quitCommandClosesProcess() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(20, 5));
        QVERIFY(waitForAttach(&conn));

        QSignalSpy spy(&conn, &NvimConnector::disconnected);
        conn.command(QStringLiteral("qa!"));
        QVERIFY(spy.wait(5000));
    }
};

QTEST_GUILESS_MAIN(TestInsertAndQuit)
#include "test_insert_and_quit.moc"
