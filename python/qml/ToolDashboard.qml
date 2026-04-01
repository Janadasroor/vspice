import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    color: "#0f172a" // Slate 900
    radius: 12
    border.color: "#334155"
    border.width: 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        // Header
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "TOOL ACTIVITY AUDIT"
                color: "#f8fafc"
                font.bold: true
                font.pixelSize: 14
                Layout.fillWidth: true
            }
            ToolButton {
                icon.name: "window-close"
                onClicked: root.visible = false
                padding: 4
                background: Rectangle { color: parent.hovered ? "#334155" : "transparent"; radius: 4 }
            }
        }

        // Table Header
        Rectangle {
            Layout.fillWidth: true
            height: 30
            color: "#1e293b"
            radius: 4
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                Text { text: "TIME"; color: "#94a3b8"; font.pixelSize: 11; Layout.preferredWidth: 60 }
                Text { text: "TOOL"; color: "#94a3b8"; font.pixelSize: 11; Layout.preferredWidth: 100 }
                Text { text: "STATUS"; color: "#94a3b8"; font.pixelSize: 11; Layout.preferredWidth: 70 }
                Text { text: "ARGUMENTS / RESULT"; color: "#94a3b8"; font.pixelSize: 11; Layout.fillWidth: true }
            }
        }

        // List
        ListView {
            id: toolList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: (typeof geminiBridge !== "undefined" && geminiBridge) ? geminiBridge.toolCalls : []
            spacing: 4
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOn }

            delegate: Rectangle {
                width: toolList.width
                height: contentRow.implicitHeight + 16
                color: modelData.status === "running" ? "#1e293b" : "transparent"
                radius: 4

                RowLayout {
                    id: contentRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10

                    Text {
                        text: modelData.timestamp || "--:--:--"
                        color: "#64748b"
                        font.pixelSize: 11
                        Layout.preferredWidth: 60
                    }

                    Text {
                        text: modelData.name || "unknown"
                        color: "#3b82f6"
                        font.bold: true
                        font.pixelSize: 12
                        Layout.preferredWidth: 100
                    }

                    Rectangle {
                        Layout.preferredWidth: 70
                        height: 18
                        radius: 9
                        color: modelData.status === "success" ? "#10b98122" : (modelData.status === "running" ? "#f59e0b22" : "#ef444422")
                        border.color: modelData.status === "success" ? "#10b981" : (modelData.status === "running" ? "#f59e0b" : "#ef4444")
                        border.width: 1
                        
                        Text {
                            anchors.centerIn: parent
                            text: modelData.status.toUpperCase()
                            color: parent.border.color
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            text: "ARGS: " + JSON.stringify(modelData.args)
                            color: "#cbd5e1"
                            font.family: "Monospace"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            visible: modelData.status === "running"
                        }
                        Text {
                            text: "RESULT: " + (modelData.result ? JSON.stringify(modelData.result) : "Waiting...")
                            color: modelData.status === "success" ? "#94a3b8" : "#f87171"
                            font.family: "Monospace"
                            font.pixelSize: 10
                            wrapMode: Text.Wrap
                            Layout.fillWidth: true
                            visible: modelData.status !== "running"
                        }
                    }
                }
            }
        }
    }
}
