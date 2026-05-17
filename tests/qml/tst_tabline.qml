import QtQuick
import QtTest
import Qvim 1.0

TestCase {
    name: "Tabline"
    width: 400
    height: 30
    visible: false
    when: windowShown

    Tabline {
        id: tabline
        anchors.fill: parent
    }

    function test_instantiates() {
        verify(tabline.height > 0)
        verify(tabline.width > 0)
    }

    function test_bound_to_connector_tabline() {
        // The Tabline's ListView binds to $connector.tabline; with no tabs
        // open the rowCount should be 0 and the list should be empty.
        verify($connector.tabline.rowCount() === 0)
    }
}
