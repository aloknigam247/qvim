// Tier 2 — chat panel end-to-end through the real Main.qml scene and a live
// nvim. Drives input via QTest::mouseClick / QTest::keyClick through Qt's focus
// chain (never NvimConnector::input directly) so a focus-handoff regression
// actually fails the test. Verifies, in order:
//   1. panel hidden by default; clicking the toggle button opens it and moves
//      focus to the chat input;
//   2. typing in the chat injects the line into the copilot bridge (a fake hub)
//      and produces NO local echo; a mirror reply from the hub then appears in
//      the transcript via appendBlock;
//   3. opening the panel actually shrinks the nvim grid (proves the anchor ->
//      geometryChange -> requestResize path, not a direct geometry poke);
//   4. the panel subtree renders (>1 distinct colour);
//   5. Escape closes the panel, restores grid columns, and hands focus back to
//      the grid so keystrokes reach nvim again.

#include <QAbstractSocket>
#include <QColor>
#include <QHash>
#include <QHostAddress>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSharedPointer>
#include <QStringList>
#include <QtTest>
#include <QWebSocket>
#include <QWebSocketServer>

#include "ChatModel.h"
#include "CopilotBridgeClient.h"
#include "IntegrationHelpers.h"
#include "NvimConnector.h"

using namespace qvim;
using namespace qvim::test;

