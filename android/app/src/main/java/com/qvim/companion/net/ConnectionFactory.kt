package com.qvim.companion.net

import okhttp3.Request

/**
 * Builds the OkHttp [Request] used to open the session-mirror socket. This is the
 * seam later slices extend without touching call sites: #53 adds an Authorization
 * header here, #54 switches ws:// -> wss://. Today it just maps the ws(s) endpoint to
 * the http(s) URL OkHttp's HttpUrl requires.
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
