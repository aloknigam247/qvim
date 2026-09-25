package com.qvim.companion

import com.microsoft.agenthostprotocol.chatReducer
import com.microsoft.agenthostprotocol.generated.ActionType
import com.microsoft.agenthostprotocol.generated.ChatDeltaAction
import com.microsoft.agenthostprotocol.generated.ChatResponsePartAction
import com.microsoft.agenthostprotocol.generated.ChatState
import com.microsoft.agenthostprotocol.generated.ChatTurnCompleteAction
import com.microsoft.agenthostprotocol.generated.ChatTurnStartedAction
import com.microsoft.agenthostprotocol.generated.MarkdownResponsePart
import com.microsoft.agenthostprotocol.generated.Message
import com.microsoft.agenthostprotocol.generated.MessageKind
import com.microsoft.agenthostprotocol.generated.MessageOrigin
import com.microsoft.agenthostprotocol.generated.ResponsePartKind
import com.microsoft.agenthostprotocol.generated.ResponsePartMarkdown
import com.microsoft.agenthostprotocol.generated.SessionStatus
import com.microsoft.agenthostprotocol.generated.StateActionChatDelta
import com.microsoft.agenthostprotocol.generated.StateActionChatResponsePart
import com.microsoft.agenthostprotocol.generated.StateActionChatTurnComplete
import com.microsoft.agenthostprotocol.generated.StateActionChatTurnStarted
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Exercises the real AHP [chatReducer] streaming fold followed by [AhpTranscript]
 * derivation — the client's hot-path correctness gate. Folds the same `chat`
 * action sequence a server streams (turnStarted, responsePart, delta, turnComplete)
 * and asserts the user prompt plus the assembled assistant reply appear in order.
 */
class AhpTranscriptTest {
    private val chatUri = "ahp-chat:/c1"

    private fun baseChat(): ChatState =
        ChatState(
            resource = chatUri,
            title = "Chat",
            status = SessionStatus.IDLE,
            modifiedAt = "2025-01-01T00:00:00.000Z",
            turns = emptyList(),
        )

    private fun turnStarted(): StateActionChatTurnStarted =
        StateActionChatTurnStarted(
            ChatTurnStartedAction(
                type = ActionType.CHAT_TURN_STARTED,
                turnId = "t1",
                startedAt = "2025-01-01T00:00:01.000Z",
                message = Message(text = "hello", origin = MessageOrigin(kind = MessageKind.USER)),
            ),
        )

    private fun markdownPart(): StateActionChatResponsePart =
        StateActionChatResponsePart(
            ChatResponsePartAction(
                type = ActionType.CHAT_RESPONSE_PART,
                turnId = "t1",
                part =
                    ResponsePartMarkdown(
                        MarkdownResponsePart(kind = ResponsePartKind.MARKDOWN, id = "p1", content = "Hi"),
                    ),
            ),
        )

    private fun delta(): StateActionChatDelta =
        StateActionChatDelta(
            ChatDeltaAction(type = ActionType.CHAT_DELTA, turnId = "t1", partId = "p1", content = " there"),
        )

    private fun turnComplete(): StateActionChatTurnComplete =
        StateActionChatTurnComplete(
            ChatTurnCompleteAction(type = ActionType.CHAT_TURN_COMPLETE, turnId = "t1", duration = 5L),
        )

    @Test
    fun streamsUserPromptAndAssembledAssistantReply() {
        var chat = baseChat()
        chat = chatReducer(chat, turnStarted())

        // While streaming: user bubble plus an in-progress assistant bubble.
        var rows = AhpTranscript.fromChats(listOf(chat))
        assertEquals(2, rows.size)
        assertEquals("user", rows[0].role)
        assertEquals("hello", rows[0].text)
        assertEquals("assistant", rows[1].role)
        assertTrue(rows[1].streaming)

        chat = chatReducer(chat, markdownPart())
        chat = chatReducer(chat, delta())
        rows = AhpTranscript.fromChats(listOf(chat))
        assertEquals("Hi there", rows[1].text)
        assertTrue(rows[1].streaming)

        chat = chatReducer(chat, turnComplete())
        rows = AhpTranscript.fromChats(listOf(chat))
        assertEquals(2, rows.size)
        assertEquals("hello", rows[0].text)
        assertEquals("Hi there", rows[1].text)
        assertFalse(rows[1].streaming)
    }
}
