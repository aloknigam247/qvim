package com.qvim.companion

import com.qvim.companion.model.Protocol
import com.qvim.companion.model.ServerFrame
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class ProtocolTest {

    @Test
    fun decodesHello() {
        val f = Protocol.decode("""{"type":"hello","protocol":1,"sessionId":"s-1"}""")
        assertTrue(f is ServerFrame.Hello)
        f as ServerFrame.Hello
        assertEquals(1, f.protocol)
        assertEquals("s-1", f.sessionId)
    }

    @Test
    fun decodesAtomicMessage() {
        val f = Protocol.decode("""{"seq":3,"type":"message","id":"m1","role":"user","text":"hi"}""")
        assertTrue(f is ServerFrame.Message)
        f as ServerFrame.Message
        assertEquals(3L, f.seq)
        assertEquals("m1", f.id)
        assertEquals("user", f.role)
        assertEquals("hi", f.text)
    }

    @Test
    fun decodesStreamedTriple() {
        val begin = Protocol.decode("""{"seq":4,"type":"message.begin","id":"m2","role":"assistant"}""")
        val delta = Protocol.decode("""{"seq":5,"type":"message.delta","id":"m2","text":"Echo: "}""")
        val end = Protocol.decode("""{"seq":6,"type":"message.end","id":"m2"}""")
        assertTrue(begin is ServerFrame.MessageBegin)
        assertTrue(delta is ServerFrame.MessageDelta)
        assertTrue(end is ServerFrame.MessageEnd)
        assertEquals("assistant", (begin as ServerFrame.MessageBegin).role)
        assertEquals("Echo: ", (delta as ServerFrame.MessageDelta).text)
        assertEquals(6L, (end as ServerFrame.MessageEnd).seq)
    }

    @Test
    fun unknownTypeDegradesToUnknownWithoutThrowing() {
        val f = Protocol.decode("""{"seq":9,"type":"tool.approval","id":"x"}""")
        assertTrue(f is ServerFrame.Unknown)
        assertEquals("tool.approval", (f as ServerFrame.Unknown).type)
    }

    @Test
    fun encodesInputExactly() {
        assertEquals("""{"type":"input","text":"hi"}""", Protocol.encodeInput("hi"))
    }

    @Test
    fun encodesResumeExactly() {
        assertEquals(
            """{"type":"resume","lastSeq":5,"sessionId":"s-1"}""",
            Protocol.encodeResume(lastSeq = 5, sessionId = "s-1"),
        )
    }
}
