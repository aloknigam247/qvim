#include <QtTest>

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QWebSocket>

#include "ChatModel.h"
#include "SessionMirrorServer.h"

using namespace qvim;

namespace {
QString compact(const QJsonObject& obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject parse(const QString& text) {
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

QString typeOf(const QString& frame) {
    return parse(frame).value(QStringLiteral("type")).toString();
}

template <typename Pred>
bool waitUntil(Pred pred, int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (!pred() && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 25);
    }
    return pred();
}
} // namespace

class TestSessionMirror : public QObject {
    Q_OBJECT

private slots:
    void echoMirrorsPanelSession();
    void activeToggleReleasesPort();
};

// The remote client drives the *real* panel model: its `input` is fed through
// ChatModel::submit(), and the frames it receives are that model's own streamed
// echo, proving the session the LAN sees is the desktop chat session — not a
// parallel echo.
void TestSessionMirror::echoMirrorsPanelSession() {
    ChatModel model;
    SessionMirrorServer server;
    server.setSource(&model);
    server.setPort(0); // OS-assigned ephemeral port — no fixed-8765 collision.
    server.setActive(true);
    QVERIFY(server.isActive());
    const quint16 port = server.serverPort();
    QVERIFY(port != 0);

    QWebSocket client;
    QStringList frames;
    connect(&client, &QWebSocket::textMessageReceived,
            &client, [&frames](const QString& m) { frames << m; });

    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port)));
    QVERIFY(waitUntil([&] { return client.state() == QAbstractSocket::ConnectedState; }, 5000));

    // Frame 0: hello, sent first, no seq.
    QVERIFY(waitUntil([&] { return frames.size() >= 1; }, 5000));
    const QJsonObject hello = parse(frames.at(0));
    QCOMPARE(hello.value(QStringLiteral("type")).toString(), QStringLiteral("hello"));
    QCOMPARE(hello.value(QStringLiteral("protocol")).toInt(), 1);
    QVERIFY(!hello.value(QStringLiteral("sessionId")).toString().isEmpty());
    QVERIFY(!hello.contains(QStringLiteral("seq")));

    // Complete the handshake, then send an input. Message ordering is preserved
    // on a WebSocket, so the consecutive sends are safe.
    client.sendTextMessage(compact(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("resume")},
        {QStringLiteral("lastSeq"), 0},
    }));
    client.sendTextMessage(compact(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("input")},
        {QStringLiteral("text"), QStringLiteral("hi")},
    }));

    // The turn is a streamed echo, so wait for message.end to arrive (delta
    // count is ChatModel's business — do not hard-code it).
    QVERIFY(waitUntil([&] {
        return frames.size() >= 2
            && typeOf(frames.last()) == QStringLiteral("message.end");
    }, 5000));

    // frames: [hello, message(user), message.begin, delta*, message.end]
    const QJsonObject userMsg = parse(frames.at(1));
    QCOMPARE(userMsg.value(QStringLiteral("type")).toString(), QStringLiteral("message"));
    QCOMPARE(userMsg.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    QCOMPARE(userMsg.value(QStringLiteral("text")).toString(), QStringLiteral("hi"));
    const QString userId = userMsg.value(QStringLiteral("id")).toString();
    QVERIFY(!userId.isEmpty());

    const QJsonObject begin = parse(frames.at(2));
    QCOMPARE(begin.value(QStringLiteral("type")).toString(), QStringLiteral("message.begin"));
    QCOMPARE(begin.value(QStringLiteral("role")).toString(), QStringLiteral("assistant"));
    const QString assistantId = begin.value(QStringLiteral("id")).toString();
    QVERIFY(!assistantId.isEmpty());
    QVERIFY(assistantId != userId);

    const int lastIdx = frames.size() - 1;
    const QJsonObject end = parse(frames.at(lastIdx));
    QCOMPARE(end.value(QStringLiteral("type")).toString(), QStringLiteral("message.end"));
    QCOMPARE(end.value(QStringLiteral("id")).toString(), assistantId);

    // Middle frames are the assistant deltas; they all carry the assistant id
    // and reassemble the echoed reply.
    QString reply;
    for (int i = 3; i < lastIdx; ++i) {
        const QJsonObject d = parse(frames.at(i));
        QCOMPARE(d.value(QStringLiteral("type")).toString(), QStringLiteral("message.delta"));
        QCOMPARE(d.value(QStringLiteral("id")).toString(), assistantId);
        reply += d.value(QStringLiteral("text")).toString();
    }
    QCOMPARE(reply, QStringLiteral("Echo: hi"));

    // Every non-hello frame carries a strictly increasing seq.
    quint64 prev = 0;
    for (int i = 1; i < frames.size(); ++i) {
        const QJsonObject f = parse(frames.at(i));
        QVERIFY(f.contains(QStringLiteral("seq")));
        const quint64 seq = static_cast<quint64>(f.value(QStringLiteral("seq")).toInteger());
        QVERIFY(seq > prev);
        prev = seq;
    }

    // The remote input landed in the actual panel model: user block + assistant
    // block.
    QCOMPARE(model.count(), 2);

    client.close();
}

// `active` is the single lifecycle authority: false releases the port and drops
// clients synchronously; true rebinds. This is the automated guard that the
// server only listens while the chat panel is open.
void TestSessionMirror::activeToggleReleasesPort() {
    ChatModel model;
    SessionMirrorServer server;
    server.setSource(&model);
    server.setPort(0);

    server.setActive(true);
    QVERIFY(server.isActive());
    QVERIFY(server.serverPort() != 0);

    QWebSocket client1;
    bool client1Disconnected = false;
    connect(&client1, &QWebSocket::disconnected,
            &client1, [&] { client1Disconnected = true; });
    client1.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.serverPort())));
    QVERIFY(waitUntil([&] { return client1.state() == QAbstractSocket::ConnectedState; }, 5000));

    // Close the panel: port released, the connected client is torn down.
    server.setActive(false);
    QVERIFY(!server.isActive());
    QCOMPARE(server.serverPort(), quint16(0));
    QVERIFY(waitUntil([&] { return client1Disconnected; }, 5000));

    // Reopen: the server rebinds and accepts a fresh subscriber that gets hello.
    server.setActive(true);
    QVERIFY(server.isActive());
    const quint16 rebindPort = server.serverPort();
    QVERIFY(rebindPort != 0);

    QWebSocket client2;
    QStringList frames2;
    connect(&client2, &QWebSocket::textMessageReceived,
            &client2, [&frames2](const QString& m) { frames2 << m; });
    client2.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(rebindPort)));
    QVERIFY(waitUntil([&] { return client2.state() == QAbstractSocket::ConnectedState; }, 5000));
    QVERIFY(waitUntil([&] { return frames2.size() >= 1; }, 5000));
    QCOMPARE(typeOf(frames2.at(0)), QStringLiteral("hello"));

    client2.close();
}

QTEST_GUILESS_MAIN(TestSessionMirror)
#include "test_session_mirror.moc"
