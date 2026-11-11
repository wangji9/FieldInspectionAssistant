import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Dialog {
    id: dialog
    anchors.centerIn: parent
    modal: true
    width: Math.min(650, parent.width - 20)
    title: "大模型复核"
    standardButtons: Dialog.Close
    property string targetKind: ""
    property string targetId: ""
    property bool awaitingService: false
    readonly property var model: App.projectInference
    readonly property bool selected: model.subjectKind === targetKind && model.subjectId === targetId

    function review(kind, id) {
        if (!id) return
        targetKind = kind
        targetId = id
        open()
        awaitingService = !model.ready
        if (awaitingService) model.refresh()
        else if (!model.busy) model.analyzeSubject(targetKind, targetId)
    }
    onClosed: awaitingService = false

    Connections {
        target: dialog.model
        function onChanged() {
            if (!dialog.visible || !dialog.awaitingService || !dialog.model.ready || dialog.model.busy) return
            dialog.awaitingService = false
            Qt.callLater(function() {
                if (dialog.visible) dialog.model.analyzeSubject(dialog.targetKind, dialog.targetId)
            })
        }
    }

    Connections {
        target: App.session
        function onSessionEnding() { dialog.close(); dialog.targetId = "" }
    }
    contentItem: ColumnLayout {
        RowLayout {
            Layout.fillWidth: true
            Label { Layout.fillWidth: true; text: dialog.model.status; elide: Text.ElideRight }
            TouchButton {
                text: dialog.selected && dialog.model.answer ? "重新分析" : "开始分析"
                autoWidth: true; height: 28
                enabled: dialog.model.ready && !dialog.model.busy
                onClicked: dialog.model.analyzeSubject(dialog.targetKind, dialog.targetId)
            }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(285, dialog.parent.height * .45)
            clip: true; contentWidth: availableWidth
            TextArea {
                text: dialog.selected ? dialog.model.answer : ""
                placeholderText: "复核建议将在此显示，签审结论由巡检人员维护。"
                textFormat: TextEdit.PlainText
                readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
            }
        }
    }
}
