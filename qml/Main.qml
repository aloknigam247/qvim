import QtQuick
import QtQuick.Window
import Qvim 1.0

Window {
    id: window
    width: 1200
    height: 780
    // Start hidden. main.cpp fires nvim_ui_attach(80, 24) before this engine
    // is constructed, so the very first redraw flush paints at the 80x24
    // placeholder size. We flip visible=true only after the post-attach
    // tryResize has reached the real geometry — otherwise the user sees the
    // window pop up at 80x24 and visibly jump to 1200x780 a moment later.
    visible: false
    title: $connector.title || "qvim"
    color: "#1e1e1e"

    // Target grid size requested by Component.onCompleted. Set once; the
    // flush-listener below compares against this to decide when to show.
    property int _requestedCols: 0
    property int _requestedRows: 0
    property bool _shown: false

    Component.onCompleted: {
        _recomputeTarget()
        _maybeResize()
        _syncTitleBar()
    }

    // Recompute target (cols, rows) from the current window and cell dimensions.
    // Called at startup and again if guifont changes before the window is shown.
    function _recomputeTarget() {
        const cw = Math.max(1, shell.cellWidth)
        const ch = Math.max(1, shell.cellHeight)
        const chromeH = tabline.height + (cmdline.visible ? cmdline.height : 0)
        const availH = height - chromeH
        const cols = Math.max(80, Math.floor(width  / cw))
        const rows = Math.max(24, Math.floor(availH / ch))
        _requestedCols = cols
        _requestedRows = rows
    }

    // main.cpp fires nvim_ui_attach(80, 24) early (before this engine is
    // constructed) so the handshake overlaps with QML cold load. By the time
    // this runs `$connector.attached` is usually true; if it isn't yet (slow
    // RPC, or a test that didn't pre-attach), fall back to attachUi here.
    // The onAttachedChanged Connections below re-runs this when attach lands
    // late, which fires tryResize against an already-attached UI.
    property bool _attachIssued: false
    function _maybeResize() {
        if (_requestedCols === 0 || _requestedRows === 0) return
        if ($connector.attached) {
            $connector.tryResize(_requestedCols, _requestedRows)
            _showSafetyTimer.restart()
        } else if (!_attachIssued) {
            _attachIssued = true
            $connector.attachUi(_requestedCols, _requestedRows)
        }
    }

    function _showIfReady() {
        if (_shown) return
        if (_requestedCols === 0 || _requestedRows === 0) return
        const g = $connector.grid
        const gc = g.gridCols(1)
        const gr = g.gridRows(1)
        if (gc === _requestedCols && gr === _requestedRows) {
            _shown = true
            const cw = shell.cellWidth
            const ch = shell.cellHeight
            if (cw > 0 && ch > 0) {
                const chromeH = tabline.height + (cmdline.visible ? cmdline.height : 0)
                window.width  = _requestedCols * cw
                window.height = _requestedRows * ch + chromeH
            }
            window.visible = true
        }
    }

    Connections {
        target: $connector
        // When attach lands after Component.onCompleted (engine cold-loaded
        // faster than nvim handshake), fire the deferred tryResize.
        function onAttachedChanged() { _maybeResize() }
        // The first flush whose grid 1 matches the requested geometry is
        // the cue: at this point nvim has resized AND repainted at the real
        // size, so the very first frame Qt swaps will be correct.
        function onFlush() { _showIfReady() }
    }

    // React to cellWidthChanged (not guifontChanged) because GridItem
    // re-measures AFTER the guifontChanged signal — at that moment
    // shell.cellWidth is still the old value.
    Connections {
        target: shell
        function onCellWidthChanged() {
            if (_shown) return
            _recomputeTarget()
            _maybeResize()
        }
    }

    Timer {
        id: _showSafetyTimer
        interval: 1000
        repeat: false
        onTriggered: {
            if (!_shown) {
                _shown = true
                const g = $connector.grid
                const gc = g.gridCols(1)
                const gr = g.gridRows(1)
                const cw = shell.cellWidth
                const ch = shell.cellHeight
                if (gc > 0 && gr > 0 && cw > 0 && ch > 0) {
                    const chromeH = tabline.height + (cmdline.visible ? cmdline.height : 0)
                    window.width  = gc * cw
                    window.height = gr * ch + chromeH
                }
                window.visible = true
            }
        }
    }

    function _syncTitleBar() {
        if (typeof $windowChrome === "undefined") return
        const bg = $connector.defaultBackground
        if (!bg || !bg.valid) return
        $windowChrome.applyToWindow(window, bg)
    }

    Connections {
        target: $connector
        function onDefaultBackgroundChanged() { _syncTitleBar() }
    }

    Tabline {
        id: tabline
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        visible: $connector.tabline.rowCount() > 0
        height: visible ? 28 : 0
    }

    Shell {
        id: shell
        objectName: "shell"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabline.bottom
        anchors.bottom: cmdline.visible ? cmdline.top : parent.bottom
    }

    Cmdline {
        id: cmdline
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: $connector.cmdline.visible
        height: visible ? 28 : 0
    }

    PopupMenu {
        anchorRow: $connector.popupmenu.anchorRow
        anchorCol: $connector.popupmenu.anchorCol
        visible: $connector.popupmenu.visible
        cellWidth: shell.cellWidth
        cellHeight: shell.cellHeight
        z: 100
    }

    Messages {
        anchors.fill: parent
        z: 200
    }

    Shortcut {
        sequence: "F11"
        context: Qt.WindowShortcut
        onActivated: {
            const willBeFullscreen = window.visibility !== Window.FullScreen
            if (willBeFullscreen) {
                // Pre-issue tryResize so nvim resizes the grid IN PARALLEL with
                // the OS fullscreen animation. Otherwise the user sees the
                // window expand instantly but the grid lag by the RPC RTT.
                const scr = window.screen
                if (scr) {
                    const cw = Math.max(1, shell.cellWidth)
                    const ch = Math.max(1, shell.cellHeight)
                    const chromeH = tabline.height + (cmdline.visible ? cmdline.height : 0)
                    const cols = Math.max(1, Math.floor(scr.width / cw))
                    const rows = Math.max(1, Math.floor((scr.height - chromeH) / ch))
                    $connector.tryResize(cols, rows)
                }
                window.visibility = Window.FullScreen
            } else {
                window.visibility = Window.Windowed
            }
        }
    }
}
