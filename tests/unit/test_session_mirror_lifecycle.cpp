// Tier-1 lifecycle test for SessionMirrorServer: the port / address mutators,
// the invalid-address fallback in startListening(), and the client-disconnect
// cleanup path. The transcript-forwarding protocol is covered end-to-end by the
// integration test; here we drive only the standalone socket lifecycle.

#include <QAbstractSocket>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QtTest>
#include <QWebSocket>

#include "SessionMirrorServer.h"

using namespace qvim;

namespace {
template <typename Pred>
bool waitUntil(Pred pred, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while(!pred() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 25);
    }
    return pred();
}
} // namespace

class TestSessionMirrorLifecycle : public QObject {
    Q_OBJECT
private slots:
    void portMutatorFiresOnceOnChange() {
        SessionMirrorServer s;
        QCOMPARE(s.source(), nullptr);
        QSignalSpy spy(&s, &SessionMirrorServer::portChanged);
        s.setPort(9100);
        s.setPort(9100); // redundant — no second signal
        QCOMPARE(s.port(), 9100);
        QCOMPARE(spy.count(), 1);
    }

    void addressMutatorFiresOnceOnChange() {
        SessionMirrorServer s;
        QSignalSpy spy(&s, &SessionMirrorServer::addressChanged);
        s.setAddress(QStringLiteral("127.0.0.1"));
        s.setAddress(QStringLiteral("127.0.0.1")); // redundant
        QCOMPARE(s.address(), QStringLiteral("127.0.0.1"));
        QCOMPARE(spy.count(), 1);
    }

    void invalidAddressFallsBackToAnyIPv4() {
        SessionMirrorServer s;
        s.setPort(0); // OS-assigned free port
        s.setAddress(QStringLiteral("definitely.not.an.address"));
        s.setActive(true);
        // Despite the unparseable address, the server still binds (AnyIPv4).
        QVERIFY(waitUntil([&] { return s.serverPort() != 0; }, 5000));
        s.setActive(false);
        QCOMPARE(s.serverPort(), quint16(0));
    }

    void clientDisconnectIsCleanedUp() {
        SessionMirrorServer s;
        s.setPort(0);
        s.setAddress(QStringLiteral("127.0.0.1"));
        s.setActive(true);
        QVERIFY(waitUntil([&] { return s.serverPort() != 0; }, 5000));

        QWebSocket client;
        client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(s.serverPort())));
        QVERIFY(waitUntil([&] { return client.state() == QAbstractSocket::ConnectedState; }, 5000));

        client.close();
        QVERIFY(
            waitUntil([&] { return client.state() == QAbstractSocket::UnconnectedState; }, 5000));
        // Give the server's disconnect slot a chance to run; it must not crash
        // and the endpoint stays bound for further subscribers.
        QVERIFY(waitUntil([&] { return false; }, 200) == false);
        QVERIFY(s.serverPort() != 0);
    }
};

QTEST_GUILESS_MAIN(TestSessionMirrorLifecycle)
#include "test_session_mirror_lifecycle.moc"
