package com.qvim.companion.model

/**
 * A single rendered transcript entry. [streaming] is true between begin and end.
 * [kind] identifies the AHP response-part kind (`markdown`, `reasoning`, `toolCall`,
 * `error`, ...) or `message` for a turn's initiating prompt, so the UI can colour and
 * label each box by kind.
 */
data class UiMessage(
    val id: String,
    val role: String,
    val text: String,
    val streaming: Boolean,
    val kind: String = "message",
)
