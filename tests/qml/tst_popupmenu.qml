import QtQuick
import QtTest
import Qvim 1.0

TestCase {
    name: "PopupMenu"
    width: 400
    height: 240
    visible: false
    when: windowShown

    PopupMenu {
        id: pmenu
        anchorRow: 0
        anchorCol: 0
        cellWidth: 8
        cellHeight: 16
    }

    function test_hidden_by_default() {
        verify(!$connector.popupmenu.visible)
        verify(!pmenu.visible)
    }
}
