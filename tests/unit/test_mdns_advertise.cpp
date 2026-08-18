#include <QtTest>

#include "MdnsAdvertiser.h"

using namespace qvim;

namespace {

// Records what the advertiser hands the platform responder, so a test can assert
// the advertised type and port without a live mDNS stack.
class FakeMdnsBackend : public MdnsBackend {
public:
    void registerService(const MdnsService &service) override {
        last = service;
        ++registerCount;
    }
    void unregisterService() override { ++unregisterCount; }

    MdnsService last;
    int registerCount = 0;
    int unregisterCount = 0;
};

} // namespace

class TestMdnsAdvertise : public QObject {
    Q_OBJECT

private slots:
    void advertisesTypeAndPort();
    void deactivateWithdraws();
    void portChangeReadvertises();
    void inactiveDoesNotAdvertise();
    void wireFormServiceNameAndHostOrderPort();
};

// Activating with a bound port registers the service under the expected DNS-SD
// type and the exact port.
void TestMdnsAdvertise::advertisesTypeAndPort() {
    MdnsAdvertiser advertiser;
    auto backend = std::make_unique<FakeMdnsBackend>();
    FakeMdnsBackend *fake = backend.get();
    advertiser.setBackend(std::move(backend));

    advertiser.setPort(8765);
    advertiser.setActive(true);

    QCOMPARE(fake->registerCount, 1);
    QCOMPARE(fake->last.type, QStringLiteral("_qvim-mirror._tcp"));
    QCOMPARE(fake->last.port, quint16(8765));
    QVERIFY(!fake->last.instanceName.isEmpty());
    QVERIFY(!fake->last.hostName.isEmpty());
    QCOMPARE(fake->last.txt.value(QStringLiteral("proto")), QStringLiteral("1"));
}

// Deactivating withdraws the advertisement.
void TestMdnsAdvertise::deactivateWithdraws() {
    MdnsAdvertiser advertiser;
    auto backend = std::make_unique<FakeMdnsBackend>();
    FakeMdnsBackend *fake = backend.get();
    advertiser.setBackend(std::move(backend));

    advertiser.setPort(8765);
    advertiser.setActive(true);
    QCOMPARE(fake->registerCount, 1);

    advertiser.setActive(false);
    QCOMPARE(fake->unregisterCount, 1);
}

// Changing the port while active re-advertises on the new port.
void TestMdnsAdvertise::portChangeReadvertises() {
    MdnsAdvertiser advertiser;
    auto backend = std::make_unique<FakeMdnsBackend>();
    FakeMdnsBackend *fake = backend.get();
    advertiser.setBackend(std::move(backend));

    advertiser.setPort(8765);
    advertiser.setActive(true);
    QCOMPARE(fake->registerCount, 1);
    QCOMPARE(fake->last.port, quint16(8765));

    advertiser.setPort(9000);
    QCOMPARE(fake->unregisterCount, 1);
    QCOMPARE(fake->registerCount, 2);
    QCOMPARE(fake->last.port, quint16(9000));
}

// A zero/unbound port never advertises even when active.
void TestMdnsAdvertise::inactiveDoesNotAdvertise() {
    MdnsAdvertiser advertiser;
    auto backend = std::make_unique<FakeMdnsBackend>();
    FakeMdnsBackend *fake = backend.get();
    advertiser.setBackend(std::move(backend));

    advertiser.setActive(true); // port still 0
    QCOMPARE(fake->registerCount, 0);

    advertiser.setPort(8765); // now active + bound -> advertises
    QCOMPARE(fake->registerCount, 1);
}

// The Win32 wire translation the real backend feeds DnsServiceConstructInstance
// builds the fully-qualified mDNS service name and passes the port in HOST byte
// order — the API converts to network order for the SRV record itself, so a
// byteswap here double-converts it (a real Android NsdManager resolve surfaced
// the swapped 0x3D22 = 15650 instead of 8765). Pinned so a regression in the
// adapter fails loud.
void TestMdnsAdvertise::wireFormServiceNameAndHostOrderPort() {
    const MdnsService service{
        QStringLiteral("_qvim-mirror._tcp"),
        QStringLiteral("qvim on DESKTOP"),
        QStringLiteral("DESKTOP"),
        quint16(8765),
        { { QStringLiteral("proto"), QStringLiteral("1") } },
    };
    const MdnsWireForm wire = MdnsAdvertiser::toWireForm(service);

    QCOMPARE(wire.serviceName, QStringLiteral("qvim on DESKTOP._qvim-mirror._tcp.local"));
    QCOMPARE(wire.hostName, QStringLiteral("DESKTOP.local"));
    // Passed through unswapped: DnsServiceConstructInstance expects host order and
    // builds the network-order SRV record itself. 8765, not 0x3D22 (15650).
    QCOMPARE(wire.wPort, quint16(8765));
}

QTEST_GUILESS_MAIN(TestMdnsAdvertise)
#include "test_mdns_advertise.moc"
