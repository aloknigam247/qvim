#include "MdnsAdvertiser.h"

#include <QHostInfo>

#ifdef Q_OS_WIN
#include <windows.h>
// windns.h must follow windows.h.
#include <vector>
#include <windns.h>
#endif

namespace qvim {

namespace {

// A no-op backend for platforms without a native responder (and the fallback
// when the Win32 path is unavailable). Keeps MdnsAdvertiser buildable and inert
// off Windows.
class NullMdnsBackend : public MdnsBackend {
public:
    void registerService(const MdnsService &) override {}
    void unregisterService() override {}
};
} // namespace

#ifdef Q_OS_WIN

namespace {
// One live advertisement. Heap-owned and self-cleaning: it outlives the backend
// so an async register/deregister completion callback (which runs on an OS
// thread) never dereferences a destroyed backend. The main thread detaches it
// (clears the backend's pointer) before withdrawing; the deregister completion
// frees it.
struct Win32Registration {
    DNS_SERVICE_REGISTER_REQUEST request{};
    PDNS_SERVICE_INSTANCE instance = nullptr;
};
} // namespace

static void CALLBACK onRegisterComplete(DWORD /*status*/, PVOID /*context*/,
                                        PDNS_SERVICE_INSTANCE /*instance*/) {
    // The advertisement lives until it is withdrawn; nothing to free here. The
    // registration object is owned by the backend until unregisterService()
    // detaches it, then by onDeregisterComplete().
}

static void CALLBACK onDeregisterComplete(DWORD /*status*/, PVOID context,
                                          PDNS_SERVICE_INSTANCE instance) {
    if(instance) { DnsServiceFreeInstance(instance); }
    delete static_cast<Win32Registration *>(context);
}

namespace {
class Win32MdnsBackend : public MdnsBackend {
public:
    Win32MdnsBackend() = default;
    ~Win32MdnsBackend() override { unregisterService(); }
    QVIM_DISABLE_COPY_MOVE(Win32MdnsBackend)

    void registerService(const MdnsService &service) override {
        if(m_reg) { unregisterService(); }
        const MdnsWireForm wire = MdnsAdvertiser::toWireForm(service);

        // Keep the wide-string buffers alive across the construct call — the API
        // copies them into the returned instance.
        const std::wstring serviceName = wire.serviceName.toStdWString();
        const std::wstring hostName = wire.hostName.toStdWString();

        std::vector<std::wstring> keyStore;
        std::vector<std::wstring> valueStore;
        keyStore.reserve(service.txt.size());
        valueStore.reserve(service.txt.size());
        for(auto it = service.txt.constBegin(); it != service.txt.constEnd(); ++it) {
            keyStore.push_back(it.key().toStdWString());
            valueStore.push_back(it.value().toStdWString());
        }
        std::vector<PCWSTR> keys;
        std::vector<PCWSTR> values;
        keys.reserve(keyStore.size());
        values.reserve(valueStore.size());
        for(std::size_t i = 0; i < keyStore.size(); ++i) {
            keys.push_back(keyStore[i].c_str());
            values.push_back(valueStore[i].c_str());
        }

        PDNS_SERVICE_INSTANCE instance = DnsServiceConstructInstance(
            serviceName.c_str(), hostName.c_str(), nullptr, nullptr, wire.wPort, 0, 0,
            static_cast<DWORD>(keys.size()), keys.empty() ? nullptr : keys.data(),
            values.empty() ? nullptr : values.data());
        if(!instance) {
            qWarning("qvim: mDNS DnsServiceConstructInstance failed for %ls", serviceName.c_str());
            return;
        }

        auto *reg = new Win32Registration;
        reg->instance = instance;
        reg->request.Version = DNS_QUERY_REQUEST_VERSION1;
        reg->request.InterfaceIndex = 0;
        reg->request.pServiceInstance = instance;
        reg->request.pRegisterCompletionCallback = &onRegisterComplete;
        reg->request.pQueryContext = reg;
        reg->request.unicastEnabled = FALSE;

        const DWORD status = DnsServiceRegister(&reg->request, nullptr);
        if(status != DNS_REQUEST_PENDING) {
            qWarning("qvim: mDNS DnsServiceRegister returned %lu", status);
            DnsServiceFreeInstance(instance);
            delete reg;
            return;
        }
        m_reg = reg;
    }

    void unregisterService() override {
        if(!m_reg) { return; }
        // Detach first so a late register completion / destructor can't touch it,
        // then hand ownership to the deregister completion, which frees both the
        // instance and the registration.
        Win32Registration *reg = m_reg;
        m_reg = nullptr;
        reg->request.pRegisterCompletionCallback = &onDeregisterComplete;
        reg->request.pQueryContext = reg;
        const DWORD status = DnsServiceDeRegister(&reg->request, nullptr);
        if(status != DNS_REQUEST_PENDING) {
            // No completion callback will fire; clean up synchronously.
            if(reg->instance) { DnsServiceFreeInstance(reg->instance); }
            delete reg;
        }
    }

private:
    Win32Registration *m_reg = nullptr;
};
} // namespace

#endif // Q_OS_WIN

static std::unique_ptr<MdnsBackend> makeDefaultBackend() {
#ifdef Q_OS_WIN
    return std::make_unique<Win32MdnsBackend>();
#else
    return std::make_unique<NullMdnsBackend>();
#endif
}

MdnsAdvertiser::MdnsAdvertiser(QObject *parent) :
    QObject(parent), m_backend(makeDefaultBackend()) {}

MdnsAdvertiser::~MdnsAdvertiser() = default;

void MdnsAdvertiser::setActive(bool active) {
    if(active == m_active) { return; }
    m_active = active;
    reevaluate();
    emit activeChanged();
}

void MdnsAdvertiser::setPort(int port) {
    if(port == m_port) { return; }
    m_port = port;
    reevaluate();
    emit portChanged();
}

void MdnsAdvertiser::setBackend(std::unique_ptr<MdnsBackend> backend) {
    if(m_registered && m_backend) {
        m_backend->unregisterService();
        m_registered = false;
        m_registeredPort = 0;
    }
    m_backend = std::move(backend);
    reevaluate();
}

MdnsService MdnsAdvertiser::currentService() const {
    const QString host = QHostInfo::localHostName();
    return MdnsService{
        .type = QString::fromLatin1(kMirrorServiceType),
        .instanceName = QStringLiteral("qvim on %1").arg(host),
        .hostName = host,
        .port = static_cast<quint16>(m_port),
        .txt = { { QStringLiteral("proto"), QStringLiteral("1") } },
    };
}

MdnsWireForm MdnsAdvertiser::toWireForm(const MdnsService &service) {
    return MdnsWireForm{
        .serviceName = QStringLiteral("%1.%2.local").arg(service.instanceName, service.type),
        .hostName = QStringLiteral("%1.local").arg(service.hostName),
        .wPort = service.port,
    };
}

void MdnsAdvertiser::reevaluate() {
    if(!m_backend) { return; }
    const bool shouldAdvertise = m_active && m_port > 0;
    if(shouldAdvertise) {
        if(m_registered && m_registeredPort == static_cast<quint16>(m_port)) { return; }
        if(m_registered) { m_backend->unregisterService(); }
        m_backend->registerService(currentService());
        m_registered = true;
        m_registeredPort = static_cast<quint16>(m_port);
    } else if(m_registered) {
        m_backend->unregisterService();
        m_registered = false;
        m_registeredPort = 0;
    }
}

} // namespace qvim
