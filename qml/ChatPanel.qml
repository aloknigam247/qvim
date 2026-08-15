import QtQuick
import Qvim 1.0

// Dockable chat panel. Long-lived item (never a Repeater delegate) so its
// focus, the ChatModel instance, and per-delegate state survive nvim window
// churn. Owns one ChatModel (creatable QML_ELEMENT). Focus-handoff protocol:
// open() takes focus for the input; requestClose() emits closed() so the host
// (Main.qml) can hand focus back to the long-lived baseGrid — otherwise
// keystrokes would stop reaching nvim.
Rectangle {
    id: panel
    objectName: "chatPanel"
    color: "#252526"
    visible: false

    // Public surface for bindings and tests.
    property alias model: chatModel
    property alias inputField: input

    signal closed()

    function open() {
        // visible MUST be set before forceActiveFocus(): focusing a hidden
        // item is a no-op.
        visible = true
        input.forceActiveFocus()
    }

    function requestClose() {
        visible = false
        closed()
    }

    ChatModel { id: chatModel }

    // Left divider between the grid and the panel.
    Rectangle {
        id: divider
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 1
        color: "#3c3c3c"
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: divider.width

        ListView {
            id: list
            width: parent.width
            height: parent.height - inputRow.height
            model: chatModel
            clip: true
            spacing: 6
            topMargin: 6
            bottomMargin: 6
            // Keep the newest message in view as it streams in.
            onCountChanged: positionViewAtEnd()

            delegate: Item {
                id: block
                required property string author
                required property string text

                width: ListView.view ? ListView.view.width : 0
                height: bubble.height + 4

                Rectangle {
                    id: bubble
                    x: 8
                    y: 2
                    width: parent.width - 16
                    height: msg.implicitHeight + 12
                    radius: 6
                    color: block.author === "user" ? "#0e4429" : "#333333"

                    Text {
                        id: msg
                        x: 6
                        y: 6
                        width: parent.width - 12
                        text: block.text
                        // Plain text only — never interpret user input as rich
                        // text / HTML.
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: "#e6e6e6"
                        font.family: $connector.guifontFamily
                        font.pointSize: $connector.guifontSize
                    }
                }
            }
        }

        Rectangle {
            id: inputRow
            width: parent.width
            height: 32
            color: "#1e1e1e"
            border.color: "#3c3c3c"
            border.width: 1

            TextInput {
                id: input
                objectName: "chatInput"
                anchors.fill: parent
                anchors.margins: 6
                verticalAlignment: TextInput.AlignVCenter
                color: "#e6e6e6"
                clip: true
                selectByMouse: true
                font.family: $connector.guifontFamily
                font.pointSize: $connector.guifontSize

                // Submit on Enter.
                onAccepted: {
                    chatModel.submit(text)
                    clear()
                }
                // Escape hands focus back to the grid via requestClose().
                Keys.onEscapePressed: panel.requestClose()
            }
        }
    }
}
