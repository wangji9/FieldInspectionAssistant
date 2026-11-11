import QtQuick
import "../theme"

TouchButton {
    property var targetInput
    text: "语音输入"; autoWidth: true; height: 36; variant: "ghost"
    enabled: targetInput && targetInput.enabled && !targetInput.readOnly && !App.ai.busy && !App.dictation.active && !App.speech.recording && !App.speech.speechBusy
    onClicked: App.input.open(targetInput, true)
}
