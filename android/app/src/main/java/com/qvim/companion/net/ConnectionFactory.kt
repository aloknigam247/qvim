package com.qvim.companion.net

import okhttp3.Request

/**
 * Builds the OkHttp [Request] that opens the session-mirror socket, mapping the ws(s)
 * endpoint to the http(s) URL OkHttp's HttpUrl requires. Kept as a seam so request
 * construction (auth headers, wss upgrade) can change without touching call sites.
 */
fun interface ConnectionFactory {
    fun requestFor(endpoint: String): Request
}

class PlainConnectionFactory : ConnectionFactory {
    override fun requestFor(endpoint: String): Request {
        val httpUrl = endpoint
            .replaceFirst("wss://", "https://")
            .replaceFirst("ws://", "http://")
        return Request.Builder().url(httpUrl).build()
    }
}
