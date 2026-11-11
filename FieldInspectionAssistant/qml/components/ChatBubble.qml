import QtQuick
import QtQuick.Controls
import "../theme"

Item {
    id: root
    property bool outgoing: false
    property string kind: "text"
    property string body: ""
    property string caption: outgoing ? "我" : "巡检助手"
    property string status: ""
    property string systemEvidenceJson:""
    property string audioPath: ""
    property string imagePath: ""
    property real duration: 0
    property bool streaming: false
    property bool ready: true
    property bool showActions: true
    property bool transcribable: true
    property bool actionsEnabled: true
    property bool allowEdit: true
    property bool externalPlaying:false
    readonly property bool active: externalPlaying || audioPath.length > 0 && App.speech.currentPlaybackPath === audioPath && App.speech.speechBusy
    property string rendered: ""
    readonly property bool blockContent: /(^|\n)\s*(```|~~~|\|)/.test(body)
    signal saveAudioRequested()
    signal playRequested()
    signal transcribeRequested()
    signal readRequested()
    signal editRequested()
    signal textRequested()
    signal imageRequested(string path)
    readonly property real maxBubbleWidth: Math.max(100, Math.min(width - 60, width * 0.82))
    implicitHeight: bubble.y + bubble.height + (actions.visible ? actions.height + 8 : 0) + 14
    height: implicitHeight
    // Streaming updates can arrive dozens of times per second. Keep them as
    // lightweight text; parse Markdown once when the response is complete.
    function renderSoon() {
        renderTimer.stop()
        if (outgoing || kind === "error" || kind === "audio" || streaming || !body.length) { rendered = ""; return }
        renderTimer.start()
    }
    onBodyChanged: { rendered = ""; renderSoon() }
    onStreamingChanged: renderSoon()
    onKindChanged: renderSoon()
    onOutgoingChanged: renderSoon()
    Component.onCompleted: renderSoon()
    Timer { id: renderTimer; interval: 35; onTriggered: if (!root.streaming && !root.outgoing && root.kind !== "error" && root.kind !== "audio") root.rendered = App.ai.renderMarkdown(root.body, bubble.width - 28) }
    Connections { target: App.ai; function onPreferencesChanged() { root.renderSoon() } }
    Text { id: measure; visible: false; text: root.body.length > 160 ? root.body.slice(0,160) : root.body; textFormat: Text.PlainText; font.pixelSize: Typography.body; font.family: Typography.cjkFamily }
    Rectangle {
        width: 34; height: 34; radius: 7; y: bubble.y
        x: root.outgoing ? root.width - width : 0
        color: root.outgoing ? "#E4EEE9" : Colors.normal
        Text { anchors.centerIn:parent; visible:root.outgoing; text:"我"; color:"#55826F"; font.pixelSize:Typography.body; font.weight:Font.Medium }
        AppIcon { anchors.centerIn:parent; visible:!root.outgoing; name:"sparkles"; width:20; height:20; color:"white" }
    }
    Text {
        x: bubble.x; width: bubble.width; height: 20
        text: root.caption; elide: Text.ElideRight
        horizontalAlignment: root.outgoing ? Text.AlignRight : Text.AlignLeft
        color: Colors.textSecondary; font.pixelSize: Typography.small
    }
    Rectangle {
        id: bubble
        objectName: "chatBubble"
        y: 23
        width: root.imagePath.length ? Math.min(root.maxBubbleWidth,420) : root.kind === "audio" ? Math.min(root.maxBubbleWidth, Math.max(210, 210 + root.duration * 1.5))
                                    : !root.outgoing&&root.blockContent ? root.maxBubbleWidth : Math.min(root.maxBubbleWidth, Math.max(160, measure.implicitWidth + 50))
        x: root.outgoing ? root.width - width - 46 : 46
        height: bubbleContent.height + 24
        color: root.kind === "error" ? "#FFF1F0" : root.outgoing ? "#EAF5EE" : "#FFFFFF"
        border.color: root.kind === "error" ? "#E9B4B1" : root.outgoing ? "#DCEBE3" : "#EAEDF4"
        radius: 12
        onWidthChanged: root.renderSoon()
        Rectangle {
            width: 10; height: 10; rotation: 45; y: 14
            x: root.outgoing ? parent.width - 6 : -4; color: parent.color
        }
        Column {
            id: bubbleContent
            x: 14; y: 12; width: parent.width - 28; spacing: 8
            Image {
                visible: root.imagePath.length > 0; width: parent.width; height: visible ? Math.min(260, width) : 0
                source: visible ? "file://" + root.imagePath : ""; fillMode: Image.PreserveAspectFit; asynchronous: true
                MouseArea { anchors.fill:parent;onClicked:root.imageRequested(root.imagePath) }
            }
            Item {
                visible: root.kind === "audio" || root.audioPath.length>0; width: parent.width; height: visible ? 42 : 0
                Text {
                    x: 0; anchors.verticalCenter: parent.verticalCenter
                    text: root.active && (root.externalPlaying || App.speech.playing) ? "Ⅱ" : "▶"
                    font.pixelSize: 22; color: "#244C36"
                }
                Row {
                    x: 34; anchors.verticalCenter: parent.verticalCenter; spacing: 4
                    Repeater {
                        model: 7
                        Rectangle {
                            width: 4; radius: 2; height: 8 + (index % 4) * 5
                            anchors.verticalCenter: parent.verticalCenter
                            color: root.outgoing ? "#39856A" : Colors.normal
                            opacity: root.active ? 0.4 + ((Math.floor(App.speech.playbackPosition * 5) + index) % 4) * 0.2 : 0.8
                        }
                    }
                }
                Text {
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    text: root.duration > 0 ? Math.ceil(root.duration) + "″" : root.streaming ? "生成中" : "语音"
                    font.pixelSize: Typography.body; font.family: Typography.numericFamily; color: Colors.text
                }
                MouseArea { anchors.fill: parent; enabled: root.active || (root.ready && root.actionsEnabled); onClicked: root.playRequested() }
            }
            TextArea {
                visible: root.kind !== "audio"; width: parent.width
                height: visible ? contentHeight + topPadding + bottomPadding : 0
                text: root.outgoing || root.kind === "error" || root.streaming || !root.rendered.length ? root.body : root.rendered
                textFormat: root.outgoing || root.kind === "error" || root.streaming || !root.rendered.length ? TextEdit.PlainText : TextEdit.RichText
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; clip: true
                font.pixelSize: App.ai.answerFontSize; font.family: Typography.cjkFamily; color: Colors.text
                padding: 0
                background: null
                onLinkActivated: function(link) { if (/^https?:\/\//.test(link)) Qt.openUrlExternally(link) }
            }
            Text {
                visible: root.streaming || root.active
                height: visible ? Math.ceil(contentHeight) : 0
                width: parent.width; wrapMode: Text.Wrap
                text: root.externalPlaying ? "播放中" : root.active && App.speech.playing ? "播放中 · " + Math.floor(App.speech.playbackPosition) + " / " + Math.ceil(App.speech.playbackDuration) + " 秒"
                      : root.active && !root.streaming ? "正在准备播放…" : root.status || "正在输出…"
                font.pixelSize: Typography.small; color: Colors.textSecondary
            }
            SystemSources {width:parent.width;evidenceJson:root.outgoing?"":root.systemEvidenceJson}
            Rectangle {
                visible: root.kind === "audio" && root.active && App.speech.playbackDuration > 0
                width: parent.width; height: visible ? 3 : 0; radius: 1; color: "#D2DDD3"
                Rectangle { height: parent.height; radius: 1; color: Colors.normal; width: parent.width * Math.min(1, App.speech.playbackPosition / Math.max(0.1, App.speech.playbackDuration)) }
            }
        }
    }
    Flow {
        id: actions
        x: bubble.x; y: bubble.y + bubble.height + 6; width: bubble.width; spacing: 6
        visible: root.showActions && root.kind !== "error"
        ToolButton {
            visible:root.kind==="text"&&root.body.length>0;width:30;height:28
            Accessible.name:"朗读";enabled:root.actionsEnabled
            contentItem:AppIcon{name:"voice";color:Colors.textSecondary}
            background:Rectangle{radius:7;color:parent.hovered?"#E9EAF2":"transparent"}
            ToolTip.visible:hovered;ToolTip.text:"朗读";onClicked:root.readRequested()
        }
        ToolButton {
            visible:root.kind==="text"&&root.body.length>0;width:30;height:28
            Accessible.name:"复制";contentItem:AppIcon{name:"copy";color:Colors.textSecondary}
            background:Rectangle{radius:7;color:parent.hovered?"#E9EAF2":"transparent"}
            ToolTip.visible:hovered;ToolTip.text:"复制";onClicked:App.ai.copyText(root.body)
        }
        TouchButton {
            text: "转文字"; visible: root.kind === "audio" && root.transcribable
            autoWidth: true; height: 28; variant: "ghost"; enabled: root.ready && root.actionsEnabled
            onClicked: root.transcribeRequested()
        }
        ToolButton {
            text:"⋯";font.pixelSize:22;width:30;height:28
            visible:root.kind==="audio"||root.allowEdit&&root.body.length>0;Accessible.name:"更多操作"
            background:Rectangle{radius:7;color:parent.hovered?"#E9EAF2":"transparent"}
            ToolTip.visible:hovered;ToolTip.text:"更多操作";onClicked:messageMenu.popup()
        }
    }
    Menu {
        id:messageMenu
        MenuItem{text:root.active?"停止播放":"播放";visible:root.kind==="audio";height:visible?implicitHeight:0;enabled:root.active||root.ready&&root.actionsEnabled;onTriggered:root.playRequested()}
        MenuItem{text:"保存 WAV";visible:root.kind==="audio";height:visible?implicitHeight:0;enabled:root.ready&&root.audioPath.length>0&&root.actionsEnabled;onTriggered:root.saveAudioRequested()}
        MenuItem{text:root.kind==="audio"?"查看文字":"编辑文字";visible:root.body.length>0&&root.allowEdit;height:visible?implicitHeight:0;enabled:root.actionsEnabled;onTriggered:root.kind==="audio"?root.textRequested():root.editRequested()}
    }
}
