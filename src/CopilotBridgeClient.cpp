#include "CopilotBridgeClient.h"

#include <QAbstractSocket>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>

namespace qvim {

// The hub binds a fixed port on localhost (copilot-bridge/README.md). The
// reference client honours COPILOT_BRIDGE_URL to point elsewhere.
static QString defaultUrl() {
    return qEnvironmentVariable("COPILOT_BRIDGE_URL", QStringLiteral("ws://127.0.0.1:47823"));
}

// The hub comes and goes with the Copilot sessions; retry on this cadence while
// active so the panel reconnects once a session (re)spawns it.
constexpr int kReconnectMs = 2000;

static QString serialise(const QJsonObject &frame) {
    return QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact));
}

static QString truncate(const QString &text, int max) {
    const QString flat = text.simplified();
    return flat.size() > max ? flat.left(max) + QStringLiteral("\u2026") : flat;
}

// First non-empty string value among the candidate keys.
static QString firstString(const QJsonObject &obj, std::initializer_list<QStringView> keys) {
    for(const QStringView key: keys) {
        const QJsonValue v = obj.value(key);
        if(v.isString() && !v.toString().isEmpty()) return v.toString();
    }
    return {};
}

// The selectable options an ask_user-style tool offers, pulled from its JSON
// Schema (enum / oneOf.title / items.enum / items.anyOf.title across fields).
static QStringList schemaOptions(const QJsonObject &schema) {
    QStringList options;
    const QJsonObject props = schema.value(QStringLiteral("properties")).toObject();
    for(const auto &field: props) {
        const QJsonObject f = field.toObject();
        for(const auto &e: f.value(QStringLiteral("enum")).toArray()) {
            options << e.toVariant().toString();
        }
        for(const auto &o: f.value(QStringLiteral("oneOf")).toArray()) {
            options << o.toObject().value(QStringLiteral("title")).toString();
        }
        const QJsonObject items = f.value(QStringLiteral("items")).toObject();
        for(const auto &e: items.value(QStringLiteral("enum")).toArray()) {
            options << e.toVariant().toString();
        }
        for(const auto &o: items.value(QStringLiteral("anyOf")).toArray()) {
            options << o.toObject().value(QStringLiteral("title")).toString();
        }
    }
    options.removeAll(QString());
    return options;
}

// The hub encodes `toolArgs` / `result` as a JSON *string* (not a nested
// object). Accept both: parse a string, pass an object through.
static QJsonObject asObject(const QJsonValue &value) {
    if(value.isObject()) return value.toObject();
    if(value.isString()) { return QJsonDocument::fromJson(value.toString().toUtf8()).object(); }
    return {};
}

// A one-line summary of what a tool call is actually doing, so the panel shows
// the file / command / question instead of the bare tool name. Falls back to
// compact JSON so an unrecognised tool still surfaces its arguments.
static QString formatToolArgs(const QJsonObject &args) {
    if(args.isEmpty()) return {};
    const QString question = firstString(args, { u"message", u"question", u"prompt", u"query" });
    if(!question.isEmpty()) {
        QString summary = truncate(question, 200);
        const QStringList options =
            schemaOptions(args.value(QStringLiteral("requestedSchema")).toObject());
        if(!options.isEmpty()) {
            summary +=
                QStringLiteral(" [") + options.join(QStringLiteral(", ")) + QStringLiteral("]");
        }
        return summary;
    }
    const QString command = firstString(args, { u"command", u"cmd", u"script" });
    if(!command.isEmpty()) return truncate(command, 200);
    const QString path = firstString(args, { u"path", u"filePath", u"file", u"filename" });
    if(!path.isEmpty()) return path;
    const QString pattern = firstString(args, { u"pattern" });
    if(!pattern.isEmpty()) return truncate(pattern, 200);
    return truncate(QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact)), 200);
}

// A tool's textual result. Like toolArgs it may be a JSON string wrapping a
// richer payload; pull the human-readable text out when so, else use it raw.
static QString formatToolResult(const QJsonValue &value) {
    if(!value.isString()) return {};
    const QString raw = value.toString();
    const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8());
    if(doc.isObject()) {
        const QString text = firstString(doc.object(), { u"content", u"detailedContent", u"result",
                                                         u"output", u"stdout", u"message" });
        return truncate(text.isEmpty() ? raw : text, 200);
    }
    return truncate(raw, 200);
}

CopilotBridgeClient::CopilotBridgeClient(QObject *parent) :
    QObject(parent), m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this)),
    m_reconnectTimer(new QTimer(this)), m_url(defaultUrl()) {
    m_reconnectTimer->setInterval(kReconnectMs);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &CopilotBridgeClient::openConnection);

    connect(m_socket, &QWebSocket::connected, this, &CopilotBridgeClient::onConnected);
    connect(m_socket, &QWebSocket::disconnected, this, &CopilotBridgeClient::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &CopilotBridgeClient::onTextMessage);
    // A refused/failed connect never emits `connected`; retry from the error so
    // the client keeps polling for a hub that isn't up yet.
    connect(m_socket, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { scheduleReconnect(); });
}

CopilotBridgeClient::~CopilotBridgeClient() { closeConnection(); }

