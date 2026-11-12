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

    function channelPlot(kind) {
        const frequency = kind === "frequency"
        const title = frequency ? "频谱图" : "时域信号"
        const plot = service.plots[kind]
        if (!plot) {
            return frequency
                ? {title: title, xLabel: "频率 (Hz)", yLabel: "幅值 (dBFS)", xRange: [0, 8000], yRange: [-100, 0]}
                : {title: title, xLabel: "采样时长 (ms)", yLabel: "幅值 (FS)", xRange: [0, 0.128],
                   xDisplayScale: 1000, yRange: [-1, 1]}
        }
        return Object.assign({}, plot, {
            title: title,
            series: plotChannel.currentIndex > 0 ? [plot.series[plotChannel.currentIndex - 1]] : plot.series
        })
    }

    onVisibleChanged: {
        if (visible && App.session.loggedIn) {
            if (canInspect)
                refreshCameras()
            logImagingStatus()
        } else {
            service.leave()
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
            readonly property int padding: 8
            readonly property int gutter: stacked ? 8 : 16
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
                spacing: 5

                GridLayout {
                    Layout.fillWidth: true
                    columns: width >= 610 ? 2 : 1
                    columnSpacing: 8
                    rowSpacing: 4

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
                                    ? "ListenGo 六麦原始输入" : "未检测到六麦阵列"
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
                    title: "目标频带控制"
                    Layout.fillWidth: true
                    contentItem: GridLayout {
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 6
                        PanelLabel { text: "采样率" }
                        PanelLabel { text: "16 kHz" }
                        Basic.RangeSlider {
                            id: bandSlider
                            objectName: "acousticFrequencyBand"
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            from: 80
                            to: 7400
                            stepSize: 10
                            snapMode: RangeSlider.SnapAlways
                            first.value: pane.parameters.lower_hz
                            second.value: pane.parameters.upper_hz
                            enabled: pane.canInspect
                            Accessible.name: "目标频带"
                            background: PanelSliderTrack {
                                x: bandSlider.leftPadding + 6
                                width: bandSlider.availableWidth - 12
                                start: Math.min(bandSlider.first.visualPosition, bandSlider.second.visualPosition)
                                end: Math.max(bandSlider.first.visualPosition, bandSlider.second.visualPosition)
                            }
                            first.handle: PanelSliderThumb {
                                x: bandSlider.leftPadding + bandSlider.first.visualPosition * (bandSlider.availableWidth - width)
                                pressed: bandSlider.first.pressed
                            }
                            second.handle: PanelSliderThumb {
                                x: bandSlider.leftPadding + bandSlider.second.visualPosition * (bandSlider.availableWidth - width)
                                pressed: bandSlider.second.pressed
                            }

                            function applyBand() {
                                const upper = Math.max(200, Math.round(second.value))
                                const lower = Math.min(5500, Math.round(first.value), upper - 100)
                                if (pane.parameters.lower_hz !== lower || pane.parameters.upper_hz !== upper)
                                    pane.service.configure({lower_hz: lower, upper_hz: upper})
                                // Apply the minimum band width to the displayed handles too.
                                setValues(pane.parameters.lower_hz, pane.parameters.upper_hz)
                            }
                            first.onPressedChanged: { if (!first.pressed) applyBand() }
                            second.onPressedChanged: { if (!second.pressed) applyBand() }
                            first.onMoved: { if (!first.pressed) applyBand() }
                            second.onMoved: { if (!second.pressed) applyBand() }
                        }
                        PanelLabel { text: "频带下限 (Hz)"; Layout.fillWidth: true; Layout.preferredWidth: 114 }
                        PanelNumber {
                            objectName: "acousticLowerFrequency"
                            from: 80
                            to: Math.min(5500, pane.parameters.upper_hz - 100)
                            value: pane.parameters.lower_hz
                            enabled: pane.canInspect
                            onValueModified: pane.updateParameter("lower_hz", value)
                        }
                        PanelLabel { text: "频带上限 (Hz)"; Layout.fillWidth: true; Layout.preferredWidth: 114 }
                        PanelNumber {
                            objectName: "acousticUpperFrequency"
                            from: Math.max(200, pane.parameters.lower_hz + 100)
                            to: 7400
                            value: pane.parameters.upper_hz
                            enabled: pane.canInspect
                            onValueModified: pane.updateParameter("upper_hz", value)
                        }
                    }
                }

                FieldGroup {
                    title: "成像显示控制"
                    Layout.fillWidth: true
                    contentItem: GridLayout {
                        columns: 2
                        columnSpacing: 10
                        rowSpacing: 6
                        Basic.Slider {
                            id: gateSlider
                            objectName: "acousticLevelGate"
                            Layout.columnSpan: 2
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            from: -100
                            to: -12
                            stepSize: 1
                            snapMode: Slider.SnapAlways
                            value: pane.parameters.gate_dbfs
                            enabled: pane.canInspect
                            Accessible.name: "最低电平 (dBFS)"
                            background: PanelSliderTrack {
                                x: gateSlider.leftPadding + 6
                                width: gateSlider.availableWidth - 12
                                end: gateSlider.visualPosition
                            }
                            handle: PanelSliderThumb {
                                x: gateSlider.leftPadding + gateSlider.visualPosition * (gateSlider.availableWidth - width)
                                pressed: gateSlider.pressed
                            }
                            onPressedChanged: { if (!pressed) pane.updateParameter("gate_dbfs", Math.round(value)) }
                            onMoved: { if (!pressed) pane.updateParameter("gate_dbfs", Math.round(value)) }
                        }
                        PanelLabel { text: "最低电平 (dBFS)"; Layout.fillWidth: true; Layout.preferredWidth: 114 }
                        PanelNumber {
                            from: -100
                            to: -12
                            value: pane.parameters.gate_dbfs
                            enabled: pane.canInspect
                            onValueModified: pane.updateParameter("gate_dbfs", value)
                        }
                        PanelLabel { text: "热图透明度 (%)"; Layout.fillWidth: true; Layout.preferredWidth: 114 }
                        PanelNumber {
                            from: 8
                            to: 95
                            value: Math.round(pane.parameters.opacity * 100)
                            enabled: pane.canInspect
                            onValueModified: pane.updateParameter("opacity", value / 100)
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
                    Layout.minimumHeight: 86
                    Layout.preferredHeight: 96
                    darkStyle: true
                    plotData: pane.channelPlot("time")
                    Accessible.name: "时域信号"
                }

                NativeHeatmap {
                    objectName: "acousticFrequencyChart"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 86
                    Layout.preferredHeight: 96
                    darkStyle: true
                    plotData: pane.channelPlot("frequency")
                    Accessible.name: "频谱图"
                }
            }

            Rectangle {
                id: viewport
                objectName: "acousticVideoViewport"
                x: dashboard.rightX
                y: dashboard.padding + mirrorControl.height + 3
                width: dashboard.rightWidth
                height: dashboard.stacked ? Math.max(145, Math.round(width * 0.56))
                    : Math.min(dashboard.desktopHeight - y - 114, Math.round(dashboard.desktopHeight * 0.64))
                color: "#ffffff"
                clip: true
                readonly property rect overlayRect: App.camera.hasFrame ? camera.contentRect
                    : Qt.rect(0, 0, width, height)
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
                        visible: App.camera.hasFrame
                        Connections {
                            target: App.camera
                            enabled: pane.visible
                            function onFrameReady(frame) { camera.frame = frame }
                        }
                    }
                    Image {
                        x: viewport.overlayRect.x
                        y: viewport.overlayRect.y
                        width: viewport.overlayRect.width
                        height: viewport.overlayRect.height
                        source: pane.overlayVisible ? pane.service.heatmap : ""
                        cache: false
                        smooth: true
                        fillMode: Image.Stretch
                        opacity: Number(pane.parameters.opacity)
                    }
                }

                Item {
                    readonly property real sourceX: viewport.overlayRect.x
                        + Number(pane.observation.x || 0) * viewport.overlayRect.width
                    x: viewport.mirrored ? viewport.width - sourceX : sourceX
                    y: viewport.overlayRect.y + Number(pane.observation.y || 0) * viewport.overlayRect.height
                    visible: pane.overlayVisible && x >= 0 && x <= viewport.width && y >= 0 && y <= viewport.height
                    Rectangle {
                        anchors.centerIn: parent
                        width: 22; height: 22; radius: 11
                        color: "transparent"; border.color: "#fbfbf6"; border.width: 1
                    }
                    Rectangle { anchors.centerIn: parent; width: 12; height: 1; color: "#fbfbf6" }
                    Rectangle { anchors.centerIn: parent; width: 1; height: 12; color: "#fbfbf6" }
                    Rectangle {
                        x: Math.max(4 - parent.x, Math.min(14, viewport.width - parent.x - width - 19))
                        y: parent.y > 34 ? -29 : 14
                        width: marker.implicitWidth + 12; height: 22
                        color: "#dd212121"
                        PanelLabel {
                            id: marker
                            anchors.centerIn: parent
                            color: "white"
                            text: pane.observation.speaker_name
                                ? pane.observation.speaker_name + "  " + pane.observation.speaker_score
                                : pane.measured(pane.observation.azimuth_deg, 1, "°")
                        }
                    }
                }

                Rectangle {
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: 5
                    width: 8
                    visible: pane.overlayVisible
                    Accessible.name: "归一化空间响应，从下到上 0 到 1"
                    gradient: Gradient {
                        GradientStop { position: 0; color: "#bb0000" }
                        GradientStop { position: 1/6; color: "#ff6800" }
                        GradientStop { position: 2/6; color: "#ffff00" }
                        GradientStop { position: 3/6; color: "#54ff68" }
                        GradientStop { position: 4/6; color: "#00dbff" }
                        GradientStop { position: 5/6; color: "#0000ff" }
                        GradientStop { position: 1; color: "#000080" }
                    }
                }

                PanelLabel {
                    anchors.centerIn: parent
                    visible: !App.camera.hasFrame && !pane.overlayVisible
                    text: App.camera.opening ? "正在打开摄像头…" : pane.cameraInUse
                        ? "等待摄像头画面…" : "请选择并打开摄像头"
                    color: "#727272"
                }

                PanelLabel {
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.margins: 5
                    visible: pane.service.running && !pane.cameraFresh && pane.hasLocation
                    text: App.camera.hasFrame ? "音画时间差过大" : "角度图 · 摄像头未连接"
                    color: "#ffffff"
                    padding: 4
                    background: Rectangle { color: "#c02a2a2a" }
                }
            }

            Basic.CheckBox {
                id: mirrorControl
                objectName: "acousticMirrorPreview"
                anchors.right: viewport.right
                y: dashboard.padding
                height: 19
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
                    PanelLabel {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: pane.hasLocation
                            ? "方位 / 俯仰  " + pane.measured(pane.observation.azimuth_deg, 1, "°")
                              + " / " + pane.measured(pane.observation.elevation_deg, 1, "°")
                              + "    音画差  " + pane.measured(pane.observation.skew_ms, 0, " ms")
                            : pane.service.running ? "六路电平  " + pane.measured(pane.observation.level_dbfs, 1, " dBFS")
                              : pane.parameters.calibrated ? "机位已标定" : "参考机位参数 · 待现场标定"
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
                spacing: 5
                TouchButton { text: "取消"; autoWidth: true; height: 30; onClicked: settings.close() }
                TouchButton { text: "应用参数"; autoWidth: true; height: 30; variant: "primary"; onClicked: pane.applySettings() }
            }
        }
    }
}
