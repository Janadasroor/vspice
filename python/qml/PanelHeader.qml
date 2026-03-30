import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: headerRoot
    width: parent ? parent.width : 400
    height: 48
    color: "transparent"

    property string title: (typeof geminiBridge !== "undefined" && geminiBridge && geminiBridge.conversationTitle) ? geminiBridge.conversationTitle : "VIORA AI"
    signal showDashboardRequested()
    signal requestInputFocus()

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

        // Usage Stats
        Row {
            spacing: 8
            Layout.alignment: Qt.AlignVCenter
            visible: typeof geminiBridge !== "undefined" && geminiBridge && geminiBridge.tokenCount > 0

            // Custom Usage Icon
            Item {
                width: 14; height: 14
                Rectangle {
                    width: 12; height: 12
                    radius: 6
                    color: "transparent"
                    border.color: "#3b82f6"
                    border.width: 1.5
                    anchors.centerIn: parent
                    Rectangle {
                        width: 4; height: 4; radius: 2
                        color: "#3b82f6"; anchors.centerIn: parent
                    }
                }
            }

            Text {
                text: (typeof geminiBridge !== "undefined" && geminiBridge) ? geminiBridge.tokenCount.toLocaleString(Qt.locale(), "f", 0) : "0"
                color: "#94a3b8"
                font.pixelSize: 12
                font.family: "JetBrains Mono" // Premium look
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                text: (typeof geminiBridge !== "undefined" && geminiBridge) ? Math.round(geminiBridge.usagePercentage * 100) + "%" : "0%"
                color: "#64748b" // Dimmed
                font.pixelSize: 12
                font.family: "JetBrains Mono"
                verticalAlignment: Text.AlignVCenter
            }
        }

        // Action Icons
        Row {
            spacing: 4
            Layout.alignment: Qt.AlignVCenter

            // New Chat (+)
            HeaderButton {
                text: "+"
                toolTip: "New Conversation"
                onClicked: if (typeof geminiBridge !== "undefined" && geminiBridge) geminiBridge.clearHistory()
            }

            // History (Clock-ish)
            HeaderButton {
                text: "🕒"
                toolTip: "History"
                onClicked: {
                    if (typeof geminiBridge !== "undefined" && geminiBridge && typeof geminiBridge.showHistory === "function") {
                        geminiBridge.showHistory()
                    }
                }
            }

            HeaderButton {
                id: moreBtn
                text: "•••"
                toolTip: "More Options"
                onClicked: moreMenu.open()
            }

            // Activity Dashboard (📊)
            HeaderButton {
                text: "📊"
                toolTip: "Tool Activity Audit"
                onClicked: headerRoot.showDashboardRequested()
                visible: typeof geminiBridge !== "undefined" && geminiBridge && geminiBridge.toolCalls.length > 0
            }
                
            Menu {
                id: moreMenu
                onClosed: headerRoot.requestInputFocus()
                    y: moreBtn.height + 5
                    x: -120 // Offset to the left to avoid edge
                    width: 160

                    background: Rectangle {
                        color: "#1e293b" // Slate 800
                        border.color: "#334155" // Slate 700
                        radius: 8
                    }

                    MenuItem {
                        text: "Custom Instructions"
                        onTriggered: {
                            if (typeof geminiBridge !== "undefined" && geminiBridge && typeof geminiBridge.showInstructions === "function") {
                                geminiBridge.showInstructions()
                            }
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 13
                            color: parent.highlighted ? "white" : "#e2e8f0"
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                        }
                        
                        background: Rectangle {
                            color: parent.highlighted ? "#3b82f6" : "transparent"
                            radius: 4
                        }
                    }

                    Rectangle {
                        width: parent.width - 20
                        height: 1
                        color: "#ffffff"
                        opacity: 0.1
                        anchors.horizontalCenter: parent.horizontalCenter
                    }

                    MenuItem {
                        text: "Zoom In"
                        onTriggered: if (typeof geminiBridge !== "undefined" && geminiBridge) geminiBridge.zoomIn()
                        contentItem: Text {
                            text: parent.text; font.pixelSize: 13; color: parent.highlighted ? "white" : "#e2e8f0"; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter; leftPadding: 10
                        }
                        background: Rectangle { color: parent.highlighted ? "#3b82f6" : "transparent"; radius: 4 }
                    }

                    MenuItem {
                        text: "Zoom Out"
                        onTriggered: if (typeof geminiBridge !== "undefined" && geminiBridge) geminiBridge.zoomOut()
                        contentItem: Text {
                            text: parent.text; font.pixelSize: 13; color: parent.highlighted ? "white" : "#e2e8f0"; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter; leftPadding: 10
                        }
                        background: Rectangle { color: parent.highlighted ? "#3b82f6" : "transparent"; radius: 4 }
                    }

                    MenuItem {
                        text: "Reset Zoom"
                        onTriggered: if (typeof geminiBridge !== "undefined" && geminiBridge) geminiBridge.resetZoom()
                        contentItem: Text {
                            text: parent.text; font.pixelSize: 13; color: parent.highlighted ? "white" : "#e2e8f0"; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter; leftPadding: 10
                        }
                        background: Rectangle { color: parent.highlighted ? "#3b82f6" : "transparent"; radius: 4 }
                    }

                    MenuItem {
                        text: "Export Chat (.md)"
                        onTriggered: if (typeof geminiBridge !== "undefined" && geminiBridge) geminiBridge.exportChat()
                        contentItem: Text {
                            text: parent.text; font.pixelSize: 13; color: parent.highlighted ? "white" : "#e2e8f0"; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter; leftPadding: 10
                        }
                        background: Rectangle { color: parent.highlighted ? "#3b82f6" : "transparent"; radius: 4 }
                    }
                }

            // Close (X)
            HeaderButton {
                text: "✕"
                toolTip: "Close Panel"
                fontColor: hovered ? "#ef4444" : "#94a3b8"
                onClicked: if (typeof geminiBridge !== "undefined" && geminiBridge) geminiBridge.closePanel()
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
