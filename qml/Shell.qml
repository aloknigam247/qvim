import QtQuick
import Qvim 1.0

Item {
    id: root
    focus: true

    property alias cellWidth:  baseGrid.cellWidth
    property alias cellHeight: baseGrid.cellHeight

    // Global grid (id=1) backs the whole shell. With ext_multigrid this still
    // exists; per-window grids are overlaid on top via the Repeater below,
    // positioned by the per-grid GridSurfaceProxy that GridModel exposes.
    GridItem {
        id: baseGrid
        anchors.fill: parent
        connector: $connector
        gridId: 1
        focus: true
    }

    // Per-window sub-grids. Each delegate binds directly to a GridSurfaceProxy
    // whose x/y/cols/rows/visible/isFloat/zindex are real Q_PROPERTYs with
    // change notifications. A win_pos / grid_resize on an existing grid
    // re-evaluates these bindings in-place — the delegate is never destroyed,
    // so GridItem's focus, cursor-blink phase, glyph cache (task #12), and
    // per-hl_id font cache (task #8) all survive the geometry edit.
    Repeater {
        id: subGrids
        model: $connector.grid.gridIds

        delegate: GridItem {
            required property var modelData
            readonly property int  gid:     modelData
            readonly property var  surface: $connector.grid.surfaceFor(gid)

            // The id=1 base grid is rendered by `baseGrid` above; the Repeater
            // delegate for id=1 stays invisible. Guard `surface` because the
            // proxy may not exist for a microsecond after gridsChanged fires.
            visible: gid !== 1 && surface !== null && surface.visible
            connector: $connector
            gridId: gid
            x:      surface ? surface.x    * baseGrid.cellWidth  : 0
            y:      surface ? surface.y    * baseGrid.cellHeight : 0
            width:  surface ? surface.cols * baseGrid.cellWidth  : 0
            height: surface ? surface.rows * baseGrid.cellHeight : 0
            z:      surface && surface.isFloat ? 50 + surface.zindex : 1
            // Unfocusable floats (e.g. hover doc popups) must let clicks pass
            // through to the grid underneath rather than swallowing them.
            enabled: !surface || surface.isFocusable
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton  // GridItem handles mouse
        onWheel: function(wheelEvent) { wheelEvent.accepted = false }
        propagateComposedEvents: true
        z: -1
    }

    Component.onCompleted: baseGrid.forceActiveFocus()
}
