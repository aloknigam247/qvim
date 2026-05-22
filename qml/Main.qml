import QtQuick
import QtQuick.Window
import Qvim 1.0

Window {
    id: window
    width: 1200
    height: 800
    visible: true
    title: $connector.title || "qvim"
    color: "#1e1e1e"

    Component.onCompleted: {
        const cols = Math.max(80, Math.floor(width  / Math.max(1, shell.cellWidth)))
        const rows = Math.max(24, Math.floor(height / Math.max(1, shell.cellHeight)))
        $connector.attachUi(cols, rows)
        _syncTitleBar()
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
