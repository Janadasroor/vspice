import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    width: 400
    height: 600
    color: "#0a0f18" // Deep base background

    // Top Navigation/Header
    PanelHeader {
        id: header
        anchors.top: parent.top
        z: 10
    }

    // Main Chat History
    ListView {
        id: chatListView
        width: parent.width
        anchors.top: header.bottom
        anchors.bottom: composer.top
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 15
        clip: true
        
        model: ListModel { id: chatModel }
        
        delegate: ChatCard {
            width: chatListView.width
            modelData: chatModel.get(index)
        }
        
        // Auto-scroll to bottom on new items
        onCountChanged: {
            if (count > 0) {
                positionViewAtEnd();
            }
        }

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            active: true
        }
    }

    // Floating Composer (as designed in Composer.qml)
    Composer {
        id: composer
        onSendMessage: (text) => geminiBridge.sendMessage(text)
        onStopRun: geminiBridge.stopRun()
    }

    // Connect bridge signals to update our local model
    Connections {
        target: geminiBridge
        
        function onMessagesChanged() {
            chatModel.clear();
            let msgs = geminiBridge.messages;
            for (let i = 0; i < msgs.length; ++i) {
                chatModel.append(msgs[i]);
            }
        }
    }

    // Initial load
    Component.onCompleted: {
        onMessagesChanged();
    }
}
