#pragma once

#include <QMap>
#include <QObject>
#include <qqmlregistration.h>
#include <QString>

#include <memory>

namespace qvim {

// The DNS-SD service type qvim advertises the session mirror under. The Android
// companion browses for exactly this type.
inline constexpr char kMirrorServiceType[] = "_qvim-mirror._tcp";

// A resolved description of the service to advertise. Pure value type — no
// platform state — so the advertising logic is testable without touching the
// network.
struct MdnsService {
    QString type;         // e.g. "_qvim-mirror._tcp"
    QString instanceName; // e.g. "qvim on HOSTNAME"
    QString hostName;     // the local host label, e.g. "HOSTNAME"
    quint16 port = 0;
    QMap<QString, QString> txt; // e.g. { "proto": "1" }
};

// The Win32 wire form of an MdnsService, as passed to DnsServiceConstructInstance:
// the fully-qualified mDNS service name, the ".local" host name, and the port.
// DnsServiceConstructInstance takes wPort in host byte order and performs the
// network-order conversion for the SRV record itself, so no byteswap is applied
// here — a real device (Android NsdManager) confirmed a byteswapped value is
// double-converted and surfaces as the wrong port. Kept as a pure translation so
// a unit test can pin it without linking dnsapi or hitting the network.
struct MdnsWireForm {
    QString serviceName; // "<instance>.<type>.local"
    QString hostName;    // "<host>.local"
    quint16 wPort;       // host byte order, as DnsServiceConstructInstance expects
};

// Platform responder seam. The real backend registers over the OS mDNS stack;
// tests inject a fake that records the descriptor.
class MdnsBackend {
public:
    virtual ~MdnsBackend() = default;
    virtual void registerService(const MdnsService &service) = 0;
    virtual void unregisterService() = 0;
};

// Advertises the session-mirror endpoint over mDNS/DNS-SD while `active`. Wiring
// mirrors SessionMirrorServer's lifecycle: bind the advertiser's `port` to the
// mirror's `boundPort` and its `active` to the panel visibility, so the service is
// only announced while the panel is open and the port is actually bound.
//
// Registration happens entirely off the redraw->paint hot path: one OS call when
// the panel opens, one when it closes. Never per frame, per row, or per message.
class MdnsAdvertiser : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)

public:
    explicit MdnsAdvertiser(QObject *parent = nullptr);
    ~MdnsAdvertiser() override;

    bool isActive() const { return m_active; }
    void setActive(bool active);

    int port() const { return m_port; }
    void setPort(int port);

    // Swap the platform backend. Tests call this before activation to inject a
    // fake. Ownership transfers to the advertiser.
    void setBackend(std::unique_ptr<MdnsBackend> backend);

    // The descriptor the advertiser would register for the current state. Exposed
    // so a test can assert the advertised type and port without a live responder.
    MdnsService currentService() const;

    // Pure translation from a descriptor to its Win32 DNS-SD wire form. Static and
    // network-free so a test can pin the exact service name and (host-order) port.
    static MdnsWireForm toWireForm(const MdnsService &service);

signals:
    void activeChanged();
    void portChanged();

private:
    void reevaluate(); // (re)register or withdraw to match (active, port)

    std::unique_ptr<MdnsBackend> m_backend;
    bool m_active = false;
    int m_port = 0;
    bool m_registered = false;
    quint16 m_registeredPort = 0;
};

} // namespace qvim
