import QtQuick
import QtTest
import Qvim 1.0

TestCase {
    name: "Cmdline"
    width: 600
    height: 30
    visible: false
    when: windowShown

    Cmdline {
        id: cmdline
        anchors.fill: parent
    }

    function test_initial_state() {
        verify(!$connector.cmdline.visible)
        verify($connector.cmdline.content === "")
    }
}
