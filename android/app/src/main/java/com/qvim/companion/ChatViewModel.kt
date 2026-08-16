package com.qvim.companion

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.qvim.companion.model.UiMessage
import com.qvim.companion.net.ConnectionState
import com.qvim.companion.net.SessionClient
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Owns the connection and the transcript. A single [viewModelScope] collector feeds
 * every incoming frame through one [ChatReducer], so transcript mutation is serialized
 * and the exposed [messages] StateFlow is always a consistent immutable snapshot.
 */
class ChatViewModel(
    private val client: SessionClient = SessionClient(),
) : ViewModel() {

    private val reducer = ChatReducer()

    private val _messages = MutableStateFlow<List<UiMessage>>(emptyList())
    val messages: StateFlow<List<UiMessage>> = _messages.asStateFlow()

    val connectionState: StateFlow<ConnectionState> = client.state

    private val _endpoint = MutableStateFlow(DEFAULT_ENDPOINT)
    val endpoint: StateFlow<String> = _endpoint.asStateFlow()

    private var collectJob: Job? = null

    fun setEndpoint(value: String) {
        _endpoint.value = value.trim()
    }

    /**
     * (Re)connect. Cancels any prior connection and clears transient transcript state
     * first, so an explicit reconnect can never duplicate history (auto-reconnect with
     * replay-based dedup is #52).
     */
    fun connect() {
        collectJob?.cancel()
        reducer.reset()
        _messages.value = emptyList()
        collectJob = viewModelScope.launch {
            client.frames(_endpoint.value).collect { frame ->
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
    }

    companion object {
        // Placeholder LAN address; the user edits this in-app to their PC's IP.
        const val DEFAULT_ENDPOINT = "ws://192.168.1.100:8765"
    }
}
