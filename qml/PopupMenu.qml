import QtQuick
import Qvim 1.0

Rectangle {
    id: root
    property int   anchorRow:  0
    property int   anchorCol:  0
    property real  cellWidth:  8
    property real  cellHeight: 16

    x: anchorCol * cellWidth
    y: (anchorRow + 1) * cellHeight
    width: 300
    height: Math.min(240, list.contentHeight + 4)
    color: "#252526"
    border.color: "#3c3c3c"
    border.width: 1
    radius: 3

    ListView {
        id: list
        anchors.fill: parent
        anchors.margins: 2
        clip: true
        model: $connector.popupmenu
        currentIndex: $connector.popupmenu.selectedIndex

        delegate: Rectangle {
            width: list.width
            height: 22
            color: index === list.currentIndex ? "#094771" : "transparent"
            Row {
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                spacing: 8
                Text {
                    text: model.word
                    color: "#d4d4d4"
                    font.family: $connector.guifontFamily
                    font.pointSize: $connector.guifontSize
                    width: 150
                    elide: Text.ElideRight
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: model.kind
                    color: "#9cdcfe"
                    font.family: $connector.guifontFamily
                    font.pointSize: $connector.guifontSize
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: model.menu
                    color: "#808080"
                    font.family: $connector.guifontFamily
                    font.pointSize: $connector.guifontSize
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
