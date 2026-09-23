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

    // Single entry point for outbound input regardless of source (local input
    // field or a LAN subscriber via the mirror). Injected into the Copilot
    // session via the bridge; the prompt returns through the mirror stream.
    function sendInput(text) {
        bridge.inject(text)
    }

    ChatModel { id: chatModel }

    // The chat backend: streams the copilot-bridge hub's mirror traffic into the
    // ChatModel. Only connects while the panel is open, so no outbound socket
    // exists otherwise.
    CopilotBridgeClient {
        id: bridge
        sink: chatModel
        showTools: true
        active: panel.visible
    }

    // Mirrors this panel's session to LAN subscribers over ws://, but only
    // while the panel is open — `active` binds the server's listen lifecycle to
    // visibility, so no port is held when chat isn't in use.
    SessionMirrorServer {
        id: mirror
        source: chatModel
        active: panel.visible
    }

    // Input from a LAN subscriber joins the active Copilot session through the
    // same dispatch as local input.
    Connections {
        target: mirror
        function onInputReceived(text) {
            panel.sendInput(text)
        }
    }

    // Advertises the mirror endpoint over mDNS/DNS-SD so the companion app finds
    // it without a hard-coded host:port. Bound to the actually-listening port and
    // gated on the same visibility, so the announcement tracks the listen socket.
    MdnsAdvertiser {
        port: mirror.boundPort
        active: panel.visible && mirror.boundPort !== 0
    }

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
                    color: block.author === "user" ? "#0e4429"
                         : block.author === "system" ? "#1f2d3d"
                         : "#333333"

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

                // Submit on Enter: the line is injected as a user prompt into
                // the Copilot session(s) via the bridge — it returns through the
                // mirror stream, so it is not echoed locally.
                onAccepted: {
                    panel.sendInput(text)
                    clear()
                }
                // Escape hands focus back to the grid via requestClose().
                Keys.onEscapePressed: panel.requestClose()
            }
        }
    }
}
