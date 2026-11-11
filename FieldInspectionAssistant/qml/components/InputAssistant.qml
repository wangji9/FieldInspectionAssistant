import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.VirtualKeyboard
import QtQuick.VirtualKeyboard.Settings
import "../theme"

Popup {
    id: sheet
    objectName: "inputAssistant"
    parent: App.input.targetWindow ? App.input.targetWindow.contentItem : Overlay.overlay
    z: 100000
    modal: true; focus: true; dim: true
    closePolicy: Popup.NoAutoClose
    padding: 12
    width: Math.min(parent.width - 24, 1080, Math.max(320, (parent.height - 220) * 2.5))
    height: Math.min(parent.height - 24, content.implicitHeight + 24)
    x: (parent.width - width) / 2
    y: parent.height - height - 12
    visible: App.input.opened
    property string draft: ""
    property string baseline: ""
    property string prefix: ""
    property string suffix: ""
    property string lastPartial: ""
    property bool slotActive: false
    property bool gotPartial: false
    property bool voiceUsed: false
    property bool autoCommitVoice: false
    readonly property var editor: App.input.multiline ? multiEditor : singleEditor
    readonly property bool secret: App.input.echoMode !== TextInput.Normal
    readonly property string englishLocale: VirtualKeyboardSettings.availableLocales.indexOf("en_US") >= 0 ? "en_US" : "en_GB"
    readonly property var keyboardLocales: VirtualKeyboardSettings.availableLocales
    function focusEditor() {
        if (!visible || App.dictation.active) return
        editor.forceActiveFocus()
        Qt.inputMethod.show()
    }
    function switchLanguage() {
        Qt.inputMethod.commit()
        VirtualKeyboardSettings.locale = VirtualKeyboardSettings.locale === "zh_CN" ? englishLocale : "zh_CN"
        Qt.callLater(focusEditor)
    }
    function prepareSlot() {
        Qt.inputMethod.commit()
        baseline = draft
        var a = editor.selectionStart, b = editor.selectionEnd
        if (a < 0 || b < 0) a = b = editor.cursorPosition
        prefix = draft.substring(0, Math.min(a,b))
        suffix = draft.substring(Math.max(a,b))
        lastPartial = ""; gotPartial = false; slotActive = true; voiceUsed = true
    }
    function startVoice() {
        if (secret || App.dictation.active) return
        prepareSlot(); App.dictation.start()
    }
    function transcribeFile(path) {
        if (secret || App.dictation.active) return
        prepareSlot(); App.dictation.transcribeFile(path)
    }
    function cancelVoice() {
        slotActive = false; draft = baseline; App.dictation.cancel()
        Qt.callLater(focusEditor)
    }
    function applyDraft() {
        if (App.dictation.active) return
        Qt.inputMethod.commit()
        App.input.apply(draft, editor.cursorPosition)
    }
    Component.onCompleted: {
        VirtualKeyboardSettings.fullScreenMode = false
        VirtualKeyboardSettings.wordCandidateList.alwaysVisible = true
        VirtualKeyboardSettings.activeLocales = ["zh_CN", englishLocale]
        VirtualKeyboardSettings.locale = "zh_CN"
    }
    onOpened: {
        slotActive = false; voiceUsed = false; autoCommitVoice = App.input.voiceRequested; draft = App.input.initialText
        Qt.callLater(function() {
            if (!sheet.visible) return
            sheet.editor.cursorPosition = Math.min(sheet.draft.length, App.input.cursorPosition)
            sheet.focusEditor()
            if (App.input.voiceRequested) Qt.callLater(sheet.startVoice)
        })
    }
    onClosed: { slotActive = false; if (App.input.opened) App.input.cancel() }
    background: Rectangle { color: Colors.panel; border.color: Colors.border; radius: 8 }
    contentItem: ColumnLayout {
        id: content
        spacing: 8
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 36
            Text { Layout.fillWidth: true; text: App.input.title; elide: Text.ElideRight; font.pixelSize: Typography.body; font.bold: true; color: Colors.text }
            TouchButton { objectName: "keyboardLanguageButton"; text: VirtualKeyboardSettings.locale === "zh_CN" ? "中 / EN" : "EN / 中"; autoWidth: true; height: 36; variant: "ghost"; enabled: !App.dictation.active; onClicked: sheet.switchLanguage() }
            TouchButton { text: "取消"; autoWidth: true; height: 36; variant: "ghost"; onClicked: App.input.cancel() }
            TouchButton { objectName: "applyInputButton"; text: "完成输入"; autoWidth: true; height: 36; variant: "primary"; enabled: !App.dictation.active; onClicked: sheet.applyDraft() }
        }
        Item {
            Layout.fillWidth: true; Layout.preferredHeight: App.input.multiline ? 72 : 46
            TextField {
                id: singleEditor; objectName: "keyboardSingleEditor"
                property bool inputAssistantInternal: true
                anchors.fill: parent; visible: !App.input.multiline
                text: sheet.draft; onTextChanged: sheet.draft = text
                font.pixelSize: Typography.body; font.family: Typography.cjkFamily; color: Colors.text
                readOnly: App.dictation.active; echoMode: App.input.echoMode; maximumLength: App.input.maximumLength
                inputMethodHints: App.input.hints; selectByMouse: true
                background: Rectangle { color: Colors.panelAlt; border.color: Colors.border; radius: Dimensions.radius }
                onAccepted: sheet.applyDraft()
            }
            ScrollView {
                anchors.fill: parent; visible: App.input.multiline; clip: true
                TextArea {
                    id: multiEditor; objectName: "keyboardMultiEditor"
                    property bool inputAssistantInternal: true
                    text: sheet.draft; onTextChanged: sheet.draft = text
                    font.pixelSize: Typography.body; font.family: Typography.cjkFamily; color: Colors.text
                    readOnly: App.dictation.active; inputMethodHints: App.input.hints
                    selectByMouse: true; wrapMode: TextEdit.Wrap; clip: true
                    background: Rectangle { color: Colors.panelAlt; border.color: Colors.border; radius: Dimensions.radius }
                    Keys.onPressed: function(event) { if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) { sheet.applyDraft(); event.accepted = true } }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 38; spacing: 8
            TouchButton { objectName: "dictationStartButton"; text: "语音输入"; autoWidth: true; height: 36; variant: "ghost"; enabled: !sheet.secret && !App.ai.busy && !App.dictation.active && !App.speech.recording && !App.speech.speechBusy; onClicked: sheet.startVoice() }
            TouchButton { text: "导入录音"; autoWidth: true; height: 36; variant: "ghost"; enabled: !sheet.secret && !App.ai.busy && !App.dictation.active && !App.speech.recording && !App.speech.speechBusy; onClicked: audioPicker.open() }
            TouchButton { text: "结束并识别"; autoWidth: true; height: 36; variant: "primary"; visible: App.dictation.recording; onClicked: App.dictation.stopRecording() }
            TouchButton { text: "取消语音"; autoWidth: true; height: 36; variant: "ghost"; visible: App.dictation.active; onClicked: sheet.cancelVoice() }
            Text {
                Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: Typography.small
                text: App.dictation.active || sheet.voiceUsed ? App.dictation.message : ""
                color: App.dictation.error.length && sheet.voiceUsed ? Colors.danger : Colors.textSecondary
            }
        }
        Item {
            Layout.fillWidth: true; Layout.preferredHeight: keyboard.height
            InputPanel {
                id: keyboard; objectName: "onScreenKeyboard"
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                enabled: !App.dictation.active
            }
        }
    }
    FileDialog {
        id: audioPicker
        title: "选择用于语音输入的录音"; acceptLabel: "选择"; rejectLabel: "取消"
        nameFilters: ["音频 (*.wav *.mp3 *.flac *.m4a *.ogg *.webm *.aac)"]
        onAccepted: sheet.transcribeFile(decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, "")))
    }
    Connections {
        target: App.dictation
        function onChanged() {
            if (!sheet.visible || !sheet.slotActive) return
            var partial = App.dictation.partial
            if (partial !== sheet.lastPartial && (partial.length || sheet.gotPartial)) {
                sheet.lastPartial = partial; sheet.gotPartial = true
                sheet.draft = sheet.prefix + partial + sheet.suffix
                sheet.editor.cursorPosition = sheet.prefix.length + partial.length
            }
        }
        function onFinished(ok) {
            if (!sheet.visible) return
            sheet.slotActive = false
            if (ok && sheet.autoCommitVoice) { sheet.applyDraft(); return }
            Qt.callLater(sheet.focusEditor)
        }
    }
    Connections {
        target: Qt.inputMethod
        function onVisibleChanged() {
            if (!Qt.inputMethod.visible && sheet.visible && !App.dictation.active)
                Qt.callLater(sheet.focusEditor)
        }
    }
}
