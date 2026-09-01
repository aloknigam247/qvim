#ifndef MSGPACKRPC_H
#define MSGPACKRPC_H

#include <expected>
#include <functional>
#include <memory>
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

#include <msgpack.hpp>

#include "QvimMacros.h"

namespace qvim {

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
    QVIM_DISABLE_COPY_MOVE(MsgpackRpc)

    bool startEmbeddedNvim(const QString &nvimExe, const QStringList &extraArgs = {});

    void request(const QString &method, PackFn packArgs, RpcCallback cb);
    void notify(const QString &method, PackFn packArgs);

    bool isRunning() const;

signals:
    void notification(const qvim::Notification &note);
    void disconnected();
    void error(const QString &message);

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError err);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void dispatchUnpacked(ObjectHandlePtr handle);
    void writeMessage(const msgpack::sbuffer &buf);

    std::unique_ptr<QProcess> m_process;
    msgpack::unpacker m_unpacker;
    QHash<uint32_t, RpcCallback> m_pending;
    uint32_t m_nextMsgId = 1;
};

inline std::string toStd(const QString &s) { return s.toStdString(); }

} // namespace qvim

Q_DECLARE_METATYPE(qvim::ObjectHandlePtr)
Q_DECLARE_METATYPE(qvim::Notification)

#endif
