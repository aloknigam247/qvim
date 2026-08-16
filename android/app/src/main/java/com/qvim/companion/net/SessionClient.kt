package com.qvim.companion.net

import com.qvim.companion.model.Protocol
import com.qvim.companion.model.ServerFrame
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.callbackFlow
import okhttp3.OkHttpClient
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.util.concurrent.atomic.AtomicLong

enum class ConnectionState { Disconnected, Connecting, Connected }

/**
 * Thin OkHttp WebSocket wrapper. [frames] opens a connection and emits decoded
 * [ServerFrame]s; collecting it drives the socket's lifetime (cancel the collector to
 * close). There is deliberately NO automatic reconnect in this slice — auto-reconnect
 * with resume/dedup is issue #52. Each connection is tagged with a generation so that
 * callbacks from a socket superseded by a newer connect are ignored.
 */
class SessionClient(
    private val factory: ConnectionFactory = PlainConnectionFactory(),
    private val client: OkHttpClient = OkHttpClient(),
) {
    private val _state = MutableStateFlow(ConnectionState.Disconnected)
    val state: StateFlow<ConnectionState> = _state.asStateFlow()

    private val generation = AtomicLong(0)

    @Volatile
    private var webSocket: WebSocket? = null

    fun frames(endpoint: String): Flow<ServerFrame> = callbackFlow {
        val gen = generation.incrementAndGet()
        _state.value = ConnectionState.Connecting

        val listener = object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                if (gen != generation.get()) return
                _state.value = ConnectionState.Connected
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                if (gen != generation.get()) return
                val frame = Protocol.decode(text)
                // Protocol: server sends hello first, client replies with resume.
                if (frame is ServerFrame.Hello) {
                    webSocket.send(Protocol.encodeResume(lastSeq = 0, sessionId = frame.sessionId))
                }
                trySend(frame)
            }

            override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
                if (gen == generation.get()) _state.value = ConnectionState.Disconnected
                webSocket.close(NORMAL_CLOSURE, null)
                close()
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (gen == generation.get()) _state.value = ConnectionState.Disconnected
                close(t)
            }
        }

        val ws = client.newWebSocket(factory.requestFor(endpoint), listener)
        webSocket = ws

        awaitClose {
            ws.cancel()
            if (gen == generation.get()) {
                webSocket = null
                _state.value = ConnectionState.Disconnected
            }
        }
    }

    /** Sends a user input frame on the current socket, if connected. */
    fun send(text: String) {
        webSocket?.send(Protocol.encodeInput(text))
    }

    private companion object {
        const val NORMAL_CLOSURE = 1000
    }
}
