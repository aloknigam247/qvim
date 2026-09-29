package com.qvim.companion.net

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import okhttp3.OkHttpClient
import okhttp3.Request

/** One AHP host advertised by cptower's `/hosts` discovery endpoint. */
@Serializable
data class CatalogHost(
    val id: String,
    val port: Int,
    val label: String = "",
    val protocol: String? = null,
    val sessions: Int = 0,
)

/**
 * Reads the cptower multiplexer's host catalog from `GET http://<base>/hosts`.
 *
 * cptower discovers every live Copilot AHP host on the machine and exposes each one
 * behind a `/ws/<port>` route on a single external port. The app fetches this catalog
 * to learn which hosts exist, then opens one [AhpConnection] per host. A failed or
 * non-JSON response means the endpoint is not a cptower instance (e.g. a bare AHP
 * host), which the caller treats as "connect directly" rather than an error.
 */
class HostCatalog(private val client: OkHttpClient = OkHttpClient()) {
    suspend fun fetch(base: String): List<CatalogHost> =
        withContext(Dispatchers.IO) {
            val request = Request.Builder().url(hostsUrl(base)).build()
            client.newCall(request).execute().use { response ->
                val body = response.body?.string()
                if (!response.isSuccessful || body.isNullOrBlank()) {
                    throw IllegalStateException("no host catalog at $base (${response.code})")
                }
                json.decodeFromString<List<CatalogHost>>(body)
            }
        }

    private fun hostsUrl(base: String): String {
        val trimmed = base.trim().trimEnd('/')
        val withScheme =
            when {
                trimmed.startsWith("ws://") -> "http://" + trimmed.removePrefix("ws://")
                trimmed.startsWith("wss://") -> "https://" + trimmed.removePrefix("wss://")
                trimmed.startsWith("http://") || trimmed.startsWith("https://") -> trimmed
                else -> "http://$trimmed"
            }
        return "$withScheme/hosts"
    }

    private companion object {
        val json = Json { ignoreUnknownKeys = true }
    }
}
