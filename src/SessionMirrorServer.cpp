#include "SessionMirrorServer.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QUuid>
#include <QWebSocket>
#include <QWebSocketServer>

namespace qvim {

namespace {
constexpr int kProtocolVersion = 1;
// Cap on a single client input so one frame can't make a logged event
// arbitrarily large from untrusted LAN input.
constexpr int kMaxInputChars = 4096;

QByteArray serialise(const QJsonObject& frame) {
    return QJsonDocument(frame).toJson(QJsonDocument::Compact);
}
} // namespace

SessionMirrorServer::SessionMirrorServer(QObject* parent)
    : QObject(parent)
    , m_server(new QWebSocketServer(QStringLiteral("qvim-session-mirror"),
                                    QWebSocketServer::NonSecureMode, this))
    , m_sessionId(QUuid::createUuid().toString(QUuid::WithoutBraces))
    , m_address(qEnvironmentVariable("QVIM_SESSION_MIRROR_ADDRESS",
                                     QStringLiteral("0.0.0.0"))) {
    connect(m_server, &QWebSocketServer::newConnection,
            this, &SessionMirrorServer::onNewConnection);
}

SessionMirrorServer::~SessionMirrorServer() {
    stopListening();
}

void SessionMirrorServer::setSource(ChatModel* source) {
    if (m_source == source) return;
    if (m_source) {
        disconnect(m_source, nullptr, this, nullptr);
    }
    m_source = source;
    if (m_source) {
        connect(m_source, &ChatModel::userMessageAdded,
                this, &SessionMirrorServer::onUserMessage);
        connect(m_source, &ChatModel::assistantMessageBegan,
                this, &SessionMirrorServer::onAssistantBegin);
        connect(m_source, &ChatModel::assistantMessageDelta,
                this, &SessionMirrorServer::onAssistantDelta);
        connect(m_source, &ChatModel::assistantMessageEnded,
                this, &SessionMirrorServer::onAssistantEnd);
    }
    emit sourceChanged();
}

void SessionMirrorServer::setActive(bool active) {
    if (active == m_active) return;
    m_active = active;
    if (m_active) {
        startListening();
    } else {
        stopListening();
    }
    emit activeChanged();
}

void SessionMirrorServer::setPort(int port) {
    if (port == m_port) return;
    m_port = port;
    emit portChanged();
}

void SessionMirrorServer::setAddress(const QString& address) {
    if (address == m_address) return;
    m_address = address;
    emit addressChanged();
}

quint16 SessionMirrorServer::serverPort() const {
    return m_server->serverPort();
}

void SessionMirrorServer::startListening() {
    QHostAddress addr;
    if (!addr.setAddress(m_address)) {
        addr = QHostAddress(QHostAddress::AnyIPv4);
    }
    if (!m_server->listen(addr, static_cast<quint16>(m_port))) {
        // A failed listen (e.g. port already in use) is non-fatal — editing and
        // the local panel do not depend on the mirror.
        qWarning("qvim: session mirror failed to listen on ws://%s:%d",
                 qPrintable(m_address), m_port);
    }
}

void SessionMirrorServer::stopListening() {
    m_server->close();
    // Synchronous teardown: drop each client's signal wiring before aborting so
    // no queued frame can still call handleInput()/submit() after close, then
    // release the sockets. abort() (not close()) tears the connection down
    // immediately without waiting on a close handshake.
    const QList<QWebSocket*> clients = m_clients.values();
    for (QWebSocket* client : clients) {
        disconnect(client, nullptr, this, nullptr);
        client->abort();
        client->deleteLater();
    }
    m_clients.clear();
    m_ready.clear();
}

void SessionMirrorServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QWebSocket* client = m_server->nextPendingConnection();
        client->setParent(this);
        connect(client, &QWebSocket::textMessageReceived,
                this, &SessionMirrorServer::onTextMessage);
        connect(client, &QWebSocket::disconnected,
                this, &SessionMirrorServer::onSocketDisconnected);
        m_clients.insert(client);
        sendHello(client);
    }
}

void SessionMirrorServer::onTextMessage(const QString& message) {
    auto* client = qobject_cast<QWebSocket*>(sender());
    if (!client) return;

    const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString type = obj.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("resume")) {
        // lastSeq is ignored — resume-from-sequence replay is a later slice.
        m_ready.insert(client);
    } else if (type == QStringLiteral("input")) {
        // Only subscribers that completed the handshake drive the session.
        if (!m_ready.contains(client)) return;
        handleInput(obj.value(QStringLiteral("text")).toString());
    }
    // Unknown types are ignored for forward compatibility.
}

void SessionMirrorServer::onSocketDisconnected() {
    auto* client = qobject_cast<QWebSocket*>(sender());
    if (!client) return;
    m_clients.remove(client);
    m_ready.remove(client);
    client->deleteLater();
}

void SessionMirrorServer::onUserMessage(const QString& id, const QString& text) {
    if (!m_active) return;
    logAndBroadcast(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message")},
        {QStringLiteral("id"), id},
        {QStringLiteral("role"), QStringLiteral("user")},
        {QStringLiteral("text"), text},
    });
}

void SessionMirrorServer::onAssistantBegin(const QString& id) {
    if (!m_active) return;
    logAndBroadcast(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message.begin")},
        {QStringLiteral("id"), id},
        {QStringLiteral("role"), QStringLiteral("assistant")},
    });
}

void SessionMirrorServer::onAssistantDelta(const QString& id, const QString& text) {
    if (!m_active) return;
    logAndBroadcast(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message.delta")},
        {QStringLiteral("id"), id},
        {QStringLiteral("text"), text},
    });
}

void SessionMirrorServer::onAssistantEnd(const QString& id) {
    if (!m_active) return;
    logAndBroadcast(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("message.end")},
        {QStringLiteral("id"), id},
    });
}

void SessionMirrorServer::sendHello(QWebSocket* client) {
    // `hello` carries no `seq` and is not logged.
    const QJsonObject hello{
        {QStringLiteral("type"), QStringLiteral("hello")},
        {QStringLiteral("protocol"), kProtocolVersion},
        {QStringLiteral("sessionId"), m_sessionId},
    };
    client->sendTextMessage(QString::fromUtf8(serialise(hello)));
}

void SessionMirrorServer::handleInput(const QString& text) {
    if (!m_active || !m_source) return;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > kMaxInputChars) return;
    // Feed the remote input through the real panel model. The resulting user
    // block and streamed reply come back via the ChatModel taps and broadcast
    // to every subscriber, so remote input mirrors exactly like local input.
    m_source->submit(trimmed);
}

void SessionMirrorServer::logAndBroadcast(QJsonObject frame) {
    const LoggedEvent event = m_log.append(std::move(frame));
    broadcast(event.json);
}

void SessionMirrorServer::broadcast(const QByteArray& payload) {
    const QString text = QString::fromUtf8(payload);
    const QList<QWebSocket*> ready = m_ready.values();
    for (QWebSocket* client : ready) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            client->sendTextMessage(text);
        }
    }
}

} // namespace qvim
