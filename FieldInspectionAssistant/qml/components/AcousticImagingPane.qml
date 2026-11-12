import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
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
    readonly property bool cameraCalibrated: parameters.calibrated && App.camera.hasFrame
    readonly property var fields: [
        {id:"gate_dbfs",name:"最低电平 (dBFS)"}, {id:"smoothing",name:"时序平滑"},
        {id:"lower_hz",name:"SRP 频带下限 (Hz)",srp:true}, {id:"upper_hz",name:"SRP 频带上限 (Hz)",srp:true},
        {id:"temperature",name:"空气温度 (°C)"}, {id:"opacity",name:"热图透明度"},
        {id:"radius_mm",name:"阵列半径 (mm)"}, {id:"channel_one_deg",name:"阵列起点角 (°)"},
        {id:"horizontal_fov",name:"水平视场 (°)"}, {id:"vertical_fov",name:"垂直视场 (°)"},
        {id:"yaw",name:"镜头偏航 (°)"}, {id:"pitch",name:"镜头俯仰 (°)"},
        {id:"roll",name:"镜头滚转 (°)"}, {id:"speaker_threshold",name:"声纹阈值"},
        {id:"camera_x_mm",name:"镜头左右偏移 (mm)",mount:true},
        {id:"camera_y_mm",name:"镜头上下偏移 (mm)",mount:true},
        {id:"camera_z_mm",name:"镜头前后偏移 (mm)",mount:true},
        {id:"focus_depth_m",name:"参考工作距离 (m)",mount:true}
    ]

    function showSettings() {
        for (let i=0; i<fields.length; ++i)
            parameterFields.itemAt(i).entry.text = String(parameters[fields[i].id])
        orderField.text = parameters.channel_order
        clockwise.checked = parameters.clockwise
        identify.checked = parameters.speaker
        calibrated.checked = parameters.calibrated
        fixedMount.checked = parameters.fixed_mount
        settingsMessage.text = ""
        settings.open()
    }
    function applyPhotoPreset() {
        const radiusEntry = parameterFields.itemAt(fields.findIndex(field => field.id === "radius_mm")).entry
        const radius = Number(radiusEntry.text)
        const values = {camera_x_mm: 0, camera_z_mm: 0, focus_depth_m: 1,
                        camera_y_mm: ((radius > 0 ? radius : parameters.radius_mm) * parameters.photo_vertical_offset_ratio).toFixed(1)}
        for (let i = 0; i < fields.length; ++i)
            if (fields[i].mount)
                parameterFields.itemAt(i).entry.text = String(values[fields[i].id])
        fixedMount.checked = true
        calibrated.checked = false
    }
    function applySettings() {
        const values = {channel_order:orderField.text, clockwise:clockwise.checked, speaker:identify.checked,
                        calibrated:calibrated.checked, fixed_mount:fixedMount.checked}
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
        return typeof value === "number" && Number.isFinite(value) ? value.toFixed(decimals) + suffix : "—"
    }


    readonly property color panelColor: "#eeeeee"
    readonly property color lineColor: "#d4d4d4"
    readonly property bool canInspect: App.session.permissions.inspect
    readonly property bool cameraBusy: App.camera.opening || App.camera.closing
    readonly property bool cameraInUse: App.evidence.cameraConnected || cameraBusy
    property bool mirrorPreview: false
    property var logLines: []
    property string lastImagingStatus: ""

    function appendLog(message) {
        if (message)
            logLines = logLines.slice(-79).concat("[" + Qt.formatTime(new Date(), "hh:mm:ss.zzz") + "] " + message)
    }

    function logImagingStatus() {
        if (lastImagingStatus === service.status)
            return
        lastImagingStatus = service.status
        if (!observation.state)
            appendLog(lastImagingStatus)
    }

    function refreshCameras() {
        service.refresh()
        appendLog(service.cameras.length ? "已检测到 " + service.cameras.length + " 个摄像头" : "未检测到摄像头")
    }

    function updateParameter(key, value) {
        if (parameters[key] === value)
            return
        const values = {}
        values[key] = value
        service.configure(values)
    }

    onVisibleChanged: {
        if (visible && App.session.loggedIn) {
            if (canInspect)
                refreshCameras()
            logImagingStatus()
        } else {
            service.leave()
            if (camera)
                camera.clearFrame()
        }
    }

    Connections {
        target: pane.service
        enabled: pane.visible
        function onChanged() { pane.logImagingStatus() }
    }

    Connections {
        target: App.camera
        enabled: pane.visible
        function onStatusChanged() { pane.appendLog(App.camera.status) }
    }

    Connections {
        target: App.analysis
        enabled: pane.visible
        function onSaved(path) { pane.appendLog("录音已保存：" + path) }
    }

    Connections {
        target: App.session
        function onSessionEnding() {
            settings.close()
            pane.logLines = []
            pane.lastImagingStatus = ""
            pane.mirrorPreview = false
        }
    }

    component PanelLabel: Label {
        color: "#171717"
        font.pixelSize: 12
    }

    component PanelButton: Basic.Button {
        implicitHeight: 23
        leftPadding: 7
        rightPadding: 7
        topPadding: 2
        bottomPadding: 2
        font.pixelSize: 12
        contentItem: Text {
            text: parent.text
            font: parent.font
            color: parent.enabled ? "#151515" : "#808080"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: !parent.enabled ? "#e4e4e4" : parent.down ? "#c9c9c9" : parent.hovered ? "#eaeaea" : "#dfdfdf"
            border.color: parent.activeFocus ? "#557da2" : "#aaaaaa"
        }
    }

    component PanelCombo: Basic.ComboBox {
        id: combo
        implicitHeight: 23
        padding: 4
        font.pixelSize: 12
        contentItem: Text {
            text: combo.displayText
            font: combo.font
            color: combo.enabled ? "#171717" : "#777777"
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: combo.width - width - 5
            y: (combo.height - height) / 2
            text: "▾"
            font.pixelSize: 12
            color: combo.enabled ? "#333333" : "#888888"
        }
        background: Rectangle {
            color: "#e1e1e1"
            border.color: parent.activeFocus ? "#557da2" : "#aaaaaa"
        }
    }

    component PanelNumber: Basic.SpinBox {
        id: numberField
        implicitHeight: 24
        font.pixelSize: 12
        editable: true
        leftPadding: 4
        rightPadding: 22
        Layout.fillWidth: true
        Layout.minimumWidth: 78
        Layout.preferredWidth: 114
        background: Rectangle { color: "#f6f6f6"; border.color: "#bdbdbd" }
        up.indicator: Rectangle {
            x: numberField.width - width - 1
            y: 1
            width: 19; height: (numberField.height - 2) / 2
            color: numberField.up.pressed ? "#cdcdcd" : "#e7e7e7"
            Text { anchors.centerIn: parent; text: "▴"; font.pixelSize: 10; color: parent.enabled ? "#333333" : "#888888" }
        }
        down.indicator: Rectangle {
            x: numberField.width - width - 1
            y: numberField.height / 2
            width: 19; height: (numberField.height - 2) / 2
            color: numberField.down.pressed ? "#cdcdcd" : "#e7e7e7"
            Text { anchors.centerIn: parent; text: "▾"; font.pixelSize: 10; color: parent.enabled ? "#333333" : "#888888" }
        }
    }

    component PanelSliderTrack: Item {
        property real start: 0
        property real end: 1
        Rectangle {
            width: parent.width
            height: 4
            y: 7
            color: "#e1e1e1"
            border.color: "#c9c9c9"
        }
        Rectangle {
            x: parent.width * parent.start
            width: parent.width * (parent.end - parent.start)
            height: 4
            y: 7
            color: "#bcbcbc"
        }
        Repeater {
            model: 31
            Rectangle {
                required property int index
                x: Math.round(parent.width * index / 30)
                y: 16
                width: 1; height: index % 5 === 0 ? 5 : 3
                color: "#b2b2b2"
            }
        }
    }

    component PanelSliderThumb: Rectangle {
        property bool pressed: false
        implicitWidth: 12
        implicitHeight: 17
        y: 1
        color: pressed ? "#ababab" : "#cbcbcb"
        border.color: "#b6b6b6"
    }

    component FieldGroup: Basic.GroupBox {
        leftPadding: 8
        rightPadding: 8
        topPadding: 17
        bottomPadding: 7
        font.pixelSize: 12
        label: PanelLabel {
            x: 8
            text: parent.title
            leftPadding: 3
            rightPadding: 3
            background: Rectangle { color: pane.panelColor }
        }
        background: Rectangle {
            y: 8
            height: parent.height - y
            color: "transparent"
            border.color: pane.lineColor
        }
    }

    Rectangle {
        anchors.fill: parent
        color: pane.panelColor
    }

    ScrollView {
        id: pageScroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            id: dashboard
            width: pageScroll.availableWidth
            height: stacked ? logPanel.y + logPanel.height + padding : desktopHeight
            readonly property bool stacked: width < 800
            readonly property int padding: 3
            readonly property int gutter: stacked ? 4 : 8
            readonly property real desktopHeight: Math.max(controls.implicitHeight + viewport.y + padding,
                                                           pageScroll.availableHeight)
            readonly property real innerWidth: Math.max(0, width - 2 * padding)
            readonly property real leftWidth: stacked ? innerWidth : Math.floor((innerWidth - gutter) * 0.526)
            readonly property real rightWidth: stacked ? innerWidth : innerWidth - gutter - leftWidth
            readonly property real rightX: stacked ? padding : padding + leftWidth + gutter

            ColumnLayout {
                id: controls
                x: dashboard.padding
                y: dashboard.stacked ? viewport.y + viewport.height + dashboard.gutter : viewport.y
                width: dashboard.leftWidth
                height: dashboard.stacked ? implicitHeight : dashboard.desktopHeight - y - dashboard.padding
                spacing: 3

                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 610 ? 2 : 1
                    columnSpacing: 5
                    rowSpacing: 3

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        PanelButton {
                            objectName: "startAcousticImaging"
                            text: pane.service.running ? "停止并保存" : "启动系统"
                            enabled: pane.canInspect
                            onClicked: {
                                if (pane.service.running)
                                    pane.service.stop()
                                else
                                    pane.service.start(cameraChoice.currentIndex >= 0 ? cameraChoice.currentText : "")
                            }
                        }
                        PanelCombo {
                            objectName: "acousticAlgorithmChoice"
                            Layout.preferredWidth: 138
                            model: ["单频波束形成", "时域波束形成", "宽带 SRP-PHAT", "单频 MVDR"]
                            currentIndex: pane.parameters.algorithm
                            enabled: pane.canInspect
                            onActivated: pane.updateParameter("algorithm", currentIndex)
                        }
                        PanelButton {
                            text: "阵列和声学参数"
                            enabled: pane.canInspect
                            onClicked: pane.showSettings()
                        }
                        PanelLabel {
                            Layout.fillWidth: true
                            text: "状态：" + pane.service.status
                            elide: Text.ElideRight
                            ToolTip.visible: statusHover.hovered && truncated
                            ToolTip.text: text
                            HoverHandler { id: statusHover }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        PanelButton {
                            text: "刷新摄像头"
                            enabled: pane.canInspect && !pane.cameraBusy
                            onClicked: pane.refreshCameras()
                        }
                        PanelCombo {
                            id: cameraChoice
                            objectName: "acousticCameraChoice"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 119
                            Layout.minimumWidth: 48
                            model: pane.service.cameras
                            enabled: pane.canInspect && !pane.cameraInUse
                            displayText: currentIndex >= 0 ? currentText : "未检测到"
                        }
                        PanelButton {
                            objectName: "toggleAcousticCamera"
                            text: App.camera.opening ? "打开中…" : App.camera.closing ? "关闭中…"
                                : pane.cameraInUse ? "关闭摄像头" : "打开摄像头"
                            enabled: pane.canInspect && !pane.cameraBusy && !App.camera.recording && !App.camera.finalizing
                                && (pane.cameraInUse ? pane.service.ownsPreview : cameraChoice.currentIndex >= 0)
                            onClicked: {
                                if (pane.cameraInUse)
                                    pane.service.closePreview()
                                else
                                    pane.service.openPreview(cameraChoice.currentText)
                            }
                        }
                    }
                }

                FieldGroup {
                    title: "采集设备"
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        spacing: 6
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 23
                            color: "#e1e1e1"
                            border.color: "#bdbdbd"
                            PanelLabel {
                                anchors.fill: parent
                                anchors.margins: 5
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                text: App.analysis.discovering ? "正在查找六麦阵列…" : pane.service.arrayAvailable
                                    ? "ListenGo 六麦原始输入 · 16 kHz" : "未检测到六麦阵列"
                            }
                        }
                        PanelButton {
                            text: "刷新"
                            enabled: pane.canInspect && !App.analysis.running && !App.analysis.discovering
                            onClicked: App.analysis.refreshDevices()
                        }
                    }
                }

                FieldGroup {
                    title: pane.parameters.target_frequency_enabled ? "目标频率控制" : "目标频率控制（当前算法禁用）"
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        spacing: 6
                        PanelLabel { text: "频率 (Hz)" }
                        Basic.Slider {
                            id: targetSlider
                            objectName: "acousticTargetSlider"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            from: pane.parameters.target_min_hz
                            to: pane.parameters.target_max_hz
                            stepSize: 1
                            snapMode: Slider.SnapAlways
                            value: pane.parameters.target_hz
                            enabled: pane.canInspect && pane.parameters.target_frequency_enabled
                            Accessible.name: "目标频率 (Hz)"
                            background: PanelSliderTrack {
                                x: targetSlider.leftPadding + 6
                                width: targetSlider.availableWidth - 12
                                end: targetSlider.visualPosition
                            }
                            handle: PanelSliderThumb {
                                x: targetSlider.leftPadding + targetSlider.visualPosition * (targetSlider.availableWidth - width)
                                pressed: targetSlider.pressed
                            }
                            onPressedChanged: { if (!pressed) pane.updateParameter("target_hz", Math.round(value)) }
                            onMoved: { if (!pressed) pane.updateParameter("target_hz", Math.round(value)) }
                        }
                        PanelNumber {
                            objectName: "acousticTargetFrequency"
                            Layout.fillWidth: false
                            from: pane.parameters.target_min_hz
                            to: pane.parameters.target_max_hz
                            value: pane.parameters.target_hz
                            enabled: pane.canInspect && pane.parameters.target_frequency_enabled
                            onValueModified: pane.updateParameter("target_hz", value)
                        }
                    }
                }

                FieldGroup {
                    title: "动态范围控制"
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        spacing: 6
                        PanelLabel { text: "范围 (dB)" }
                        Basic.Slider {
                            id: rangeSlider
                            objectName: "acousticDynamicRangeSlider"
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            from: 0.1
                            to: 30
                            stepSize: 0.1
                            snapMode: Slider.SnapAlways
                            value: pane.parameters.dynamic_range_db
                            enabled: pane.canInspect
                            Accessible.name: "动态范围 (dB)"
                            background: PanelSliderTrack {
                                x: rangeSlider.leftPadding + 6
                                width: rangeSlider.availableWidth - 12
                                end: rangeSlider.visualPosition
                            }
                            handle: PanelSliderThumb {
                                x: rangeSlider.leftPadding + rangeSlider.visualPosition * (rangeSlider.availableWidth - width)
                                pressed: rangeSlider.pressed
                            }
                            onMoved: { if (!pressed) pane.updateParameter("dynamic_range_db", Math.round(value * 10) / 10) }
                            onPressedChanged: { if (!pressed) pane.updateParameter("dynamic_range_db", Math.round(value * 10) / 10) }
                        }
                        PanelNumber {
                            objectName: "acousticDynamicRange"
                            Layout.fillWidth: false
                            from: 1
                            to: 300
                            value: Math.round(pane.parameters.dynamic_range_db * 10)
                            enabled: pane.canInspect
                            validator: DoubleValidator { bottom: 0.1; top: 30; decimals: 1; notation: DoubleValidator.StandardNotation }
                            textFromValue: (scaled, locale) => (scaled / 10).toLocaleString(locale, "f", 1)
                            valueFromText: (text, locale) => Math.round(Number.fromLocaleString(locale, text) * 10)
                            onValueModified: pane.updateParameter("dynamic_range_db", value / 10)
                        }
                    }
                }

                FieldGroup {
                    title: "绘图通道选择"
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        spacing: 6
                        PanelLabel { text: "显示视图" }
                        PanelCombo {
                            id: plotChannel
                            objectName: "acousticPlotChannel"
                            Layout.fillWidth: true
                            model: ["多通道概览", "通道 1", "通道 2", "通道 3", "通道 4", "通道 5", "通道 6"]
                        }
                    }
                }

                NativeHeatmap {
                    objectName: "acousticTimeChart"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 104
                    Layout.preferredHeight: 112
                    darkStyle: true
                    analysisSource: pane.service
                    analysisField: "time"
                    seriesIndex: plotChannel.currentIndex - 1
                    Accessible.name: "时域信号"
                }

                NativeHeatmap {
                    objectName: "acousticFrequencyChart"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 104
                    Layout.preferredHeight: 112
                    darkStyle: true
                    analysisSource: pane.service
                    analysisField: "frequency"
                    seriesIndex: plotChannel.currentIndex - 1
                    referenceX: pane.parameters.target_frequency_enabled ? pane.parameters.target_hz : NaN
                    manualY: true
                    yMinimum: -115
                    yMaximum: 0
                    Accessible.name: "频谱图"
                }
            }

            Rectangle {
                id: viewport
                objectName: "acousticVideoViewport"
                x: dashboard.rightX
                y: dashboard.padding
                width: dashboard.rightWidth
                height: dashboard.stacked ? Math.max(145, Math.round(width * 0.56))
                    : Math.min(dashboard.desktopHeight - y - 114, Math.round(dashboard.desktopHeight * 0.64))
                color: "#ffffff"
                clip: true
                readonly property bool mirrored: pane.mirrorPreview && App.camera.hasFrame

                Item {
                    anchors.fill: parent
                    transform: Scale {
                        origin.x: viewport.width / 2
                        xScale: viewport.mirrored ? -1 : 1
                    }
                    CameraPreview {
                        id: camera
                        anchors.fill: parent
                        fillCrop: true
                        overlayFrame: pane.service.heatmap
                        overlayOpacity: Number(pane.parameters.opacity)
                        Connections {
                            target: App.camera
                            enabled: pane.visible
                            function onFrameReady(frame) { camera.frame = frame }
                        }
                    }
                }

                PanelLabel {
                    anchors.centerIn: parent
                    visible: !App.camera.hasFrame && !pane.service.hasHeatmap
                    text: App.camera.opening ? "正在打开摄像头…" : pane.cameraInUse
                        ? "等待摄像头画面…" : "请选择并打开摄像头"
                    color: "#727272"
                }
            }

            FieldGroup {
                id: logPanel
                title: "系统日志"
                x: dashboard.rightX
                y: (dashboard.stacked ? controls.y + controls.height : viewport.y + viewport.height) + 4
                width: dashboard.rightWidth
                height: dashboard.stacked ? 145 : dashboard.desktopHeight - y - dashboard.padding
                contentItem: ColumnLayout {
                    spacing: 3
                    ScrollView {
                        id: logScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        clip: true
                        Basic.TextArea {
                            objectName: "acousticSystemLog"
                            text: pane.logLines.join("\n")
                            readOnly: true
                            selectByMouse: true
                            wrapMode: Text.WrapAnywhere
                            font.pixelSize: 11
                            font.family: "monospace"
                            color: "#151515"
                            padding: 5
                            background: Rectangle { color: "#ffffff"; border.color: "#929292" }
                            onTextChanged: Qt.callLater(function() {
                                const flick = logScroll.contentItem
                                flick.contentY = Math.max(0, flick.contentHeight - flick.height)
                            })
                        }
                    }
                    RowLayout {
                        spacing: 4
                        PanelLabel {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: pane.hasLocation
                                ? (pane.cameraCalibrated ? "镜头" : "阵列") + "方位 / 俯仰  "
                                  + pane.measured(pane.cameraCalibrated ? pane.observation.azimuth_deg : pane.observation.array_azimuth_deg, 1, "°")
                                  + " / " + pane.measured(pane.cameraCalibrated ? pane.observation.elevation_deg : pane.observation.array_elevation_deg, 1, "°")
                                  + "    音画差  " + pane.measured(pane.observation.skew_ms, 0, " ms")
                                : pane.service.running ? (!pane.parameters.target_frequency_enabled ? "阵列电平  " + pane.measured(pane.observation.level_dbfs, 1, " dBFS")
                                    : "目标频率电平  " + pane.measured(pane.observation.target_dbfs, 1, " dBFS"))
                                  : pane.parameters.calibrated ? "机位已标定" : "参考机位参数 · 待现场标定"
                        }
                        Basic.CheckBox {
                            id: mirrorControl
                            objectName: "acousticMirrorPreview"
                            Layout.preferredHeight: 19
                            padding: 2
                            text: "相机镜像反转"
                            font.pixelSize: 12
                            checked: pane.mirrorPreview
                            onToggled: pane.mirrorPreview = checked
                            indicator: Rectangle {
                                x: mirrorControl.leftPadding
                                y: (mirrorControl.height - height) / 2
                                width: 13; height: 13
                                color: "#f9f9f9"
                                border.color: mirrorControl.activeFocus ? "#557da2" : "#aaaaaa"
                                Text {
                                    anchors.centerIn: parent
                                    text: "✓"
                                    font.pixelSize: 11
                                    visible: mirrorControl.checked
                                }
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
                RowLayout {
                    Layout.fillWidth: true
                    CheckBox { id: fixedMount; objectName: "acousticFixedMount"; text: "固定环形机位" }
                    TouchButton { text: "采用照片初值"; autoWidth: true; height: 30; onClicked: pane.applyPhotoPreset() }
                }
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
                            visible: (!modelData.srp || pane.parameters.algorithm === 2) && (!modelData.mount || fixedMount.checked)
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
                    text: fixedMount.checked
                        ? "偏移以麦阵中心为原点，朝拍摄方向看右、上、前为正。参考距离沿镜头光轴量取。用同频测试音在中央、左右和上下复核通道及视场。"
                        : "用目标频率的测试音在画面中央、左右和上下复核。移动相机或麦阵后，请取消已标定并重新校准。"
                    color: Colors.textSecondary; wrapMode: Text.Wrap
                }
                MicrophoneArrayView {
                    objectName: "microphoneArrayView"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 145
                    positions: pane.parameters.positions || []
                    levels: pane.observation.channels_dbfs || []
                    observation: pane.observation
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: (pane.observation.channels_dbfs || []).map(function(level, index) {
                        return "CH " + (index + 1) + " " + pane.measured(level, 1, " dBFS")
                    }).join("　")
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
                spacing: 3
                TouchButton { text: "取消"; autoWidth: true; height: 30; onClicked: settings.close() }
                TouchButton { text: "应用参数"; autoWidth: true; height: 30; variant: "primary"; onClicked: pane.applySettings() }
            }
        }
    }
}
