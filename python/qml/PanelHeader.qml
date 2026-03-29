import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: headerRoot
    width: parent.width
    height: 48
    color: "transparent"

    property string title: geminiBridge.conversationTitle || "VIORA AI"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 15
        spacing: 12

        // Title
        Text {
            Layout.fillWidth: true
            text: headerRoot.title
            color: "#e2e8f0"
            font.pixelSize: 14
            font.bold: true
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }

        // Action Icons
        Row {
            spacing: 4
            Layout.alignment: Qt.AlignVCenter

            // New Chat (+)
            HeaderButton {
                text: "+"
                toolTip: "New Conversation"
                onClicked: geminiBridge.clearHistory()
            }

            // History (Clock-ish)
            HeaderButton {
                text: "🕒"
                toolTip: "History"
                // Placeholder action
            }

            // More (...)
            HeaderButton {
                text: "•••"
                toolTip: "More"
                // Placeholder action
            }

            // Close (X)
            HeaderButton {
                text: "✕"
                toolTip: "Close Panel"
                color: hovered ? "#ef4444" : "#94a3b8"
                onClicked: geminiBridge.closePanel()
            }
        }
    }

    // Bottom border/separator
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: "#ffffff"
        opacity: 0.05
    }

    // Internal helper component for header buttons
    component HeaderButton : Button {
        property string toolTip: ""
        property color fontColor: "#94a3b8"
        
        id: btn
        font.pixelSize: 16
        flat: true
        
        background: Rectangle {
            implicitWidth: 32
            implicitHeight: 32
            color: btn.hovered ? "rgba(255, 255, 255, 0.08)" : "transparent"
            radius: 6
        }

        contentItem: Text {
            text: btn.text
            color: btn.hovered ? "white" : btn.fontColor
            font: btn.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        ToolTip.visible: hovered && toolTip !== ""
        ToolTip.text: toolTip
        ToolTip.delay: 500
    }
}
