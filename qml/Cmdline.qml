import QtQuick
import Qvim 1.0

Rectangle {
    color: "#1e1e1e"
    border.color: "#3c3c3c"
    border.width: 1
    height: 28

    Row {
        anchors.fill: parent
        anchors.leftMargin: 6
        spacing: 2

        Text {
            text: $connector.cmdline.firstChar.length > 0 ? $connector.cmdline.firstChar : ""
            color: "#9cdcfe"
            font.family: "JetBrains Mono Nerd Font"
            font.pointSize: 14
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: $connector.cmdline.prompt
            color: "#d4d4d4"
            font.family: "JetBrains Mono Nerd Font"
            font.pointSize: 14
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: contentText
            text: $connector.cmdline.content
            color: "#d4d4d4"
            font.family: "JetBrains Mono Nerd Font"
            font.pointSize: 14
            anchors.verticalCenter: parent.verticalCenter
        }

        Rectangle {
            width: 2
            height: contentText.height
            color: "#d4d4d4"
            anchors.verticalCenter: parent.verticalCenter
            x: contentText.x + contentText.font.pixelSize * 0.55 * $connector.cmdline.cursorPos
        }
    }
}
