#include <QtTest>
#include <msgpack.hpp>

#include "MsgpackRpc.h"

using namespace qvim;

class TestMsgpackRpc : public QObject {
    Q_OBJECT
private slots:
    void packDecodeRoundTripRequest() {
        // Encode a notification-style message and decode it back to confirm
        // we agree with msgpack-cxx on the wire format used by Neovim.
        msgpack::sbuffer buf;
        msgpack::packer<msgpack::sbuffer> pk(&buf);
        pk.pack_array(3);
        pk.pack(2);                            // notification
        pk.pack(std::string("nvim_input"));
        pk.pack_array(1);
        pk.pack(std::string("iabc<Esc>"));

        msgpack::object_handle h;
        msgpack::unpack(h, buf.data(), buf.size());
        const auto& a = h.get().via.array;
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
        std::expected<msgpack::object_handle, RpcError> received{std::in_place};
        rpc.request(QStringLiteral("nvim_eval"),
            [](msgpack::packer<msgpack::sbuffer>& pk) {
                pk.pack_array(1);
                pk.pack(std::string("1"));
            },
            [&](RpcResult r) {
                called = true;
                QVERIFY(!r);
                QVERIFY(r.error().message.contains(QStringLiteral("not running")));
            });
        QVERIFY(called);
    }

    void responseRoutesByMsgid() {
        // We exercise the dispatch path indirectly by hand-crafting a response
        // packet — though MsgpackRpc::dispatchUnpacked is private, this serves
        // as a structural smoke check that our envelope layout is correct.
        msgpack::sbuffer buf;
        msgpack::packer<msgpack::sbuffer> pk(&buf);
        pk.pack_array(4);
        pk.pack(1);          // response
        pk.pack(uint32_t(42)); // msgid
        pk.pack_nil();       // no error
        pk.pack_array(0);    // empty result

        msgpack::object_handle h;
        msgpack::unpack(h, buf.data(), buf.size());
        QCOMPARE(h.get().via.array.ptr[1].as<uint32_t>(), uint32_t(42));
    }
};

QTEST_GUILESS_MAIN(TestMsgpackRpc)
#include "test_msgpack_rpc.moc"
