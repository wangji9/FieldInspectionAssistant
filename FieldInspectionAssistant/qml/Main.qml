import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Dialogs
import "theme"
import "components"
import "pages"

Window {
    id: rootWindow
    width: Dimensions.appWidth
    height: Dimensions.appHeight
    minimumWidth: 320
    minimumHeight: 240
    visible: false
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    title: qsTr("现场巡检助手")
    color: Colors.background
    property alias inputAssistant: inputAssistantPanel
    property bool showResources: true
    property bool showInspector: true
    property var editTarget: null
    property string commandNotice: ""
    readonly property var thermal: pageThermal.source
    readonly property string section: activeSection()
    readonly property string subsection: activeSubsection()
    readonly property bool thermalPage: App.nav.page === "thermal"
    readonly property bool canStopCapture: thermal.connected || App.speech.recording || App.visionLive.running
        || App.camera.recording || App.analysis.running || App.videoUnderstanding.running || App.faceAccess.running
    readonly property var statusValues: [
        "工程：" + (App.projects.project.name || "未打开"),
        "设备：" + (thermal.device || (App.evidence.cameraConnected ? "摄像头" : "—")),
        "连接：" + (thermal.connected || App.evidence.cameraConnected ? "已连接" : "未连接"),
        "采集：" + (thermal.paused ? "暂停" : thermal.connected && thermal.fps > 0 ? "运行中"
            : thermal.connected ? "等待数据" : App.analysis.running || App.camera.recording || App.speech.recording || App.visionLive.running || App.videoUnderstanding.running ? "运行中" : "停止"),
        "AI：" + (App.ai.available || App.projectInference.ready ? "在线" : "离线"),
        "数据库：" + (App.dataDir ? "已打开" : "不可用"),
        thermal.connected ? "FPS：" + thermal.fps.toFixed(1)
            : App.visionLive.running && App.visionLive.summary.inferenceFps > 0
                ? "FPS：" + App.visionLive.summary.inferenceFps.toFixed(1) : "FPS：—"
    ]
    readonly property var sections: [
        {id: "projects", title: "工程与采集"},
        {id: "tasks", title: "巡检管理"},
        {id: "fusion", title: "智能分析"},
        {id: "ai", title: "大模型诊断"},
        {id: "events", title: "事件管理"},
        {id: "reports", title: "巡检报告"}
    ]
    readonly property var subsections: ({
        projects: [{id:"projects", title:"工程总览"}, {id:"devices", title:"设备管理"},
                   {id:"camera", title:"视频与图像"}, {id:"audioCapture", title:"声音采集"},
                   {id:"thermal", title:"热成像采集"}, {id:"projectEvidence", title:"资料与文字"},
                   {id:"projectGraph", title:"多模态图谱"}],
        fusion: [{id:"fusion", title:"多模态融合"}, {id:"visionAlgorithms", title:"视觉算法"},
                 {id:"analysis", title:"声音与信号"}, {id:"thermalReview", title:"热像分析"}],
        ai: [{id:"projectDiagnosis", title:"工程诊断"}, {id:"ai", title:"大模型与助手"}],
        events: [{id:"events", title:"分析事件"}, {id:"defects", title:"缺陷与整改"}],
        tasks: [{id:"tasks", title:"巡检任务"}, {id:"history", title:"巡检记录"}],
        reports: [{id:"reports", title:"工程报告"}, {id:"formalReports", title:"正式巡检报告"}]
    })
    Binding { target: Dimensions; property: "windowWidth"; value: rootWindow.width }
    Binding { target: Dimensions; property: "windowHeight"; value: rootWindow.height }
    onClosing: {
        App.faceAccess.stop()
        App.videoUnderstanding.stop()
        App.visionLive.stop()
        pageThermal.close()
        pageLegacy.close()
        pageAi.persistAll()
        pageDevices.manager.close()
        pageDevices.deviceEditor.close()
        pageDevices.recordDetail.close()
        pageDevices.newInspection.close()
    }
    Connections {
        target: App.nav
        function onPageChanged() {
            if (App.nav.page === "legacyThermal") pageThermal.close()
            else if (App.nav.page === "thermal") pageLegacy.close()
        }
    }
    onActiveFocusItemChanged: {
        const item = activeFocusItem
        if (item && typeof item.copy === "function") editTarget = item
    }

    function activeSection() {
        const selected = subsection
        const group = sections.find(group => subsections[group.id].some(item => item.id === selected))
        return group ? group.id : selected
    }
    function activeSubsection() {
        const page = App.nav.page
        if (page === "operations")
            return ({device:"devices", defects:"defects", tasks:"tasks", records:"history"})[pageDevices.view] || "history"
        if (page === "dashboard" || page === "overview") return "projects"
        if (page === "analysis" && App.nav.param("capture")) return "audioCapture"
        if (page === "thermal" && App.nav.param("review")) return "thermalReview"
        if (page === "ai" && pageAi.mode === "algorithms") return "visionAlgorithms"
        if (page === "product" && App.nav.param("section") === "reports") return "formalReports"
        if (page === "inspection") return "tasks"
        return page
    }
    function navigate(page) {
        if (page === "ai") App.nav.push("projectDiagnosis")
        else navigateSubsection(page)
    }
    function navigateSubsection(page) {
        if (page === "ai") App.nav.push("ai", {mode:"agent"})
        else if (page === "visionAlgorithms") App.nav.push("ai", {mode:"algorithms"})
        else if (page === "audioCapture") App.nav.push("analysis", {analysis_tab:0, capture:true})
        else if (page === "analysis") App.nav.push("analysis", {analysis_tab:1})
        else if (page === "thermalReview") thermalSetup("image", true)
        else if (page === "formalReports") App.nav.push("product", {section:"reports"})
        else App.nav.push(page)
    }
    function pageIndex() {
        const map = {overview: 0, tasks: 1, inspection: 2, camera: 3,
                     operations: 4, devices: 4, events: 13, defects: 5, history: 6, settings: 7,
                     ai: 8, analysis: 9, users: 10, product: 11, thermal: 12,
                     dashboard: 13, projects: 13, projectEvidence: 13, fusion: 13, reports: 13, legacyThermal: 14}
        return map[App.nav.page] ?? 13
    }
    function value(key) {
        const reading = thermal.stats[key]
        return reading === undefined ? "—" : Number(reading).toFixed(2) + " °C"
    }
    function editProject() {
        properties.currentIndex = 0
        showInspector = true
        App.nav.push("projects")
    }
    function saveProject() {
        pageProject.showResult(App.projects.saveProject(projectName.text, projectDescription.text), "工程已保存")
    }
    function newProject() { pageProject.newProject() }
    function openProject() { pageProject.openProject() }
    function importProject() { pageProject.importProject() }
    function openDemoProject() { pageProject.openDemoProject() }
    function closeProject() { App.projects.closeProject(); App.nav.push("projects") }
    function openRecent(url) {
        App.nav.push("projects")
        pageProject.showResult(App.projects.openProject(url), "工程已打开")
    }
    function copyProject(archive) {
        if (archive) archiveFolder.open()
        else {
            copyName.text = (App.projects.project.name || "工程") + " 副本"
            copyDialog.open()
        }
    }
    function canEdit(operation) {
        const item = editTarget
        if (!item || !item.visible || !item.enabled || typeof item[operation] !== "function") return false
        if (operation === "copy" || operation === "remove")
            return item.selectionEnd > item.selectionStart && (operation === "copy" || !item.readOnly)
        if (item.readOnly) return false
        return operation === "undo" ? !!item.canUndo : operation === "redo" ? !!item.canRedo : !!item.canPaste
    }
    function edit(operation) {
        if (!canEdit(operation)) return
        if (operation === "remove") editTarget.remove(editTarget.selectionStart, editTarget.selectionEnd)
        else editTarget[operation]()
    }
    function find() { findDialog.open() }
    function restoreLayout() {
        showResources = true
        showInspector = true
        resourcePane.SplitView.preferredWidth = Qt.binding(function() { return rootWindow.width >= 1250 ? 190 : 132 })
        inspectorPane.SplitView.preferredWidth = Qt.binding(function() { return rootWindow.width >= 1250 ? 200 : 150 })
        commandNotice = "已恢复默认布局"
    }
    function showOutput() { logDialog.open() }
    function thermalSetup(kind, review) {
        App.nav.push("thermal", review ? {review:true} : {})
        if (kind === "software") pageThermal.showSoftwareSetup()
        else if (kind === "image") pageThermal.showImage()
        else pageThermal.showDeviceSetup(kind === "test")
    }
    function startCapture() {
        thermalSetup("image")
        Qt.callLater(function() { pageThermal.begin(false) })
    }
    function stopCapture() {
        if (App.faceAccess.running) App.faceAccess.stop()
        else if (App.videoUnderstanding.running) App.videoUnderstanding.stop()
        else if (App.visionLive.running) App.visionLive.stop()
        else if (App.nav.page === "camera" && App.camera.recording) App.camera.stopVideo()
        else if (App.nav.page === "analysis" && App.analysis.running) App.analysis.stop()
        else if (thermal.connected) pageThermal.close()
        else if (App.speech.recording) App.speech.stopRecording()
        else if (App.camera.recording) App.camera.stopVideo()
        else if (App.analysis.running) App.analysis.stop()
    }
    function takePhoto() {
        App.nav.push("camera")
        if (App.evidence.cameraConnected && App.camera.hasFrame && App.camera.savePhotoAsync())
            commandNotice = "正在保存照片"
        else commandNotice = App.camera.status || "请连接摄像头后拍照"
    }
    function toggleRecording() {
        if (App.speech.recording) App.speech.stopRecording()
        else App.speech.startRecording(false)
    }
    function saveCapture() { thermalSetup("image"); pageThermal.exportData(false) }
    function addDevice() {
        App.nav.push("devices")
        Qt.callLater(function() { pageDevices.deviceEditor.openDevice({}) })
    }
    function openTask(create) {
        App.nav.push("tasks", {asset_id: pageDevices.asset.id || ""})
        Qt.callLater(function() {
            if (!pageDevices.asset.id) commandNotice = "请先在设备列表选择设备"
            else if (create) pageDevices.openManager("tasks", true)
            else pageDevices.openInspection()
        })
    }
    function newReport() { pageProject.newReport() }
    function registerAnalysisEvent() {
        const entry = (subsections[section] || []).find(item => item.id === subsection)
        pageProject.registerAnalysis(entry ? entry.title : "人工观察")
    }
    function showReport() {
        App.nav.push("reports")
        reportDialog.body = App.projects.reportHtml(pageProject.selectedReport)
        if (reportDialog.body) reportDialog.open()
        else commandNotice = "报告文件不可访问"
    }
    function exportPdf() {
        pdfPicker.reportId = pageProject.selectedReport
        pdfPicker.open()
    }
    function rk1828() {
        App.projectInference.refresh()
        App.nav.push("settings", {section:"runtime"})
    }
    function closeTab() {
        if (App.nav.page === "thermal") pageThermal.close()
        if (App.nav.page === "legacyThermal") pageLegacy.close()
        App.nav.push("projects")
    }
    function showHelp() { helpDialog.open() }
    function showVersion() { versionDialog.open() }

    Shortcut { sequence: StandardKey.New; enabled: App.session.loggedIn; onActivated: rootWindow.newProject() }
    Shortcut { sequence: StandardKey.Open; enabled: App.session.loggedIn; onActivated: rootWindow.openProject() }
    Shortcut { sequence: StandardKey.Save; enabled: App.session.loggedIn && !!App.projects.project.id; onActivated: rootWindow.saveProject() }
    Shortcut { sequence: StandardKey.Find; enabled: App.session.loggedIn; onActivated: rootWindow.find() }

    ColumnLayout {
        anchors.fill: parent
        visible: App.session.loggedIn
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: menuStrip.implicitHeight
            color: Colors.panelAlt
            border.color: Colors.border
            RowLayout {
                id: menuStrip
                anchors.fill: parent
                spacing: 0
                WorkbenchMenuBar { Layout.fillWidth: true; workbench: rootWindow }
                Label {
                    objectName: "menuAccountName"
                    Layout.rightMargin: 10; Layout.leftMargin: 6
                    Layout.maximumWidth: Math.min(150, rootWindow.width * .18)
                    text: App.session.userName
                    elide: Text.ElideRight
                    font.family: Typography.uiFamily
                    font.pixelSize: Typography.small
                }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 4
            orientation: Qt.Horizontal
            Rectangle {
                id: resourcePane
                visible: rootWindow.showResources
                SplitView.preferredWidth: rootWindow.width >= 1250 ? 190 : 132
                SplitView.minimumWidth: 95
                color: Colors.panel
                border.color: Colors.border
                ProjectResources {
                    anchors.fill: parent
                    anchors.margins: 1
                    workspace: App.projects
                    onResourceActivated: function(type, id) { pageProject.openResource(type, id) }
                }
            }

            Rectangle {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 360
                color: Colors.panel
                border.color: Colors.border
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 0
                    AppTabs {
                        Layout.fillWidth: true
                        sections: rootWindow.sections
                        selected: rootWindow.section
                        onNavigate: function(route) { rootWindow.navigate(route) }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 4
                        visible: workspaceSubtabs.sections.length > 0
                        AppTabs {
                            id: workspaceSubtabs
                            Layout.fillWidth: true
                            sections: rootWindow.subsections[rootWindow.section] || []
                            selected: rootWindow.subsection
                            objectName: "workspaceSubtabs"
                            objectPrefix: "subTab_"
                            onNavigate: function(route) { rootWindow.navigateSubsection(route) }
                        }
                        TouchButton {
                            visible: ["fusion","ai","events"].includes(rootWindow.section)
                            enabled: !!App.projects.project.id && !!App.session.permissions.inspect
                            text: "手动登记事件"; autoWidth: true; height: 25
                            onClicked: rootWindow.registerAnalysisEvent()
                        }
                    }
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: rootWindow.pageIndex()
                        DashboardPage {}
                        TaskListPage {}
                        InspectionPage {}
                        CameraCapturePage {
                            onPhotoForQuestion: function(path) { if (pageAi.importPhoto(path)) App.nav.push("ai") }
                            onProjectCapture: function(path, kind) { pageProject.attachCapture(path, kind) }
                        }
                        OperationsPage { id: pageDevices }
                        DefectPage {}
                        HistoryPage {}
                        SettingsPage {}
                        AiModelPage { id: pageAi }
                        SignalAnalysisPage { onProjectCapture: function(path, kind) { pageProject.attachCapture(path, kind) } }
                        UserManagementPage {}
                        ProductPage {}
                        ThermalAnalysisPage {
                            id: pageThermal
                            onProjectCapture: function(paths, kind) { pageProject.attachCapture(paths, kind) }
                        }
                        ProjectPage {
                            id: pageProject
                            onPreviewReportRequested: rootWindow.showReport()
                        }
                        LegacyThermalPage { id: pageLegacy }
                    }
                }
            }

            Rectangle {
                id: inspectorPane
                visible: rootWindow.showInspector && !pageAi.wideVisionPreview
                SplitView.preferredWidth: rootWindow.width >= 1250 ? 200 : 150
                SplitView.minimumWidth: 126
                color: Colors.panel
                border.color: Colors.border
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 1
                    spacing: 3
                    TabBar {
                        id: properties
                        Layout.fillWidth: true
                        DesktopTab { text: "属性" }
                        DesktopTab { text: "分析结果" }
                    }
                    ScrollView {
                        id: inspectorScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        clip: true
                        ColumnLayout {
                            width: Math.max(0, inspectorScroll.availableWidth - 8)
                            x: 4
                            spacing: 6
                            ColumnLayout {
                                visible: properties.currentIndex === 0 && !rootWindow.thermalPage
                                Layout.fillWidth: true
                                Label { text: "工程名称" }
                                TextField {
                                    id: projectName
                                    Layout.fillWidth: true
                                    text: App.projects.project.name || ""
                                    placeholderText: "未打开工程"
                                    enabled: !!App.projects.project.id
                                    maximumLength: 80
                                }
                                Label { text: "工程说明" }
                                TextArea {
                                    id: projectDescription
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 92
                                    enabled: !!App.projects.project.id
                                    text: App.projects.project.description || ""
                                    wrapMode: TextEdit.Wrap
                                    background: Rectangle { color: Colors.panel; border.color: Colors.border }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: App.projects.project.path || ""
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                            ColumnLayout {
                                visible: properties.currentIndex === 0 && rootWindow.thermalPage
                                Layout.fillWidth: true
                                Label { text: "设备：LEPTON2.5" }
                                Label { text: "串口：" + (thermal.device || "—") }
                                Label { text: "设备 ID：" + (thermal.stats.id ?? "—") }
                                Repeater {
                                    model: ["MAX", "MIN", "AVG", "CENTER", "S1", "S2", "S3", "S4", "S5", "ROI Max", "ROI Min", "ROI Avg"]
                                    Label { required property string modelData; text: modelData + "：" + rootWindow.value(modelData) }
                                }
                                Label { text: "ROI：" + (thermal.stats.roi || "—") }
                            }
                            ColumnLayout {
                                visible: properties.currentIndex === 1
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: rootWindow.thermalPage ? thermal.status : App.projectInference.status
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: rootWindow.thermalPage
                                        ? ("近 10 秒均温变化：" + rootWindow.value("trendDelta") + "\nROI 均温：" + rootWindow.value("ROI Avg") + "\n异常判断：未配置阈值")
                                        : ((App.projectInference.subjectKind === "event" && App.projectInference.answer) || "选择工程融合事件后可查看分析结果。")
                                }
                                TouchButton {
                                    visible: rootWindow.thermalPage
                                    text: "打开融合分析"
                                    autoWidth: true
                                    onClicked: App.nav.push("fusion")
                                }
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 23
            border.color: Colors.border
            color: Colors.panelAlt
            RowLayout {
                anchors { fill: parent; leftMargin: 5; rightMargin: 5 }
                spacing: 7
                Repeater {
                    model: 7
                    Label {
                        required property int index
                        Layout.fillWidth: true
                        text: rootWindow.statusValues[index]
                        elide: Text.ElideRight
                        font.pixelSize: Typography.small
                    }
                }
                Label {
                    readonly property string notice: App.speech.recording ? "录音 " + App.speech.seconds + " 秒"
                        : rootWindow.thermalPage ? (thermal.autoSaveStatus || pageThermal.notice || rootWindow.commandNotice)
                        : (rootWindow.commandNotice || pageProject.message)
                    visible: !!notice && rootWindow.width > 1100
                    Layout.preferredWidth: Math.min(310, implicitWidth)
                    text: notice
                    elide: Text.ElideMiddle
                    ToolTip.visible: noticePointer.containsMouse
                    ToolTip.text: notice
                    MouseArea { id: noticePointer; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                }
            }
        }
    }
    Dialog {
        id: copyDialog
        title: "工程另存为"
        anchors.centerIn: parent
        width: Math.min(390, rootWindow.width - 20)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: copyFolder.open()
        TextField { id: copyName; width: parent.width; maximumLength: 80; placeholderText: "新工程名称" }
    }
    FolderDialog {
        id: copyFolder
        title: "选择工程副本存放目录"
        onAccepted: {
            const result = App.projects.copyProject(selectedFolder, copyName.text, projectDescription.text, false)
            pageProject.showResult(result, "已另存并打开工程副本")
            if (result.ok) App.nav.push("projects")
        }
    }
    FolderDialog {
        id: archiveFolder
        title: "选择工程归档目录"
        onAccepted: {
            const result = App.projects.copyProject(selectedFolder, "", "", true)
            pageProject.showResult(result, result.ok ? "工程已归档：" + result.path : "")
        }
    }
    FileDialog {
        id: pdfPicker
        property string reportId
        title: "导出 PDF"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: ["PDF 文件 (*.pdf)"]
        onAccepted: {
            const result = App.projects.exportReportPdf(reportId, selectedFile)
            rootWindow.commandNotice = result.ok ? "PDF 已导出：" + result.path : result.error
        }
    }
    Dialog {
        id: reportDialog
        property string body: ""
        title: "报告预览"
        anchors.centerIn: parent
        width: Math.min(760, rootWindow.width - 20)
        height: Math.min(620, rootWindow.height - 20)
        standardButtons: Dialog.Close
        modal: true
        ScrollView {
            anchors.fill: parent
            clip: true
            TextArea {
                text: reportDialog.body
                textFormat: TextEdit.RichText
                font.family: Typography.uiFamily
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
            }
        }
    }
    Dialog {
        id: findDialog
        title: "查找工程资料"
        anchors.centerIn: parent
        width: Math.min(590, rootWindow.width - 20)
        height: Math.min(395, rootWindow.height - 20)
        standardButtons: Dialog.Close
        modal: true
        property var matches: []
        onOpened: { keyword.clear(); matches = [] }
        function search() {
            const text = keyword.text.trim().toLowerCase()
            if (!text) { matches = []; return }
            const source = []
            App.projects.devices.forEach(function(row) { source.push({name:row.name, kind:"设备", route:"projects"}) })
            App.projects.evidence.forEach(function(row) {
                source.push({name:row.title + " / " + row.point, kind:"证据", route:"projectEvidence"})
            })
            App.projects.reports.forEach(function(row) { source.push({name:row.path, kind:"报告", route:"reports", id:row.id}) })
            matches = source.filter(function(row) { return row.name.toLowerCase().includes(text) })
        }
        ColumnLayout {
            anchors.fill: parent
            RowLayout {
                Layout.fillWidth: true
                TextField { id: keyword; Layout.fillWidth: true; placeholderText: "输入设备、证据或报告名称"; onAccepted: findDialog.search() }
                TouchButton { text: "查找"; autoWidth: true; onClicked: findDialog.search() }
            }
            DataTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                items: findDialog.matches
                columns: [{key:"kind",title:"类别",width:70}, {key:"name",title:"名称",width:0}]
                onRowDoubleClicked: function(index, row) {
                    findDialog.close()
                    if (row.id) pageProject.reportId = row.id
                    App.nav.push(row.route)
                }
            }
        }
    }
    Dialog {
        id: helpDialog
        title: "用户手册"
        anchors.centerIn: parent
        width: Math.min(540, rootWindow.width - 20)
        standardButtons: Dialog.Close
        modal: true
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: "1. 在「文件」新建或打开 .miproj 工程。\n\n"
                + "2. 在「设备」连接热像仪并测试，在「采集」控制记录。\n\n"
                + "3. 在工程的证据管理页导入文件，在「分析」关联证据或执行诊断。\n\n"
                + "4. 在「报告」选择报告、预览或导出 PDF。"
        }
    }
    Dialog {
        id: versionDialog
        title: "版本信息"
        anchors.centerIn: parent
        width: Math.min(390, rootWindow.width - 20)
        standardButtons: Dialog.Close
        modal: true
        Label { text: "现场巡检助手 " + App.version + "\nMIIEP 工程工作区\n平台：" + Qt.platform.os }
    }
    Dialog {
        id: logDialog
        title: "输出窗口"
        anchors.centerIn: parent
        width: Math.min(700, rootWindow.width - 20)
        height: Math.min(420, rootWindow.height - 20)
        standardButtons: Dialog.Close
        modal: true
        DataTable {
            anchors.fill: parent
            items: App.projects.project.id ? App.projects.logs : [
                {action:"AI 推理", detail:App.ai.statusText},
                {action:"热像采集", detail:thermal.status},
                {action:"数据库", detail:App.dataDir}
            ]
            columns: [{key:"time",title:"时间",width:125,format:"datetime"},
                      {key:"action",title:"操作",width:112}, {key:"detail",title:"详情",width:0}]
        }
    }
    LoginPage { anchors.fill: parent; visible: !App.session.loggedIn }
    InputAssistant { id: inputAssistantPanel }
}
