import QtQuick
import Qvim 1.0

Rectangle {
    color: "#2d2d2d"
    height: 28

    ListView {
        anchors.fill: parent
        anchors.margins: 2
        orientation: ListView.Horizontal
        spacing: 1
        model: $connector.tabline
        clip: true

        delegate: Rectangle {
            width: nameLabel.width + 24
            height: ListView.view ? ListView.view.height : 24
            radius: 3
            color: model.isCurrent ? "#3c3c3c" : "transparent"
            border.color: model.isCurrent ? "#5a9fd4" : "transparent"
            border.width: 1

            Text {
                id: nameLabel
                anchors.centerIn: parent
                text: model.name
                color: model.isCurrent ? "#ffffff" : "#bbbbbb"
                font.family: "JetBrains Mono Nerd Font"
                font.pointSize: 14
            }

            MouseArea {
                anchors.fill: parent
                onClicked: $connector.command("tabnext " + (index + 1))
            }
        }
    }
}
