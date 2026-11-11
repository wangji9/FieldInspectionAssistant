import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FieldInspection
import "../components"
import "../theme"

Rectangle {
    id: page
    objectName: "thermalAnalysisPage"
    color: Colors.background
    readonly property alias source: capture
    property var ports: []
    property var draft: ({})
    property string notice: ""
    property url referenceImage
    property bool showReference: false
    property var exportedFiles: []
    signal projectCapture(var paths, string kind)
    readonly property var seriesNames: ["MAX", "MIN", "AVG", "CENTER", "S1", "S2", "S3", "S4", "S5", "ROI Max", "ROI Min", "ROI Avg"]

    function close() { if (capture.connected) capture.stop() }
    function showImage() { tabs.currentIndex = 0; showReference = false }
    function showDeviceSetup(testOnly) {
        tabs.currentIndex = 2
        scan()
        if (testOnly && !capture.connected) begin(true)
        else if (testOnly) notice = capture.hasFrame ? "通信正常，已接收有效热像帧" : "已连接，正在等待有效帧"
    }
    function showSoftwareSetup() { tabs.currentIndex = 3; draft = capture.options }
    function scan() {
        ports = capture.ports()
        portChoice.currentIndex = ports.length ? 0 : -1
    }
    function begin(testOnly) {
        if (!ports.length) scan()
        notice = ""
        capture.start(portChoice.currentValue || "", testOnly)
    }
    function setOption(key, value) {
        const next = Object.assign({}, draft)
        next[key] = value
        draft = next
    }
    function apply(persist) {
        const ok = capture.applyOptions(draft, persist)
        notice = ok ? (persist ? "热像配置已保存" : "热像配置已应用") : "检查温度范围、标记设置和自动保存类型"
    }
    function exportData(trend) {
        if (trend) {
            const path = capture.exportTrend(capture.options.folder)
            exportedFiles = path ? [path] : []
        } else exportedFiles = capture.saveFrameFiles(capture.options.folder)
        notice = exportedFiles.length ? "已保存 " + exportedFiles.join("，") : "保存失败，请检查保存目录、数据和文件类型"
    }
    Component.onCompleted: draft = capture.options
    onVisibleChanged: if (visible) scan()
    Connections {
        target: App.session
        function onSessionEnding() { page.close(); page.exportedFiles = [] }
    }
    FolderDialog {
        id: folderPicker
        title: "选择热像数据保存目录"
        onAccepted: page.setOption("folder", selectedFolder.toString())
    }
    FileDialog {
        id: referencePicker
        title: "选择可见光参考图像"
        nameFilters: ["图像 (*.jpg *.jpeg *.png *.bmp *.webp)"]
        onAccepted: { page.referenceImage = selectedFile; page.showReference = true }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 3
        TabBar {
            id: tabs
            Layout.fillWidth: true
            DesktopTab { text: "图像显示" }
            DesktopTab { text: "温度显示"; onClicked: page.showReference = false }
            DesktopTab { text: "设备配置"; onClicked: { page.scan(); App.hardware.refresh() } }
            DesktopTab { text: "软件配置"; onClicked: page.showSoftwareSetup() }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex < 2 ? 0 : tabs.currentIndex - 1

            SplitView {
                orientation: Qt.Horizontal
                Rectangle {
                    color: Colors.panel
                    border.color: Colors.border
                    SplitView.minimumWidth: 180
                    SplitView.preferredWidth: parent.width * 0.45
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 3
                        spacing: 3
                        Flow {
                            Layout.fillWidth: true
                            spacing: 2
                            TouchButton { text: capture.tool === "point" ? "测点 ✓" : "测点"; autoWidth: true; height: 24; onClicked: capture.tool = "point" }
                            TouchButton { text: capture.tool === "roi" ? "框选 ✓" : "框选"; autoWidth: true; height: 24; onClicked: capture.tool = "roi" }
                            TouchButton { text: "清除"; autoWidth: true; height: 24; enabled: capture.hasFrame; onClicked: capture.clearMarks() }
                            TouchButton { text: "保存图像"; autoWidth: true; height: 24; enabled: capture.hasFrame; onClicked: page.exportData(false) }
                            TouchButton { text: "热像归入工程"; autoWidth: true; height: 24; enabled: !!App.projects.project.id && page.exportedFiles.length > 0; onClicked: page.projectCapture(page.exportedFiles, "thermal") }
                            ToolButton {
                                text: "视图 ▾"; height: 24
                                onClicked: viewMenu.open()
                                Menu {
                                    id: viewMenu
                                    y: parent.height
                                    MenuItem { text: "放大"; onTriggered: capture.setZoom(1.25) }
                                    MenuItem { text: "缩小"; onTriggered: capture.setZoom(0.8) }
                                    MenuItem { text: "平移"; onTriggered: capture.tool = "pan" }
                                    MenuItem { text: "适配窗口"; onTriggered: capture.fit() }
                                    MenuSeparator {}
                                    MenuItem { text: "打开可见光参考…"; onTriggered: referencePicker.open() }
                                    MenuItem { text: "返回热像"; enabled: page.showReference; onTriggered: page.showReference = false }
                                    MenuSeparator {}
                                    MenuItem {
                                        text: "显示标注"; checkable: true; checked: capture.options.annotations
                                        onTriggered: capture.applyOptions(Object.assign({}, capture.options, {annotations: checked}), false)
                                    }
                                }
                            }
                        }
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            ThermalCapture {
                                id: capture
                                objectName: "thermalCapture"
                                anchors.fill: parent
                                visible: !page.showReference
                                matrix: tabs.currentIndex === 1
                            }
                            Image {
                                id: reference
                                anchors.fill: parent
                                visible: page.showReference
                                source: page.referenceImage
                                asynchronous: true
                                sourceSize.width: 1600
                                fillMode: Image.PreserveAspectFit
                                verticalAlignment: Image.AlignTop
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: page.showReference && reference.status !== Image.Ready
                                text: reference.status === Image.Error ? "可见光参考图无法读取" : "正在读取参考图像"
                            }
                            Label {
                                visible: page.showReference && reference.status === Image.Ready
                                anchors { left: parent.left; top: parent.top }
                                padding: 4
                                text: "可见光参考"
                                background: Rectangle { color: Colors.panel }
                            }
                        }
                    }
                }
                Rectangle {
                    color: Colors.panel
                    border.color: Colors.border
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: 210
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 3
                        spacing: 3
                        Flow {
                            Layout.fillWidth: true
                            spacing: 2
                            TouchButton { objectName: "thermalStart"; text: "开始采集"; autoWidth: true; height: 24; enabled: !capture.connected; onClicked: page.begin(false) }
                            TouchButton { text: capture.paused ? "继续" : "暂停"; autoWidth: true; height: 24; enabled: capture.connected && capture.hasFrame; onClicked: capture.togglePause() }
                            TouchButton { objectName: "thermalStop"; text: "停止"; autoWidth: true; height: 24; enabled: capture.connected; onClicked: page.close() }
                            TouchButton { text: "导出曲线"; autoWidth: true; height: 24; enabled: capture.hasFrame; onClicked: page.exportData(true) }
                            ToolButton {
                                text: "曲线 ▾"; height: 24
                                onClicked: chartMenu.open()
                                Menu {
                                    id: chartMenu
                                    y: parent.height
                                    MenuItem { text: "清空曲线"; onTriggered: capture.clearTrend() }
                                    MenuSeparator {}
                                    Repeater {
                                        model: [30, 60, 120, 300]
                                        MenuItem {
                                            required property int modelData
                                            text: modelData + " 秒"
                                            checkable: true
                                            checked: capture.options.seconds === modelData
                                            onTriggered: capture.applyOptions(Object.assign({}, capture.options, {seconds: modelData}), false)
                                        }
                                    }
                                    MenuSeparator {}
                                    MenuItem { text: "设置曲线显示项…"; onTriggered: { page.draft = capture.options; tabs.currentIndex = 3 } }
                                }
                            }
                        }
                        ThermalTrend {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            source: capture
                            series: capture.options.series || []
                            seconds: capture.options.seconds || 60
                        }
                    }
                }
            }

            SplitView {
                orientation: Qt.Horizontal
                ListView {
                    id: deviceKinds
                    SplitView.preferredWidth: 145
                    SplitView.minimumWidth: 115
                    model: ["热成像设备", "可见光设备", "声音采集设备", "工业信号设备"]
                    currentIndex: 0
                    delegate: ItemDelegate {
                        required property string modelData
                        required property int index
                        width: ListView.view.width
                        text: modelData
                        highlighted: deviceKinds.currentIndex === index
                        onClicked: deviceKinds.currentIndex = index
                    }
                }
                ScrollView {
                    id: deviceScroll
                    SplitView.fillWidth: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: deviceScroll.availableWidth
                        spacing: 8
                        GroupBox {
                            title: "LEPTON 热像串口"
                            visible: deviceKinds.currentIndex === 0
                            Layout.fillWidth: true
                            ColumnLayout {
                                width: parent.width
                                GridLayout {
                                    columns: 2
                                    Label { text: "串口号" }
                                    AppComboBox {
                                        id: portChoice
                                        Layout.preferredWidth: 155; model: page.ports; textRole: "name"; valueRole: "path"
                                        enabled: !capture.connected
                                        displayText: currentIndex < 0 ? "未检测到热像仪" : currentText
                                    }
                                    Label { text: "波特率" } Label { text: "921600" }
                                    Label { text: "协议与分辨率" } Label { text: "LEPTON 数据帧 · 80 × 60" }
                                    Label { text: "状态" } Label { text: capture.status; wrapMode: Text.Wrap; Layout.fillWidth: true }
                                    Label { text: "有效帧率" } Label { text: capture.connected ? capture.fps.toFixed(1) + " FPS" : "—" }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    TouchButton { text: "扫描设备"; autoWidth: true; enabled: !capture.connected; onClicked: page.scan() }
                                    TouchButton { text: "连接"; autoWidth: true; enabled: !capture.connected && page.ports.length > 0; onClicked: page.begin(false) }
                                    TouchButton { text: "断开"; autoWidth: true; enabled: capture.connected; onClicked: page.close() }
                                    TouchButton { text: "测试连接"; autoWidth: true; enabled: !capture.connected && page.ports.length > 0; onClicked: page.begin(true) }
                                }
                            }
                        }
                        GroupBox {
                            visible: deviceKinds.currentIndex > 0
                            title: deviceKinds.model[deviceKinds.currentIndex] || ""
                            Layout.fillWidth: true
                            ColumnLayout {
                                width: parent.width
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: deviceKinds.currentIndex === 1
                                        ? (App.evidence.cameraConnected ? App.camera.status : "摄像头未打开")
                                        : deviceKinds.currentIndex === 2
                                            ? ((App.hardware.snapshot.microphones || []).map(function(d) { return d.name + " · " + d.state }).join("\n") || "未读取到麦克风")
                                            : (App.analysis.deviceStatus || "在采集页扫描工业信号设备")
                                }
                                TouchButton {
                                    text: "打开采集设置"; autoWidth: true
                                    onClicked: deviceKinds.currentIndex === 1 ? App.nav.push("camera")
                                        : App.nav.push("analysis", {open_capture_settings: true})
                                }
                            }
                        }
                    }
                }
            }

            ScrollView {
                id: softwareScroll
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    width: softwareScroll.availableWidth
                    spacing: 5
                    GridLayout {
                        Layout.fillWidth: true
                        columns: softwareScroll.availableWidth >= 610 ? 2 : 1
                        columnSpacing: 5
                        GroupBox {
                            title: "图像与标记"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: (softwareScroll.availableWidth - 5) / parent.columns
                            GridLayout {
                                columns: 2; width: parent.width
                                Label { text: "色板" }
                                AppComboBox {
                                    Layout.preferredWidth: 118; textRole: "label"; valueRole: "key"
                                    model: [{key:"rainbow",label:"彩虹"}, {key:"blue-red",label:"蓝红"}, {key:"gray",label:"灰度"}]
                                    currentIndex: Math.max(0, model.findIndex(function(v) { return v.key === page.draft.palette }))
                                    onActivated: page.setOption("palette", currentValue)
                                }
                                Label { text: "温度范围 °C" }
                                RowLayout {
                                    AppCheckBox { text: "自动"; checked: !!page.draft.automatic; onToggled: page.setOption("automatic", checked) }
                                    TextField { Layout.preferredWidth: 56; enabled: !page.draft.automatic; text: page.draft.low ?? ""; onTextEdited: page.setOption("low", Number(text)) }
                                    TextField { Layout.preferredWidth: 56; enabled: !page.draft.automatic; text: page.draft.high ?? ""; onTextEdited: page.setOption("high", Number(text)) }
                                }
                                Label { text: "对比度" }
                                TextField { Layout.preferredWidth: 118; text: page.draft.contrast ?? ""; onTextEdited: page.setOption("contrast", Number(text)) }
                                Label { text: "温度补偿 °C" }
                                TextField { Layout.preferredWidth: 118; text: page.draft.offset ?? ""; onTextEdited: page.setOption("offset", Number(text)) }
                                Label { text: "高斯滤波半径" }
                                SpinBox { from: 0; to: 4; value: page.draft.filter || 0; onValueModified: page.setOption("filter", value) }
                                Label { text: "标记颜色" }
                                AppComboBox {
                                    Layout.preferredWidth: 118; textRole: "label"; valueRole: "key"
                                    model: [{key:"#ffffff",label:"白色"}, {key:"#ffff00",label:"黄色"}, {key:"#00ffff",label:"青色"}]
                                    currentIndex: Math.max(0, model.findIndex(function(v) { return v.key === page.draft.markColor }))
                                    onActivated: page.setOption("markColor", currentValue)
                                }
                                Label { text: "标记字号" }
                                SpinBox { from: 8; to: 24; value: page.draft.markSize || 12; onValueModified: page.setOption("markSize", value) }
                                AppCheckBox { text: "平滑缩放"; checked: !!page.draft.smooth; onToggled: page.setOption("smooth", checked) }
                                AppCheckBox { text: "显示编号"; checked: !!page.draft.numbered; onToggled: page.setOption("numbered", checked) }
                                AppCheckBox { text: "显示标注"; checked: !!page.draft.annotations; onToggled: page.setOption("annotations", checked) }
                                AppCheckBox { text: "显示 ROI"; checked: !!page.draft.showRoi; onToggled: page.setOption("showRoi", checked) }
                            }
                        }
                        GroupBox {
                            title: "曲线与数据保存"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredWidth: (softwareScroll.availableWidth - 5) / parent.columns
                            ColumnLayout {
                                width: parent.width
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Repeater {
                                        model: page.seriesNames
                                        AppCheckBox {
                                            required property string modelData
                                            text: modelData
                                            checked: (page.draft.series || []).includes(modelData)
                                            onToggled: {
                                                const selected = Array.from(page.draft.series || []).filter(function(v) { return v !== modelData })
                                                if (checked) selected.push(modelData)
                                                page.setOption("series", selected)
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    Label { text: "保存类型" }
                                    AppCheckBox { text: "PNG"; checked: !!page.draft.png; onToggled: page.setOption("png", checked) }
                                    AppCheckBox { text: "原始矩阵"; checked: !!page.draft.raw; onToggled: page.setOption("raw", checked) }
                                    AppCheckBox { text: "温度 CSV"; checked: !!page.draft.csv; onToggled: page.setOption("csv", checked) }
                                }
                                RowLayout {
                                    AppCheckBox { text: "自动保存"; checked: !!page.draft.autoSave; onToggled: page.setOption("autoSave", checked) }
                                    SpinBox {
                                        from: 10; to: 300; value: page.draft.autoSeconds || 30
                                        enabled: !!page.draft.autoSave; onValueModified: page.setOption("autoSeconds", value)
                                    }
                                    Label { text: "秒 / 帧" }
                                }
                                Label { text: "命名方式：热像类型与采集时间" }
                                Label { Layout.fillWidth: true; text: page.draft.folder || ""; wrapMode: Text.WrapAnywhere }
                                TouchButton { text: "选择保存目录…"; autoWidth: true; onClicked: folderPicker.open() }
                            }
                        }
                    }
                    RowLayout {
                        TouchButton { text: "应用"; autoWidth: true; onClicked: page.apply(false) }
                        TouchButton { text: "恢复默认"; autoWidth: true; onClicked: page.draft = capture.defaults() }
                        TouchButton { text: "保存配置"; autoWidth: true; onClicked: page.apply(true) }
                    }
                }
            }
        }
    }
}