void CopilotBridgeClient::setSink(ChatModel *sink) {
    if(m_sink == sink) return;
    m_sink = sink;
    emit sinkChanged();
}

void CopilotBridgeClient::setActive(bool active) {
    if(active == m_active) return;
    m_active = active;
    if(m_active) {
        openConnection();
    } else {
        closeConnection();
    }
    emit activeChanged();
}

void CopilotBridgeClient::setUrl(const QString &url) {
    if(url == m_url) return;
    m_url = url;
    emit urlChanged();
    if(m_active) {
        // Rebind against the new endpoint.
        closeConnection();
        openConnection();
    }
}

bool CopilotBridgeClient::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void CopilotBridgeClient::setShowTools(bool showTools) {
    if(showTools == m_showTools) return;
    m_showTools = showTools;
    emit showToolsChanged();
}

bool CopilotBridgeClient::inject(const QString &prompt, const QString &sessionId) {
    const QString trimmed = prompt.trimmed();
    if(trimmed.isEmpty() || !isConnected()) return false;
    QJsonObject frame{
        { QStringLiteral("type"), QStringLiteral("inject") },
        { QStringLiteral("data"), QJsonObject{ { QStringLiteral("prompt"), trimmed } } },
    };
    if(!sessionId.isEmpty()) { frame.insert(QStringLiteral("sessionId"), sessionId); }
    m_socket->sendTextMessage(serialise(frame));
    return true;
}

void CopilotBridgeClient::openConnection() {
    if(!m_active) return;
    const QAbstractSocket::SocketState state = m_socket->state();
    if(state == QAbstractSocket::ConnectedState || state == QAbstractSocket::ConnectingState) {
        return;
    }
    m_socket->open(QUrl(m_url));
}

void CopilotBridgeClient::closeConnection() {
    m_reconnectTimer->stop();
    // abort() (not close()) tears the connection down immediately without waiting
    // on a close handshake, matching SessionMirrorServer's synchronous teardown.
    m_socket->abort();
}

void CopilotBridgeClient::scheduleReconnect() {
    if(!m_active) return;
    if(!m_reconnectTimer->isActive()) { m_reconnectTimer->start(); }
}

void CopilotBridgeClient::onConnected() {
    emit connectedChanged();
    const QJsonObject hello{
        { QStringLiteral("type"), QStringLiteral("hello") },
        { QStringLiteral("role"), QStringLiteral("client") },
        { QStringLiteral("data"),
          QJsonObject{ { QStringLiteral("name"), QStringLiteral("qvim") } } },
    };
    m_socket->sendTextMessage(serialise(hello));
}

void CopilotBridgeClient::onDisconnected() {
    emit connectedChanged();
    scheduleReconnect();
}

void CopilotBridgeClient::onTextMessage(const QString &message) {
    const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString type = obj.value(QStringLiteral("type")).toString();
    const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
    const QString sid = obj.value(QStringLiteral("sessionId")).toString();

    if(type == QStringLiteral("user.prompt")) {
        appendLine(QStringLiteral("user"), data.value(QStringLiteral("prompt")).toString(), sid);
    } else if(type == QStringLiteral("assistant.message")) {
        appendLine(QStringLiteral("assistant"), data.value(QStringLiteral("content")).toString(),
                   sid);
    } else if(type == QStringLiteral("session.start")) {
        appendLine(QStringLiteral("system"),
                   QStringLiteral("session started: ") +
                       data.value(QStringLiteral("cwd")).toString(),
                   sid);
    } else if(type == QStringLiteral("session.end")) {
        appendLine(QStringLiteral("system"), QStringLiteral("session ended"), sid);
    } else if(type == QStringLiteral("tool.requested")) {
        if(m_showTools) {
            const QString toolName = data.value(QStringLiteral("toolName")).toString();
            const QString summary =
                formatToolArgs(asObject(data.value(QStringLiteral("toolArgs"))));
            QString line = QStringLiteral("\u25B6 ") + toolName;
            if(!summary.isEmpty()) { line += QStringLiteral("  ") + summary; }
            appendLine(QStringLiteral("system"), line, sid);
        }
    } else if(type == QStringLiteral("tool.complete")) {
        if(m_showTools) {
            const bool ok = data.value(QStringLiteral("success")).toBool();
            const QString result = formatToolResult(data.value(QStringLiteral("result")));
            QString line = data.value(QStringLiteral("toolName")).toString() +
                           (ok ? QStringLiteral(" \u2713") : QStringLiteral(" \u2717"));
            if(!result.isEmpty()) { line += QStringLiteral("  ") + result; }
            appendLine(QStringLiteral("system"), line, sid);
        }
    }
    // Unknown types are ignored for forward compatibility.
}

void CopilotBridgeClient::appendLine(const QString &author, const QString &text,
                                     const QString &sessionId) {
    if(!m_sink || text.isEmpty()) return;
    // Surface which Copilot session a line came from; the tag rides in the
    // message text so it reaches the mobile mirror unchanged.
    const QString tagged =
        sessionId.isEmpty() ? text
                            : QStringLiteral("[") + sessionId.left(6) + QStringLiteral("] ") + text;
    m_sink->appendBlock(author, tagged);
}

} // namespace qvim
