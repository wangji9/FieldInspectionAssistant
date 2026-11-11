import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Flickable {
    id: overview
    required property var workspace
    property string message: ""
    signal recentRequested(url folder)
    signal stepRequested(string step, var device)
    signal evidenceRequested(string kind)
    signal deviceRequested(var device)
    signal previewRequested(string id)

    readonly property bool hasProject: !!workspace.project.id
    readonly property real usableWidth: Math.max(0, width - 12)
    readonly property bool twoColumns: usableWidth >= 680
    readonly property var files: workspace.evidence
    readonly property var events: workspace.events
    property string selectedDeviceId: ""
    property var inspectionCounts: ({})
    readonly property var selectedDevice: workspace.devices.find(device => device.id === selectedDeviceId)
        || workspace.devices.find(device => !!device.asset_id) || ({})
    function refreshInspection() {
        const asset = selectedDevice.asset_id
        if (!visible || !asset || !App.session.loggedIn) { inspectionCounts = ({}); return }
        inspectionCounts = {
            tasks: App.workspace.query("tasks", {asset_id:asset}, 0, 1).total || 0,
            inProgress: App.workspace.query("tasks", {asset_id:asset,status:"in_progress"}, 0, 1).total || 0,
            openDefects: App.workflow.overview(asset).open_defects || 0
        }
    }
    onSelectedDeviceChanged: refreshInspection()
    onVisibleChanged: refreshInspection()
    Connections {
        target: App.workspace
        function onDataChanged() { overview.refreshInspection() }
    }
    Connections {
        target: App.inspection
        function onTasksChanged() { overview.refreshInspection() }
        function onRecordChanged() { overview.refreshInspection() }
    }
    Connections {
        target: App.defects
        function onDefectsChanged() { overview.refreshInspection() }
    }
    readonly property var imageFiles: files.filter(function(file) {
        return file.kind === "image" || file.kind === "thermal"
    }).slice(0, 4)
    readonly property var stages: {
        const matches = row => !selectedDevice.id || row.device_id === selectedDevice.id
        const evidence = files.filter(matches).length
        const candidates = events.filter(matches)
        const diagnosed = candidates.filter(event => !!event.analyzed).length
        const related = candidates.filter(event => !!event.record_id).length
        const eventIds = candidates.map(event => event.id)
        const reports = workspace.reports.filter(report => eventIds.includes(report.event_id)).length
        const steps = [
            {id:"device", name:"登记设备与测点", status:selectedDevice.asset_id ? "已绑定台账" : "待绑定"},
            {id:"task", name:"派发与执行巡检", status:inspectionCounts.inProgress ? inspectionCounts.inProgress+" 项进行中"
                : inspectionCounts.tasks ? inspectionCounts.tasks+" 项任务" : "待派发"},
            {id:"evidence", name:"归档现场资料", status:evidence ? evidence+" 份资料" : "待采集"},
            {id:"diagnosis", name:"融合诊断", status:diagnosed ? diagnosed+" 项已诊断" : candidates.length ? "待诊断" : "待融合"},
            {id:"event", name:"事件与整改", status:inspectionCounts.openDefects ? inspectionCounts.openDefects+" 项未关闭"
                : related ? related+" 项已关联" : "待关联"},
            {id:"report", name:"查看事件分析报告", status:reports ? reports+" 份报告" : "待归档"}
        ]
        return steps.map((step, index) => Object.assign({order:index + 1}, step))
    }
    readonly property var distribution: {
        const kinds = [{id:"image",name:"照片"}, {id:"thermal",name:"热像"},
                       {id:"audio",name:"音频"}, {id:"signal",name:"信号"},
                       {id:"video",name:"视频"}, {id:"text",name:"文字"}]
        return kinds.map(function(kind) {
            return {id: kind.id, name: kind.name,
                    count: files.filter(function(file) { return file.kind === kind.id }).length}
        })
    }
    readonly property int tickInterval: Math.max(1, Math.ceil(Math.max.apply(null,
        distribution.map(function(kind) { return kind.count })) / 4))
    readonly property int axisMaximum: tickInterval * 4
    readonly property int categoryWidth: 64
    readonly property int countWidth: 30
    readonly property int workflowHeight: 212 + 112 + 5
    readonly property int thumbnailColumns: usableWidth >= (twoColumns ? 1580 : 960) ? 4 : 2
    readonly property real plotColumnWidth: twoColumns ? (usableWidth - 6) * 640 / 1050 : usableWidth
    readonly property int previewHeight: {
        if (!imageFiles.length) return 138
        const columns = Math.min(thumbnailColumns, imageFiles.length)
        const rows = Math.ceil(imageFiles.length / columns)
        const imageHeight = Math.max(115, Math.min(235, (plotColumnWidth - 24) / columns * 0.625))
        const preferred = Math.ceil(rows * (imageHeight + 38) + 30)
        if (!twoColumns || rows > 1) return preferred
        const remaining = height - workflowHeight - commandBar.implicitHeight - 21
        return Math.min(preferred, Math.max(173, remaining))
    }

    contentWidth: width
    contentHeight: Math.max(height, workArea.implicitHeight + 4)
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    clip: true

    Column {
        id: workArea
        width: overview.usableWidth
        spacing: 5

        Rectangle {
            width: workArea.width
            height: commandBar.implicitHeight + 10
            color: "#ededed"
            border.color: "#b7b7b7"
            ColumnLayout {
                id: commandBar
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 5 }
                spacing: 4
                Label {
                    Layout.fillWidth: true
                    text: overview.hasProject ? overview.workspace.project.name : "未打开工程"
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideRight
                }
                Label {
                    Layout.fillWidth: true
                    text: overview.hasProject
                        ? overview.workspace.project.description || overview.message
                        : overview.message || "从文件菜单新建、打开或导入工程。"
                    visible: text.length > 0
                    color: Colors.textSecondary
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

            }
        }

        GroupBox {
            visible: !overview.hasProject
            width: workArea.width
            height: 210
            title: "最近工程"
            DataTable {
                anchors.fill: parent
                items: overview.workspace.recentProjects
                emptyText: "暂无最近工程。请从文件菜单新建、打开或导入工程。"
                columns: [{key:"name",title:"工程名称",width:160}, {key:"path",title:"位置",width:0}]
                onRowDoubleClicked: function(index, item) { overview.recentRequested(item.url) }
            }
        }
        Item {
            id: projectPanels
            visible: overview.hasProject
            width: workArea.width
            readonly property int gap: 5
            readonly property real leftWidth: overview.plotColumnWidth
            readonly property real rightX: overview.twoColumns ? leftWidth + gap : 0
            readonly property real rightWidth: width - rightX
            readonly property int topHeight: overview.twoColumns ? overview.workflowHeight : 197
            readonly property real lowerY: overview.twoColumns ? topHeight + gap : workflow.y + workflow.height + gap
            height: activity.y + activity.height

            GroupBox {
                title: "现场资料分布"
                padding: 5
                width: projectPanels.leftWidth
                height: projectPanels.topHeight
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 2
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "white"
                        border.color: "#c6c6c6"
                        Item {
                            id: axisGrid
                            anchors { fill: parent; leftMargin: overview.categoryWidth; rightMargin: overview.countWidth }
                            Repeater {
                                model: 5
                                Rectangle {
                                    required property int index
                                    x: Math.round(axisGrid.width * index / 4)
                                    width: 1
                                    height: axisGrid.height
                                    color: index === 0 ? "#6c6c6c" : "#dedede"
                                }
                            }
                        }
                        Column {
                            id: bars
                            anchors { fill: parent; topMargin: 3; bottomMargin: 3 }
                            Repeater {
                                model: overview.distribution
                                delegate: Button {
                                    id: barButton
                                    required property var modelData
                                    objectName: "projectEvidenceBar-" + modelData.id
                                    width: bars.width
                                    height: bars.height / overview.distribution.length
                                    padding: 0
                                    enabled: modelData.count > 0
                                    onClicked: overview.evidenceRequested(modelData.id)
                                    Accessible.name: modelData.name + " " + modelData.count
                                    background: Rectangle {
                                        color: barButton.hovered || barButton.activeFocus ? "#1a2e5f91" : "transparent"
                                    }
                                    contentItem: RowLayout {
                                        spacing: 0
                                        Label {
                                            Layout.preferredWidth: overview.categoryWidth
                                            horizontalAlignment: Text.AlignRight
                                            rightPadding: 6
                                            text: barButton.modelData.name
                                        }
                                        Item {
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            Rectangle {
                                                anchors.verticalCenter: parent.verticalCenter
                                                height: Math.min(19, Math.max(9, parent.height * 0.55))
                                                width: parent.width * barButton.modelData.count / overview.axisMaximum
                                                color: barButton.hovered ? "#185a8c" : "#0072bd"
                                            }
                                        }
                                        Label {
                                            Layout.preferredWidth: overview.countWidth
                                            horizontalAlignment: Text.AlignHCenter
                                            text: barButton.modelData.count
                                        }
                                    }
                                }
                            }
                        }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 17
                        Item {
                            id: tickLabels
                            anchors { fill: parent; leftMargin: overview.categoryWidth; rightMargin: overview.countWidth }
                            Repeater {
                                model: 5
                                Label {
                                    required property int index
                                    x: Math.round(tickLabels.width * index / 4 - implicitWidth / 2)
                                    text: index * overview.tickInterval
                                    color: Colors.textSecondary
                                }
                            }
                        }
                        Label { anchors.right: parent.right; text: "份"; color: Colors.textSecondary }
                    }
                }
            }

            ColumnLayout {
                id: workflow
                x: projectPanels.rightX
                y: overview.twoColumns ? 0 : projectPanels.topHeight + projectPanels.gap
                width: projectPanels.rightWidth
                height: overview.workflowHeight
                spacing: 5

                GroupBox {
                    title: "巡检流程"
                    padding: 5
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredHeight: 186
                    Layout.minimumHeight: 186
                    DataTable {
                        anchors.fill: parent
                        rowHeight: 23
                        items: overview.stages
                        columns: [{key:"order",title:"序号",width:46}, {key:"name",title:"操作",width:0},
                                  {key:"status",title:"状态",width:96}]
                        onRowClicked: function(index, item) { if (overview.hasProject) overview.stepRequested(item.id, overview.selectedDevice) }
                    }
                }

                GroupBox {
                    title: "工程设备"
                    padding: 5
                    Layout.fillWidth: true
                    Layout.preferredHeight: 112
                    Layout.minimumHeight: 112
                    DataTable {
                        anchors.fill: parent
                        rowHeight: 23
                        items: overview.workspace.devices
                        emptyText: "暂无设备"
                        columns: [{key:"name",title:"设备名称",width:0}]
                        selectedIndex: items.findIndex(device => device.id === overview.selectedDevice.id)
                        onRowClicked: function(index, item) { overview.selectedDeviceId = item.id }
                        onRowDoubleClicked: function(index, item) { overview.deviceRequested(item) }
                    }
                }
            }

            GroupBox {
                id: previews
                title: "现场影像"
                padding: 5
                y: projectPanels.lowerY
                width: projectPanels.leftWidth
                height: overview.twoColumns ? Math.max(overview.previewHeight, 155) : overview.previewHeight

                Label {
                    anchors.centerIn: parent
                    width: parent.width - 16
                    visible: !overview.imageFiles.length
                    text: "归档照片或热像后可在这里查看。"
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Colors.textSecondary
                }
                Grid {
                    id: thumbnailGrid
                    anchors.fill: parent
                    visible: overview.imageFiles.length > 0
                    columns: Math.max(1, Math.min(overview.thumbnailColumns, overview.imageFiles.length))
                    rows: Math.max(1, Math.ceil(overview.imageFiles.length / columns))
                    spacing: 5
                    Repeater {
                        model: overview.imageFiles
                        delegate: Button {
                            id: imageButton
                            required property var modelData
                            readonly property var preview: overview.workspace.evidenceFile(modelData.id)
                            width: Math.max(0, (thumbnailGrid.width + thumbnailGrid.spacing) / thumbnailGrid.columns - thumbnailGrid.spacing)
                            height: Math.max(0, (thumbnailGrid.height + thumbnailGrid.spacing) / thumbnailGrid.rows - thumbnailGrid.spacing)
                            padding: 3
                            enabled: !!preview.url
                            onClicked: overview.previewRequested(modelData.id)
                            Accessible.name: modelData.title
                            background: Rectangle {
                                color: imageButton.hovered ? "#e9eff4" : "white"
                                border.color: imageButton.activeFocus ? Colors.normal : "#b7b7b7"
                            }
                            contentItem: ColumnLayout {
                                spacing: 2
                                Image {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    source: imageButton.preview.image ? imageButton.preview.url : ""
                                    asynchronous: true
                                    sourceSize.width: 640
                                    sourceSize.height: 400
                                    fillMode: Image.PreserveAspectFit
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: imageButton.modelData.title
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: imageButton.modelData.device + " / " + imageButton.modelData.point
                                    color: Colors.textSecondary
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            GroupBox {
                id: activity
                title: "操作记录"
                padding: 5
                x: projectPanels.rightX
                y: overview.twoColumns ? projectPanels.lowerY : previews.y + previews.height + projectPanels.gap
                width: projectPanels.rightWidth
                height: overview.twoColumns ? previews.height : 155
                DataTable {
                    anchors.fill: parent
                    rowHeight: 23
                    items: overview.workspace.logs
                    columns: [{key:"time",title:"时间",width:90,format:"datetime"},
                              {key:"action",title:"操作",width:78}, {key:"detail",title:"说明",width:0}]
                }
            }
        }
    }
}
