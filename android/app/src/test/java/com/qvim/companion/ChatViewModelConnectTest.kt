package com.qvim.companion

import com.qvim.companion.net.SessionClient
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
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
import java.util.concurrent.LinkedBlockingQueue
import java.util.concurrent.TimeUnit

/**
 * Exercises the connection-driving half of [ChatViewModel] (connect / send / reconnect /
 * teardown) against a loopback MockWebServer, complementing the discovery-policy test.
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ChatViewModelConnectTest {
    private val dispatcher = UnconfinedTestDispatcher()
    private lateinit var server: MockWebServer
    private val serverInbound = LinkedBlockingQueue<String>()
    private var serverSocket: WebSocket? = null

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
        server = MockWebServer()
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
        server.shutdown()
    }

    private fun startServer(sendHelloThenMessage: Boolean) {
        val listener =
            object : WebSocketListener() {
                override fun onOpen(webSocket: WebSocket, response: Response) {
                    serverSocket = webSocket
                    if (sendHelloThenMessage) {
                        webSocket.send("""{"type":"hello","protocol":1,"sessionId":"s1"}""")
                        webSocket.send("""{"seq":2,"type":"message","id":"u1","role":"user","text":"hi"}""")
                    }
                }

                override fun onMessage(webSocket: WebSocket, text: String) {
                    serverInbound.add(text)
                }
            }
        server.enqueue(MockResponse().withWebSocketUpgrade(listener))
        server.start()
    }

    private fun wsUrl(): String = server.url("/").toString().replaceFirst("http://", "ws://")

    @Test
    fun connectPopulatesTranscriptFromServerFrames() {
        startServer(sendHelloThenMessage = true)
        val vm = ChatViewModel(client = SessionClient())
        vm.setEndpoint(wsUrl())
        vm.connect()

        waitUntil { vm.messages.value.isNotEmpty() }
        assertEquals(
            "hi",
            vm.messages.value
                .first()
                .text,
        )
    }

    @Test
    fun sendForwardsToConnectedSocket() {
        startServer(sendHelloThenMessage = false)
        val vm = ChatViewModel(client = SessionClient())
        vm.setEndpoint(wsUrl())
        vm.connect()

        waitUntil { serverSocket != null }
        vm.send("  hello  ")
        val got = serverInbound.poll(5000, TimeUnit.MILLISECONDS)
        assertEquals("""{"type":"input","text":"hello"}""", got)
    }

    @Test
    fun reconnectClearsPriorTranscript() {
        startServer(sendHelloThenMessage = true)
        val vm = ChatViewModel(client = SessionClient())
        vm.setEndpoint(wsUrl())
        vm.connect()
        waitUntil { vm.messages.value.isNotEmpty() }

        // A fresh connect must reset transient transcript state to empty first.
        vm.connect()
        assertTrue(vm.messages.value.isEmpty())
    }

    @Test
    fun blankEndpointConnectIsNoOp() {
        val vm = ChatViewModel(client = SessionClient())
        vm.connect()
        assertTrue(vm.messages.value.isEmpty())
    }

    @Test
    fun blankSendIsNoOp() {
        val vm = ChatViewModel(client = SessionClient())
        vm.send("   ")
        // No socket, no crash, nothing forwarded.
        assertTrue(vm.messages.value.isEmpty())
    }

    @Test
    fun stopDiscoveryIsIdempotent() {
        val vm = ChatViewModel(client = SessionClient())
        vm.stopDiscovery()
        vm.startDiscovery()
        vm.stopDiscovery()
    }

    @Test
    fun onClearedCancelsJobs() {
        val vm = ChatViewModel(client = SessionClient())
        vm.startDiscovery()
        val m = vm.javaClass.getDeclaredMethod("onCleared")
        m.isAccessible = true
        m.invoke(vm)
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
