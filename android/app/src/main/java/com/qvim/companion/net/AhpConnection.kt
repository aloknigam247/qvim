package com.qvim.companion.net

import com.microsoft.agenthostprotocol.Ahp
import com.microsoft.agenthostprotocol.chatReducer
import com.microsoft.agenthostprotocol.generated.ActionEnvelope
import com.microsoft.agenthostprotocol.generated.ActionType
import com.microsoft.agenthostprotocol.generated.AhpClientNotifications
import com.microsoft.agenthostprotocol.generated.AhpCommands
import com.microsoft.agenthostprotocol.generated.ChatState
import com.microsoft.agenthostprotocol.generated.ChatTurnStartedAction
import com.microsoft.agenthostprotocol.generated.DispatchActionParams
import com.microsoft.agenthostprotocol.generated.InitializeParams
import com.microsoft.agenthostprotocol.generated.InitializeResult
import com.microsoft.agenthostprotocol.generated.JsonRpcNotification
import com.microsoft.agenthostprotocol.generated.JsonRpcRequest
import com.microsoft.agenthostprotocol.generated.ListSessionsParams
import com.microsoft.agenthostprotocol.generated.ListSessionsResult
import com.microsoft.agenthostprotocol.generated.Message
import com.microsoft.agenthostprotocol.generated.MessageKind
import com.microsoft.agenthostprotocol.generated.MessageOrigin
import com.microsoft.agenthostprotocol.generated.SUPPORTED_PROTOCOL_VERSIONS
import com.microsoft.agenthostprotocol.generated.SessionState
import com.microsoft.agenthostprotocol.generated.Snapshot
import com.microsoft.agenthostprotocol.generated.SnapshotState
import com.microsoft.agenthostprotocol.generated.StateActionChatTurnStarted
import com.microsoft.agenthostprotocol.generated.SubscribeParams
import com.microsoft.agenthostprotocol.generated.SubscribeResult
import com.microsoft.agenthostprotocol.sessionReducer
import com.qvim.companion.AhpTranscript
import com.qvim.companion.model.UiMessage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.serialization.KSerializer
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.longOrNull
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.time.Instant
import java.util.UUID
import java.util.concurrent.atomic.AtomicLong

/**
 * A single live connection to an Agent Host Protocol (AHP) server over a WebSocket.
 *
 * The socket is the source of truth: on open the client runs the JSON-RPC handshake
 * (`initialize` → `listSessions` → `subscribe` to every session, then to every chat
 * of each session) and folds the resulting `chat/` action stream through the
 * canonical AHP reducers ([chatReducer] / [sessionReducer]). The folded [ChatState]s
 * are projected to a flat [UiMessage] transcript via [AhpTranscript].
 *
 * All inbound frame handling runs on OkHttp's single reader thread, which delivers
 * listener callbacks serially — so the state mirror ([chats] / [sessions] / [pending]
 * / [subscribedChannels]) needs no locking. Only [outboundChatUri] and the two
 * [StateFlow]s are read off-thread; those are `@Volatile` / inherently thread-safe.
 * There is deliberately no automatic reconnect — the owner reconnects by discarding
 * this instance and creating a new one.
 */
class AhpConnection(private val endpoint: String, private val client: OkHttpClient = OkHttpClient()) {
    private val _state = MutableStateFlow(ConnectionState.Disconnected)
    val state: StateFlow<ConnectionState> = _state.asStateFlow()

    private val _transcript = MutableStateFlow<List<UiMessage>>(emptyList())
    val transcript: StateFlow<List<UiMessage>> = _transcript.asStateFlow()

    private val chats = LinkedHashMap<String, ChatState>()
    private val sessions = LinkedHashMap<String, SessionState>()
    private val subscribedChannels = HashSet<String>()
    private val pending = HashMap<Long, Pending>()

    private val nextId = AtomicLong(1)
    private val clientSeq = AtomicLong(1)
    private val clientId = UUID.randomUUID().toString()

    @Volatile
    private var outboundChatUri: String? = null

    @Volatile
    private var webSocket: WebSocket? = null

    private enum class Pending { Initialize, ListSessions, Subscribe }

    /** Opens the socket. Callbacks drive the rest of the lifecycle. */
    fun start() {
        _state.value = ConnectionState.Connecting
        webSocket = client.newWebSocket(requestFor(endpoint), Listener())
    }

    /** Releases the socket; the mirror is discarded with this instance. */
    fun close() {
        webSocket?.cancel()
        webSocket = null
        _state.value = ConnectionState.Disconnected
    }

    /**
     * Starts a new turn on the default chat by dispatching a `chat/turnStarted`
     * action. Returns false when not connected or no chat is available yet.
     */
    fun send(text: String): Boolean {
        val ws = webSocket ?: return false
        val chat = outboundChatUri ?: return false
        val action =
            ChatTurnStartedAction(
                type = ActionType.CHAT_TURN_STARTED,
                turnId = UUID.randomUUID().toString(),
                startedAt = Instant.now().toString(),
                message = Message(text = text, origin = MessageOrigin(kind = MessageKind.USER)),
            )
        val params =
            DispatchActionParams(
                channel = chat,
                clientSeq = clientSeq.getAndIncrement(),
                action = StateActionChatTurnStarted(action),
            )
        val note = AhpClientNotifications.dispatchAction(params)
        return ws.send(
            Ahp.json.encodeToString(JsonRpcNotification.serializer(DispatchActionParams.serializer()), note),
        )
    }

