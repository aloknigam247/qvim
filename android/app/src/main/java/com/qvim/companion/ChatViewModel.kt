package com.qvim.companion

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.qvim.companion.model.UiMessage
import com.qvim.companion.net.ConnectionState
import com.qvim.companion.net.MirrorDiscovery
import com.qvim.companion.net.SessionClient
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.emptyFlow
import kotlinx.coroutines.launch

/**
 * Owns the connection and the transcript. A single [viewModelScope] collector feeds
 * every incoming frame through one [ChatReducer], so transcript mutation is serialized
 * and the exposed [messages] StateFlow is always a consistent immutable snapshot.
 *
 * The endpoint is discovered on the LAN via [discovery] (mDNS): the first discovered
 * mirror auto-fills and auto-connects, but a manual edit always wins so off-LAN users
 * (e.g. over Tailscale) can still type an endpoint by hand.
 */
class ChatViewModel(
    private val client: SessionClient = SessionClient(),
    private val discovery: MirrorDiscovery = MirrorDiscovery { emptyFlow() },
) : ViewModel() {

    private val reducer = ChatReducer()

    private val _messages = MutableStateFlow<List<UiMessage>>(emptyList())
    val messages: StateFlow<List<UiMessage>> = _messages.asStateFlow()

    val connectionState: StateFlow<ConnectionState> = client.state

    private val _endpoint = MutableStateFlow("")
    val endpoint: StateFlow<String> = _endpoint.asStateFlow()

    private val _discovered = MutableStateFlow<String?>(null)
    val discovered: StateFlow<String?> = _discovered.asStateFlow()

    private var collectJob: Job? = null
    private var discoveryJob: Job? = null
    private var userEdited = false

    /** A manual edit (or a restored saved endpoint) — takes precedence over discovery. */
    fun setEndpoint(value: String) {
        userEdited = true
        _endpoint.value = value.trim()
    }

    /**
     * Begin LAN discovery. Idempotent — a prior discovery collector is cancelled first.
     * The first discovered endpoint auto-connects only while the user has not overridden
     * the endpoint and the field is still blank.
     */
    fun startDiscovery() {
        discoveryJob?.cancel()
        discoveryJob = viewModelScope.launch {
            discovery.endpoints().collect { endpoint ->
                _discovered.value = endpoint
                if (!userEdited && _endpoint.value.isBlank()) {
                    _endpoint.value = endpoint
                    connect()
                }
            }
        }
    }

    fun stopDiscovery() {
        discoveryJob?.cancel()
        discoveryJob = null
    }

    /**
     * (Re)connect. Cancels any prior connection and clears transient transcript state
     * first, so an explicit reconnect can never duplicate history. A blank endpoint is a
     * no-op — nothing is connected until a mirror is discovered or an endpoint is typed.
     */
    fun connect() {
        val target = _endpoint.value.trim()
        if (target.isEmpty()) return
        collectJob?.cancel()
        reducer.reset()
        _messages.value = emptyList()
        collectJob = viewModelScope.launch {
            client.frames(target).collect { frame ->
                _messages.value = reducer.apply(frame)
            }
        }
    }

    fun send(text: String) {
        if (text.isBlank()) return
        client.send(text.trim())
    }

    override fun onCleared() {
        collectJob?.cancel()
        discoveryJob?.cancel()
    }
}
