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

    function test_append_block_adds_external_messages() {
        var p = make()
        p.model.appendBlock("user", "hello")
        p.model.appendBlock("assistant", "reply")

        compare(p.model.count, 2)
        compare(p.model.authorAt(0), "user")
        compare(p.model.textAt(0), "hello")
        compare(p.model.authorAt(1), "assistant")
        compare(p.model.textAt(1), "reply")
    }

    function test_empty_append_block_is_ignored() {
        var p = make()
        p.model.appendBlock("assistant", "")
        compare(p.model.count, 0)
    }
}
