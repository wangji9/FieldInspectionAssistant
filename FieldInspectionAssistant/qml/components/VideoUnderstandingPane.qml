import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FieldInspection
import "../theme"

ColumnLayout {
    id: pane
    spacing: 5
    readonly property var service: App.videoUnderstanding
    readonly property bool permitted: !!App.session.permissions.inspect
    property var devices: []

    function refreshDevices() {
        const previous = device.currentText
        devices = App.visionLive.cameras()
        const usb = devices.findIndex(name => name === "/dev/video1" || name.indexOf("/dev/video1 ") === 0)
        const selected = devices.indexOf(previous)
        device.currentIndex = selected >= 0 ? selected : usb >= 0 ? usb : devices.length ? 0 : -1
    }
    onVisibleChanged: if (visible) refreshDevices(); else service.stop()

    RowLayout {
        Layout.fillWidth: true
        spacing: 5
        AppComboBox {
            id: device
            objectName: "videoUnderstandingCamera"
            Layout.preferredWidth: 140
            Layout.preferredHeight: 28
            model: pane.devices
            enabled: !pane.service.running
            displayText: currentIndex >= 0 ? currentText : "无摄像头"
        }
        TouchButton {
            text: "扫描"; autoWidth: true; height: 28
            enabled: !pane.service.running
            onClicked: pane.refreshDevices()
        }
        AppComboBox {
            id: modelChoice
            objectName: "videoUnderstandingModel"
            Layout.preferredWidth: 128; Layout.preferredHeight: 28
            model: ["Qwen3-VL-2B", "Qwen3-VL-4B"]
            currentIndex: 1
            enabled: !pane.service.running
        }
        TouchButton {
            objectName: "videoUnderstandingStart"
            text: "开始理解"; variant: "primary"; autoWidth: true; height: 28
            enabled: pane.permitted && device.currentIndex >= 0 && !pane.service.running
                && !App.camera.closing && !App.camera.recording && !App.ai.busy && !App.agent.busy && !App.visionLive.running
            onClicked: if (App.voiceAssistant.suspendForActivity())
                pane.service.start(device.currentText, modelChoice.currentIndex === 1 ? "qwen3_video_4b" : "qwen3_video")
        }
        TouchButton {
            objectName: "videoUnderstandingStop"
            text: "停止"; autoWidth: true; height: 28
            enabled: pane.service.running
            onClicked: pane.service.stop()
        }
        Text {
            Layout.fillWidth: true
            text: pane.service.status
            font.pixelSize: Typography.small
            color: Colors.textSecondary
            elide: Text.ElideRight
        }
    }

    RowLayout {
        Layout.fillWidth: true; Layout.fillHeight: true
        spacing: 6

        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.preferredWidth: pane.width * 0.63
            Layout.minimumWidth: 160
            spacing: 0
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 29
                color: "#223443"
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    text: "摄像头实时画面"
                    color: "#F3F6FA"; font.pixelSize: Typography.small
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    visible: pane.service.running && !!App.camera.metrics.preview_fps
                    text: visible ? Number(App.camera.metrics.preview_fps).toFixed(1) + " FPS" : ""
                    color: "#CCDAE6"; font.pixelSize: Typography.small
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: "#16222D"; clip: true
                CameraPreview { anchors.fill: parent; frame: pane.service.preview; fillCrop: false }
                Text {
                    anchors.centerIn: parent
                    visible: !pane.service.running || !App.camera.hasFrame
                    text: pane.service.running ? "等待摄像头画面" : "摄像头预览"
                    color: "#CBD8E3"; font.pixelSize: Typography.body
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.preferredWidth: pane.width * 0.37
            Layout.minimumWidth: 218
            spacing: 0
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 29
                color: "#223443"
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    text: "事件时间线"; color: "#F3F6FA"; font.pixelSize: Typography.small
                }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    text: modelChoice.currentText + " / RK1828"; color: "#CCDAE6"; font.pixelSize: Typography.small
                }
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 29
                color: Colors.panelAlt; border.color: Colors.border
                Text {
                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                    verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                    text: "三帧对比 · 采集时间" + (pane.service.delayMs ? "    出字延迟 " + (pane.service.delayMs / 1000).toFixed(2) + " s" : "")
                    color: Colors.textSecondary; font.pixelSize: Typography.small
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                color: Colors.panel; border.color: Colors.border
                ListView {
                    id: timeline
                    objectName: "videoUnderstandingTimeline"
                    anchors.fill: parent; anchors.margins: 1; clip: true
                    model: pane.service.events
                    property bool follow: true
                    onMovementStarted: follow = false
                    onMovementEnded: follow = atYEnd
                    onModelChanged: if (follow) Qt.callLater(positionViewAtEnd)
                    ScrollBar.vertical: ScrollBar {
                        onPressedChanged: timeline.follow = !pressed && timeline.atYEnd
                    }
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        width: timeline.width - 14
                        height: details.implicitHeight + 14
                        color: modelData.event ? "#EDF4FA" : index % 2 ? Colors.panelAlt : Colors.panel
                        ColumnLayout {
                            id: details
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 7; spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: (modelData.event ? "事件  " : "场景  ") + modelData.start
                                    + (modelData.end !== modelData.start ? " – " + modelData.end : "")
                                color: Colors.info; font.pixelSize: Typography.small; wrapMode: Text.Wrap
                            }
                            Text {
                                Layout.fillWidth: true; text: modelData.text; textFormat: Text.PlainText
                                color: Colors.text; font.pixelSize: Typography.body; wrapMode: Text.Wrap
                            }
                        }
                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: "#DFE7EE" }
                    }
                }
                Text {
                    anchors.fill: parent; anchors.margins: 12
                    visible: timeline.count === 0
                    text: pane.service.running ? "等待第一个片段的理解结果" : "开始理解后，这里会按采集时间显示画面和事件描述。"
                    wrapMode: Text.Wrap; color: Colors.textSecondary; font.pixelSize: Typography.body
                }
            }
        }
    }
}
