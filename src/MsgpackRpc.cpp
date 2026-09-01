#include "MsgpackRpc.h"

#include <cstring>
#include <QDebug>

namespace qvim {

namespace {
constexpr int kMsgTypeRequest = 0;
constexpr int kMsgTypeResponse = 1;
constexpr int kMsgTypeNotification = 2;
constexpr std::size_t kReadChunk = 64 * 1024;
} // namespace

MsgpackRpc::MsgpackRpc(QObject *parent) :
    QObject(parent), m_unpacker(nullptr, nullptr, kReadChunk) {}

MsgpackRpc::~MsgpackRpc() {
    if(m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool MsgpackRpc::startEmbeddedNvim(const QString &nvimExe, const QStringList &extraArgs) {
    if(m_process) return false;

    m_process = std::make_unique<QProcess>(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    QStringList args;
    // Announce ourselves to init.vim *before* it is sourced so users can do
    // `if exists("g:qvim")` at the top level — same pattern neovide uses with
    // its `--cmd "let g:neovide = v:true"`. The value v:true (not 1) matches
    // neovide so consumers can compare identically.
    args << QStringLiteral("--cmd") << QStringLiteral("let g:qvim = v:true");
    args << QStringLiteral("--embed");
    args.append(extraArgs);

    connect(m_process.get(), &QProcess::readyReadStandardOutput, this, &MsgpackRpc::onReadyRead);
    connect(m_process.get(), &QProcess::errorOccurred, this, &MsgpackRpc::onProcessError);
    connect(m_process.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &MsgpackRpc::onProcessFinished);

    m_process->start(nvimExe, args);
    if(!m_process->waitForStarted(5000)) {
        emit error(QStringLiteral("nvim failed to start: %1").arg(m_process->errorString()));
        m_process.reset();
        return false;
    }
    return true;
}

bool MsgpackRpc::isRunning() const { return m_process && m_process->state() == QProcess::Running; }

void MsgpackRpc::request(const QString &method, PackFn packArgs, RpcCallback cb) {
    if(!isRunning()) {
        if(cb) cb(std::unexpected(RpcError{ -1, QStringLiteral("nvim not running") }));
        return;
    }
    const uint32_t msgid = m_nextMsgId++;
    if(cb) m_pending.insert(msgid, std::move(cb));

    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(4);
    pk.pack(kMsgTypeRequest);
    pk.pack(msgid);
    pk.pack(method.toStdString());
    if(packArgs) packArgs(pk);
    else pk.pack_array(0);
    writeMessage(buf);
}

void MsgpackRpc::notify(const QString &method, PackFn packArgs) {
    if(!isRunning()) return;
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    pk.pack_array(3);
    pk.pack(kMsgTypeNotification);
    pk.pack(method.toStdString());
    if(packArgs) packArgs(pk);
    else pk.pack_array(0);
    writeMessage(buf);
}

void MsgpackRpc::writeMessage(const msgpack::sbuffer &buf) {
    m_process->write(buf.data(), static_cast<qint64>(buf.size()));
}

void MsgpackRpc::onReadyRead() {
    const QByteArray data = m_process->readAllStandardOutput();
    if(data.isEmpty()) return;

    m_unpacker.reserve_buffer(data.size());
    std::memcpy(m_unpacker.buffer(), data.constData(), data.size());
    m_unpacker.buffer_consumed(data.size());

    msgpack::object_handle handle;
    while(m_unpacker.next(handle)) {
        auto shared = std::make_shared<msgpack::object_handle>(std::move(handle));
        dispatchUnpacked(std::move(shared));
    }
}

void MsgpackRpc::dispatchUnpacked(ObjectHandlePtr handle) {
    const msgpack::object &root = handle->get();
    if(root.type != msgpack::type::ARRAY || root.via.array.size < 2) {
        emit error(QStringLiteral("malformed msgpack message"));
        return;
    }
    const auto &arr = root.via.array;
    const int type = arr.ptr[0].as<int>();

    if(type == kMsgTypeResponse && arr.size == 4) {
        const uint32_t msgid = arr.ptr[1].as<uint32_t>();
        auto it = m_pending.find(msgid);
        if(it == m_pending.end()) return;
        RpcCallback cb = it.value();
        m_pending.erase(it);

        const msgpack::object &err = arr.ptr[2];
        if(!err.is_nil()) {
            RpcError e;
            if(err.type == msgpack::type::ARRAY && err.via.array.size >= 2) {
                e.code = err.via.array.ptr[0].as<int64_t>();
                e.message = QString::fromStdString(err.via.array.ptr[1].as<std::string>());
            } else {
                e.message = QStringLiteral("rpc error");
            }
            if(cb) cb(std::unexpected(std::move(e)));
            return;
        }
        // Receiver consumes the result; whole message handle shares the same zone,
        // so passing it keeps arr.ptr[3] alive.
        if(cb) cb(handle);
        return;
    }

    if(type == kMsgTypeNotification && arr.size == 3) {
        QString method = QString::fromStdString(arr.ptr[1].as<std::string>());
        // The handle keeps the msgpack arena alive; Notification::params()
        // extracts the params object lazily on demand.
        emit notification(Notification{ std::move(method), std::move(handle) });
        return;
    }

    // Requests from server (clipboard provider, etc.) ignored in v0.
}

void MsgpackRpc::onProcessError(QProcess::ProcessError err) {
    emit error(QStringLiteral("QProcess error: %1").arg(static_cast<int>(err)));
}

void MsgpackRpc::onProcessFinished(int /*exitCode*/, QProcess::ExitStatus /*status*/) {
    emit disconnected();
}

} // namespace qvim
