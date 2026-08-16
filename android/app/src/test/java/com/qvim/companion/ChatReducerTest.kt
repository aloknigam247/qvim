package com.qvim.companion

import com.qvim.companion.model.ServerFrame
import org.junit.Assert.assertEquals
import org.junit.Test

/**
 * Pins the actual echo-assembly behaviour the ViewModel delegates to. Mirrors qvim's
 * ChatModel: the user block is atomic, the assistant reply streams in via begin/delta/end.
 */
class ChatReducerTest {

    private fun assistantStream(reducer: ChatReducer) {
        reducer.apply(ServerFrame.MessageBegin(seq = 3, id = "a1", role = "assistant"))
        reducer.apply(ServerFrame.MessageDelta(seq = 4, id = "a1", text = "Echo: "))
        reducer.apply(ServerFrame.MessageDelta(seq = 5, id = "a1", text = "hi"))
    }

    @Test
    fun assemblesUserThenStreamedAssistantEcho() {
        val r = ChatReducer()
        r.apply(ServerFrame.Hello(protocol = 1, sessionId = "s1"))

        var msgs = r.apply(ServerFrame.Message(seq = 2, id = "u1", role = "user", text = "hi"))
        assertEquals(1, msgs.size)
        assertEquals("user", msgs[0].role)
        assertEquals("hi", msgs[0].text)

        r.apply(ServerFrame.MessageBegin(seq = 3, id = "a1", role = "assistant"))
        // Intermediate assembly after each delta (progressive streaming).
        msgs = r.apply(ServerFrame.MessageDelta(seq = 4, id = "a1", text = "Echo: "))
        assertEquals("Echo: ", msgs[1].text)
        assertEquals(true, msgs[1].streaming)
        msgs = r.apply(ServerFrame.MessageDelta(seq = 5, id = "a1", text = "hi"))
        assertEquals("Echo: hi", msgs[1].text)

        msgs = r.apply(ServerFrame.MessageEnd(seq = 6, id = "a1"))
        assertEquals(2, msgs.size)
        assertEquals("assistant", msgs[1].role)
        assertEquals("Echo: hi", msgs[1].text)
        assertEquals(false, msgs[1].streaming)
    }

    @Test
    fun duplicateSeqDoesNotDoubleApply() {
        val r = ChatReducer()
        r.apply(ServerFrame.Hello(protocol = 1, sessionId = "s1"))
        r.apply(ServerFrame.MessageBegin(seq = 3, id = "a1", role = "assistant"))
        r.apply(ServerFrame.MessageDelta(seq = 4, id = "a1", text = "Echo: "))
        // Replay of seq 4 must be dropped.
        val msgs = r.apply(ServerFrame.MessageDelta(seq = 4, id = "a1", text = "Echo: "))
        assertEquals("Echo: ", msgs[0].text)
    }

    @Test
    fun newSessionIdResetsTranscript() {
        val r = ChatReducer()
        r.apply(ServerFrame.Hello(protocol = 1, sessionId = "s1"))
        r.apply(ServerFrame.Message(seq = 2, id = "u1", role = "user", text = "old"))
        // qvim restarted: new sessionId, seq restarts at 1.
        var msgs = r.apply(ServerFrame.Hello(protocol = 1, sessionId = "s2"))
        assertEquals(0, msgs.size)
        msgs = r.apply(ServerFrame.Message(seq = 1, id = "u2", role = "user", text = "new"))
        assertEquals(1, msgs.size)
        assertEquals("new", msgs[0].text)
    }

    @Test
    fun unknownFrameIsIgnoredAndDoesNotAdvanceSeq() {
        val r = ChatReducer()
        r.apply(ServerFrame.Hello(protocol = 1, sessionId = "s1"))
        r.apply(ServerFrame.Unknown("tool.approval"))
        // A real frame at seq 1 must still apply (unknown did not consume the seq space).
        val msgs = r.apply(ServerFrame.Message(seq = 1, id = "u1", role = "user", text = "hi"))
        assertEquals(1, msgs.size)
        assertEquals("hi", msgs[0].text)
    }
}