    private inner class Listener : WebSocketListener() {
        override fun onOpen(webSocket: WebSocket, response: Response) {
            _state.value = ConnectionState.Connected
            sendInitialize()
        }

        override fun onMessage(webSocket: WebSocket, text: String) {
            handleFrame(text)
        }

        override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
            webSocket.close(NORMAL_CLOSURE, null)
            _state.value = ConnectionState.Disconnected
        }

        override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
            _state.value = ConnectionState.Disconnected
        }
    }

    private fun handleFrame(text: String) {
        val obj = Ahp.json.parseToJsonElement(text).jsonObject
        val id = obj["id"]?.jsonPrimitive?.longOrNull
        if (id != null && (obj.containsKey("result") || obj.containsKey("error"))) {
            val kind = pending.remove(id) ?: return
            if (obj.containsKey("error")) return
            val result = obj["result"] ?: return
            when (kind) {
                Pending.Initialize -> {
                    val init = Ahp.json.decodeFromJsonElement(InitializeResult.serializer(), result)
                    init.snapshots.forEach(::applySnapshot)
                    sendListSessions()
                }
                Pending.ListSessions -> {
                    val list = Ahp.json.decodeFromJsonElement(ListSessionsResult.serializer(), result)
                    list.items.forEach { subscribeChannel(it.resource) }
                }
                Pending.Subscribe -> {
                    val sub = Ahp.json.decodeFromJsonElement(SubscribeResult.serializer(), result)
                    sub.snapshot?.let(::applySnapshot)
                }
            }
            return
        }
        when (obj["method"]?.jsonPrimitive?.contentOrNull) {
            "action" ->
                obj["params"]?.let {
                    applyAction(
                        Ahp.json.decodeFromJsonElement(ActionEnvelope.serializer(), it),
                    )
                }
            "root/sessionAdded" -> {
                obj["params"]
                    ?.jsonObject
                    ?.get("summary")
                    ?.jsonObject
                    ?.get("resource")
                    ?.jsonPrimitive
                    ?.contentOrNull
                    ?.let { subscribeChannel(it) }
            }
        }
    }

    private fun applyAction(env: ActionEnvelope) {
        val channel = env.channel
        when {
            channel.startsWith(CHAT_PREFIX) -> {
                val current = chats[channel] ?: return
                chats[channel] = chatReducer(current, env.action)
                recompute()
            }
            channel.startsWith(SESSION_PREFIX) -> {
                val current = sessions[channel] ?: return
                val next = sessionReducer(current, env.action)
                sessions[channel] = next
                subscribeChatsOf(next)
            }
        }
    }

    private fun applySnapshot(snapshot: Snapshot) {
        when (val state = snapshot.state) {
            is SnapshotState.Chat -> {
                chats[snapshot.resource] = state.value
                if (outboundChatUri == null) outboundChatUri = snapshot.resource
                recompute()
            }
            is SnapshotState.Session -> {
                sessions[snapshot.resource] = state.value
                subscribeChatsOf(state.value)
            }
            else -> Unit
        }
    }

    private fun subscribeChatsOf(session: SessionState) {
        if (outboundChatUri == null) {
            outboundChatUri = session.defaultChat ?: session.chats.firstOrNull()?.resource
        }
        session.chats.forEach { subscribeChannel(it.resource) }
    }

    private fun sendInitialize() {
        val id = nextId.getAndIncrement()
        pending[id] = Pending.Initialize
        val params =
            InitializeParams(
                channel = ROOT_CHANNEL,
                protocolVersions = SUPPORTED_PROTOCOL_VERSIONS,
                clientId = clientId,
                initialSubscriptions = listOf(ROOT_CHANNEL),
            )
        sendRpc(AhpCommands.initialize(id, params), InitializeParams.serializer())
    }

    private fun sendListSessions() {
        val id = nextId.getAndIncrement()
        pending[id] = Pending.ListSessions
        sendRpc(
            AhpCommands.listSessions(id, ListSessionsParams(channel = ROOT_CHANNEL)),
            ListSessionsParams.serializer(),
        )
    }

    private fun subscribeChannel(channel: String) {
        if (!subscribedChannels.add(channel)) return
        val id = nextId.getAndIncrement()
        pending[id] = Pending.Subscribe
        sendRpc(AhpCommands.subscribe(id, SubscribeParams(channel = channel)), SubscribeParams.serializer())
    }

    private fun <P> sendRpc(request: JsonRpcRequest<P>, paramSerializer: KSerializer<P>) {
        webSocket?.send(Ahp.json.encodeToString(JsonRpcRequest.serializer(paramSerializer), request))
    }

    private fun recompute() {
        _transcript.value = AhpTranscript.fromChats(chats.values)
    }

    private fun requestFor(endpoint: String): Request {
        val trimmed = endpoint.trim()
        val url =
            when {
                trimmed.startsWith("ws://") -> "http://" + trimmed.removePrefix("ws://")
                trimmed.startsWith("wss://") -> "https://" + trimmed.removePrefix("wss://")
                trimmed.startsWith("http://") || trimmed.startsWith("https://") -> trimmed
                else -> "http://$trimmed"
            }
        return Request.Builder().url(url).build()
    }

    private companion object {
        const val NORMAL_CLOSURE = 1000
        const val ROOT_CHANNEL = "ahp-root://"
        const val SESSION_PREFIX = "ahp-session:"
        const val CHAT_PREFIX = "ahp-chat:"
    }
}
