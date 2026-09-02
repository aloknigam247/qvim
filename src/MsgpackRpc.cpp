#include "MsgpackRpc.h"

#include <cstring>
#include <QDebug>

namespace qvim {

constexpr int kMsgTypeRequest = 0;
constexpr int kMsgTypeResponse = 1;
constexpr int kMsgTypeNotification = 2;
constexpr std::size_t kReadChunk = std::size_t{ 64 } * 1024;

// A listen_addr is TCP when it ends in `:<port>` and the host part carries no
// path separators (which would mark it as a pipe / filesystem socket instead).
static bool looksLikeTcpAddress(const QString &addr) {
    const qsizetype colon = addr.lastIndexOf(QLatin1Char(':'));
    if(colon <= 0 || colon == addr.size() - 1) return false;
    bool ok = false;
    addr.mid(colon + 1).toUShort(&ok);
    if(!ok) return false;
    const QStringView host = QStringView(addr).left(colon);
    return !host.contains(QLatin1Char('/')) && !host.contains(QLatin1Char('\\'));
}

QString canonicalizePipeAddress(const QString &addr) {
    // nvim reports the Windows pipe as `//./pipe/<name>`; QLocalSocket expects the
    // native `\\.\pipe\<name>`. Normalise both slash conventions of that prefix.
    static const QString kForward = QStringLiteral("//./pipe/");
    static const QString kNative = QStringLiteral("\\\\.\\pipe\\");
    if(addr.startsWith(kForward, Qt::CaseInsensitive)) {
        return kNative + addr.mid(kForward.size());
    }
    return addr;
}

MsgpackRpc::MsgpackRpc(QObject *parent) :
    QObject(parent), m_unpacker(nullptr, nullptr, kReadChunk) {}

// NOLINTNEXTLINE(bugprone-exception-escape): Qt/msgpack teardown does not throw in practice.
MsgpackRpc::~MsgpackRpc() {
    if(m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    } else if(isRunning()) {
        // Socket transport (post-restart): the embedded QProcess is gone, so kill()
        // can't reap the server. Ask it to quit so it doesn't linger orphaned.
        shutdownAndWait(1000);
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

    connect(m_process.get(), &QProcess::readyReadStandardOutput, this,
            &MsgpackRpc::onReadyReadProcess);
    connect(m_process.get(), &QProcess::errorOccurred, this, &MsgpackRpc::onProcessError);
    connect(m_process.get(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &MsgpackRpc::onProcessFinished);

    m_process->start(nvimExe, args);
    if(!m_process->waitForStarted(5000)) {
        emit error(QStringLiteral("nvim failed to start: %1").arg(m_process->errorString()));
        m_process = nullptr;
        return false;
    }
    m_io = m_process.get();
    return true;
}

void MsgpackRpc::connectToAddress(const QString &listenAddr) {
    // Switching transports: no callback from the old channel is valid any more, and
    // the unpacker arena may hold a partial old-server frame.
    failAllPending(QStringLiteral("transport switched (restart)"));
    retireCurrentTransport();
    m_unpacker.reset();

    const quint64 gen = m_generation;
    if(looksLikeTcpAddress(listenAddr)) {
        m_tcpSocket = std::make_unique<QTcpSocket>(this);
        m_io = m_tcpSocket.get();
        QTcpSocket *sock = m_tcpSocket.get();
        connect(sock, &QIODevice::readyRead, this, [this, gen, sock] {
            if(gen == m_generation) feed(sock->readAll());
        });
        connect(sock, &QAbstractSocket::connected, this, [this, gen] {
            if(gen == m_generation) emit connected();
        });
        connect(sock, &QAbstractSocket::disconnected, this, [this, gen] {
            if(gen == m_generation) emit transportClosed();
        });
        connect(sock, &QAbstractSocket::errorOccurred, this,
                [this, gen, sock](QAbstractSocket::SocketError) {
            if(gen == m_generation) emit transportError(sock->errorString());
        });
        const qsizetype colon = listenAddr.lastIndexOf(QLatin1Char(':'));
        sock->connectToHost(listenAddr.left(colon), listenAddr.mid(colon + 1).toUShort());
    } else {
        m_localSocket = std::make_unique<QLocalSocket>(this);
        m_io = m_localSocket.get();
        QLocalSocket *sock = m_localSocket.get();
        connect(sock, &QIODevice::readyRead, this, [this, gen, sock] {
            if(gen == m_generation) feed(sock->readAll());
        });
        connect(sock, &QLocalSocket::connected, this, [this, gen] {
            if(gen == m_generation) emit connected();
        });
        connect(sock, &QLocalSocket::disconnected, this, [this, gen] {
            if(gen == m_generation) emit transportClosed();
        });
        connect(sock, &QLocalSocket::errorOccurred, this,
                [this, gen, sock](QLocalSocket::LocalSocketError) {
            if(gen == m_generation) emit transportError(sock->errorString());
        });
        sock->connectToServer(canonicalizePipeAddress(listenAddr));
    }
}

void MsgpackRpc::retireCurrentTransport() {
    // Bump the generation first so any already-queued signal from the outgoing
    // device is dropped by the gen-guarded lambdas. Detach from `this`, then defer
    // deletion — we may be inside the outgoing device's own finished/EOF slot.
    ++m_generation;
    m_io = nullptr;
    if(m_process) {
        m_process->disconnect(this);
        m_process.release()->deleteLater();
    }
    if(m_localSocket) {
        m_localSocket->disconnect(this);
        m_localSocket.release()->deleteLater();
    }
    if(m_tcpSocket) {
        m_tcpSocket->disconnect(this);
        m_tcpSocket.release()->deleteLater();
    }
}

bool MsgpackRpc::isRunning() const {
    if(m_process) return m_process->state() == QProcess::Running;
    if(m_localSocket) return m_localSocket->state() == QLocalSocket::ConnectedState;
    if(m_tcpSocket) return m_tcpSocket->state() == QAbstractSocket::ConnectedState;
    return false;
}

void MsgpackRpc::shutdownAndWait(int timeoutMs) {
    if(!isRunning()) return;
    notify(QStringLiteral("nvim_command"), [](msgpack::packer<msgpack::sbuffer> &pk) {
        pk.pack_array(1);
        pk.pack(std::string("qa!"));
    });
    if(m_io) m_io->waitForBytesWritten(timeoutMs);
    if(m_localSocket) m_localSocket->waitForDisconnected(timeoutMs);
    else if(m_tcpSocket) m_tcpSocket->waitForDisconnected(timeoutMs);
    else if(m_process) m_process->waitForFinished(timeoutMs);
}

void MsgpackRpc::request(const QString &method, const PackFn &packArgs, const RpcCallback &cb) {
    if(!isRunning()) {
        if(cb) cb(std::unexpected(RpcError{ -1, QStringLiteral("nvim not running") }));
        return;
    }
    const uint32_t msgid = m_nextMsgId++;
    if(cb) m_pending.insert(msgid, cb);

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

void MsgpackRpc::notify(const QString &method, const PackFn &packArgs) {
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
    if(m_io) m_io->write(buf.data(), static_cast<qint64>(buf.size()));
}

void MsgpackRpc::failAllPending(const QString &message) {
    QHash<uint32_t, RpcCallback> pending = std::move(m_pending);
    m_pending.clear();
    for(auto it = pending.begin(); it != pending.end(); ++it) {
        if(it.value()) it.value()(std::unexpected(RpcError{ -1, message }));
    }
}

void MsgpackRpc::onReadyReadProcess() {
    if(m_process) feed(m_process->readAllStandardOutput());
}

void MsgpackRpc::feed(const QByteArray &data) {
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
        const RpcCallback cb = it.value();
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
    // The embedded process channel closed. NvimConnector decides whether this is a
    // real quit (propagate disconnected) or a :restart handoff (reconnect to a
    // socket) — so we only report the low-level transport close here.
    emit transportClosed();
}

} // namespace qvim
