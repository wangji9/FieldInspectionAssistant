import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Item {
    id: root
    objectName: "settingsHardwarePane"

    readonly property var status: App.hardware.snapshot
    readonly property var cameras: status.cameras || []
    property string selectedCamera: ""
    property string cameraMessage: ""
    property bool performanceOnly: false

    function openPerformance() {
        performanceOnly = true
    }

    function openConnections() {
        performanceOnly = false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Colors.panelAlt
            border.color: Colors.border
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                Label {
                    objectName: "runtimeUpdatedAt"
                    Layout.fillWidth: true
                    text: App.hardware.error || (root.status.timestamp
                        ? "更新于 " + Qt.formatDateTime(new Date(root.status.timestamp), "HH:mm:ss")
                        : "正在读取设备状态…")
                    color: App.hardware.error ? Colors.warning : Colors.textSecondary
                    elide: Text.ElideRight
                }
                TouchButton {
                    objectName: "settingsConnectionsButton"
                    text: "外设"; autoWidth: true; height: 27
                    visible: panes.narrow
                    variant: root.performanceOnly ? "secondary" : "tonal"
                    onClicked: root.openConnections()
                }
                TouchButton {
                    objectName: "settingsPerformanceButton"
                    text: "性能"; autoWidth: true; height: 27
                    visible: panes.narrow
                    variant: root.performanceOnly ? "tonal" : "secondary"
                    onClicked: root.openPerformance()
                }
                TouchButton {
                    objectName: "refreshHardwareDevices"
                    text: App.hardware.busy ? "更新中…" : "刷新状态"
                    autoWidth: true
                    height: 27
                    enabled: !App.hardware.busy
                    onClicked: {
                        App.hardware.refresh()
                        App.ai.refreshStatus()
                        App.visionLive.refreshHardware()
                    }
                }
            }
        }

        Item {
            id: panes
            objectName: "settingsDevicePanes"
            Layout.fillWidth: true
            Layout.fillHeight: true
            readonly property bool narrow: width < 730

            SettingsPanel {
                id: connections
                title: "外设连接"
                visible: !panes.narrow || !root.performanceOnly
                width: panes.narrow ? panes.width : (panes.width - Dimensions.spacing) / 2
                height: panes.height

                ScrollView {
                    id: deviceScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    Column {
                        width: deviceScroll.availableWidth
                        spacing: 6
                        Label { text: "摄像头"; font.bold: true }
                        AppComboBox {
                            id: camera
                            objectName: "hardwareCameraChoice"
                            width: parent.width
                            model: root.cameras
                            textRole: "name"
                            valueRole: "path"
                            currentIndex: root.cameras.length ? Math.max(0, root.cameras.findIndex(item => item.path === root.selectedCamera)) : -1
                            displayText: currentIndex >= 0 ? currentText + " · " + currentValue : "未检测到摄像头"
                            onActivated: root.selectedCamera = currentValue
                        }
                        Row {
                            spacing: 6
                            TouchButton {
                                text: "连接"; autoWidth: true; variant: "primary"
                                enabled: camera.currentIndex >= 0 && !App.camera.opening && !App.evidence.cameraConnected
                                onClicked: {
                                    root.cameraMessage = App.evidence.openCamera(camera.currentValue) ? "" : App.camera.status
                                }
                            }
                            TouchButton {
                                text: "断开"; autoWidth: true
                                enabled: App.evidence.cameraConnected
                                onClicked: App.evidence.closeCamera()
                            }
                        }
                        Label {
                            width: parent.width
                            text: App.camera.status || root.cameraMessage || (camera.currentIndex < 0 ? ""
                                : root.cameras[camera.currentIndex].accessible ? "设备可访问 · 未打开" : "设备已连接 · 权限不足")
                            color: Colors.textSecondary
                            wrapMode: Text.Wrap
                            visible: !!text
                        }

                        Label {
                            width: parent.width
                            visible: App.camera.hasFrame
                            text: App.camera.resolution + " · " + Number(App.camera.metrics.preview_fps || 0).toFixed(1) + " FPS"
                            color: Colors.textSecondary
                            wrapMode: Text.Wrap
                        }
                        Rectangle { width: parent.width; height: 1; color: Colors.border }
                        RowLayout {
                            width: parent.width
                            Label { text: "麦克风"; font.bold: true; Layout.fillWidth: true }
                            TouchButton {
                                objectName: "openMicrophoneSettings"
                                text: "采集设置"; autoWidth: true; height: 27
                                onClicked: App.nav.push("analysis", {analysis_tab:0, open_capture_settings:true})
                            }
                        }
                        Label {
                            width: parent.width
                            visible: App.analysis.running
                            text: "采集中 · " + (App.analysis.summary.rate || "—") + " Hz · "
                                + (App.analysis.summary.channels || "—") + " 通道"
                            color: Colors.success
                            wrapMode: Text.Wrap
                        }
                        Label {
                            width: parent.width
                            text: root.status.audio_note || ""
                            visible: !!text
                            color: Colors.warning
                            wrapMode: Text.Wrap
                        }
                        Repeater {
                            model: root.status.microphones || []
                            Column {
                                required property var modelData
                                width: deviceScroll.availableWidth
                                spacing: 2
                                Label { width: parent.width; text: modelData.name; wrapMode: Text.Wrap }
                                Label {
                                    width: parent.width
                                    text: modelData.state
                                    color: modelData.accessible && !modelData.muted ? Colors.success : Colors.warning
                                    wrapMode: Text.Wrap
                                }
                                Label { width: parent.width; text: modelData.detail; color: Colors.textSecondary; wrapMode: Text.WrapAnywhere }
                            }
                        }
                        Label {
                            visible: !(root.status.microphones || []).length
                            width: parent.width
                            text: root.status.audio_service === false ? "音频服务查询失败" : "未检测到麦克风"
                            color: Colors.textSecondary
                            wrapMode: Text.Wrap
                        }

                        Rectangle { width: parent.width; height: 1; color: Colors.border }
                        Label { text: "音频输出"; font.bold: true }
                        Repeater {
                            model: root.status.speakers || []
                            Column {
                                required property var modelData
                                width: deviceScroll.availableWidth
                                spacing: 2
                                Label { width: parent.width; text: modelData.name + (modelData.default ? " · 默认输出" : ""); wrapMode: Text.Wrap }
                                Label { width: parent.width; text: modelData.state; color: modelData.muted ? Colors.warning : Colors.textSecondary; wrapMode: Text.Wrap }
                                Label { width: parent.width; text: modelData.detail; color: Colors.textSecondary; wrapMode: Text.Wrap }
                            }
                        }
                        Label {
                            visible: !(root.status.speakers || []).length
                            width: parent.width
                            text: root.status.audio_service === false ? "音频服务查询失败" : "未检测到音频输出设备"
                            color: Colors.textSecondary
                            wrapMode: Text.Wrap
                        }

                        Rectangle { width: parent.width; height: 1; color: Colors.border }
                        Label { text: "网络接口"; font.bold: true }
                        Repeater {
                            model: root.status.networks || []
                            Label {
                                required property var modelData
                                width: deviceScroll.availableWidth
                                text: modelData.name + " · " + (modelData.state === "up" ? "已连接" : modelData.state === "down" ? "未连接" : modelData.state)
                                    + " · " + (modelData.address || "无 IPv4 地址")
                                color: Colors.textSecondary
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }

            RuntimeStatusPanel {
                id: performance
                objectName: "devicePerformancePanel"
                visible: !panes.narrow || root.performanceOnly
                x: panes.narrow ? 0 : connections.width + Dimensions.spacing
                width: connections.width
                height: panes.height
            }
        }
    }
}
