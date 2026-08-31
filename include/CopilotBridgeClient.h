#ifndef COPILOTBRIDGECLIENT_H
#define COPILOTBRIDGECLIENT_H

#include <QObject>
#include <QPointer>
#include <qqmlregistration.h>
#include <QString>

#include "ChatModel.h"

class QTimer;
class QWebSocket;

namespace qvim {

// WebSocket client for the copilot-bridge hub (protocol: copilot-bridge/CLIENT.md).
// Connects to ws://127.0.0.1:47823 as role "client", performs the `hello`
// handshake, and renders the mirror traffic from every attached Copilot CLI
// session into the chat panel's `sink` ChatModel.
//
// This is a second chat backend alongside ChatModel's built-in echo. It renders
// the mirror traffic from every session into `sink`, and inject()s user prompts
// (from the panel or a LAN subscriber) back into a session — a pure
// output/input relay. Tool-permission prompts are owned by the hub, so this
// client does not handle them.
//
// Lifecycle mirrors SessionMirrorServer: `active` is the single authority, bound
// to chat-panel visibility, so the outbound connection only exists while the
// panel is open. The hub self-terminates ~30s after the last session detaches
// and is re-spawned by the next session, so the client auto-reconnects while
// active.
class CopilotBridgeClient : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(qvim::ChatModel *sink READ sink WRITE setSink NOTIFY sinkChanged)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)
    Q_PROPERTY(bool showTools READ showTools WRITE setShowTools NOTIFY showToolsChanged)

public:
    explicit CopilotBridgeClient(QObject *parent = nullptr);
    ~CopilotBridgeClient() override;

    ChatModel *sink() const { return m_sink; }
    void setSink(ChatModel *sink);

    bool isActive() const { return m_active; }
    void setActive(bool active);

    QString url() const { return m_url; }
    void setUrl(const QString &url);

    bool isConnected() const;

    bool showTools() const { return m_showTools; }
    void setShowTools(bool showTools);

    // Injects a user prompt into the connected Copilot session(s). Empty
    // `sessionId` broadcasts to every session; otherwise the hub routes it to
    // sessions whose id starts with `sessionId`. The injected turn comes back
    // through the mirror stream as a user.prompt, so this does not append to the
    // sink locally. Returns false on empty prompt or when not connected.
    Q_INVOKABLE bool inject(const QString &prompt, const QString &sessionId = {});

signals:
    void sinkChanged();
    void activeChanged();
    void urlChanged();
    void connectedChanged();
    void showToolsChanged();

private:
    void onConnected();
    void onDisconnected();
    void onTextMessage(const QString &message);

    void openConnection();
    void closeConnection();
    void scheduleReconnect();

    void appendLine(const QString &author, const QString &text, const QString &sessionId = {});

    QWebSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QPointer<ChatModel> m_sink;
    QString m_url;
    bool m_active = false;
    bool m_showTools = true;
};

} // namespace qvim

#endif
