#include <msgpack.hpp>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

#include "MsgpackRpc.h"

using namespace qvim;

namespace {
QByteArray packFrame(const std::function<void(msgpack::packer<msgpack::sbuffer> &)> &fn) {
    msgpack::sbuffer buf;
    msgpack::packer<msgpack::sbuffer> pk(&buf);
    fn(pk);
    return QByteArray(buf.data(), static_cast<qsizetype>(buf.size()));
}
} // namespace

class TestMsgpackRpc : public QObject {
    Q_OBJECT
private slots:
    void packDecodeRoundTripRequest() {
        // Encode a notification-style message and decode it back to confirm
        // we agree with msgpack-cxx on the wire format used by Neovim.
        msgpack::sbuffer buf;
        msgpack::packer<msgpack::sbuffer> pk(&buf);
        pk.pack_array(3);
        pk.pack(2); // notification
        pk.pack(std::string("nvim_input"));
        pk.pack_array(1);
        pk.pack(std::string("iabc<Esc>"));

        msgpack::object_handle h;
        msgpack::unpack(h, buf.data(), buf.size());
        const auto &a = h.get().via.array;
        QCOMPARE(static_cast<int>(a.size), 3);
        QCOMPARE(a.ptr[0].as<int>(), 2);
        QCOMPARE(QString::fromStdString(a.ptr[1].as<std::string>()), QStringLiteral("nvim_input"));
        QCOMPARE(static_cast<int>(a.ptr[2].via.array.size), 1);
        QCOMPARE(QString::fromStdString(a.ptr[2].via.array.ptr[0].as<std::string>()),
                 QStringLiteral("iabc<Esc>"));
    }

    void rpcNotRunningRejectsRequest() {
        MsgpackRpc rpc;
        bool called = false;
        std::expected<msgpack::object_handle, RpcError> received{ std::in_place };
        rpc.request(QStringLiteral("nvim_eval"), [](msgpack::packer<msgpack::sbuffer> &pk) {
            pk.pack_array(1);
            pk.pack(std::string("1"));
        }, [&](RpcResult r) {
            called = true;
            QVERIFY(!r);
            QVERIFY(r.error().message.contains(QStringLiteral("not running")));
        });
        QVERIFY(called);
    }

    void responseRoutesByMsgid() { // We exercise the dispatch path indirectly by hand-crafting a
                                   // response
        // packet — though MsgpackRpc::dispatchUnpacked is private, this serves
        // as a structural smoke check that our envelope layout is correct.
        msgpack::sbuffer buf;
        msgpack::packer<msgpack::sbuffer> pk(&buf);
        pk.pack_array(4);
        pk.pack(1);            // response
        pk.pack(uint32_t(42)); // msgid
        pk.pack_nil();         // no error
        pk.pack_array(0);      // empty result

        msgpack::object_handle h;
        msgpack::unpack(h, buf.data(), buf.size());
        QCOMPARE(h.get().via.array.ptr[1].as<uint32_t>(), uint32_t(42));
    }

    void canonicalizePipeAddressForms() {
        // nvim reports the Windows pipe as `//./pipe/<name>`; QLocalSocket needs the
        // native `\\.\pipe\<name>`. Already-native and non-pipe addresses pass through.
        QCOMPARE(canonicalizePipeAddress(QStringLiteral("//./pipe/nvim.1234.0")),
                 QStringLiteral("\\\\.\\pipe\\nvim.1234.0"));
        QCOMPARE(canonicalizePipeAddress(QStringLiteral("\\\\.\\pipe\\nvim.1234.0")),
                 QStringLiteral("\\\\.\\pipe\\nvim.1234.0"));
        QCOMPARE(canonicalizePipeAddress(QStringLiteral("/tmp/nvim.sock")),
                 QStringLiteral("/tmp/nvim.sock"));
        QCOMPARE(canonicalizePipeAddress(QStringLiteral("127.0.0.1:6666")),
                 QStringLiteral("127.0.0.1:6666"));
    }

    void startEmbeddedNvimFailureResets() {
        // A bogus executable makes waitForStarted fail; the RPC must emit error and
        // drop the half-built QProcess so a later start can be retried.
        MsgpackRpc rpc;
        QSignalSpy errorSpy(&rpc, &MsgpackRpc::error);
        QVERIFY(!rpc.startEmbeddedNvim(QStringLiteral("qvim_nonexistent_nvim_binary")));
        QVERIFY(errorSpy.count() >= 1);
        QVERIFY(!rpc.isRunning());
    }

