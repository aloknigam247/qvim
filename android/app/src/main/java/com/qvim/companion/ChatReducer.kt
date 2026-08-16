package com.qvim.companion

import com.qvim.companion.model.ServerFrame
import com.qvim.companion.model.UiMessage

/**
 * Pure transcript reducer: folds [ServerFrame]s into an ordered message list. Kept
 * free of Android/coroutine dependencies so it is unit-testable on the plain JVM and
 * so all state mutation happens in one place (the ViewModel drives it from a single
 * collector, so no locking is needed).
 *
 * Ordering and dedup rely on the monotonic seq: a frame whose seq is not greater than
 * the highest already applied is a duplicate and is dropped. A [ServerFrame.Hello]
 * with a new sessionId resets the transcript (qvim restarted; seq numbering restarts).
 */
class ChatReducer {
    private val messages = mutableListOf<UiMessage>()
    private val indexById = mutableMapOf<String, Int>()
    private var maxSeq = 0L
    private var sessionId: String? = null

    fun snapshot(): List<UiMessage> = messages.toList()

    fun reset() {
        messages.clear()
        indexById.clear()
        maxSeq = 0L
    }

    fun apply(frame: ServerFrame): List<UiMessage> {
        when (frame) {
            is ServerFrame.Hello -> {
                if (sessionId != null && sessionId != frame.sessionId) reset()
                sessionId = frame.sessionId
            }
            is ServerFrame.Message ->
                if (advance(frame.seq)) upsertFull(frame.id, frame.role, frame.text)
            is ServerFrame.MessageBegin ->
                if (advance(frame.seq)) begin(frame.id, frame.role)
            is ServerFrame.MessageDelta ->
                if (advance(frame.seq)) delta(frame.id, frame.text)
            is ServerFrame.MessageEnd ->
                if (advance(frame.seq)) end(frame.id)
            is ServerFrame.Unknown -> Unit // ignore; do not advance seq
        }
        return snapshot()
    }

    private fun advance(seq: Long): Boolean {
        if (seq <= maxSeq) return false
        maxSeq = seq
        return true
    }

    private fun begin(id: String, role: String) {
        if (indexById.containsKey(id)) return
        indexById[id] = messages.size
        messages.add(UiMessage(id = id, role = role, text = "", streaming = true))
    }

    private fun delta(id: String, text: String) {
        val i = indexById[id] ?: return
        val m = messages[i]
        messages[i] = m.copy(text = m.text + text)
    }

    private fun end(id: String) {
        val i = indexById[id] ?: return
        messages[i] = messages[i].copy(streaming = false)
    }

    private fun upsertFull(id: String, role: String, text: String) {
        val i = indexById[id]
        if (i != null) {
            messages[i] = messages[i].copy(role = role, text = text, streaming = false)
        } else {
            indexById[id] = messages.size
            messages.add(UiMessage(id = id, role = role, text = text, streaming = false))
        }
    }
}
