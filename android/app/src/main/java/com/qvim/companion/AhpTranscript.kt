package com.qvim.companion

import com.microsoft.agenthostprotocol.generated.ChatState
import com.microsoft.agenthostprotocol.generated.Message
import com.microsoft.agenthostprotocol.generated.MessageKind
import com.microsoft.agenthostprotocol.generated.ResponsePart
import com.microsoft.agenthostprotocol.generated.ResponsePartMarkdown
import com.qvim.companion.model.UiMessage

/**
 * Pure derivation of a flat transcript from AHP [ChatState]s. Kept free of any
 * network/coroutine dependency so it is unit-testable on the plain JVM and so the
 * transcript is a pure function of the folded chat state (the connection folds the
 * `chat/` action stream through the canonical AHP reducers, this turns the result
 * into rows for the UI).
 *
 * For each chat, every completed turn and the in-progress `activeTurn` (rendered
 * last, flagged [UiMessage.streaming]) contribute the initiating message bubble
 * followed by the assistant bubble — the concatenation of the turn's markdown
 * response parts, per the protocol's "derive display text by concatenating markdown
 * parts" contract. Non-markdown parts (tool calls, reasoning, errors) are omitted.
 */
object AhpTranscript {
    fun fromChats(chats: Collection<ChatState>): List<UiMessage> {
        val out = ArrayList<UiMessage>()
        for (chat in chats) {
            for (turn in chat.turns) {
                appendTurn(out, chat.resource, turn.id, turn.message, turn.responseParts, streaming = false)
            }
            chat.activeTurn?.let { active ->
                appendTurn(out, chat.resource, active.id, active.message, active.responseParts, streaming = true)
            }
        }
        return out
    }

    private fun appendTurn(
        out: MutableList<UiMessage>,
        chatUri: String,
        turnId: String,
        message: Message,
        parts: List<ResponsePart>,
        streaming: Boolean,
    ) {
        if (message.text.isNotEmpty()) {
            val role = if (message.origin.kind == MessageKind.USER) "user" else "assistant"
            out.add(UiMessage(id = "$chatUri#$turnId:msg", role = role, text = message.text, streaming = false))
        }
        val assistant = assistantText(parts)
        if (assistant.isNotEmpty() || streaming) {
            out.add(UiMessage(id = "$chatUri#$turnId:reply", role = "assistant", text = assistant, streaming = streaming))
        }
    }

    private fun assistantText(parts: List<ResponsePart>): String =
        buildString {
            for (part in parts) {
                if (part is ResponsePartMarkdown) append(part.value.content)
            }
        }
}
