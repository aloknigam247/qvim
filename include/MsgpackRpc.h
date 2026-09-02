#pragma once

#include <expected>
#include <functional>
#include <memory>
#include <QByteArray>
#include <QHash>
#include <QLocalSocket>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTcpSocket>

#include <msgpack.hpp>

namespace qvim {

// Maps an nvim listen_addr pipe form to what QLocalSocket::connectToServer expects.
// nvim emits `//./pipe/<name>`; on Windows QLocalSocket wants `\\.\pipe\<name>`. An
// already-canonical `\\.\pipe\...` address and non-pipe paths are returned unchanged.
QString canonicalizePipeAddress(const QString &addr);

struct RpcError {
    int64_t code = 0;
    QString message;
};

using ObjectHandlePtr = std::shared_ptr<msgpack::object_handle>;
using RpcResult = std::expected<ObjectHandlePtr, RpcError>;
using RpcCallback = std::function<void(RpcResult)>;
using PackFn = std::function<void(msgpack::packer<msgpack::sbuffer> &)>;

// Notification carries an RPC notification across signal/slot boundaries.
// `handle` keeps the msgpack arena alive; `params()` returns the already-
// extracted params object from the [type, method, params] envelope.
// Returns a nil object if the handle is missing/malformed — receivers should
// still validate the shape they expect.
struct Notification {
    QString method;
    ObjectHandlePtr handle;

    const msgpack::object &params() const noexcept {
        static const msgpack::object kNil{};
        if(!handle) return kNil;
        const msgpack::object &root = handle->get();
        if(root.type != msgpack::type::ARRAY || root.via.array.size < 3) return kNil;
        return root.via.array.ptr[2];
    }
};

class MsgpackRpc : public QObject {
    Q_OBJECT
public:
    explicit MsgpackRpc(QObject *parent = nullptr);
    ~MsgpackRpc() override;

    bool startEmbeddedNvim(const QString &nvimExe, const QStringList &extraArgs = {});
    // Connects to an already-running nvim server's listen address (Windows named
    // pipe / unix socket via QLocalSocket, or host:port via QTcpSocket). Used by
    // the :restart reconnect path. Retires the current transport, resets the
    // unpacker, and fails any pending request callbacks with a transport error.
    // Emits connected() once the socket is ready, or transportError() on failure.
    void connectToAddress(const QString &listenAddr);

    void request(const QString &method, PackFn packArgs, RpcCallback cb);
    void notify(const QString &method, PackFn packArgs);

    bool isRunning() const;

    // Best-effort bounded shutdown of the current server: sends `qa!`, flushes the
    // written bytes, then waits up to timeoutMs for the transport to close. Used to
    // avoid orphaning a socket-attached server (after :restart) when qvim exits.
    void shutdownAndWait(int timeoutMs = 1000);

signals:
    void notification(const qvim::Notification &note);
    void connected();       // socket transport reached ConnectedState (reconnect)
    void transportClosed(); // internal: active transport hit EOF / finished
    void transportError(const QString &message); // socket connect / runtime error
    void error(const QString &message);

private slots:
    void onReadyReadProcess();
    void onProcessError(QProcess::ProcessError err);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void dispatchUnpacked(ObjectHandlePtr handle);
    void writeMessage(const msgpack::sbuffer &buf);
    void feed(const QByteArray &data);
    void failAllPending(const QString &message);
    void retireCurrentTransport();

    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<QLocalSocket> m_localSocket;
    std::unique_ptr<QTcpSocket> m_tcpSocket;
    QPointer<QIODevice> m_io; // non-owning view of the active transport for writes
    quint64 m_generation = 0; // bumped on each transport switch; guards stale signals
    msgpack::unpacker m_unpacker;
    QHash<uint32_t, RpcCallback> m_pending;
    uint32_t m_nextMsgId = 1;
};

inline std::string toStd(const QString &s) { return s.toStdString(); }

} // namespace qvim

Q_DECLARE_METATYPE(qvim::ObjectHandlePtr)
Q_DECLARE_METATYPE(qvim::Notification)
