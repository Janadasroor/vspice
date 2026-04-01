import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: composerRoot
    width: parent ? parent.width - 40 : 400
    height: Math.max(100, inputColumn.implicitHeight + 40)
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.bottomMargin: 20
    
    radius: 16
    color: (typeof geminiBridge !== "undefined" && geminiBridge && geminiBridge.glassBackground) ? geminiBridge.glassBackground : "rgba(15, 23, 42, 0.95)"
    border.color: {
        if (typeof geminiBridge === "undefined" || !geminiBridge) return "#334155";
        var m = geminiBridge.currentMode;
        if (m === "Planning") return "#10b981"; // Green
        if (m === "Cmd") return "#f59e0b";      // Yellow
        return "#334155";                      // Normal
    }
    border.width: 1

    signal sendMessage(string text)
    signal stopRun()
    
    function forceFocus() {
        inputField.forceActiveFocus()
    }

    ColumnLayout {
        id: inputColumn
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 8

        // Tool Action Bar
        RowLayout {
            id: toolActionBar
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            spacing: 8
            visible: (typeof geminiBridge !== "undefined" && geminiBridge) && geminiBridge.isWorking
            opacity: visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 250 } }

            // Pulsing dot
            Rectangle {
                width: 6; height: 6; radius: 3
                color: "#10b981"
                Layout.alignment: Qt.AlignVCenter
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.3; duration: 800; easing.type: Easing.InOutQuad }
                    NumberAnimation { from: 0.3; to: 1; duration: 800; easing.type: Easing.InOutQuad }
                }
            }

            Text {
                id: toolName
                text: (typeof geminiBridge !== "undefined" && geminiBridge) ? geminiBridge.currentTool : "ViorAI"
                color: (typeof geminiBridge !== "undefined" && geminiBridge) ? geminiBridge.accentColor : "#3b82f6"
                font.pixelSize: 10 * (typeof geminiBridge !== "undefined" && geminiBridge ? geminiBridge.zoomFactor : 1.0)
                font.bold: true
                Layout.alignment: Qt.AlignVCenter
            }

            Rectangle {
                width: 1; height: 10
                color: Qt.rgba(1, 1, 1, 0.15)
                Layout.alignment: Qt.AlignVCenter
            }

            Text {
                id: actionSubtext
                text: (typeof geminiBridge !== "undefined" && geminiBridge) ? geminiBridge.currentAction : ""
                color: "#94a3b8"
                font.pixelSize: 10 * (typeof geminiBridge !== "undefined" && geminiBridge ? geminiBridge.zoomFactor : 1.0)
                font.italic: true
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }
        }

        // Input Area
        ScrollView {
            Layout.fillWidth: true
            Layout.maximumHeight: 200
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            TextArea {
                id: inputField
                placeholderText: "Message Viora AI..."
                placeholderTextColor: "#4b5563"
                color: "white"
                font.pixelSize: 14 * (typeof geminiBridge !== "undefined" && geminiBridge ? geminiBridge.zoomFactor : 1.0)
                wrapMode: TextArea.Wrap
                background: null
                
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Return && !(event.modifiers & Qt.ControlModifier)) {
                        if (text.trim() !== "") {
                            composerRoot.sendMessage(text.trim())
                            text = ""
                            inputField.forceActiveFocus()
                        }
                        event.accepted = true
                    }
                }
                
                Component.onCompleted: forceActiveFocus()
            }
        }

        // Action Bar (Redesigned)
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            spacing: 16

            // Left: Utility and Selectors
            RowLayout {
                spacing: 12
                Layout.alignment: Qt.AlignVCenter

                IconButton {
                    text: "+"
                    ToolTip.text: "New Chat"
                    onClicked: if (typeof geminiBridge !== "undefined" && geminiBridge) geminiBridge.startNewChat()
                }

                // Mode Selector (with Descriptions)
                MinimalSelector {
                    id: modeSelector
                    model: (typeof geminiBridge !== "undefined" && geminiBridge) ? geminiBridge.availableModes : []
                    currentIndex: {
                        if (typeof geminiBridge === "undefined" || !geminiBridge || !model) return 0;
                        for (var i = 0; i < model.length; i++) {
                            if (model[i].name === geminiBridge.currentMode) return i;
                        }
                        return 0;
                    }
                    onActivated: (index) => { if (typeof geminiBridge !== "undefined" && geminiBridge) geminiBridge.currentMode = model[index].name }
                    
                    textRole: "name"
                    popupWidth: 280
                    showDescriptions: true
                    headerTitle: "Conversation mode"
                }

                // Model Selector
                MinimalSelector {
                    id: modelSelector
                    model: (typeof geminiBridge !== "undefined" && geminiBridge && geminiBridge.availableModels) ? geminiBridge.availableModels : ["Gemini 2.0 Flash"]
                    currentIndex: (typeof geminiBridge !== "undefined" && geminiBridge && geminiBridge.availableModels) ? Math.max(0, model.indexOf(geminiBridge.currentModel)) : 0
                    onActivated: (index) => { if (typeof geminiBridge !== "undefined" && geminiBridge) geminiBridge.currentModel = model[index] }
                }
            }

            Item { Layout.fillWidth: true }

            // Right: Secondary Actions and Primary Round Button
            RowLayout {
                spacing: 12
                Layout.alignment: Qt.AlignVCenter

                IconButton {
                    text: "🎤"
                    font.pixelSize: 14
                    ToolTip.text: "Voice Input (Placeholder)"
                }

                // Primary Action Button (Circular)
                Button {
                    id: mainActionBtn
                    property bool isWorking: (typeof geminiBridge !== "undefined" && geminiBridge) ? geminiBridge.isWorking : false
                    
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    
                    onClicked: {
                        if (isWorking) {
                            composerRoot.stopRun()
                        } else if (inputField.text.trim() !== "") {
                            composerRoot.sendMessage(inputField.text.trim())
                            inputField.text = ""
                            inputField.forceActiveFocus()
                        }
                    }

                    background: Rectangle {
                        radius: 16
                        color: mainActionBtn.isWorking ? "#ef4444" : (mainActionBtn.enabled ? "#2563eb" : "#1e293b")
                        
                        // Icon Content
                        Text {
                            anchors.centerIn: parent
                            text: mainActionBtn.isWorking ? "■" : "→"
                            color: "white"
                            font.pixelSize: mainActionBtn.isWorking ? 10 : 16
                            font.bold: true
                        }
                    }
                    
                    enabled: isWorking || inputField.text.trim() !== ""
                }
            }
        }
    }

    // --- Sub-Components ---

    // Minimal IconButton
    component IconButton : Button {
        id: iconBtn
        flat: true
        font.pixelSize: 18
        
        background: Rectangle {
            implicitWidth: 28
            implicitHeight: 28
            color: iconBtn.hovered ? "rgba(255, 255, 255, 0.08)" : "transparent"
            radius: 6
        }
        
        contentItem: Text {
            text: iconBtn.text
            color: iconBtn.hovered ? "white" : "#94a3b8"
            font: iconBtn.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        ToolTip.visible: hovered
        ToolTip.delay: 500
    }

    // Minimal minimalist dropdown selector
    component MinimalSelector : ComboBox {
        id: cb
        
        property int popupWidth: 200
        property bool showDescriptions: false
        property string headerTitle: ""

        background: Item { 
            implicitWidth: contentLayout.implicitWidth + 8
            implicitHeight: 28
        }
        
        contentItem: RowLayout {
            id: contentLayout
            spacing: 4
            
            Text {
                text: "^" 
                color: "#94a3b8"
                font.pixelSize: 10
                font.bold: true
                rotation: cb.popup.visible ? 0 : 180 // Toggle orientation
                Layout.alignment: Qt.AlignVCenter
            }
            
            Text {
                text: cb.currentText
                color: cb.hovered ? "white" : "#94a3b8"
                font.pixelSize: 11
                font.bold: true
                elide: Text.ElideRight
                Layout.alignment: Qt.AlignVCenter
                Layout.maximumWidth: 120
            }
        }

        delegate: ItemDelegate {
            id: delegateRoot
            width: cb.popupWidth
            padding: 12
            
            contentItem: Column {
                spacing: 4
                Text {
                    text: (typeof modelData === "object") ? modelData.name : modelData
                    color: highlighted || hovered ? "white" : "#e2e8f0"
                    font.pixelSize: 12 * (typeof geminiBridge !== "undefined" && geminiBridge ? geminiBridge.zoomFactor : 1.0)
                    font.bold: true
                }
                Text {
                    visible: cb.showDescriptions && (typeof modelData === "object") && modelData.desc
                    text: (typeof modelData === "object") ? modelData.desc : ""
                    color: "#94a3b8"
                    font.pixelSize: 10
                    wrapMode: Text.Wrap
                    width: parent.width
                }
            }
            background: Rectangle {
                color: highlighted ? "rgba(37, 99, 235, 0.15)" : (hovered ? "rgba(255, 255, 255, 0.05)" : "transparent")
                border.color: highlighted ? "#2563eb" : "transparent"
                border.width: 1
                radius: 8
            }
        }

        popup: Popup {
            onClosed: composerRoot.forceFocus()
            y: -height - 8
            width: cb.popupWidth
            implicitHeight: contentColumn.implicitHeight + (cb.headerTitle !== "" ? 40 : 0)
            padding: 8
            
            contentItem: Column {
                id: contentColumn
                spacing: 8
                
                Text {
                    visible: cb.headerTitle !== ""
                    text: cb.headerTitle
                    color: "#64748b"
                    font.pixelSize: 11
                    font.bold: true
                    padding: 4
                }
                
                ListView {
                    id: listView
                    clip: true
                    implicitHeight: contentHeight
                    width: parent.width
                    model: cb.popup.visible ? cb.delegateModel : null
                    ScrollIndicator.vertical: ScrollIndicator { }
                }
            }
            
            background: Rectangle {
                color: "#1e293b"
                border.color: "#334155"
                radius: 12
                
                // Subtle shadow
                layer.enabled: true
                layer.effect: ShaderEffect { } 
                // Using layer for standard shadows is complex without generic libs, 
                // so I'll just use a slightly darker border.
            }
        }
    }
}
