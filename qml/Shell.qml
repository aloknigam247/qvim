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
    // so GridItem's focus, glyph cache (task #12), and per-hl_id font cache
    // (task #8) all survive the geometry edit. Cursor blink phase lives on
    // the sibling CursorItem below.
    Repeater {
        id: subGrids
        // Filter out grid 1 — baseGrid above already renders it. A delegate
        // for grid 1 would be invisible but its width/height bindings (set to
        // surface.cols * cellWidth, surface.rows * cellHeight) would override
        // the anchors.fill that baseGrid relies on; the resulting geometryChange
        // then fires maybeResizeUi with sub-grid dimensions instead of window
        // dimensions, telling nvim to grow past the window on guifont changes.
        model: $connector.grid.gridIds.filter(function(g) { return g !== 1 })

        delegate: GridItem {
            required property var modelData
            readonly property int  gid:     modelData
            readonly property var  surface: $connector.grid.surfaceFor(gid)

            // Guard `surface` because the proxy may not exist for a microsecond
            // after gridsChanged fires.
            visible: surface !== null && surface.visible
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

    // Cursor overlay. Sibling of baseGrid so its texture composites on top of
    // every grid (z above all sub-grids, below PopupMenu z=100 and Messages
    // z=200 in Main.qml). Cursor blink and cursor moves invalidate only the
    // previous + current cell on this item — the grid item never repaints for
    // cursor activity. Bound to baseGrid's metrics so font/linespace changes
    // propagate through Shell without a separate signal hop.
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
