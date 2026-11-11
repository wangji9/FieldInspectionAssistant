import QtQuick
import QtQuick.Controls
import "../theme"

ScrollView {
    id: root
    property string markdown: ""
    property string prompt: ""
    property string errorMessage: ""
    property bool streaming: false
    property bool followOutput: true
    property bool readable: true
    property string topic: "text"
    signal promptChosen(string text)
    clip: true
    contentWidth: availableWidth
    contentHeight: conversation.height
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    background: Rectangle { color: Colors.panel; border.color: Colors.border }
    onMarkdownChanged: if (followOutput && !follow.running) follow.start()
    onStreamingChanged: if (streaming) followOutput = true
    Column {
        parent: root; z: 2
        visible: !root.markdown.length && !root.streaming && !root.errorMessage.length
        anchors.centerIn: parent; width:Math.max(160,Math.min(530,root.availableWidth-48)); spacing:14
        Text { width:parent.width; text:root.topic==="vision" ? "选择现场图片后提问" : "输入巡检问题"; wrapMode:Text.Wrap; horizontalAlignment:Text.AlignHCenter; font.pixelSize:Typography.h2; color:Colors.text }
        Flow {
            width:parent.width; spacing:8; visible:root.topic==="text" && root.height>210
            Repeater {
                model:[{label:"梳理巡检要点",prompt:"请帮我梳理设备巡检的关键检查要点。"},{label:"分析设备状态",prompt:"分析设备状态时，需要关注哪些指标？"},{label:"整理异常记录",prompt:"请给我一个清晰的设备异常记录模板。"}]
                Rectangle {
                    width:(parent.width-16)/3; height:32; color:Colors.panelAlt; border.color:Colors.border
                    Text { anchors.centerIn:parent; text:modelData.label; font.pixelSize:Typography.body; color:Colors.textSecondary }
                    MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:root.promptChosen(modelData.prompt) }
                }
            }
        }
    }
    Column {
        id: conversation
        x: 8; y: 10; width: Math.max(100, root.availableWidth - 16); spacing: 6
        height: implicitHeight + 16
        ChatBubble {
            width: parent.width; outgoing: true; body: root.prompt; caption: "我"
            visible: root.prompt.length > 0 && (root.markdown.length > 0 || root.streaming)
            height: visible ? implicitHeight : 0; showActions: false
        }
        ChatBubble {
            width: parent.width; body: root.markdown; caption: "巡检助手"; streaming: root.streaming
            visible: root.markdown.length > 0 || root.streaming
            height: visible ? implicitHeight : 0; showActions: root.readable && !root.streaming
            allowEdit: false
            actionsEnabled: !App.speech.recording && !App.speech.speechBusy && !App.dictation.active
            onReadRequested: App.speech.speak(root.markdown)
            status: root.streaming ? root.markdown.length ? "正在输出…" : "正在准备…" : ""
            onHeightChanged: if (root.followOutput && !follow.running) follow.start()
        }
        ChatBubble {
            width: parent.width; body: root.errorMessage; caption: "巡检助手 · 提示"; kind: "error"
            visible: root.errorMessage.length > 0; height: visible ? implicitHeight : 0; showActions: false
            onHeightChanged: if (root.followOutput && !follow.running) follow.start()
        }
    }
    Timer { id: follow; interval: 45; onTriggered: if (root.followOutput) root.contentItem.contentY = Math.max(0, root.contentHeight - root.availableHeight) }
    Connections {
        target: root.contentItem
        function onMovementStarted() { root.followOutput = false }
        function onMovementEnded() { root.followOutput = root.contentItem.contentY >= root.contentHeight - root.availableHeight - 36 }
    }
}
