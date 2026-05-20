import QtQuick
import Qvim 1.0

Item {
    id: root
    // Overlay is non-interactive; anchored by parent layout.

    Column {
        id: stack
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: 8
        anchors.rightMargin: 8
        spacing: 4
        width: Math.min(parent.width - 16, 600)

        Repeater {
            model: $connector.messages

            delegate: Rectangle {
                id: bubble
                required property string kind
                required property string text
                required property int    attrId
                required property int    index

                width: stack.width
                height: label.implicitHeight + 12
                radius: 4
                color: kind === "echoerr" || kind === "emsg" ? "#5a1d1d"
                     : kind === "wmsg"                       ? "#5a4a1d"
                     :                                         "#1e1e1e"
                border.color: "#3c3c3c"
                border.width: 1
                opacity: 1.0

                Behavior on opacity { NumberAnimation { duration: 350 } }

                Text {
                    id: label
                    anchors.fill: parent
                    anchors.margins: 6
                    wrapMode: Text.Wrap
                    text: bubble.text
                    color: bubble.kind === "echoerr" || bubble.kind === "emsg" ? "#ff8080"
                         : bubble.kind === "wmsg"                              ? "#ffd080"
                         :                                                       "#d4d4d4"
                    font.family: "JetBrains Mono Nerd Font"
                    font.pointSize: 14
                }

                Timer {
                    interval: 4000
                    running: true
                    repeat: false
                    onTriggered: bubble.opacity = 0.0
                }
            }
        }
    }

    // mode/cmd/ruler strip — anchored bottom-right, mirrors classic statusline echo.
    Row {
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        anchors.bottomMargin: 4
        anchors.rightMargin: 8
        spacing: 12

        Text {
            visible: $connector.messages.mode.length > 0
            text: $connector.messages.mode
            color: "#9cdcfe"
            font.family: "JetBrains Mono Nerd Font"
            font.pointSize: 14
        }
        Text {
            visible: $connector.messages.cmd.length > 0
            text: $connector.messages.cmd
            color: "#d4d4d4"
            font.family: "JetBrains Mono Nerd Font"
            font.pointSize: 14
        }
        Text {
            visible: $connector.messages.ruler.length > 0
            text: $connector.messages.ruler
            color: "#808080"
            font.family: "JetBrains Mono Nerd Font"
            font.pointSize: 14
        }
    }
}
