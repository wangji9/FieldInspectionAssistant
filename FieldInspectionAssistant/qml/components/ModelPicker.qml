import QtQuick
import QtQuick.Controls
import "../theme"

AppComboBox {
    property var panel
    property string taskMode: "vision"
    objectName: taskMode === "text" ? "textModelSelector" : "visionModelSelector"
    textRole: "title"; valueRole: "key"
    font.pixelSize: Typography.small
    enabled: !App.ai.busy && !App.dictation.active
    implicitHeight: 32; height: 32
    model: taskMode === "text" ? [
        {key: "qwen35", title: "Qwen3.5-4B · RK1828"},
        {key: "qwen3", title: "Qwen3-4B · RK3588"}] : [
        {key: "qwen25vl", title: "Qwen2.5-VL-7B prune · RK1828"},
        {key: "qwen3vl", title: "Qwen3-VL-4B · RK3588"}]
    currentIndex: taskMode === "text" ? panel.textModel === "qwen35" ? 0 : 1 : panel.visionModel === "qwen25vl" ? 0 : 1
    onActivated: { if (taskMode === "text") panel.textModel = currentValue; else panel.visionModel = currentValue }
}
