import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../theme"

Item {
    id: panel
    objectName: "visionAlgorithmPane"
    readonly property var service: App.visionAlgorithms
    readonly property bool detection: !!selected && selected.id === "yolov8"
    readonly property var selected: service.models[modelPicker.currentIndex]
    readonly property var evidence: App.projects.evidence.filter(function(row) {
        return row.kind === "image" || row.kind === "thermal"
    })
    property url inputFile
    property string inputName: ""
    property string lastModel: ""
    property url lastFile
    readonly property bool sameRun: lastFile.toString() === inputFile.toString()
        && !!selected && lastModel === selected.id
    readonly property bool canRun: !!selected && selected.ready && inputFile.toString().length > 0
        && !service.busy && !App.visionLive.running && !App.ai.busy && !App.agent.busy && !App.projectInference.busy
        && !!App.session.permissions.inspect

    function choose(url, name, fromProject) {
        if (!fromProject) evidencePicker.currentIndex = 0
        inputFile = url
        inputName = name
    }
    function run() {
        if (!canRun) return
        lastModel = selected.id
        lastFile = inputFile
        service.run(selected.id, inputFile)
    }

    Connections {
        target: App.session
        function onSessionEnding() {
            panel.inputFile = ""
            panel.inputName = ""
            panel.lastModel = ""
            panel.lastFile = ""
        }
    }
    onVisibleChanged: if (visible) service.refresh()

    FileDialog {
        id: picker
        title: "选择待分析图片"
        nameFilters: ["图片 (*.jpg *.jpeg *.png *.bmp *.webp)"]
        onAccepted: panel.choose(selectedFile, decodeURIComponent(selectedFile.toString().split("/").pop()))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 5
        GroupBox {
            title: "RK1828 视觉推理"
            Layout.fillWidth: true
            padding: 5
            ColumnLayout {
                width: parent.width
                spacing: 4
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    AppComboBox {
                        id: modelPicker
                        objectName: "visionAlgorithmModel"
                        width: 220; height: 27
                        model: panel.service.models
                        textRole: "title"
                        enabled: !panel.service.busy
                        delegate: ItemDelegate {
                            required property int index
                            required property var modelData
                            objectName: "visionAlgorithmChoice-" + modelData.id
                            width: modelPicker.width
                            height: 29
                            text: modelData.title
                            highlighted: modelPicker.highlightedIndex === index
                        }
                    }
                    TouchButton {
                        text: "打开图片"; autoWidth: true; height: 27
                        enabled: !panel.service.busy
                        onClicked: picker.open()
                    }
                    TouchButton {
                        objectName: "visionAlgorithmSample"
                        text: "模型样图"; autoWidth: true; height: 27
                        enabled: !!panel.selected && panel.selected.sample.toString().length > 0 && !panel.service.busy
                        onClicked: panel.choose(panel.selected.sample, panel.selected.title + " 样图")
                    }
                    TouchButton {
                        objectName: "visionAlgorithmRun"
                        text: "运行推理"; autoWidth: true; height: 27; variant: "primary"
                        enabled: panel.canRun
                        onClicked: panel.run()
                    }
                    TouchButton {
                        text: "停止"; autoWidth: true; height: 27
                        enabled: panel.service.busy
                        onClicked: panel.service.cancel()
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: panel.evidence.length > 0
                    Label { text: "工程图片" }
                    AppComboBox {
                        id: evidencePicker
                        Layout.fillWidth: true
                        height: 27
                        model: [{id:"",title:"选择本工程图片"}].concat(panel.evidence)
                        textRole: "title"
                        valueRole: "id"
                        enabled: !panel.service.busy
                        onActivated: {
                            if (!currentValue) return
                            const file = App.projects.evidenceFile(currentValue)
                            if (file.image) panel.choose(file.url, file.title, true)
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: !panel.selected ? "请选择模型" : !panel.selected.ready ? "模型组件未安装"
                        : panel.selected.task + "  /  " + (panel.sameRun ? panel.service.status : "等待推理")
                    color: Colors.textSecondary
                    elide: Text.ElideRight
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: width >= 675 ? 2 : 1
            columnSpacing: 5
            rowSpacing: 5
            GroupBox {
                title: "图像"
                padding: 5
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 100
                Layout.preferredWidth: 535
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 3
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "white"
                        border.color: "#b7b7b7"
                        Image {
                            anchors.fill: parent
                            anchors.margins: 2
                            source: panel.sameRun && panel.service.resultImage.toString().length
                                ? panel.service.resultImage : panel.inputFile
                            sourceSize.width: 1500
                            sourceSize.height: 1200
                            asynchronous: true
                            autoTransform: true
                            fillMode: Image.PreserveAspectFit
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: !panel.inputFile.toString().length
                            text: "选择图片或使用模型样图"
                            color: Colors.muted
                        }
                    }
                    Label { Layout.fillWidth: true; text: panel.inputName; elide: Text.ElideMiddle; visible: text.length > 0 }
                }
            }
            GroupBox {
                title: "推理结果"
                padding: 5
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                Layout.minimumHeight: 126
                Layout.preferredWidth: 330
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 4
                    DataTable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        rowHeight: 25
                        items: panel.sameRun ? panel.service.results : []
                        emptyText: panel.sameRun && panel.service.busy ? "正在执行推理…"
                            : panel.sameRun && panel.service.resultImage.toString().length ? "未检出目标" : "暂无结果"
                        columns: panel.detection
                            ? [{key:"rank",title:"序号",width:42}, {key:"label",title:"类别",width:0},
                               {key:"confidence",title:"置信度",width:65}, {key:"position",title:"位置（像素）",width:154}]
                            : [{key:"rank",title:"排名",width:42}, {key:"label",title:"类别",width:0},
                               {key:"confidence",title:"置信度",width:65}]
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.sameRun && panel.service.elapsedMs > 0
                            ? "总耗时 " + panel.service.elapsedMs + " ms（含模型加载）" : ""
                        visible: text.length > 0
                    }
                    Label {
                        Layout.fillWidth: true
                        text: panel.detection ? "类别来自 COCO。" : "类别来自 ImageNet。"
                        color: Colors.textSecondary
                    }
                }
            }
        }
    }
}
