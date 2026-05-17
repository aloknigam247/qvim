import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import Qvim 1.0

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    visible: true
    title: $connector.title.length ? ($connector.title + " — qvim") : "qvim"
    color: "#1e1e1e"

    Component.onCompleted: {
        const cols = Math.max(80, Math.floor(width  / Math.max(1, shell.cellWidth)))
        const rows = Math.max(24, Math.floor(height / Math.max(1, shell.cellHeight)))
        $connector.attachUi(cols, rows)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Tabline {
            Layout.fillWidth: true
            visible: $connector.tabline.rowCount() > 0
        }

        Shell {
            id: shell
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Cmdline {
            Layout.fillWidth: true
            visible: $connector.cmdline.visible
        }
    }

    PopupMenu {
        anchorRow: $connector.popupmenu.anchorRow
        anchorCol: $connector.popupmenu.anchorCol
        visible: $connector.popupmenu.visible
        cellWidth: shell.cellWidth
        cellHeight: shell.cellHeight
        z: 100
    }
}
