package com.qvim.companion

import com.microsoft.agenthostprotocol.Ahp
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
 * followed by one bubble per response part, each tagged with its AHP kind
 * ([UiMessage.kind]) so the UI can colour and label it. Markdown parts render as their
 * text; every other kind (reasoning, tool calls, input requests, errors,
 * notifications, resources) is dumped as its raw JSON pending dedicated UI.
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
            out.add(
                UiMessage(
                    id = "$chatUri#$turnId:msg",
                    role = role,
                    text = message.text,
                    streaming = false,
                    kind = "message",
                ),
            )
        }
        parts.forEachIndexed { index, part ->
            out.add(
                UiMessage(
                    id = "$chatUri#$turnId:part$index",
                    role = "assistant",
                    text = partText(part),
                    streaming = streaming,
                    kind = partKind(part),
                ),
            )
        }
        if (streaming && parts.isEmpty()) {
            out.add(
                UiMessage(
                    id = "$chatUri#$turnId:reply",
                    role = "assistant",
                    text = "",
                    streaming = true,
                    kind = "markdown",
                ),
            )
        }
    }

    private fun partKind(part: ResponsePart): String =
        (part::class.simpleName?.removePrefix("ResponsePart") ?: "unknown")
            .replaceFirstChar { it.lowercaseChar() }

    private fun partText(part: ResponsePart): String =
        if (part is ResponsePartMarkdown) {
            part.value.content
        } else {
            runCatching {
                Ahp.json.encodeToString(ResponsePart.serializer(), part)
            }.getOrElse { part.toString() }
        }
}
