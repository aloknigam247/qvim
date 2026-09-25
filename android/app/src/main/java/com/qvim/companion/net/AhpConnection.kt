package com.qvim.companion.net

import android.util.Log
import com.microsoft.agenthostprotocol.Ahp
import com.microsoft.agenthostprotocol.chatReducer
import com.microsoft.agenthostprotocol.generated.ActionEnvelope
import com.microsoft.agenthostprotocol.generated.ActionType
import com.microsoft.agenthostprotocol.generated.AhpClientNotifications
import com.microsoft.agenthostprotocol.generated.AhpCommands
import com.microsoft.agenthostprotocol.generated.ChatDeltaAction
import com.microsoft.agenthostprotocol.generated.ChatState
import com.microsoft.agenthostprotocol.generated.ChatTurnStartedAction
import com.microsoft.agenthostprotocol.generated.DispatchActionParams
import com.microsoft.agenthostprotocol.generated.InitializeParams
import com.microsoft.agenthostprotocol.generated.InitializeResult
import com.microsoft.agenthostprotocol.generated.JsonRpcNotification
import com.microsoft.agenthostprotocol.generated.JsonRpcRequest
import com.microsoft.agenthostprotocol.generated.ListSessionsParams
import com.microsoft.agenthostprotocol.generated.ListSessionsResult
import com.microsoft.agenthostprotocol.generated.MarkdownResponsePart
import com.microsoft.agenthostprotocol.generated.Message
import com.microsoft.agenthostprotocol.generated.MessageKind
import com.microsoft.agenthostprotocol.generated.MessageOrigin
import com.microsoft.agenthostprotocol.generated.ResponsePartKind
import com.microsoft.agenthostprotocol.generated.ResponsePartMarkdown
import com.microsoft.agenthostprotocol.generated.SUPPORTED_PROTOCOL_VERSIONS
import com.microsoft.agenthostprotocol.generated.SessionState
import com.microsoft.agenthostprotocol.generated.Snapshot
import com.microsoft.agenthostprotocol.generated.SnapshotState
import com.microsoft.agenthostprotocol.generated.StateActionChatDelta
import com.microsoft.agenthostprotocol.generated.StateActionChatTurnStarted
import com.microsoft.agenthostprotocol.generated.SubscribeParams
import com.microsoft.agenthostprotocol.generated.SubscribeResult
import com.microsoft.agenthostprotocol.sessionReducer
import com.qvim.companion.AhpTranscript
import com.qvim.companion.model.SessionInfo
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
 * listener callbacks serially. Session selection ([selectSession]) can arrive on
 * another thread, so the RPC bookkeeping shared with it ([pending] /
 * [subscribedChannels] / [nextId]) is guarded by [ioLock]; the folded state maps
 * ([chats] / [sessions]) are touched only on the reader thread. [outboundChatUri]
 * and the [StateFlow]s are read off-thread; those are `@Volatile` / thread-safe.
 * There is deliberately no automatic reconnect — the owner reconnects by discarding
 * this instance and creating a new one.
 */
class AhpConnection(private val endpoint: String, private val client: OkHttpClient = OkHttpClient()) {
    private val _state = MutableStateFlow(ConnectionState.Disconnected)
    val state: StateFlow<ConnectionState> = _state.asStateFlow()

    private val _transcript = MutableStateFlow<List<UiMessage>>(emptyList())
    val transcript: StateFlow<List<UiMessage>> = _transcript.asStateFlow()

    private val _availableSessions = MutableStateFlow<List<SessionInfo>>(emptyList())
    val availableSessions: StateFlow<List<SessionInfo>> = _availableSessions.asStateFlow()

    private val _selectedSession = MutableStateFlow<String?>(null)
    val selectedSession: StateFlow<String?> = _selectedSession.asStateFlow()

    private val chats = LinkedHashMap<String, ChatState>()
    private val sessions = LinkedHashMap<String, SessionState>()
    private val subscribedChannels = HashSet<String>()
    private val pending = HashMap<Long, Pending>()
    private val ioLock = Any()

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

