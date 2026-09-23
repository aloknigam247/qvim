import QtQuick
import Qvim 1.0

Item {
    id: root
    focus: true

    property alias cellWidth:  baseGrid.cellWidth
    property alias cellHeight: baseGrid.cellHeight

    // Global grid (id=1) backs the whole shell.
    GridItem {
        id: baseGrid
        anchors.fill: parent
        connector: $connector
        gridId: 1
        focus: true
    }

    // Cursor overlay. Sibling of baseGrid so its texture composites on top of
    // the grid. Cursor
    // blink and cursor moves invalidate only the previous + current cell on
    // this item — the grid item never repaints for cursor activity. Bound to
    // baseGrid's metrics so font/linespace changes propagate through Shell
    // without a separate signal hop.
    CursorItem {
        anchors.fill: baseGrid
        z: 99
        connector:    $connector
        cellWidth:    baseGrid.cellWidth
        cellHeight:   baseGrid.cellHeight
        cellBaseline: baseGrid.cellBaseline
        fontName:     baseGrid.fontName
        fontSize:     baseGrid.fontSize
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton  // GridItem handles mouse
        onWheel: function(wheelEvent) { wheelEvent.accepted = false }
        propagateComposedEvents: true
        z: -1
    }

    // Hands focus back to the long-lived baseGrid (the item that owns focus).
    // Used by Main.qml when the chat panel closes.
    function focusGrid() { baseGrid.forceActiveFocus() }

    Component.onCompleted: baseGrid.forceActiveFocus()
}
