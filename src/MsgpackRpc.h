#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QProcess>
#include <expected>
#include <functional>
#include <memory>

#include <msgpack.hpp>

namespace qvim {

struct RpcError {
    int64_t code = 0;
    QString message;
};

using ObjectHandlePtr = std::shared_ptr<msgpack::object_handle>;
using RpcResult       = std::expected<ObjectHandlePtr, RpcError>;
using RpcCallback     = std::function<void(RpcResult)>;
using PackFn          = std::function<void(msgpack::packer<msgpack::sbuffer>&)>;

class MsgpackRpc : public QObject {
    Q_OBJECT
public:
    explicit MsgpackRpc(QObject* parent = nullptr);
    ~MsgpackRpc() override;

    bool startEmbeddedNvim(const QString& nvimExe, const QStringList& extraArgs = {});

    void request(const QString& method, PackFn packArgs, RpcCallback cb);
    void notify(const QString& method, PackFn packArgs);

    bool isRunning() const;

signals:
    void notification(const QString& method, qvim::ObjectHandlePtr params);
    void disconnected();
    void error(const QString& message);

private slots:
    void onReadyRead();
    void onProcessError(QProcess::ProcessError err);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    void dispatchUnpacked(ObjectHandlePtr handle);
    void writeMessage(const msgpack::sbuffer& buf);

    std::unique_ptr<QProcess> m_process;
    msgpack::unpacker m_unpacker;
    QHash<uint32_t, RpcCallback> m_pending;
    uint32_t m_nextMsgId = 1;
};

inline std::string toStd(const QString& s) { return s.toStdString(); }

// Extracts the params object from a notification handle wrapping [type, method, params].
inline const msgpack::object& paramsView(const ObjectHandlePtr& msg) {
    static const msgpack::object kNil{};
    if (!msg) return kNil;
    const msgpack::object& root = msg->get();
    if (root.type != msgpack::type::ARRAY || root.via.array.size < 3) return kNil;
    return root.via.array.ptr[2];
}

} // namespace qvim

Q_DECLARE_METATYPE(qvim::ObjectHandlePtr)
