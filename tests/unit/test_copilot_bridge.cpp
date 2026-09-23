#include <QtTest>

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketServer>

#include "ChatModel.h"
#include "CopilotBridgeClient.h"

using namespace qvim;

namespace {
QString compact(const QJsonObject &obj) {
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject parse(const QString &text) { return QJsonDocument::fromJson(text.toUtf8()).object(); }

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

// A minimal fake copilot-bridge hub: a plain WebSocketServer that records the
// client's `hello` + `inject` frames and can push mirror traffic.
class FakeHub : public QObject {
    Q_OBJECT
public:
    explicit FakeHub(QObject *parent = nullptr) :
        QObject(parent), m_server(new QWebSocketServer(QStringLiteral("fake-hub"),
                                                       QWebSocketServer::NonSecureMode, this)) {
        connect(m_server, &QWebSocketServer::newConnection, this, [this]() {
            while(m_server->hasPendingConnections()) {
                QWebSocket *s = m_server->nextPendingConnection();
                s->setParent(this);
                m_client = s;
                connect(s, &QWebSocket::textMessageReceived, this,
                        [this](const QString &m) { received << m; });
            }
        });
    }

    bool listen() { return m_server->listen(QHostAddress(QHostAddress::LocalHost), 0); }
    quint16 port() const { return m_server->serverPort(); }
    bool hasClient() const {
        return m_client && m_client->state() == QAbstractSocket::ConnectedState;
    }
    void send(const QJsonObject &frame) {
        if(m_client) m_client->sendTextMessage(compact(frame));
    }

    QStringList received;

private:
    QWebSocketServer *m_server;
    QPointer<QWebSocket> m_client;
};

class TestCopilotBridge : public QObject {
    Q_OBJECT

private slots:
    void handshakeAndMirror();
    void toolRequestShowsAskOptions();
    void injectSendsPrompt();
    void inactiveNeverConnects();
    void toolArgAndResultVariants();
    void propertyMutatorsAndSessionFrames();
};

// Connecting sends the client `hello`, and mirror traffic lands in the sink
// ChatModel as user / assistant / system blocks.
void TestCopilotBridge::handshakeAndMirror() {
    FakeHub hub;
    QVERIFY(hub.listen());

    ChatModel model;
    CopilotBridgeClient client;
    client.setSink(&model);
    client.setUrl(QStringLiteral("ws://127.0.0.1:%1").arg(hub.port()));
    client.setActive(true);

    QVERIFY(waitUntil([&] { return client.isConnected(); }, 5000));
    QVERIFY(waitUntil([&] { return !hub.received.isEmpty(); }, 5000));

    const QJsonObject hello = parse(hub.received.first());
    QCOMPARE(hello.value(QStringLiteral("type")).toString(), QStringLiteral("hello"));
    QCOMPARE(hello.value(QStringLiteral("role")).toString(), QStringLiteral("client"));

    hub.send(QJsonObject{
        { QStringLiteral("type"), QStringLiteral("user.prompt") },
        { QStringLiteral("sessionId"), QStringLiteral("s1") },
        { QStringLiteral("data"),
          QJsonObject{ { QStringLiteral("prompt"), QStringLiteral("hello there") } } },
    });
    hub.send(QJsonObject{
        { QStringLiteral("type"), QStringLiteral("assistant.message") },
        { QStringLiteral("sessionId"), QStringLiteral("s1") },
        { QStringLiteral("data"),
          QJsonObject{ { QStringLiteral("content"), QStringLiteral("general kenobi") } } },
    });
    hub.send(QJsonObject{
        { QStringLiteral("type"), QStringLiteral("tool.requested") },
        { QStringLiteral("sessionId"), QStringLiteral("s1") },
        { QStringLiteral("data"),
          QJsonObject{
              { QStringLiteral("toolName"), QStringLiteral("powershell") },
              { QStringLiteral("phase"), QStringLiteral("pending") },
              // The hub sends toolArgs as a JSON *string*, not a nested object.
              { QStringLiteral("toolArgs"),
                compact(QJsonObject{ { QStringLiteral("command"),
                                       QStringLiteral("ctest --preset release") } }) } } } });

    QVERIFY(waitUntil([&] { return model.count() >= 3; }, 5000));
    QCOMPARE(model.authorAt(0), QStringLiteral("user"));
    QCOMPARE(model.textAt(0), QStringLiteral("[s1] hello there"));
    QCOMPARE(model.authorAt(1), QStringLiteral("assistant"));
    QCOMPARE(model.textAt(1), QStringLiteral("[s1] general kenobi"));
    QCOMPARE(model.authorAt(2), QStringLiteral("system"));
    QVERIFY(model.textAt(2).startsWith(QStringLiteral("[s1] ")));
    QVERIFY(model.textAt(2).contains(QStringLiteral("powershell")));
    // The tool's arguments are surfaced, not just its name.
    QVERIFY(model.textAt(2).contains(QStringLiteral("ctest --preset release")));

    // tool.complete's result is a JSON string wrapping a richer payload; the
    // human-readable content is extracted, not the raw JSON.
    hub.send(QJsonObject{
        { QStringLiteral("type"), QStringLiteral("tool.complete") },
        { QStringLiteral("sessionId"), QStringLiteral("s1") },
        { QStringLiteral("data"),
          QJsonObject{ { QStringLiteral("toolName"), QStringLiteral("powershell") },
                       { QStringLiteral("success"), true },
                       { QStringLiteral("result"),
                         compact(QJsonObject{ { QStringLiteral("content"),
                                                QStringLiteral("All tests passed") } }) } } } });

    QVERIFY(waitUntil([&] { return model.count() >= 4; }, 5000));
    QCOMPARE(model.authorAt(3), QStringLiteral("system"));
    QVERIFY(model.textAt(3).contains(QStringLiteral("All tests passed")));
    QVERIFY(!model.textAt(3).contains(QStringLiteral("content")));
}

// tool.requested for an ask_user-style tool surfaces the question and its
// selectable options extracted from the requestedSchema.
void TestCopilotBridge::toolRequestShowsAskOptions() {
    FakeHub hub;
    QVERIFY(hub.listen());

    ChatModel model;
    CopilotBridgeClient client;
    client.setSink(&model);
    client.setUrl(QStringLiteral("ws://127.0.0.1:%1").arg(hub.port()));
    client.setActive(true);
    QVERIFY(waitUntil([&] { return client.isConnected(); }, 5000));
    QVERIFY(waitUntil([&] { return !hub.received.isEmpty(); }, 5000));

    QJsonObject schema{ { QStringLiteral("properties"),
                          QJsonObject{
                              { QStringLiteral("database"),
                                QJsonObject{ { QStringLiteral("enum"),
                                               QJsonArray{ QStringLiteral("PostgreSQL"),
                                                           QStringLiteral("SQLite") } } } } } } };
    QJsonObject toolArgs{
        { QStringLiteral("message"), QStringLiteral("Which database?") },
        { QStringLiteral("requestedSchema"), schema },
    };
    hub.send(QJsonObject{ { QStringLiteral("type"), QStringLiteral("tool.requested") },
                          { QStringLiteral("sessionId"), QStringLiteral("s1") },
                          { QStringLiteral("data"),
                            QJsonObject{ { QStringLiteral("toolName"), QStringLiteral("ask_user") },
                                         { QStringLiteral("toolArgs"), compact(toolArgs) } } } });

    QVERIFY(waitUntil([&] { return model.count() >= 1; }, 5000));
    const QString line = model.textAt(0);
    QVERIFY(line.contains(QStringLiteral("ask_user")));
    QVERIFY(line.contains(QStringLiteral("Which database?")));
    QVERIFY(line.contains(QStringLiteral("PostgreSQL")));
    QVERIFY(line.contains(QStringLiteral("SQLite")));
}

// inject() sends an `inject` frame carrying the prompt; broadcast omits
// sessionId, a targeted inject includes it. It does not append to the sink
// locally (the prompt returns via the mirror stream).
void TestCopilotBridge::injectSendsPrompt() {
    FakeHub hub;
    QVERIFY(hub.listen());

    ChatModel model;
    CopilotBridgeClient client;
    client.setSink(&model);
    client.setUrl(QStringLiteral("ws://127.0.0.1:%1").arg(hub.port()));
    client.setActive(true);
    QVERIFY(waitUntil([&] { return client.isConnected(); }, 5000));
    QVERIFY(waitUntil([&] { return !hub.received.isEmpty(); }, 5000));

    QVERIFY(!client.inject(QStringLiteral("   "))); // empty prompt rejected
    QVERIFY(client.inject(QStringLiteral("run the tests")));
    QVERIFY(client.inject(QStringLiteral("only you"), QStringLiteral("a1b2c3")));

    QVERIFY(waitUntil([&] {
        int n = 0;
        for(const QString &m: hub.received) {
            if(parse(m).value(QStringLiteral("type")).toString() == QStringLiteral("inject")) ++n;
        }
        return n >= 2;
    }, 5000));

    QJsonObject broadcast;
    QJsonObject targeted;
    for(const QString &m: hub.received) {
        const QJsonObject o = parse(m);
        if(o.value(QStringLiteral("type")).toString() != QStringLiteral("inject")) continue;
        if(o.contains(QStringLiteral("sessionId"))) {
            targeted = o;
        } else {
            broadcast = o;
        }
    }
    QCOMPARE(broadcast.value(QStringLiteral("data"))
                 .toObject()
                 .value(QStringLiteral("prompt"))
                 .toString(),
             QStringLiteral("run the tests"));
    QCOMPARE(targeted.value(QStringLiteral("sessionId")).toString(), QStringLiteral("a1b2c3"));
    QCOMPARE(targeted.value(QStringLiteral("data"))
                 .toObject()
                 .value(QStringLiteral("prompt"))
                 .toString(),
             QStringLiteral("only you"));

    // Injection does not locally echo — display comes back through the mirror.
    QCOMPARE(model.count(), 0);
}

// While inactive the client holds no connection, so an inactive panel never
// reaches out to the hub.
void TestCopilotBridge::inactiveNeverConnects() {
    FakeHub hub;
    QVERIFY(hub.listen());

    ChatModel model;
    CopilotBridgeClient client;
    client.setSink(&model);
    client.setUrl(QStringLiteral("ws://127.0.0.1:%1").arg(hub.port()));

    // Give any (erroneous) connection attempt time to land.
    QElapsedTimer t;
    t.start();
    while(t.elapsed() < 300) { QCoreApplication::processEvents(QEventLoop::AllEvents, 25); }

    QVERIFY(!client.isConnected());
    QVERIFY(!hub.hasClient());
}

// The tool-summary formatters cover more toolArgs / result shapes than the
// mirror test exercises: oneOf / items schema options, command / path / pattern
// arg keys, the compact-JSON fallback, and a non-string result.
void TestCopilotBridge::toolArgAndResultVariants() {
    FakeHub hub;
    QVERIFY(hub.listen());

    ChatModel model;
    CopilotBridgeClient client;
    client.setSink(&model);
    client.setUrl(QStringLiteral("ws://127.0.0.1:%1").arg(hub.port()));
    client.setActive(true);
    QVERIFY(waitUntil([&] { return client.isConnected(); }, 5000));
    QVERIFY(waitUntil([&] { return !hub.received.isEmpty(); }, 5000));

    auto toolReq = [&](const QJsonObject &toolArgs) {
        hub.send(
            QJsonObject{ { QStringLiteral("type"), QStringLiteral("tool.requested") },
                         { QStringLiteral("sessionId"), QStringLiteral("s1") },
                         { QStringLiteral("data"),
                           QJsonObject{ { QStringLiteral("toolName"), QStringLiteral("t") },
                                        { QStringLiteral("toolArgs"), compact(toolArgs) } } } });
    };

    // oneOf options in the requestedSchema.
    QJsonObject oneOfField{
        { QStringLiteral("oneOf"),
          QJsonArray{ QJsonObject{ { QStringLiteral("title"), QStringLiteral("Fast") } },
                      QJsonObject{ { QStringLiteral("title"), QStringLiteral("Slow") } } } }
    };
    QJsonObject oneOfSchema{ { QStringLiteral("properties"),
                               QJsonObject{ { QStringLiteral("mode"), oneOfField } } } };
    toolReq(QJsonObject{ { QStringLiteral("message"), QStringLiteral("Pick") },
                         { QStringLiteral("requestedSchema"), oneOfSchema } });

    // items.enum and items.anyOf (array-valued fields).
    QJsonObject itemsInner{
        { QStringLiteral("enum"), QJsonArray{ QStringLiteral("cpp") } },
        { QStringLiteral("anyOf"),
          QJsonArray{ QJsonObject{ { QStringLiteral("title"), QStringLiteral("Rust") } } } },
    };
    QJsonObject itemsField{ { QStringLiteral("items"), itemsInner } };
    QJsonObject itemsSchema{ { QStringLiteral("properties"),
                               QJsonObject{ { QStringLiteral("langs"), itemsField } } } };
    toolReq(QJsonObject{ { QStringLiteral("message"), QStringLiteral("Langs") },
                         { QStringLiteral("requestedSchema"), itemsSchema } });

    toolReq(QJsonObject{ { QStringLiteral("path"), QStringLiteral("src/main.cpp") } });
    toolReq(QJsonObject{ { QStringLiteral("pattern"), QStringLiteral("TODO") } });
    toolReq(QJsonObject{ { QStringLiteral("unrecognised"), QStringLiteral("blob") } });

    // toolArgs that is neither a JSON object nor a JSON string: asObject() yields
    // an empty object, so the line is just the bare tool name.
    hub.send(QJsonObject{ { QStringLiteral("type"), QStringLiteral("tool.requested") },
                          { QStringLiteral("sessionId"), QStringLiteral("s1") },
                          { QStringLiteral("data"),
                            QJsonObject{ { QStringLiteral("toolName"), QStringLiteral("bare") },
                                         { QStringLiteral("toolArgs"), 7 } } } });

    // A non-string result yields no result text; the line is just name + mark.
    hub.send(QJsonObject{
        { QStringLiteral("type"), QStringLiteral("tool.complete") },
        { QStringLiteral("sessionId"), QStringLiteral("s1") },
        { QStringLiteral("data"), QJsonObject{ { QStringLiteral("toolName"), QStringLiteral("t") },
                                               { QStringLiteral("success"), false },
                                               { QStringLiteral("result"), 123 } } } });

    // A plain-string result (not JSON) is surfaced verbatim.
    hub.send(
        QJsonObject{ { QStringLiteral("type"), QStringLiteral("tool.complete") },
                     { QStringLiteral("sessionId"), QStringLiteral("s1") },
                     { QStringLiteral("data"),
                       QJsonObject{ { QStringLiteral("toolName"), QStringLiteral("u") },
                                    { QStringLiteral("success"), true },
                                    { QStringLiteral("result"), QStringLiteral("done") } } } });

    QVERIFY(waitUntil([&] { return model.count() >= 8; }, 5000));
    QVERIFY(model.textAt(0).contains(QStringLiteral("Fast")));
    QVERIFY(model.textAt(0).contains(QStringLiteral("Slow")));
    QVERIFY(model.textAt(1).contains(QStringLiteral("cpp")));
    QVERIFY(model.textAt(1).contains(QStringLiteral("Rust")));
    QVERIFY(model.textAt(2).contains(QStringLiteral("src/main.cpp")));
    QVERIFY(model.textAt(3).contains(QStringLiteral("TODO")));
    QVERIFY(model.textAt(4).contains(QStringLiteral("unrecognised")));
    QVERIFY(model.textAt(5).contains(QStringLiteral("bare")));
    QVERIFY(model.textAt(6).contains(QStringLiteral("\u2717")));
    QVERIFY(model.textAt(7).contains(QStringLiteral("done")));
    QCOMPARE(client.sink(), &model);
}

// Property mutators (showTools, active, url) and the session.start / session.end
// mirror frames.
void TestCopilotBridge::propertyMutatorsAndSessionFrames() {
    FakeHub hub;
    QVERIFY(hub.listen());

    ChatModel model;
    CopilotBridgeClient client;
    client.setSink(&model);

    QSignalSpy showSpy(&client, &CopilotBridgeClient::showToolsChanged);
    QVERIFY(client.showTools()); // default on
    client.setShowTools(false);
    client.setShowTools(false); // redundant — no second signal
    QVERIFY(!client.showTools());
    QCOMPARE(showSpy.count(), 1);
    client.setShowTools(true);

    QVERIFY(!client.isActive());
    client.setUrl(QStringLiteral("ws://127.0.0.1:%1").arg(hub.port()));
    QCOMPARE(client.url(), QStringLiteral("ws://127.0.0.1:%1").arg(hub.port()));
    client.setActive(true);
    QVERIFY(waitUntil([&] { return client.isConnected(); }, 5000));
    QVERIFY(waitUntil([&] { return !hub.received.isEmpty(); }, 5000));

    hub.send(
        QJsonObject{ { QStringLiteral("type"), QStringLiteral("session.start") },
                     { QStringLiteral("sessionId"), QStringLiteral("s1") },
                     { QStringLiteral("data"),
                       QJsonObject{ { QStringLiteral("cwd"), QStringLiteral("D:/qvim") } } } });
    hub.send(QJsonObject{ { QStringLiteral("type"), QStringLiteral("session.end") },
                          { QStringLiteral("sessionId"), QStringLiteral("s1") } });

    QVERIFY(waitUntil([&] { return model.count() >= 2; }, 5000));
    QVERIFY(model.textAt(0).contains(QStringLiteral("session started")));
    QVERIFY(model.textAt(0).contains(QStringLiteral("D:/qvim")));
    QVERIFY(model.textAt(1).contains(QStringLiteral("session ended")));

    // Rebinding the URL while active tears down and reopens against the new
    // endpoint.
    FakeHub hub2;
    QVERIFY(hub2.listen());
    client.setUrl(QStringLiteral("ws://127.0.0.1:%1").arg(hub2.port()));
    QVERIFY(waitUntil([&] { return hub2.hasClient(); }, 5000));

    // Deactivating tears the socket down.
    client.setActive(false);
    QVERIFY(waitUntil([&] { return !client.isConnected(); }, 5000));
}

QTEST_GUILESS_MAIN(TestCopilotBridge)
#include "test_copilot_bridge.moc"
