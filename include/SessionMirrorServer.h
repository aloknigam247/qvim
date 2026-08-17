#pragma once

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <qqmlregistration.h>

#include "ChatModel.h"
#include "SessionEventLog.h"

class QWebSocket;
class QWebSocketServer;

namespace qvim {

// In-process plaintext WebSocket endpoint that mirrors qvim's chat session to
// LAN subscribers, backed by SessionEventLog. Implements the v1 wire protocol in
// docs/protocol/session-protocol.md: on connect the server sends `hello` first;
// the client replies `resume`; the server then streams live events.
//
// The mirrored session is the desktop `ChatModel` itself, not a parallel echo:
// `source` is the panel's model, and this server forwards its
// userMessageAdded / assistantMessageBegan / *Delta / *Ended taps verbatim as
// `message` / `message.begin` / `message.delta` / `message.end` frames, each
// carrying the next monotonic `seq`. A remote `input` is fed straight back into
// `ChatModel::submit()`, so remote and local input share one path and the reply
// flows out to every subscriber (including the sender) through the same taps —
// there is no self-echo shortcut.
//
// `active` is the single lifecycle authority: it is bound to the chat panel's
// visibility, so the port is only bound while the panel is open and is released
// synchronously when it closes. qvim is the sole source of truth; clients are
// subscribers that never own state. Lives entirely off the redraw->paint hot
// path.
class SessionMirrorServer : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(qvim::ChatModel* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QString address READ address WRITE setAddress NOTIFY addressChanged)

public:
    explicit SessionMirrorServer(QObject* parent = nullptr);
    ~SessionMirrorServer() override;

    ChatModel* source() const { return m_source; }
    void       setSource(ChatModel* source);

    bool isActive() const { return m_active; }
    void setActive(bool active);

    int  port() const { return m_port; }
    void setPort(int port);

    QString address() const { return m_address; }
    void    setAddress(const QString& address);

    // The bound port (0 when not listening). With port 0 the OS assigns a free
    // port — used by tests to avoid a fixed-port collision.
    Q_INVOKABLE quint16 serverPort() const;

    QString sessionId() const { return m_sessionId; }

signals:
    void sourceChanged();
    void activeChanged();
    void portChanged();
    void addressChanged();

private:
    void onNewConnection();
    void onTextMessage(const QString& message);
    void onSocketDisconnected();

    void onUserMessage(const QString& id, const QString& text);
    void onAssistantBegin(const QString& id);
    void onAssistantDelta(const QString& id, const QString& text);
    void onAssistantEnd(const QString& id);

    void startListening();
    void stopListening();

    void sendHello(QWebSocket* client);
    void handleInput(const QString& text);
    void logAndBroadcast(QJsonObject frame);
    void broadcast(const QByteArray& payload);

    QWebSocketServer*   m_server = nullptr;
    SessionEventLog     m_log;
    QString             m_sessionId;
    QPointer<ChatModel> m_source;
    QSet<QWebSocket*>   m_clients; // all accepted sockets
    QSet<QWebSocket*>   m_ready;   // clients that completed the `resume` handshake
    bool                m_active  = false;
    int                 m_port    = 8765;
    QString             m_address;
};

} // namespace qvim
