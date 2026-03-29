import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: composerRoot
    width: parent.width - 40
    height: Math.max(80, inputRow.implicitHeight + 40)
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 20
    
    radius: 16
    color: geminiBridge.glassBackground
    border.color: "#334155"
    border.width: 1

    signal sendMessage(string text)
    signal stopRun()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // Status Banner (integrated)
        Rectangle {
            Layout.fillWidth: true
            height: 24
            visible: geminiBridge.isWorking
            color: "transparent"

            RowLayout {
                anchors.centerIn: parent
                spacing: 8

                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    PropertyAnimation { from: 0.4; to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
                    PropertyAnimation { from: 1.0; to: 0.4; duration: 800; easing.type: Easing.InOutQuad }
                }

                Text {
                    text: geminiBridge.thinkingText || "VIORA THINKING..."
                    color: "#3b82f6"
                    font.pixelSize: 11
                    font.bold: true
                    font.letterSpacing: 1
                }
            }
        }

        RowLayout {
            id: inputRow
            Layout.fillWidth: true
            spacing: 12

            ScrollView {
                Layout.fillWidth: true
                Layout.maximumHeight: 150
                ScrollBar.vertical.policy: ScrollBar.AsNeeded

                TextArea {
                    id: inputField
                    placeholderText: "Message Viora AI... (Enter to send)"
                    placeholderTextColor: "#4b5563"
                    color: "white"
                    font.pixelSize: 14
                    wrapMode: TextArea.Wrap
                    background: null
                    
                    Keys.onPressed: (event) => {
                        if (event.key === Qt.Key_Return && !(event.modifiers & Qt.ControlModifier)) {
                            if (text.trim() !== "") {
                                composerRoot.sendMessage(text.trim())
                                text = ""
                            }
                            event.accepted = true
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            spacing: 8

            // Left side controls: Utility Buttons
            Row {
                id: utilityRow
                spacing: 8
                Layout.alignment: Qt.AlignVCenter

                // Refresh Button
                Button {
                    id: refreshBtn
                    text: "↻"
                    ToolTip.visible: hovered
                    ToolTip.text: "Refresh Models"
                    onClicked: geminiBridge.refreshModels()
                    font.pixelSize: 14
                    font.bold: true
                    
                    background: Rectangle {
                        implicitWidth: 28
                        implicitHeight: 24
                        color: parent.hovered ? "#1affffff" : "transparent"
                        radius: 6
                        border.color: parent.hovered ? "#475569" : "transparent"
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: parent.hovered ? "white" : "#94a3b8"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Model Selector
                ComboBox {
                    id: modelSelector
                    model: geminiBridge.availableModels
                    currentIndex: model.indexOf(geminiBridge.currentModel)
                    onActivated: (index) => geminiBridge.currentModel = model[index]
                    Layout.preferredWidth: 100
                    
                    contentItem: Text {
                        leftPadding: 6
                        rightPadding: 18
                        text: modelSelector.currentText || "Select Model..."
                        font.pixelSize: 10
                        font.bold: true
                        color: "#e2e8f0"
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                    
                    background: Rectangle {
                        implicitWidth: 100
                        implicitHeight: 24
                        color: "#801e293b" // 50% alpha dark
                        border.color: modelSelector.hovered ? "#475569" : "#334155"
                        border.width: 1
                        radius: 6
                    }

                    delegate: ItemDelegate {
                        width: modelSelector.width
                        padding: 8
                        contentItem: Text {
                            text: modelData
                            color: hovered ? "white" : "#94a3b8"
                            font.pixelSize: 10
                            font.bold: true
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            color: highlighted ? "#2563eb" : (hovered ? "#1e293b" : "transparent")
                        }
                    }

                    popup: Popup {
                        y: -height - 4
                        width: modelSelector.width
                        implicitHeight: contentItem.implicitHeight
                        padding: 1
                        
                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: modelSelector.popup.visible ? modelSelector.delegateModel : null
                            ScrollIndicator.vertical: ScrollIndicator { }
                        }
                        
                        background: Rectangle {
                            color: "#1e293b"
                            border.color: "#334155"
                            radius: 8
                        }
                    }
                }

                // Design Audit Button
                Button {
                    id: auditBtn
                    text: "AUDIT"
                    visible: !geminiBridge.isWorking
                    onClicked: composerRoot.sendMessage("Please provide a comprehensive design audit of this circuit, focusing on component selection, power dissipation, and potential reliability issues.")
                    font.pixelSize: 9
                    font.bold: true
                    
                    background: Rectangle {
                        implicitWidth: 50
                        implicitHeight: 24
                        color: parent.hovered ? "#263b82f6" : "transparent"
                        radius: 6
                        border.color: "#3b82f6"
                        border.width: 1
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "#3b82f6"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            Item { Layout.fillWidth: true }

            // Right side: Action Buttons
            Row {
                id: actionRow
                spacing: 6
                Layout.alignment: Qt.AlignVCenter

                // Stop Button
                Button {
                    id: stopBtn
                    text: "STOP"
                    visible: geminiBridge.isWorking
                    onClicked: composerRoot.stopRun()
                    font.pixelSize: 10
                    font.bold: true
                    
                    background: Rectangle {
                        implicitWidth: 54
                        implicitHeight: 28
                        radius: 8
                        color: parent.hovered ? "#ef4444" : "#dc2626"
                    }
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                // Send Button
                Button {
                    id: sendBtn
                    text: "SEND"
                    visible: !geminiBridge.isWorking
                    enabled: inputField.text.trim() !== ""
                    onClicked: {
                        if (inputField.text.trim() !== "") {
                            composerRoot.sendMessage(inputField.text.trim())
                            inputField.text = ""
                        }
                    }
                    font.pixelSize: 10
                    font.bold: true
                    
                    background: Rectangle {
                        implicitWidth: 54
                        implicitHeight: 28
                        radius: 8
                        color: parent.enabled ? (parent.hovered ? "#3b82f6" : "#2563eb") : "#1e293b"
                    }
                    contentItem: Text {
                        text: parent.text
                        color: parent.enabled ? "white" : "#475569"
                        font: parent.font
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
}