namespace {

QQuickWindow *loadMainQml(QQmlApplicationEngine &engine, NvimConnector *conn) {
    engine.rootContext()->setContextProperty(QStringLiteral("$connector"), conn);
    engine.loadFromModule(QStringLiteral("Qvim"), QStringLiteral("Main"));
    if(engine.rootObjects().isEmpty()) return nullptr;
    return qobject_cast<QQuickWindow *>(engine.rootObjects().first());
}

template <typename F>
bool waitUntil(F &&predicate, int timeoutMs) {
    QElapsedTimer t;
    t.start();
    while(!predicate()) {
        if(t.elapsed() >= timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return true;
}

QImage grabItem(QQuickItem *item, int timeoutMs = 2000) {
    QSharedPointer<QQuickItemGrabResult> result = item->grabToImage();
    if(!result) return {};
    QElapsedTimer t;
    t.start();
    while(result->image().isNull() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    }
    return result->image();
}

int distinctColours(const QImage &img) {
    QHash<QRgb, int> seen;
    for(int y = 0; y < img.height(); ++y) {
        for(int x = 0; x < img.width(); ++x) {
            seen.insert(img.pixel(x, y), 1);
            if(seen.size() > 1) return seen.size();
        }
    }
    return seen.size();
}

void clickItem(QQuickWindow *window, QQuickItem *item) {
    const QPointF c = item->mapToScene(QPointF(item->width() / 2.0, item->height() / 2.0));
    QTest::mouseClick(window, Qt::LeftButton, {}, c.toPoint());
}

} // namespace

// A minimal fake copilot-bridge hub: records the frames the client sends and can
// push mirror traffic back. Mirrors the pattern in test_copilot_bridge.cpp.
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
        if(m_client) {
            m_client->sendTextMessage(
                QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
        }
    }

    QStringList received;

private:
    QWebSocketServer *m_server;
    QPointer<QWebSocket> m_client;
};

class TestChatPanel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        qRegisterMetaType<qvim::Notification>("qvim::Notification");
        qRegisterMetaType<qvim::ObjectHandlePtr>("qvim::ObjectHandlePtr");
    }

    void chatPanelEndToEnd() {
        NvimConnector conn;
        QVERIFY(startTestNvim(conn));

        // A fake copilot-bridge hub the panel's CopilotBridgeClient connects to.
        // Point the client at it before the QML scene (and thus the client) is
        // constructed, so inject() has a live socket to the hub.
        FakeHub hub;
        QVERIFY(hub.listen());
        qputenv("COPILOT_BRIDGE_URL", QStringLiteral("ws://127.0.0.1:%1").arg(hub.port()).toUtf8());

        QQmlApplicationEngine engine;
        QQuickWindow *window = loadMainQml(engine, &conn);
        QVERIFY2(window, "Main.qml failed to load");
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(waitForAttach(&conn));
        QVERIFY(waitForFlush(&conn));

        auto *toggle = window->findChild<QQuickItem *>(QStringLiteral("chatToggle"));
        auto *panel = window->findChild<QQuickItem *>(QStringLiteral("chatPanel"));
        auto *model = window->findChild<ChatModel *>();
        auto *bridge = window->findChild<CopilotBridgeClient *>();
        QVERIFY2(toggle, "chatToggle button not found in scene");
        QVERIFY2(panel, "chatPanel not found in scene");
        QVERIFY2(model, "ChatModel not found in scene");
        QVERIFY2(bridge, "CopilotBridgeClient not found in scene");

        // Focus starts on the grid; record it so we can prove it is restored.
        QQuickItem *gridFocus = window->activeFocusItem();
        QVERIFY2(gridFocus != nullptr, "No active focus item after attach");
        QVERIFY(!panel->isVisible());
        const int baseCols = conn.grid()->gridCols(1);
        QVERIFY(baseCols > 0);

        // (1) Click the toggle -> panel opens and the chat input takes focus.
        clickItem(window, toggle);
        QVERIFY2(waitUntil([&] { return panel->isVisible(); }, 3000),
                 "Panel did not become visible after clicking the toggle");
        QVERIFY2(waitUntil(
                     [&] {
            QQuickItem *f = window->activeFocusItem();
            return f && f->objectName() == QStringLiteral("chatInput");
        }, 3000),
                 "Chat input did not take focus on open");

        // (3) Opening the panel shrinks the nvim grid via the resize path.
        QVERIFY2(waitUntil([&] { return conn.grid()->gridCols(1) < baseCols; }, 3000),
                 "Grid columns did not shrink when the panel opened");

        // (2) The bridge connects to the fake hub once the panel is visible.
        // Wait on the CLIENT-side connected state (the exact gate inject() uses),
        // not the hub's view of the socket: the server reaches ConnectedState a
        // beat before the client does, so typing on hub.hasClient() alone races
        // inject() into a no-op.
        QVERIFY2(waitUntil([&] { return bridge->isConnected(); }, 5000),
                 "Bridge client did not connect to the fake hub after the panel opened");

        // Type into the chat input via the focus chain -> injected to the bridge.
        QTest::keyClick(window, Qt::Key_H);
        QTest::keyClick(window, Qt::Key_I);
        QTest::keyClick(window, Qt::Key_Return);

        // The typed line is injected into the copilot bridge as a prompt. This is
        // the positive proof that input routes to the bridge (not merely that no
        // echo appears): an impl that dropped input on the floor would fail here.
        QVERIFY2(waitUntil(
                     [&] {
            for(const QString &f: hub.received) {
                const QJsonObject o = QJsonDocument::fromJson(f.toUtf8()).object();
                if(o.value(QStringLiteral("type")).toString() == QStringLiteral("inject") &&
                   o.value(QStringLiteral("data"))
                           .toObject()
                           .value(QStringLiteral("prompt"))
                           .toString() == QStringLiteral("hi")) {
                    return true;
                }
            }
            return false;
        }, 5000),
                 "Typed line was not injected into the copilot bridge");

        // No local echo: the panel fabricates no reply. Observe for a fixed
        // window (hub stays silent) so a delayed local echo would be caught —
        // a non-vacuous negative assertion.
        QTest::qWait(250);
        QCOMPARE(model->count(), 0);

        // A real mirror reply from the hub lands in the transcript via appendBlock.
        hub.send(QJsonObject{
            { QStringLiteral("type"), QStringLiteral("assistant.message") },
            { QStringLiteral("sessionId"), QStringLiteral("s1") },
            { QStringLiteral("data"),
              QJsonObject{ { QStringLiteral("content"), QStringLiteral("bridge reply") } } },
        });
        QVERIFY2(waitUntil([&] { return model->count() == 1; }, 5000),
                 "Bridge mirror reply did not appear in the transcript");
        QCOMPARE(model->authorAt(0), QStringLiteral("assistant"));
        QVERIFY(model->textAt(0).contains(QStringLiteral("bridge reply")));

        // (4) The panel subtree actually renders.
        const QImage shot = grabItem(panel);
        QVERIFY2(!shot.isNull(), "grabToImage returned a null image for the panel");
        QVERIFY2(distinctColours(shot) > 1,
                 "Panel rendered a single flat colour (black-on-black / opacity-0 / z-order?)");

        // (5) Escape closes the panel (real Keys.onEscapePressed binding),
        // restores grid columns, and returns focus to the grid.
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY2(waitUntil([&] { return !panel->isVisible(); }, 3000),
                 "Panel did not close on Escape");
        QVERIFY2(waitUntil([&] { return conn.grid()->gridCols(1) == baseCols; }, 3000),
                 "Grid columns did not restore after closing the panel");
        QVERIFY2(waitUntil([&] { return window->activeFocusItem() == gridFocus; }, 3000),
                 "Focus was not handed back to the grid after closing the panel");

        // Keystrokes reach nvim again: enter insert mode, type 'x', escape.
        QTest::keyClick(window, Qt::Key_I);
        QTest::keyClick(window, Qt::Key_X);
        QTest::keyClick(window, Qt::Key_Escape);
        QVERIFY2(
            waitUntil([&] { return conn.grid()->dumpAscii().contains(QLatin1Char('x')); }, 3000),
            "nvim did not receive keystrokes after the panel closed");
    }
};

QTEST_MAIN(TestChatPanel)
#include "test_chat_panel.moc"
