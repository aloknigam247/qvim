import QtQuick
import Qvim 1.0

Item {
    id: root
    focus: true

    property alias cellWidth:  grid.cellWidth
    property alias cellHeight: grid.cellHeight

    GridItem {
        id: grid
        anchors.fill: parent
        connector: $connector
        focus: true
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton  // GridItem handles mouse
        onWheel: function(wheelEvent) { wheelEvent.accepted = false }
        propagateComposedEvents: true
        z: -1
    }

    Component.onCompleted: grid.forceActiveFocus()
}
