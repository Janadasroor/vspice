import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    width: parent ? parent.width : 300
    height: cardContainer.height + 20

    property var modelData: ({})
    property string role: modelData.role || "model"
    property string content: modelData.content || ""
    property string timestamp: modelData.timestamp || ""
    property bool isUser: role === "user"
    property bool isAction: role === "action"

    Rectangle {
        id: cardContainer
        width: Math.min(parent.width * 0.9, 600)
        height: contentColumn.height + 24
        anchors.horizontalCenter: isUser ? undefined : parent.horizontalCenter
        anchors.right: isUser ? parent.right : undefined
        anchors.rightMargin: isUser ? 10 : 0
        anchors.left: isUser ? undefined : parent.left
        anchors.leftMargin: isUser ? 0 : 10
        
        radius: 12
        clip: true

        // Premium Background styling
        color: isUser ? "#2563eb" : (isAction ? "#1e293b" : "#111a2e")
        border.color: isUser ? "transparent" : "#334155"
        border.width: 1

        // Subtle shadow using a nested rectangle instead of DropShadow to avoid dependencies
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.color: "#1a000000" // 10% alpha black
            border.width: 1
            z: -1
        }

        ColumnLayout {
            id: contentColumn
            width: parent.width - 24
            anchors.centerIn: parent
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                visible: !isUser

                Rectangle {
                    width: 16
                    height: 16
                    radius: 4
                    color: isAction ? "#f59e0b" : "#3b82f6"
                    Text {
                        anchors.centerIn: parent
                        text: isAction ? "⚡" : "V"
                        color: "white"
                        font.pixelSize: 10
                        font.bold: true
                    }
                }

                Text {
                    text: isAction ? "SYSTEM ACTION" : "VIORA AI"
                    color: "#94a3b8"
                    font.pixelSize: 10
                    font.bold: true
                    font.letterSpacing: 1
                }
                
                Item { Layout.fillWidth: true }
                
                Text {
                    text: timestamp
                    color: "#475569"
                    font.pixelSize: 9
                }
            }

            Text {
                Layout.fillWidth: true
                text: content
                color: isUser ? "white" : "#e2e8f0"
                font.family: "Inter, Segoe UI, sans-serif"
                font.pixelSize: 14
                wrapMode: Text.Wrap
                textFormat: Text.RichText
            }
        }
    }
}
