package com.qvim.companion.model

/** A single rendered transcript entry. [streaming] is true between begin and end. */
data class UiMessage(
    val id: String,
    val role: String,
    val text: String,
    val streaming: Boolean,
)
