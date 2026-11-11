import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: root
    property alias text: editor.text
    property alias editor: editor
    property string inputName: "assistantInput"
    property string placeholder: "发消息…"
    property string imagePath: ""
    property string attachmentText: ""
    property bool busy: false
    property bool recording: false
    property bool canSend: true
    property bool canVoice: true
    property bool voiceEnabled: true
    property bool attachmentCanSend:false
    property bool attachmentsAvailable:true
    property bool searchAvailable:false
    property bool searchEnabled:false
    signal searchToggled(bool enabled)
    signal sendRequested()
    signal stopRequested()
    signal voiceRequested()
    signal attachmentsRequested()
    signal previewRequested()
    signal removeAttachment()
    implicitHeight: (imagePath.length || attachmentText.length ? 76 : 0) + Math.max(56, Math.min(134, editor.contentHeight + 24))
    radius: 26; color: Colors.panel; border.color: editor.activeFocus ? "#CBC5ED" : "#E5E7ED"
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 8; spacing: 4
        RowLayout {
            visible: root.imagePath.length || root.attachmentText.length
            Layout.fillWidth: true; Layout.preferredHeight: 68
            Rectangle {
                Layout.preferredWidth: root.imagePath.length ? 70 : 0; Layout.preferredHeight: 64
                visible: root.imagePath.length; radius: 10; color: Colors.panelAlt; clip: true
                Image { anchors.fill: parent; anchors.margins: 3; source: root.imagePath.length ? "file://" + root.imagePath : ""; fillMode: Image.PreserveAspectFit; asynchronous: true }
                MouseArea { anchors.fill: parent; onClicked: root.previewRequested() }
            }
            Text { Layout.fillWidth: true; text: root.attachmentText; elide: Text.ElideMiddle; color: Colors.textSecondary; font.pixelSize: Typography.body }
            ToolButton { text: "×"; implicitWidth: 32; implicitHeight: 32; Accessible.name: "移除附件"; enabled: !root.busy; onClicked: root.removeAttachment() }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 4
            ToolButton {
                objectName: "composerAdd"; visible:root.attachmentsAvailable; text: "+"; font.pixelSize: 26; implicitWidth: 36; implicitHeight: 36
                Accessible.name: "添加附件和更多操作"; enabled: !root.busy && !root.recording
                background: Rectangle { radius: 18; color: parent.hovered ? Colors.panelAlt : "transparent" }
                onClicked: root.attachmentsRequested()
                ToolTip.visible: hovered; ToolTip.text: "添加附件"
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                TextArea {
                    id: editor; objectName: root.inputName; placeholderText: root.recording ? "正在录音…" : root.placeholder
                    wrapMode: TextEdit.Wrap; selectByMouse: true; font.pixelSize: Typography.body; color: Colors.text
                    padding: 8; topPadding: 9; background: null; enabled: !root.busy && !root.recording
                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && !(event.modifiers & Qt.ShiftModifier)) {
                            if (root.canSend && text.trim().length) root.sendRequested()
                            event.accepted = true
                        }
                    }
                }
            }
            ToolButton {
                objectName:"composerWebSearch";visible:root.searchAvailable
                implicitWidth:64;implicitHeight:34;text:"联网";font.pixelSize:Typography.body
                enabled:!root.busy&&!root.recording;checkable:true;checked:root.searchEnabled
                Accessible.name:root.searchEnabled?"关闭联网搜索":"开启联网搜索"
                contentItem:Text{text:"联网";horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;font.pixelSize:Typography.body;color:root.searchEnabled?Colors.normal:Colors.textSecondary}
                background:Rectangle{radius:17;color:root.searchEnabled?Colors.infoDark:parent.hovered?Colors.panelAlt:"transparent";border.color:root.searchEnabled?Colors.normal:"transparent"}
                onClicked:{root.searchToggled(!root.searchEnabled);checked=Qt.binding(function(){return root.searchEnabled})}
                ToolTip.visible:hovered;ToolTip.text:root.searchEnabled?"已开启：搜索公开资料并附来源":"联网搜索（发送本次问题文字，不上传图片或录音）"
            }
            ToolButton {
                visible: root.voiceEnabled && root.text.trim().length > 0 && !root.busy && !root.recording
                implicitWidth: 34; implicitHeight: 36; enabled: root.canVoice
                Accessible.name: "语音输入"
                background: Rectangle { radius: 18; color: parent.hovered ? Colors.panelAlt : "transparent" }
                contentItem: AppIcon { name: "mic"; width: 18; height: 18; color: Colors.textSecondary }
                onClicked: root.voiceRequested(); ToolTip.visible: hovered; ToolTip.text: "语音输入"
            }
            Button {
                objectName: "composerSubmit"; implicitWidth: 36; implicitHeight: 36
                readonly property bool stopping: root.busy || root.recording
                readonly property bool sending: root.text.trim().length > 0||(root.attachmentCanSend&&(root.imagePath.length>0||root.attachmentText.length>0))
                enabled: stopping || (sending ? root.canSend : root.voiceEnabled && root.canVoice)
                Accessible.name: stopping ? "停止" : sending ? "发送" : "语音输入"
                background: Rectangle { radius: 18; color: parent.enabled ? "#487DF5" : "#E7E9EF" }
                contentItem: AppIcon { name: parent.stopping ? "stop" : parent.sending ? "arrow-up" : "voice"; width: 19; height: 19; color: "white" }
                onClicked: if (stopping) root.stopRequested(); else if (sending) root.sendRequested(); else root.voiceRequested()
                ToolTip.visible: hovered; ToolTip.text: Accessible.name
            }
        }
    }
}
