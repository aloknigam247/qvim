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
        const cols = Math.max(80, Math.floor(width  / Math.max(1, shell.cellWidth)))
        const rows = Math.max(24, Math.floor(height / Math.max(1, shell.cellHeight)))
        _requestedCols = cols
        _requestedRows = rows
        _maybeResize()
        _syncTitleBar()
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
        } else if (!_attachIssued) {
            _attachIssued = true
            $connector.attachUi(_requestedCols, _requestedRows)
        }
        // (Re)start the safety timer from the moment we issued the resize/
        // attach. If nvim never gets to the requested geometry within this
        // window, force-show so the editor isn't stuck invisible. Starting
        // it here (not at Component.onCompleted) means a slow attach round
        // trip doesn't burn the safety budget before tryResize is even sent.
        _showSafetyTimer.restart()
    }

    function _showIfReady() {
        if (_shown) return
        if (_requestedCols === 0 || _requestedRows === 0) return
        const g = $connector.grid
        if (g.gridCols(1) === _requestedCols && g.gridRows(1) === _requestedRows) {
            _shown = true
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

    Timer {
        id: _showSafetyTimer
        interval: 1000
        repeat: false
        onTriggered: {
            if (!_shown) {
                _shown = true
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
}