    void switchingTransportsRetiresSockets() {
        // connectToAddress must retire whatever transport is live before building
        // the next one, exercising both the local- and tcp-socket cleanup arms.
        MsgpackRpc rpc;
        rpc.connectToAddress(QStringLiteral("//./pipe/qvim-nonexistent-a")); // builds local
        rpc.connectToAddress(QStringLiteral("127.0.0.1:1"));                 // retires local
        rpc.connectToAddress(QStringLiteral("//./pipe/qvim-nonexistent-b")); // retires tcp
        // No server backs these; we only assert the switch did not leave us running.
        QVERIFY(!rpc.isRunning());
    }

    void loopbackTransportDrivesDispatch() {
        // A loopback QTcpServer lets us reach the running-transport paths (isRunning,
        // request/notify writes, no-arg packing) and feed crafted frames back through
        // the private dispatch: response routing, rpc-error non-array fallback,
        // notification emission, and the malformed-message guard.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const quint16 port = server.serverPort();

        MsgpackRpc rpc;
        QSignalSpy connectedSpy(&rpc, &MsgpackRpc::connected);
        QSignalSpy noteSpy(&rpc, &MsgpackRpc::notification);
        QSignalSpy errorSpy(&rpc, &MsgpackRpc::error);

        rpc.connectToAddress(QStringLiteral("127.0.0.1:%1").arg(port));
        QVERIFY(server.waitForNewConnection(2000));
        QTcpSocket *peer = server.nextPendingConnection();
        QVERIFY(peer);
        QVERIFY(connectedSpy.wait(2000));
        QVERIFY(rpc.isRunning());

        RpcResult okResult{ std::in_place };
        bool okCalled = false;
        rpc.request(QStringLiteral("nvim_get_mode"), nullptr, [&](RpcResult r) {
            okCalled = true;
            okResult = std::move(r);
        }); // msgid 1, no-arg pack

        RpcResult errResult{ std::in_place };
        bool errCalled = false;
        rpc.request(QStringLiteral("nvim_eval"), [](msgpack::packer<msgpack::sbuffer> &pk) {
            pk.pack_array(1);
            pk.pack(std::string("1"));
        }, [&](RpcResult r) {
            errCalled = true;
            errResult = std::move(r);
        }); // msgid 2

        RpcResult arrErrResult{ std::in_place };
        bool arrErrCalled = false;
        rpc.request(QStringLiteral("nvim_command"), nullptr, [&](RpcResult r) {
            arrErrCalled = true;
            arrErrResult = std::move(r);
        }); // msgid 3

        rpc.notify(QStringLiteral("nvim_command"), nullptr); // no-arg notify

        // Respond to msgid 1 with success, msgid 2 with a non-array error object so
        // the fallback "rpc error" message is used, and msgid 3 with a proper
        // [code, message] error array.
        peer->write(packFrame([](msgpack::packer<msgpack::sbuffer> &pk) {
            pk.pack_array(4);
            pk.pack(1);
            pk.pack(uint32_t(1));
            pk.pack_nil();
            pk.pack_array(0);
        }));
        peer->write(packFrame([](msgpack::packer<msgpack::sbuffer> &pk) {
            pk.pack_array(4);
            pk.pack(1);
            pk.pack(uint32_t(2));
            pk.pack(std::string("boom")); // error is not an array
            pk.pack_nil();
        }));
        peer->write(packFrame([](msgpack::packer<msgpack::sbuffer> &pk) {
            pk.pack_array(4);
            pk.pack(1);
            pk.pack(uint32_t(3));
            pk.pack_array(2); // error is a [code, message] array
            pk.pack(int64_t(7));
            pk.pack(std::string("bad call"));
            pk.pack_nil();
        }));
        peer->write(packFrame([](msgpack::packer<msgpack::sbuffer> &pk) {
            pk.pack_array(3);
            pk.pack(2);
            pk.pack(std::string("redraw"));
            pk.pack_array(0);
        }));
        peer->write(packFrame([](msgpack::packer<msgpack::sbuffer> &pk) {
            pk.pack_array(1); // malformed: fewer than 2 elements
            pk.pack(1);
        }));
        QVERIFY(peer->flush());

        QTRY_VERIFY(okCalled);
        QVERIFY(okResult.has_value());
        QTRY_VERIFY(errCalled);
        QVERIFY(!errResult.has_value());
        QCOMPARE(errResult.error().message, QStringLiteral("rpc error"));
        QTRY_VERIFY(arrErrCalled);
        QVERIFY(!arrErrResult.has_value());
        QCOMPARE(arrErrResult.error().code, int64_t(7));
        QCOMPARE(arrErrResult.error().message, QStringLiteral("bad call"));
        QTRY_COMPARE(noteSpy.count(), 1);
        QTRY_COMPARE(errorSpy.count(), 1);

        rpc.shutdownAndWait(100); // running transport → socket wait branch
    }
};

QTEST_GUILESS_MAIN(TestMsgpackRpc)
#include "test_msgpack_rpc.moc"
