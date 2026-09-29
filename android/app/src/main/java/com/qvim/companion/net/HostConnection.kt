package com.qvim.companion.net

import com.qvim.companion.model.SessionInfo
import com.qvim.companion.model.UiMessage
import kotlinx.coroutines.flow.StateFlow

/**
 * The surface [com.qvim.companion.ChatViewModel] drives on a single host connection:
 * observable connection state, the folded transcript, and the discovered sessions,
 * plus the lifecycle and session-selection commands. [AhpConnection] is the live
 * implementation; the seam lets the view-model be unit-tested against a fake without
 * a real socket.
 */
interface HostConnection {
    val state: StateFlow<ConnectionState>
    val transcript: StateFlow<List<UiMessage>>
    val availableSessions: StateFlow<List<SessionInfo>>

    fun start()

    fun close()

    fun send(text: String): Boolean

    fun selectSession(resource: String)
}
