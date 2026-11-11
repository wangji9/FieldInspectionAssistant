import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FieldInspection
import "../theme"

Item {
    id: panel
    property bool imageMode: false
    property url inputFile
    property bool pendingImage: false
    property string inputError: ""
    readonly property var live: App.visionLive
    readonly property var metrics: live.summary
    readonly property var hardware: live.hardware
    readonly property var model: live.models[modelChoice.currentIndex]
    readonly property bool matchingSource: (!imageMode || !pendingImage) && (metrics.sourceKind === "sample"
        || metrics.sourceKind === (imageMode ? "image" : "camera"))
    readonly property bool showingResult: matchingSource && live.hasPreview
    readonly property bool allowed: !!App.session.permissions.inspect && !App.visionAlgorithms.busy
        && !App.ai.busy && !App.agent.busy && !App.projectInference.busy
    property var devices: []
    property bool showAnnotations: true
    property bool cropCamera: true
    property bool cropImage: false
    readonly property bool fillViewport: imageMode ? cropImage : cropCamera
    property bool expandedPreview: true
    property real opacityValue: 0.82
    property var dataSnapshot: ({ summary: {}, results: [], categories: [] })
    readonly property var boxColumns: [{key:"label",title:"目标",width:0}, {key:"confidence",title:"置信度",width:64},
        {key:"size",title:"宽 × 高 (px)",width:81}]
    readonly property var pointColumns: [{key:"label",title:"目标",width:0}, {key:"confidence",title:"置信度",width:64},
        {key:"pointCount",title:"有效关键点",width:81}]

    ListModel { id: resultRows }

    function refresh() {
        const previous = cameraChoice.currentText
        devices = live.cameras()
        const index = devices.indexOf(previous)
        const usb = devices.findIndex(function(name) { return name === "/dev/video1" || name.indexOf("/dev/video1 ") === 0 })
        cameraChoice.currentIndex = index >= 0 ? index : usb >= 0 ? usb : devices.length ? 0 : -1
        live.refresh()
    }
    onVisibleChanged: if (visible) { refresh(); updateData() } else live.stop()
    onImageModeChanged: { live.stop(); updateData() }
    function updateData() {
        if (matchingSource) {
            const summary = live.summary
            const rows = summary.maskTracking ? live.annotationRows() : live.results
            let categories = live.categories
            if (summary.maskTracking) {
                const groups = new Map()
                for (const row of rows) {
                    const group = groups.get(row.id) || {label: row.label, color: row.color, count: 0}
                    group.count++
                    groups.set(row.id, group)
                }
                categories = Array.from(groups.values()).sort((a, b) => b.count - a.count)
            }
            dataSnapshot = {summary: summary, results: rows, categories: categories,
                history: resultTabs.currentIndex === 1 ? live.plotHistory : []}
        } else {
            dataSnapshot = {summary: {}, results: [], categories: []}
        }
        const rows = dataSnapshot.results
        for (let i = 0; i < rows.length; ++i) {
            const row = rows[i]
            const values = { label: row.label, confidence: row.confidence, size: row.size, pointCount: row.pointCount || 0 }
            if (i < resultRows.count)
                resultRows.set(i, values)
            else
                resultRows.append(values)
        }
        if (resultRows.count > rows.length)
            resultRows.remove(rows.length, resultRows.count - rows.length)
    }
    function reading(value, unit) { return dataSnapshot.summary.frames ? Number(value || 0).toFixed(1) + unit : "—" }
    function frameRate(value) { return dataSnapshot.summary.sourceKind !== "camera" || !value ? "—" : reading(value, " FPS") }

    Component.onCompleted: updateData()
    Timer {
        interval: 2500
        running: panel.visible && !!App.session.permissions.inspect
        triggeredOnStart: true
        repeat: true
        onTriggered: panel.live.refreshHardware()
    }
    // Coalesce result-table and chart updates independently of the video stream.
    Timer {
        id: dataRefresh
        interval: 125
        onTriggered: panel.updateData()
    }
    Connections {
        target: panel.live
        function onChanged() {
            if (!panel.visible) return
            if (!panel.live.running) {
                dataRefresh.stop()
                panel.updateData()
            } else if (!dataRefresh.running) {
                dataRefresh.start()
            }
        }
    }
    Connections {
        target: App.session
        function onSessionEnding() { panel.inputFile = ""; panel.pendingImage = false; panel.inputError = "" }
    }
    FileDialog {
        id: imagePicker
        title: "选择待分析图片"
        nameFilters: ["图片 (*.jpg *.jpeg *.png *.bmp *.webp)"]
        onAccepted: {
            panel.inputFile = selectedFile
            panel.pendingImage = true
            panel.inputError = ""
            panel.updateData()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 5
        GroupBox {
            title: panel.model && panel.model.id === "industrial_steel" ? "热轧钢材表面 · 缺陷候选"
                : panel.model && panel.model.id === "industrial_ppe" ? "人员防护装备筛查"
                : panel.imageMode ? "本地图片推理" : "视觉采集与推理"
            padding: 6
            Layout.fillWidth: true
            ColumnLayout {
                width: parent.width
                spacing: 4
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    AppComboBox {
                        id: modelChoice
                        objectName: panel.imageMode ? "imageVisionModel" : "liveVisionModel"
                        width: 276; height: 28
                        maximumPopupHeight: 380
                        model: panel.live.models
                        textRole: "title"
                        enabled: !panel.live.running
                        displayText: panel.model ? panel.model.task + " · " + panel.model.title : ""
                        delegate: ItemDelegate {
                            required property var modelData
                            required property int index
                            objectName: (panel.imageMode ? "imageVisionModel-" : "liveVisionModel-") + modelData.id
                            text: modelData.task + " · " + modelData.title + (modelData.ready ? "" : "（未安装）")
                            width: modelChoice.width; height: 28
                            highlighted: modelChoice.highlightedIndex === index
                        }
                    }
                    AppComboBox {
                        id: cameraChoice
                        visible: !panel.imageMode
                        objectName: "liveVisionCamera"
                        width: 128; height: 28
                        model: panel.devices
                        displayText: currentIndex < 0 ? "无摄像头" : currentText
                        enabled: !panel.live.running
                    }
                    TouchButton {
                        visible: !panel.imageMode
                        text: "扫描"; autoWidth: true; height: 28
                        enabled: !panel.live.running
                        onClicked: panel.refresh()
                    }
                    TouchButton {
                        visible: !panel.imageMode
                        objectName: "liveVisionStart"
                        text: "开始实时"; autoWidth: true; height: 28; variant: "primary"
                        enabled: panel.allowed && !!panel.model && panel.model.ready
                            && cameraChoice.currentIndex >= 0 && !panel.live.running
                        onClicked: panel.live.startCamera(panel.model.id, cameraChoice.currentText)
                    }
                    TouchButton {
                        visible: panel.imageMode
                        text: "打开图片"; autoWidth: true; height: 28
                        enabled: !panel.live.running
                        onClicked: imagePicker.open()
                    }
                    TouchButton {
                        visible: panel.imageMode
                        objectName: "imageVisionRun"
                        text: "运行推理"; autoWidth: true; height: 28; variant: "primary"
                        enabled: panel.allowed && !!panel.model && panel.model.ready
                            && panel.inputFile.toString().length > 0 && !panel.live.running
                        onClicked: {
                            if (panel.live.runImage(panel.model.id, panel.inputFile)) {
                                panel.pendingImage = false
                                panel.inputError = ""
                            } else {
                                panel.inputError = panel.live.status
                            }
                            panel.updateData()
                        }
                    }
                    TouchButton {
                        objectName: panel.imageMode ? "imageVisionSample" : "liveVisionSample"
                        text: "运行样图"; autoWidth: true; height: 28
                        enabled: panel.allowed && !!panel.model && panel.model.ready && panel.model.sample && !panel.live.running
                        onClicked: {
                            if (panel.live.runSample(panel.model.id)) {
                                panel.pendingImage = false
                                panel.inputError = ""
                            } else if (panel.imageMode) {
                                panel.inputError = panel.live.status
                            }
                            panel.updateData()
                        }
                    }
                    TouchButton {
                        objectName: panel.imageMode ? "imageVisionStop" : "liveVisionStop"
                        text: "停止"; autoWidth: true; height: 28
                        enabled: panel.live.running
                        onClicked: panel.live.stop()
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 7
                    AppCheckBox {
                        objectName: "liveVisionAnnotations"
                        text: "显示标注"; height: 25; checked: panel.showAnnotations
                        onToggled: {
                            panel.showAnnotations = checked
                            if (!checked)
                                panel.live.synchronizedPreview = false
                        }
                    }
                    AppCheckBox { text: "仅人员"; visible: !!panel.model && !panel.model.landmarks && !panel.model.industrial; height: 25; checked: panel.live.peopleOnly; onToggled: panel.live.peopleOnly = checked }
                    AppCheckBox {
                        text: "铺满画面"; height: 25; checked: panel.fillViewport
                        onToggled: {
                            if (panel.imageMode) panel.cropImage = checked
                            else panel.cropCamera = checked
                        }
                    }
                    AppCheckBox { text: "同步标注"; visible: !panel.imageMode; height: 25; enabled: panel.showAnnotations; checked: panel.live.synchronizedPreview; onToggled: panel.live.synchronizedPreview = checked }
                    Label { text: "置信阈值 " + Math.round(panel.live.threshold * 100) + "%"; height: 25; verticalAlignment: Text.AlignVCenter }
                    Slider { width: 95; height: 25; from: 0.25; to: 0.95; value: panel.live.threshold; onMoved: panel.live.threshold = value }
                    Label {
                        visible: !!panel.model && panel.model.segmentation
                        text: "轮廓与填色"; height: 25; verticalAlignment: Text.AlignVCenter
                    }
                    Slider {
                        visible: !!panel.model && panel.model.segmentation
                        width: 90; height: 25; from: 0; to: 1; value: panel.opacityValue
                        onMoved: panel.opacityValue = value
                    }
                    AppCheckBox { objectName: "liveVisionWide"; visible: !panel.imageMode; text: "宽屏预览"; height: 25; checked: panel.expandedPreview; onToggled: panel.expandedPreview = checked }
                }
            }
        }

        GridLayout {
            id: split
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: width >= 600 ? 2 : 1
            columnSpacing: 6
            rowSpacing: 5
            Rectangle {
                objectName: "liveVisionCameraPane"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 110
                Layout.preferredWidth: 670
                color: "#18232e"
                border.color: "#475765"
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        color: "#243544"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8; anchors.rightMargin: 8
                            Rectangle { width: 6; height: 6; radius: 3; color: panel.live.running ? "#63dccc" : "#9baab7" }
                            Label { text: panel.imageMode ? "图片" : "摄像头实时"; color: "#eff6fa" }
                            Label {
                                Layout.fillWidth: true; elide: Text.ElideRight
                                text: (panel.imageMode && panel.inputError ? panel.inputError : "") || (panel.matchingSource
                                    ? panel.live.status + (panel.metrics.model ? "   ·   " + panel.metrics.model + "   ·   " + panel.metrics.backend : "")
                                    : panel.imageMode ? "选择图片或运行样图" : "选择模型与视频源")
                                color: "#c6dbe7"
                            }
                        }
                    }
                    Item {
                        objectName: "liveVisionViewport"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        CameraPreview {
                            anchors.fill: parent
                            visible: panel.showingResult
                            frame: panel.live.previewFrame
                            fillCrop: panel.fillViewport
                        }
                        CameraPreview {
                            anchors.fill: parent
                            visible: panel.showingResult && panel.showAnnotations && panel.opacityValue > 0
                            frame: panel.live.maskFrame
                            frameAspectSize: Qt.size(panel.metrics.width, panel.metrics.height)
                            fillCrop: panel.fillViewport
                            smooth: true
                            opacity: panel.opacityValue
                        }
                        LiveVisionView {
                            anchors.fill: parent
                            visible: panel.showingResult && panel.showAnnotations
                            source: panel.live
                            fillCrop: panel.fillViewport
                            boxes: panel.opacityValue <= 0
                        }
                        Image {
                            anchors.fill: parent
                            visible: panel.imageMode && !panel.showingResult && panel.inputFile.toString().length > 0
                            source: visible ? panel.inputFile : ""
                            sourceSize: Qt.size(1200, 1200)
                            asynchronous: true
                            autoTransform: true
                            fillMode: Image.PreserveAspectFit
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: !panel.showingResult && (!panel.imageMode || !panel.inputFile.toString().length)
                            text: panel.imageMode ? "等待图片" : panel.live.running ? "正在等待视频源" : "等待视频源"
                            color: "#cad9e4"
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        color: "#243544"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8; anchors.rightMargin: 8
                            Label {
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: panel.showingResult
                                      ? panel.metrics.width + " × " + panel.metrics.height + "   ·   "
                                        + (panel.metrics.sourceKind !== "camera" ? panel.metrics.source
                                           : panel.live.synchronizedPreview ? "同步标注"
                                           : panel.metrics.maskTracking ? "分割轮廓跟踪" : "摄像头原画")
                                      : panel.imageMode ? "图片预览" : "摄像头预览"
                                color: "#c6dbe7"
                            }
                            Label {
                                Layout.maximumWidth: parent.width * 0.48
                                elide: Text.ElideRight
                                visible: panel.showingResult && !!panel.dataSnapshot.summary.frames
                                text: panel.metrics.sourceKind === "camera"
                                    ? "预览 " + panel.frameRate(panel.dataSnapshot.summary.cameraFps)
                                      + "   ·   推理 " + panel.frameRate(panel.dataSnapshot.summary.inferenceFps)
                                    : "推理 " + panel.reading(panel.dataSnapshot.summary.inferenceMs, " ms")
                                color: "#c6dbe7"
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                objectName: "liveVisionDataPane"
                Layout.fillWidth: split.columns === 1
                Layout.fillHeight: true
                Layout.preferredWidth: split.columns === 2
                    ? Math.min(460, Math.max(250, Math.round(split.width * 0.32)))
                    : split.width
                Layout.minimumWidth: 0
                spacing: 4
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: "#243544"
                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: panel.imageMode ? "推理结果" : "实时数据"
                        color: "#eff6fa"
                    }
                }
                GroupBox {
                    id: hardwareGroup
                    objectName: "liveVisionHardware"
                    title: "板端芯片性能"
                    visible: resultTabs.currentIndex === 0 || panel.height >= 480
                    padding: 7
                    Layout.fillWidth: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 5
                        Repeater {
                            model: [
                                {name: "RK3588 CPU", load: panel.hardware.cpuLoad, mhz: panel.hardware.cpuMhz, color: "#4e7fa6"},
                                {name: "RK3588 GPU", load: panel.hardware.gpuLoad, mhz: panel.hardware.gpuMhz, color: "#746fa1"},
                                {name: "RK1828 NPU", load: panel.hardware.npuLoad, color: "#3c8b83"}
                            ]
                            delegate: GridLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                columns: 3
                                columnSpacing: 7
                                rowSpacing: 3
                                Label { text: modelData.name; font.bold: true; color: "#233a4d" }
                                Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight
                                    text: modelData.mhz > 0 ? modelData.mhz + " MHz" : ""
                                    color: Colors.textSecondary
                                }
                                Label {
                                    Layout.preferredWidth: 62
                                    horizontalAlignment: Text.AlignRight
                                    text: modelData.load === undefined ? "无读数" : modelData.load + "%"
                                    color: modelData.load === undefined ? Colors.muted : "#233a4d"
                                }
                                Rectangle {
                                    Layout.columnSpan: 3
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 5
                                    color: "#dfe7ed"
                                    Rectangle {
                                        height: parent.height
                                        width: parent.width * (modelData.load || 0) / 100
                                        color: modelData.color
                                    }
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: panel.hardware.npuOnline
                                ? "PCIe 在线"
                                  + (panel.hardware.npuTemp === undefined ? "" : "   ·   " + panel.hardware.npuTemp + " °C")
                                  + (panel.hardware.npuMemory ? "   ·   " + panel.hardware.npuMemory + " MB" : "")
                                : "RK1828 遥测暂不可用"
                            color: Colors.textSecondary
                        }
                    }
                }
                Label {
                    visible: !hardwareGroup.visible
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    leftPadding: 5
                    text: "CPU " + (panel.hardware.cpuLoad === undefined ? "—" : panel.hardware.cpuLoad + "%")
                        + "   GPU " + (panel.hardware.gpuLoad === undefined ? "—" : panel.hardware.gpuLoad + "%")
                        + "   NPU " + (panel.hardware.npuLoad === undefined ? "—" : panel.hardware.npuLoad + "%")
                    color: Colors.textSecondary
                }
                GroupBox {
                    title: "识别结果"
                    padding: 5
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 80
                    ColumnLayout {
                        anchors.fill: parent; spacing: 3
                        TabBar {
                            id: resultTabs
                            Layout.fillWidth: true
                            DesktopTab { objectName: "visionResultTableTab"; text: "目标表" }
                            DesktopTab { objectName: "visionResultChartTab"; text: "数据图" }
                            onCurrentIndexChanged: panel.updateData()
                        }
                        VisionInspectionChart {
                            visible: resultTabs.currentIndex === 1
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            snapshot: panel.dataSnapshot
                            cameraMode: !panel.imageMode
                            history: panel.dataSnapshot.history || []
                        }
                        Rectangle {
                            visible: resultTabs.currentIndex === 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: 28
                            color: "#eaf0f5"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 7
                                anchors.rightMargin: 7
                                Label { text: panel.dataSnapshot.results.length + " 个目标"; font.bold: true; color: "#243d51" }
                                Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideRight
                                    text: panel.dataSnapshot.summary.maskTracking
                                        ? panel.dataSnapshot.categories.length + " 种跟踪类别"
                                        : panel.dataSnapshot.summary.coverage > 0
                                        ? "蒙版覆盖 " + Number(panel.dataSnapshot.summary.coverage).toFixed(1) + "%"
                                        : panel.dataSnapshot.categories.length + " 种类别"
                                    color: Colors.textSecondary
                                }
                            }
                        }
                        Repeater {
                            model: resultTabs.currentIndex === 0 ? Math.min(3, panel.dataSnapshot.categories.length) : 0
                            delegate: RowLayout {
                                required property int index
                                readonly property var category: panel.dataSnapshot.categories[index] || {}
                                Layout.fillWidth: true
                                Rectangle { Layout.preferredWidth: 5; Layout.preferredHeight: 12; color: category.color || Colors.border }
                                Label { text: category.label || ""; Layout.preferredWidth: 72; elide: Text.ElideRight }
                                Rectangle {
                                    Layout.fillWidth: true; Layout.preferredHeight: 7
                                    color: "#e6edf2"
                                    Rectangle { height: parent.height; width: parent.width * (category.count || 0) / Math.max(1, panel.dataSnapshot.results.length); color: category.color || Colors.border }
                                }
                                Label { text: category.count || "" }
                            }
                        }
                        DataTable {
                            visible: resultTabs.currentIndex === 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            rowHeight: 22
                            emptyText: "当前无可显示目标"
                            rowModel: resultRows
                            columns: panel.dataSnapshot.summary.landmarks ? panel.pointColumns : panel.boxColumns
                        }
                    }
                }
            }
        }
    }
}
