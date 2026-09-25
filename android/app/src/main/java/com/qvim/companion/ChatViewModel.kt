package com.qvim.companion

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.qvim.companion.model.SessionInfo
import com.qvim.companion.model.UiMessage
import com.qvim.companion.net.AhpConnection
import com.qvim.companion.net.ConnectionState
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Owns the current [AhpConnection] and mirrors its transcript and connection state
 * into StateFlows for the UI. Each [connect] discards any prior connection (there is
 * no in-place reconnect) and re-collects the fresh connection's flows, so an explicit
 * reconnect can never duplicate history.
 */
class ChatViewModel(private val connectionFactory: (String) -> AhpConnection = { AhpConnection(it) }) : ViewModel() {
    private val _messages = MutableStateFlow<List<UiMessage>>(emptyList())
    val messages: StateFlow<List<UiMessage>> = _messages.asStateFlow()

    private val _connectionState = MutableStateFlow(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _endpoint = MutableStateFlow("")
    val endpoint: StateFlow<String> = _endpoint.asStateFlow()

    private val _sessions = MutableStateFlow<List<SessionInfo>>(emptyList())
    val sessions: StateFlow<List<SessionInfo>> = _sessions.asStateFlow()

    private val _selectedSession = MutableStateFlow<String?>(null)
    val selectedSession: StateFlow<String?> = _selectedSession.asStateFlow()

    private var connection: AhpConnection? = null
    private var transcriptJob: Job? = null
    private var stateJob: Job? = null
    private var sessionsJob: Job? = null
    private var selectedJob: Job? = null

    /** Sets the AHP server endpoint (`host:port`, or a full `ws://`/`wss://` URL). */
    fun setEndpoint(value: String) {
        _endpoint.value = value.trim()
    }

    /** (Re)connect to the endpoint. A blank endpoint is a no-op. */
    fun connect() {
        val target = _endpoint.value.trim()
        if (target.isEmpty()) return

        transcriptJob?.cancel()
        stateJob?.cancel()
        sessionsJob?.cancel()
        selectedJob?.cancel()
        connection?.close()

        val conn = connectionFactory(target)
        connection = conn
        _messages.value = emptyList()
        _sessions.value = emptyList()
        _selectedSession.value = null

        transcriptJob = viewModelScope.launch { conn.transcript.collect { _messages.value = it } }
        stateJob = viewModelScope.launch { conn.state.collect { _connectionState.value = it } }
        sessionsJob = viewModelScope.launch { conn.availableSessions.collect { _sessions.value = it } }
        selectedJob = viewModelScope.launch { conn.selectedSession.collect { _selectedSession.value = it } }
        conn.start()
    }

    /** Chooses which host session to observe; shown only when more than one exists. */
    fun selectSession(resource: String) {
        connection?.selectSession(resource)
    }

    /**
     * Returns to the session selector. Reconnects from scratch (the folded chat state
     * is owned by the reader thread, so re-scoping in place would race); the fresh
     * handshake re-lists sessions and the picker reappears when more than one exists.
     */
    fun switchSession() {
        connect()
    }

    fun send(text: String) {
        if (text.isBlank()) return
        connection?.send(text.trim())
    }

    override fun onCleared() {
        transcriptJob?.cancel()
        stateJob?.cancel()
        sessionsJob?.cancel()
        selectedJob?.cancel()
        connection?.close()
    }
}
