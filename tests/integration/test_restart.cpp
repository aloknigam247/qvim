#include "IntegrationHelpers.h"
#include <QtTest>

using namespace qvim;
using namespace qvim::test;

class TestRestart : public QObject {
    Q_OBJECT
private slots:
    void restartReconnectsToNewServer() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));
        QVERIFY(conn.attachUi(80, 24));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        // The `restart` UI event landed with nvim API level 14. On older servers
        // :restart just quits, so there is nothing to exercise.
        const auto apiLevel = evalSync(conn, QStringLiteral("api_info().version.api_level"));
        if(!apiLevel || apiLevel->toInt() < 14) {
            QSKIP("nvim API level < 14: :restart UI event unavailable");
        }

        // Marker + pid on the ORIGINAL server; neither survives a real handoff to a
        // freshly spawned server.
        conn.command(QStringLiteral("let g:qvim_before_restart = 1"));
        const auto oldPid = evalSync(conn, QStringLiteral("getpid()"));
        QVERIFY(oldPid && oldPid->toInt() > 0);

        // A disconnected() during the handoff would mean we mistook :restart for a
        // real quit (the pre-fix behaviour, wired to app exit in main.cpp).
        QSignalSpy disconnectedSpy(&conn, &NvimConnector::disconnected);

        // Trigger the restart. `:restart!` quits the old server (default `qall`) and
        // hands us the new server's listen_addr; the old channel EOFs, we reconnect
        // and re-attach, so queries then hit the NEW server. The buffer is left
        // unmodified so the default `qall` isn't blocked by "No write since change".
        conn.command(QStringLiteral("restart!"));

        // The handoff is asynchronous: the old process keeps answering until it
        // EOFs, so poll getpid() until it reports a different process (the new
        // server) rather than assuming the first post-restart flush is the new one.
        int newPidVal = 0;
        QElapsedTimer handoff;
        handoff.start();
        while(handoff.elapsed() < 20000) {
            const auto p = evalSync(conn, QStringLiteral("getpid()"), 1000);
            if(p && p->toInt() > 0 && p->toInt() != oldPid->toInt()) {
                newPidVal = p->toInt();
                break;
            }
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        // The reconnected server is a genuinely different process...
        QVERIFY2(newPidVal != 0, "did not reconnect to a new nvim process after :restart");

        // ...and it is fresh: the pre-restart marker did not survive.
        const auto exists = evalSync(conn, QStringLiteral("exists('g:qvim_before_restart')"), 5000);
        QVERIFY(exists);
        QCOMPARE(exists->toInt(), 0);

        // Re-attach reused the single-source-of-truth options: ext_hlstate on,
        // ext_multigrid off (see packAttachOptions). Query them as scalars — the
        // test's getVar round-trip can't represent the nvim_list_uis() dicts.
        const auto extHl =
            evalSync(conn, QStringLiteral("get(nvim_list_uis()[0], 'ext_hlstate', v:false)"), 5000);
        QVERIFY(extHl);
        QCOMPARE(extHl->toBool(), true);
        const auto extMg = evalSync(
            conn, QStringLiteral("get(nvim_list_uis()[0], 'ext_multigrid', v:false)"), 5000);
        QVERIFY(extMg);
        QCOMPARE(extMg->toBool(), false);

        // The connection stayed up across the handoff and is attached again.
        QCOMPARE(disconnectedSpy.count(), 0);
        QVERIFY(conn.attached());
        QCOMPARE(conn.grid()->cols(), 80);
        QCOMPARE(conn.grid()->rows(), 24);
    }
};

QTEST_GUILESS_MAIN(TestRestart)
#include "test_restart.moc"
