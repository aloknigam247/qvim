package com.qvim.companion.model

import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.longOrNull

/**
 * Wire protocol for the qvim session mirror. The canonical contract lives in
 * docs/protocol/session-protocol.md; this file is the Kotlin client half.
 *
 * Frames are decoded by hand off the "type" discriminator rather than via
 * polymorphic serialization so an unknown or newer "type" degrades to
 * [ServerFrame.Unknown] instead of throwing, keeping the client forward-compatible.
 */

/** Client -> server: the user typed a message into the session. */
@Serializable
data class Input(val type: String = "input", val text: String)

/**
 * Client -> server: resume handshake, sent after the server's hello. lastSeq is
 * always 0 here and the server does not replay; the field exists so server-side
 * replay can be added later without a wire change.
 */
@Serializable
data class Resume(val type: String = "resume", val lastSeq: Long, val sessionId: String? = null)

/** Server -> client frames. */
sealed interface ServerFrame {
    /** Sent first on connect. sessionId scopes seq; a new sessionId means "reset". */
    data class Hello(val protocol: Int, val sessionId: String) : ServerFrame

    /** An atomic message block (e.g. the user echo), applied in one shot. */
    data class Message(val seq: Long, val id: String, val role: String, val text: String) : ServerFrame

    /** Start of a streamed message; text arrives via [MessageDelta]. */
    data class MessageBegin(val seq: Long, val id: String, val role: String) : ServerFrame

    /** A chunk appended to the streamed message identified by [id]. */
    data class MessageDelta(val seq: Long, val id: String, val text: String) : ServerFrame

    /** End of a streamed message. */
    data class MessageEnd(val seq: Long, val id: String) : ServerFrame

    /** An unrecognised frame type — ignored by the reducer, never advances state. */
    data class Unknown(val type: String) : ServerFrame
}

object Protocol {
    val json = Json {
        ignoreUnknownKeys = true
        encodeDefaults = true
    }

    fun encodeInput(text: String): String = json.encodeToString(Input(text = text))

    fun encodeResume(lastSeq: Long, sessionId: String?): String =
        json.encodeToString(Resume(lastSeq = lastSeq, sessionId = sessionId))

    fun decode(raw: String): ServerFrame {
        val obj = json.parseToJsonElement(raw).jsonObject
        val type = obj["type"]?.jsonPrimitive?.contentOrNull ?: return ServerFrame.Unknown("")

        fun str(key: String): String = obj[key]?.jsonPrimitive?.contentOrNull ?: ""
        fun lng(key: String): Long = obj[key]?.jsonPrimitive?.longOrNull ?: 0L

        return when (type) {
            "hello" -> ServerFrame.Hello(
                protocol = obj["protocol"]?.jsonPrimitive?.intOrNull ?: 1,
                sessionId = str("sessionId"),
            )
            "message" -> ServerFrame.Message(
                seq = lng("seq"), id = str("id"), role = str("role"), text = str("text"),
            )
            "message.begin" -> ServerFrame.MessageBegin(
                seq = lng("seq"), id = str("id"), role = str("role"),
            )
            "message.delta" -> ServerFrame.MessageDelta(
                seq = lng("seq"), id = str("id"), text = str("text"),
            )
            "message.end" -> ServerFrame.MessageEnd(seq = lng("seq"), id = str("id"))
            else -> ServerFrame.Unknown(type)
        }
    }
}
