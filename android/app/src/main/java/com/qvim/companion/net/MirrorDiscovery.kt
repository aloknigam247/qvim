package com.qvim.companion.net

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.net.wifi.WifiManager
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow

/**
 * Emits `ws://host:port` endpoints for qvim session mirrors discovered on the LAN.
 * A seam so a fake can drive discovery in tests without the Android framework.
 */
fun interface MirrorDiscovery {
    fun endpoints(): Flow<String>
}

/**
 * mDNS/DNS-SD discovery backed by the platform [NsdManager]. Browses for
 * [SERVICE_TYPE], resolves each instance, and emits its ws endpoint. Collecting
 * the flow drives discovery; cancelling it stops discovery and releases the
 * multicast lock. There is deliberately no caching — a rediscovery reflects the
 * current LAN, so a mirror that went away simply stops being emitted.
 */
class NsdMirrorDiscovery(context: Context) : MirrorDiscovery {

    private val appContext = context.applicationContext

    override fun endpoints(): Flow<String> = callbackFlow {
        val nsdManager = appContext.getSystemService(Context.NSD_SERVICE) as NsdManager
        val wifi = appContext.getSystemService(Context.WIFI_SERVICE) as WifiManager

        // Many devices drop multicast (and thus mDNS) unless a lock is held.
        val lock = wifi.createMulticastLock("qvim-mirror-discovery").apply {
            setReferenceCounted(true)
            acquire()
        }

        fun emitResolved(info: NsdServiceInfo) {
            val host = info.host?.hostAddress ?: return
            // Bracket IPv6 literals so the resulting URL is valid.
            val hostPart = if (host.contains(':')) "[${host.substringBefore('%')}]" else host
            trySend("ws://$hostPart:${info.port}")
        }

        val discoveryListener = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) {}

            override fun onServiceFound(service: NsdServiceInfo) {
                if (!service.serviceType.trimEnd('.').endsWith(SERVICE_TYPE.trimEnd('.'))) return
                // resolveService needs a fresh listener per concurrent resolve.
                nsdManager.resolveService(service, object : NsdManager.ResolveListener {
                    override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {}
                    override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                        emitResolved(serviceInfo)
                    }
                })
            }

            override fun onServiceLost(service: NsdServiceInfo) {}
            override fun onDiscoveryStopped(serviceType: String) {}

            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) {
                close()
            }

            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
        }

        nsdManager.discoverServices(SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, discoveryListener)

        awaitClose {
            runCatching { nsdManager.stopServiceDiscovery(discoveryListener) }
            if (lock.isHeld) lock.release()
        }
    }

    companion object {
        const val SERVICE_TYPE = "_qvim-mirror._tcp"
    }
}