    /**
     * Chooses which host session to observe and send to. Subscribes to that session
     * (and, transitively, its chats); the transcript is then scoped to it because no
     * other session is subscribed. Called at most once per connection — the picker is
     * shown only while no session is selected. A no-op if already selected.
     */
    fun selectSession(resource: String) {
        if (_selectedSession.value == resource) return
        _selectedSession.value = resource
        subscribeChannel(resource)
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
            val kind = synchronized(ioLock) { pending.remove(id) } ?: return
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
                    publishSessions(list.items.map { SessionInfo(it.resource, it.title) })
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
                obj["params"]?.let { params ->
                    try {
                        applyAction(Ahp.json.decodeFromJsonElement(ActionEnvelope.serializer(), params))
                    } catch (e: Exception) {
                        Log.e(TAG, "dropped malformed action: ${e.message}", e)
                    }
                }
            "root/sessionAdded" -> {
                val summary = obj["params"]?.jsonObject?.get("summary")?.jsonObject
                val resource = summary?.get("resource")?.jsonPrimitive?.contentOrNull
                if (resource != null) {
                    val title = summary["title"]?.jsonPrimitive?.contentOrNull ?: resource
                    addSession(SessionInfo(resource, title))
                }
            }
        }
    }

    /** Publishes the discovered session list; auto-selects when exactly one exists. */
    private fun publishSessions(discovered: List<SessionInfo>) {
        _availableSessions.value = discovered
        if (_selectedSession.value == null && discovered.size == 1) {
            selectSession(discovered.first().resource)
        }
    }

    /** Appends a dynamically announced session, de-duplicated by resource. */
    private fun addSession(info: SessionInfo) {
        val current = _availableSessions.value
        if (current.any { it.resource == info.resource }) return
        publishSessions(current + info)
    }

    private fun applyAction(env: ActionEnvelope) {
        val channel = env.channel
        val chat = chats[channel]
        if (chat != null) {
            val action = env.action
            chats[channel] =
                if (action is StateActionChatDelta) foldChatDelta(chat, action.value) else chatReducer(chat, action)
            recompute()
            return
        }
        val session = sessions[channel]
        if (session != null) {
            val next = sessionReducer(session, env.action)
            sessions[channel] = next
            subscribeChatsOf(next)
        }
    }

    /**
     * Folds a `chat/delta` chunk into the active turn. The host streams markdown as a
     * sequence of deltas keyed by [ChatDeltaAction.partId] without a preceding
     * `chat/responsePart`, so the canonical reducer never materialises the part; we
     * append the chunk to the matching markdown part here (creating it on first sight)
     * so streaming text renders live instead of only after a resubscribe snapshot.
     */
    private fun foldChatDelta(chat: ChatState, delta: ChatDeltaAction): ChatState {
        val active = chat.activeTurn ?: return chat
        val parts = active.responseParts
        val index = parts.indexOfFirst { it is ResponsePartMarkdown && it.value.id == delta.partId }
        val nextParts =
            if (index >= 0) {
                val existing = parts[index] as ResponsePartMarkdown
                val merged = existing.value.copy(content = existing.value.content + delta.content)
                parts.toMutableList().also { it[index] = ResponsePartMarkdown(merged) }
            } else {
                parts +
                    ResponsePartMarkdown(
                        MarkdownResponsePart(
                            kind = ResponsePartKind.MARKDOWN,
                            id = delta.partId,
                            content = delta.content,
                        ),
                    )
            }
        return chat.copy(activeTurn = active.copy(responseParts = nextParts))
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
        synchronized(ioLock) { pending[id] = Pending.Initialize }
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
        synchronized(ioLock) { pending[id] = Pending.ListSessions }
        sendRpc(
            AhpCommands.listSessions(id, ListSessionsParams(channel = ROOT_CHANNEL)),
            ListSessionsParams.serializer(),
        )
    }

    private fun subscribeChannel(channel: String) {
        val id: Long
        synchronized(ioLock) {
            if (!subscribedChannels.add(channel)) return
            id = nextId.getAndIncrement()
            pending[id] = Pending.Subscribe
        }
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
        const val TAG = "AhpConn"
    }
}
