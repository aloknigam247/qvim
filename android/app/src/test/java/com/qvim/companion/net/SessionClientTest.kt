package com.qvim.companion.net

import com.qvim.companion.model.ServerFrame
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.runBlocking
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import java.util.concurrent.CopyOnWriteArrayList
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * Drives [SessionClient.frames] against a real (loopback) WebSocket via MockWebServer,
 * so the OkHttp listener callbacks (onOpen/onMessage/onClosing/onFailure), the
 * hello->resume reply, and the state transitions all execute on the JVM without an
 * emulator.
 */
class SessionClientTest {
    private lateinit var server: MockWebServer

    @Before
    fun setUp() {
        server = MockWebServer()
    }

    @After
    fun tearDown() {
        server.shutdown()
    }

    private fun wsUrl(): String = server.url("/").toString().replaceFirst("http://", "ws://")

    private fun <T> LinkedBlockingQueue<T>.take(timeoutMs: Long): T? = poll(timeoutMs, TimeUnit.MILLISECONDS)

    @Test
    fun helloTriggersResumeAndFramesFlowThrough() {
        val serverInbound = LinkedBlockingQueue<String>()
        var serverSocket: WebSocket? = null
        val serverListener =
            object : WebSocketListener() {
                override fun onOpen(webSocket: WebSocket, response: Response) {
                    serverSocket = webSocket
                    webSocket.send("""{"type":"hello","protocol":1,"sessionId":"s1"}""")
                }

                override fun onMessage(webSocket: WebSocket, text: String) {
                    serverInbound.add(text)
                }
            }
        server.enqueue(MockResponse().withWebSocketUpgrade(serverListener))
        server.start()

        val client = SessionClient()
        val frames = CopyOnWriteArrayList<ServerFrame>()
        val states = CopyOnWriteArrayList<ConnectionState>()
        val scope = CoroutineScope(Dispatchers.IO)
        val stateJob: Job = client.state.onEach { states.add(it) }.launchIn(scope)
        val framesJob: Job = client.frames(wsUrl()).onEach { frames.add(it) }.launchIn(scope)

        // Client should reply to hello with a resume frame carrying the session id.
        val resume = serverInbound.take(5000)
        assertEquals("""{"type":"resume","lastSeq":0,"sessionId":"s1"}""", resume)

        // Server pushes an atomic message; the client decodes and emits it.
        serverSocket!!.send("""{"seq":2,"type":"message","id":"u1","role":"user","text":"hi"}""")
        waitUntil { frames.any { it is ServerFrame.Message } }
        val msg = frames.filterIsInstance<ServerFrame.Message>().first()
        assertEquals("hi", msg.text)

        // The user-input send path rides the same socket.
        client.send("typed")
        val input = serverInbound.take(5000)
        assertEquals("""{"type":"input","text":"typed"}""", input)

        waitUntil { states.contains(ConnectionState.Connected) }
        assertTrue(states.contains(ConnectionState.Connecting))
        assertTrue(states.contains(ConnectionState.Connected))

        // Server-initiated close drives the client back to Disconnected.
        serverSocket!!.close(1000, "bye")
        waitUntil { client.state.value == ConnectionState.Disconnected }
        assertEquals(ConnectionState.Disconnected, client.state.value)

        framesJob.cancel()
        stateJob.cancel()
    }

    @Test
    fun failureToConnectEndsDisconnected() {
        // Point at a port with nothing listening: onFailure fires and the flow ends
        // with the state back at Disconnected.
        val client = SessionClient()
        runBlocking {
            val job = client.frames("ws://127.0.0.1:1").onEach { }.launchIn(CoroutineScope(Dispatchers.IO))
            waitUntil(timeoutMs = 8000) { client.state.value == ConnectionState.Disconnected }
            assertEquals(ConnectionState.Disconnected, client.state.value)
            job.cancel()
        }
    }

    @Test
    fun sendWithoutSocketIsNoOp() {
        // No connection opened yet: send must not throw.
        SessionClient().send("ignored")
    }

    private fun waitUntil(timeoutMs: Long = 5000, predicate: () -> Boolean) {
        val deadline = System.currentTimeMillis() + timeoutMs
        while (System.currentTimeMillis() < deadline) {
            if (predicate()) return
            Thread.sleep(20)
        }
        throw AssertionError("condition not met within ${timeoutMs}ms")
    }
}
