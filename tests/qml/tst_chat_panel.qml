import QtQuick
import QtTest
import Qvim 1.0

// Tier 3 — ChatPanel / ChatModel in isolation. Each test gets a fresh panel via
// createTemporaryObject so model state never leaks between cases.
TestCase {
    name: "ChatPanel"
    width: 400
    height: 500
    visible: true
    when: windowShown

    Component {
        id: panelComp
        ChatPanel { anchors.fill: parent }
    }

    SignalSpy {
        id: closedSpy
        signalName: "closed"
    }

    function make() {
        var p = createTemporaryObject(panelComp, this)
        verify(p !== null)
        return p
    }

    function test_hidden_by_default() {
        var p = make()
        verify(!p.visible)
        compare(p.model.count, 0)
    }

    function test_open_focuses_input_close_hides() {
        var p = make()
        closedSpy.clear()
        closedSpy.target = p

        p.open()
        verify(p.visible)
        tryVerify(function() { return p.inputField.activeFocus })

        p.requestClose()
        verify(!p.visible)
        compare(closedSpy.count, 1)
    }

    function test_submit_appends_user_then_streamed_assistant() {
        var p = make()
        p.model.submit("hello")

        // user block is appended synchronously; assistant block is created
        // empty then streamed in chunks.
        tryCompare(p.model, "count", 2)
        compare(p.model.authorAt(0), "user")
        compare(p.model.textAt(0), "hello")
        compare(p.model.authorAt(1), "assistant")
        tryVerify(function() { return p.model.textAt(1) === "Echo: hello" })
    }

    function test_empty_submit_is_ignored() {
        var p = make()
        p.model.submit("   ")
        compare(p.model.count, 0)
    }

    // Per-row streaming ownership: a second submit mid-stream must not append
    // chunks to the first assistant block.
    function test_rapid_double_submit_keeps_blocks_separate() {
        var p = make()
        p.model.submit("a")
        p.model.submit("b")

        tryCompare(p.model, "count", 4)
        compare(p.model.authorAt(0), "user")
        compare(p.model.textAt(0), "a")
        compare(p.model.authorAt(2), "user")
        compare(p.model.textAt(2), "b")
        tryVerify(function() { return p.model.textAt(1) === "Echo: a" })
        tryVerify(function() { return p.model.textAt(3) === "Echo: b" })
    }
}
