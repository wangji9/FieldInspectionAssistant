import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FieldInspection
import "../theme"

Item {
    id: pane
    objectName: "acousticImagingPane"
    readonly property var service: App.acousticImaging
    readonly property var observation: service.observation
    readonly property var parameters: service.parameters
    readonly property bool hasLocation: observation.state === "localized"
    readonly property bool cameraFresh: App.camera.hasFrame && observation.aligned === true
    readonly property bool overlayVisible: hasLocation && (cameraFresh || !App.camera.hasFrame)
    readonly property var fields: [
        {id:"lower_hz",name:"频带下限 (Hz)"}, {id:"upper_hz",name:"频带上限 (Hz)"},
        {id:"gate_dbfs",name:"最低电平 (dBFS)"}, {id:"smoothing",name:"时序平滑"},
        {id:"temperature",name:"空气温度 (°C)"}, {id:"opacity",name:"热图透明度"},
        {id:"radius_mm",name:"阵列半径 (mm)"}, {id:"channel_one_deg",name:"阵列起点角 (°)"},
        {id:"horizontal_fov",name:"水平视场 (°)"}, {id:"vertical_fov",name:"垂直视场 (°)"},
        {id:"yaw",name:"镜头偏航 (°)"}, {id:"pitch",name:"镜头俯仰 (°)"},
        {id:"roll",name:"镜头滚转 (°)"}, {id:"speaker_threshold",name:"声纹阈值"}
    ]

    function showSettings() {
        for (let i=0; i<fields.length; ++i)
            parameterFields.itemAt(i).entry.text = String(parameters[fields[i].id])
        orderField.text = parameters.channel_order
        clockwise.checked = parameters.clockwise
        identify.checked = parameters.speaker
        calibrated.checked = parameters.calibrated
        settingsMessage.text = ""
        settings.open()
    }
    function applySettings() {
        const values = {channel_order:orderField.text, clockwise:clockwise.checked, speaker:identify.checked,
                        calibrated:calibrated.checked}
        for (let i=0;i<fields.length;++i) {
            const text = parameterFields.itemAt(i).entry.text.trim()
            if (!text.length || !Number.isFinite(Number(text))) {
                settingsMessage.text = "请填写" + fields[i].name
                return
            }
            values[fields[i].id] = Number(text)
        }
        if (service.configure(values))
            settings.close()
        else
            settingsMessage.text = service.status
    }
    function measured(value, decimals, suffix) {
        return Number.isFinite(Number(value)) ? Number(value).toFixed(decimals) + suffix : "—"
    }

    onVisibleChanged: {
        if (visible && App.session.loggedIn)
            service.refresh()
        else if (service.running)
            service.stop()
    }
    Connections {
        target: App.session
        function onSessionEnding() { settings.close() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            Label { text: "摄像头" }
            AppComboBox {
                id: cameraChoice
                objectName: "acousticCameraChoice"
                Layout.preferredWidth: 128
                model: pane.service.cameras
                enabled: !pane.service.running && !App.evidence.cameraConnected
                displayText: currentIndex >= 0 ? currentText : "未检测到"
            }
            TouchButton { text: "刷新设备"; height: 30; autoWidth: true; enabled: !pane.service.running; onClicked: pane.service.refresh() }
            TouchButton {
                objectName: "startAcousticImaging"
                text: pane.service.running ? "停止并保存录音" : "开始成像"
                autoWidth: true; height: 30; variant: pane.service.running ? "danger" : "primary"
                enabled: App.session.permissions.inspect
                onClicked: {
                    if (pane.service.running)
                        pane.service.stop()
                    else
                        pane.service.start(cameraChoice.currentIndex >= 0 ? cameraChoice.currentText : "")
                }
            }
            TouchButton { text: "阵列和声学参数…"; height: 30; autoWidth: true; onClicked: pane.showSettings() }
            Item { Layout.fillWidth: true }
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignRight
                text: pane.service.status
                color: Colors.textSecondary; elide: Text.ElideRight
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 6
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                spacing: 5
                Rectangle {
                    color: "#eff3f6"; border.color: Colors.border
                    Layout.fillWidth: true; Layout.preferredHeight: 28
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 5
                        Label { text: "视频与声场"; font.bold: true }
                        Label {
                            Layout.fillWidth: true
                            text: pane.parameters.calibrated ? "已配置机位参数" : "参考几何 · 待现场标定"
                            color: Colors.textSecondary; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight
                        }
                    }
                }
                Rectangle {
                    id: viewport
                    objectName: "acousticVideoViewport"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.minimumHeight: 150
                    color: "#182734"; clip: true
                    readonly property rect overlayRect: App.camera.hasFrame ? camera.contentRect
                        : Qt.rect(0,0,width,height)
                    CameraPreview {
                        id: camera
                        anchors.fill: parent
                        fillCrop: false
                        visible: App.camera.hasFrame
                        Connections {
                            target: App.camera
                            enabled: pane.visible
                            function onFrameReady(frame) { camera.frame = frame }
                        }
                    }
                    Image {
                        x: viewport.overlayRect.x; y: viewport.overlayRect.y
                        width: viewport.overlayRect.width; height: viewport.overlayRect.height
                        source: pane.overlayVisible ? pane.service.heatmap : ""
                        cache: false; smooth: true; fillMode: Image.Stretch
                        opacity: Number(pane.parameters.opacity)
                    }
                    Item {
                        visible: pane.overlayVisible
                        x: viewport.overlayRect.x + Number(pane.observation.x || 0)*viewport.overlayRect.width
                        y: viewport.overlayRect.y + Number(pane.observation.y || 0)*viewport.overlayRect.height
                        Rectangle {
                            anchors.centerIn: parent; width: 26; height: 26; radius: 13
                            color: "transparent"; border.color: "#fbfbf6"; border.width: 2
                        }
                        Rectangle { anchors.centerIn: parent; width: 14; height: 2; color: "#fbfbf6" }
                        Rectangle { anchors.centerIn: parent; width: 2; height: 14; color: "#fbfbf6" }
                        Rectangle {
                            x: Math.min(17,viewport.width-parent.x-width-4)
                            y: parent.y > 37 ? -35 : 18
                            width: marker.implicitWidth+16; height: 26
                            color: "#e6213544"; border.color: "#9eb2c3"
                            Label {
                                id: marker; anchors.centerIn: parent
                                color: "white"; font.bold: true
                                text: pane.observation.speaker_name
                                    ? pane.observation.speaker_name+"  "+pane.observation.speaker_score
                                    : pane.measured(pane.observation.azimuth_deg,1,"°")
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: !App.camera.hasFrame && !pane.overlayVisible
                        text: pane.service.running ? "等待摄像头和六路采样" : "开始成像后显示实时画面"
                        color: "#dce6ed"
                    }
                    Label {
                        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: 8
                        visible: pane.service.running && !pane.cameraFresh && pane.hasLocation
                        text: App.camera.hasFrame ? "音画时间差过大" : "角度图 · 摄像头未连接"
                        color: "white"; padding: 5
                        background: Rectangle { color: "#d9203543" }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 8
                    Label { text: "归一化空间响应" }
                    Label { text: "0"; color: Colors.textSecondary }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 10
                        border.color: Colors.border
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "#262c76" }
                            GradientStop { position: .2; color: "#2567a5" }
                            GradientStop { position: .4; color: "#2598b0" }
                            GradientStop { position: .6; color: "#3ab092" }
                            GradientStop { position: .8; color: "#adbf4f" }
                            GradientStop { position: 1; color: "#f6d037" }
                        }
                    }
                    Label { text: "1"; color: Colors.textSecondary }
                }
            }
            ScrollView {
                id: details
                Layout.preferredWidth: Math.min(335, Math.max(225, pane.width*0.28))
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ColumnLayout {
                    width: details.availableWidth
                    spacing: 6
                    GroupBox {
                        title: "声源观测"
                        Layout.fillWidth: true
                        GridLayout {
                            columns: 2; width: parent.width
                            columnSpacing: 7; rowSpacing: 5
                            Label { text: "方位 / 俯仰" }
                            Label { Layout.fillWidth: true; text: pane.measured(pane.observation.azimuth_deg,1,"°") + " / " + pane.measured(pane.observation.elevation_deg,1,"°") }
                            Label { text: "六路平均电平" }
                            Label { Layout.fillWidth: true; text: pane.measured(pane.observation.level_dbfs,1," dBFS") }
                            Label { text: "峰值响应 / 对比" }
                            Label { Layout.fillWidth: true; text: pane.measured(pane.observation.response,2,"") + " / " + pane.measured(pane.observation.contrast,2,"") }
                            Label { text: "计算耗时" }
                            Label { Layout.fillWidth: true; text: pane.measured(pane.observation.processing_ms,1," ms") }
                            Label { text: "音画时间差" }
                            Label { Layout.fillWidth: true; text: pane.measured(pane.observation.skew_ms,0," ms") }
                            Label {
                                Layout.columnSpan: 2; Layout.fillWidth: true; elide: Text.ElideMiddle
                                text: pane.observation.timestamp || "等待采样时间"
                                color: Colors.textSecondary
                            }
                        }
                    }
                    GroupBox {
                        title: "阵列与通道"
                        Layout.fillWidth: true
                        ColumnLayout {
                            width: parent.width
                            MicrophoneArrayView {
                                objectName: "microphoneArrayView"
                                Layout.fillWidth: true; Layout.preferredHeight: 145
                                positions: pane.parameters.positions || []
                                levels: pane.observation.channels_dbfs || []
                                observation: pane.observation
                            }
                            Repeater {
                                model: 6
                                RowLayout {
                                    required property int index
                                    readonly property var level: (pane.observation.channels_dbfs || [])[index]
                                    Layout.fillWidth: true; spacing: 5
                                    Label { text: "CH " + (index+1) }
                                    ProgressBar { from: -100; to: 0; value: level === undefined ? -100 : level; Layout.fillWidth: true; Layout.preferredHeight: 7 }
                                    Label { text: pane.measured(level,1,""); horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 36 }
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "%1 mm 半径　%2–%3 Hz".arg(pane.parameters.radius_mm).arg(pane.parameters.lower_hz).arg(pane.parameters.upper_hz)
                                color: Colors.textSecondary
                            }
                        }
                    }
                    GroupBox {
                        title: "声纹关联"
                        Layout.fillWidth: true
                        ColumnLayout {
                            width: parent.width
                            Label {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: pane.observation.speaker_name || pane.observation.speaker_state ||
                                    (pane.parameters.speaker ? "等待稳定发言" : "未启用")
                                font.bold: !!pane.observation.speaker_name
                            }
                            Label {
                                Layout.fillWidth: true; wrapMode: Text.Wrap
                                visible: !!pane.observation.speaker_name
                                text: "余弦 %1　录音 %2–%3 s".arg(pane.observation.speaker_score || "—")
                                    .arg((Number(pane.observation.speaker_from_ms || 0)/1000).toFixed(2))
                                    .arg((Number(pane.observation.speaker_to_ms || 0)/1000).toFixed(2))
                                color: Colors.textSecondary
                            }
                        }
                    }
                }
            }
        }
    }

    AssistantDialog {
        id: settings
        title: "阵列和声学参数"
        anchors.centerIn: parent
        width: Math.min(735,pane.width-12)
        height: Math.min(545,pane.height-12)
        contentItem: ScrollView {
            id: configScroll
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: configScroll.availableWidth; spacing: 7
                GridLayout {
                    Layout.fillWidth: true
                    columns: width > 525 ? 2 : 1
                    columnSpacing: 14; rowSpacing: 6
                    Repeater {
                        id: parameterFields
                        model: pane.fields
                        RowLayout {
                            required property var modelData
                            property alias entry: parameterValue
                            Layout.fillWidth: true
                            Label { text: modelData.name; Layout.preferredWidth: 130 }
                            TextField {
                                id: parameterValue
                                Layout.fillWidth: true
                                selectByMouse: true
                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "环上通道顺序"; Layout.preferredWidth: 130 }
                    TextField { id: orderField; Layout.fillWidth: true; placeholderText: "1,2,3,4,5,6" }
                    CheckBox { id: clockwise; text: "顺时针" }
                }
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox { id: identify; text: "关联声纹档案" }
                    CheckBox { id: calibrated; text: "机位已完成现场标定" }
                }
                Label {
                    Layout.fillWidth: true
                    text: "在画面左、右各放一个固定声源，核对通道顺序、视场与偏移后保存。"
                    color: Colors.textSecondary; wrapMode: Text.Wrap
                }
                Label {
                    id: settingsMessage; Layout.fillWidth: true
                    color: Colors.danger; wrapMode: Text.Wrap
                }
            }
        }
        footer: Item {
            implicitHeight: 43
            Row {
                anchors.right: parent.right; anchors.rightMargin: 12; anchors.verticalCenter: parent.verticalCenter
                spacing: 5
                TouchButton { text: "取消"; autoWidth: true; height: 30; onClicked: settings.close() }
                TouchButton { text: "应用参数"; autoWidth: true; height: 30; variant: "primary"; onClicked: pane.applySettings() }
            }
        }
    }
}
