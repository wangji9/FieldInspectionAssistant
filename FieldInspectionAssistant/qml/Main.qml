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
    property bool showInspector: false
    property var editTarget: null
    property string commandNotice: ""
    readonly property var thermal: pageThermal.source
    readonly property string section: activeSection()
    readonly property string subsection: activeSubsection()
    readonly property bool thermalPage: App.nav.page === "thermal"
    readonly property bool canStopCapture: thermal.connected || App.speech.recording || App.visionLive.running
        || App.camera.recording || App.analysis.running || App.videoUnderstanding.running || App.faceAccess.running
        || App.speakerRecognition.capturing
    readonly property var statusValues: [
        "工程：" + (App.projects.project.name || "未打开"),
        "设备：" + (thermal.device || (App.evidence.cameraConnected ? "摄像头" : "—")),
        "连接：" + (thermal.connected || App.evidence.cameraConnected ? "已连接" : "未连接"),
        "采集：" + (thermal.paused ? "暂停" : thermal.connected && thermal.fps > 0 ? "运行中"
            : thermal.connected ? "等待数据" : App.analysis.running || App.camera.recording || App.speech.recording || App.visionLive.running || App.videoUnderstanding.running || App.speakerRecognition.capturing ? "运行中" : "停止"),
        "AI：" + (App.ai.available || App.projectInference.ready ? "在线" : "离线"),
        "数据库：" + (App.dataDir ? "已打开" : "不可用"),
        thermal.connected ? "FPS：" + thermal.fps.toFixed(1)
            : App.visionLive.running && App.visionLive.summary.inferenceFps > 0
                ? "FPS：" + App.visionLive.summary.inferenceFps.toFixed(1) : "FPS：—"
    ]
    readonly property var sections: [
        {id: "projects", title: "工程与采集"},
        {id: "tasks", title: "巡检执行"},
        {id: "diagnostics", title: "分析与诊断"},
        {id: "events", title: "事件与整改"},
        {id: "reports", title: "报告归档"}
    ]
    readonly property var subsections: ({
        projects: [{id:"projects", title:"工程总览"}, {id:"devices", title:"设备台账"},
                   {id:"camera", title:"视频与图像"}, {id:"analysis", title:"声音与信号"},
                   {id:"thermal", title:"热像采集与分析"}, {id:"projectEvidence", title:"工程资料"}],
        tasks: [{id:"tasks", title:"巡检任务"}, {id:"history", title:"巡检记录"}],
        diagnostics: [{id:"projectDiagnosis", title:"融合诊断"}, {id:"visionAlgorithms", title:"视觉分析"},
                      {id:"ai", title:"AI 助手"}],
        events: [{id:"events", title:"事件台账"}, {id:"defects", title:"缺陷整改"}],
        reports: [{id:"formalReports", title:"正式巡检报告"}, {id:"reports", title:"事件分析报告"}]
    })
    readonly property bool canRegisterEvent: section === "diagnostics" || subsection === "events"
        || subsection === "analysis" || subsection === "thermal"
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
            if (!rootWindow.thermalPage) properties.currentIndex = 0
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
        if (page === "projectGraph") return "projectEvidence"
        if (page === "ai" && pageAi.mode === "algorithms") return "visionAlgorithms"
        if (page === "product" && App.nav.param("section") === "reports") return "formalReports"
        if (page === "inspection") return "tasks"
        return page
    }
    function navigate(page) {
        if (page === "diagnostics") navigateSubsection("projectDiagnosis")
        else if (page === "reports") navigateSubsection("formalReports")
        else navigateSubsection(page)
    }
    function navigateSubsection(page) {
        if (page === "ai") App.nav.push("ai", {mode:"agent"})
        else if (page === "visionAlgorithms") App.nav.push("ai", {mode:"algorithms"})
        else if (page === "analysis") App.nav.push("analysis", {analysis_tab:0})
        else if (page === "formalReports") App.nav.push("product", {section:"reports"})
        else App.nav.push(page)
    }
    function pageIndex() {
        const pages = {
            operations: pageDevices, inspection: pageInspection, camera: pageCamera,
            settings: pageSettings, ai: pageAi, analysis: pageSignals,
            product: pageProduct, thermal: pageThermal, legacyThermal: pageLegacy
        }
        const target = pages[App.nav.page] || pageProject
        return target.StackLayout.index
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
        showInspector = false
        resourcePane.SplitView.preferredWidth = Qt.binding(function() { return rootWindow.width >= 1250 ? 190 : 132 })
        inspectorPane.SplitView.preferredWidth = Qt.binding(function() { return rootWindow.width >= 1250 ? 200 : 150 })
        commandNotice = "已恢复默认布局"
    }
    function showOutput() { logDialog.open() }
    function thermalSetup(kind) {
        App.nav.push("thermal")
        if (kind === "image") pageThermal.showImage()
        else pageThermal.showDeviceSetup(kind === "test")
    }
    function startCapture() {
        thermalSetup("image")
        Qt.callLater(function() { pageThermal.begin(false) })
    }
    function stopCapture() {
        if (App.nav.page === "analysis" && App.speakerRecognition.capturing) App.speakerRecognition.stopLive()
        else if (App.faceAccess.running) App.faceAccess.stop()
        else if (App.videoUnderstanding.running) App.videoUnderstanding.stop()
        else if (App.visionLive.running) App.visionLive.stop()
        else if (App.nav.page === "camera" && App.camera.recording) App.camera.stopVideo()
        else if (App.nav.page === "analysis" && App.analysis.running) App.analysis.stop()
        else if (thermal.connected) pageThermal.close()
        else if (App.speech.recording) App.speech.stopRecording()
        else if (App.camera.recording) App.camera.stopVideo()
        else if (App.analysis.running) App.analysis.stop()
    }
    function addDevice() {
        App.nav.push("devices")
        Qt.callLater(function() { pageDevices.deviceEditor.openDevice({}) })
    }
    function registerAnalysisEvent() {
        const entry = (subsections[section] || []).find(item => item.id === subsection)
        const useSelectedEvent = subsection === "projectDiagnosis" || subsection === "events"
        pageProject.registerAnalysis(entry ? entry.title : "人工观察", useSelectedEvent)
    }
    function showReport() {
        App.nav.push("reports")
        reportDialog.body = App.projects.reportHtml(pageProject.selectedReport, pageProject.reportTemplate)
        if (reportDialog.body) reportDialog.open()
        else commandNotice = "报告文件不可访问"
    }
    function exportPdf() {
        pdfPicker.reportId = pageProject.selectedReport
        pdfPicker.templateId = pageProject.reportTemplate
        pdfPicker.open()
    }
    function rk1828() {
        App.projectInference.refresh()
        App.nav.push("settings", {section:"runtime"})
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
                            objectName: "registerAnalysisEvent"
                            visible: rootWindow.canRegisterEvent
                            enabled: !!App.projects.project.id && !!App.session.permissions.inspect
                            text: "手动登记事件"; autoWidth: true; height: 25
                            onClicked: rootWindow.registerAnalysisEvent()
                        }
                    }
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: rootWindow.pageIndex()
                        InspectionPage { id: pageInspection }
                        CameraCapturePage {
                            id: pageCamera
                            onPhotoForQuestion: function(path) { if (pageAi.importPhoto(path)) App.nav.push("ai") }
                            onProjectCapture: function(path, kind) { pageProject.attachCapture(path, kind) }
                        }
                        OperationsPage { id: pageDevices }
                        SettingsPage { id: pageSettings }
                        AiModelPage { id: pageAi }
                        SignalAnalysisPage {
                            id: pageSignals
                            onProjectCapture: function(path, kind) { pageProject.attachCapture(path, kind) }
                        }
                        ProductPage { id: pageProduct }
                        ThermalAnalysisPage {
                            id: pageThermal
                            onProjectCapture: function(paths, kind) { pageProject.attachCapture(paths, kind) }
                        }
                        ProjectPage {
                            id: pageProject
                            onPreviewReportRequested: rootWindow.showReport()
                            onExportReportRequested: rootWindow.exportPdf()
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
                        DesktopTab { text: "热像读数"; visible: rootWindow.thermalPage }
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
                                visible: properties.currentIndex === 1 && rootWindow.thermalPage
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: thermal.status
                                }
                                Label {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: "近 10 秒均温变化：" + rootWindow.value("trendDelta")
                                        + "\nROI 均温：" + rootWindow.value("ROI Avg")
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
        property string templateId
        title: "导出 PDF"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: ["PDF 文件 (*.pdf)"]
        onAccepted: {
            const result = App.projects.exportReportPdf(reportId, selectedFile, templateId)
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
            App.projects.devices.forEach(function(row) { source.push({name:row.name, kind:"设备", type:"device", id:row.id}) })
            App.projects.evidence.forEach(function(row) {
                source.push({name:row.title + " / " + row.point, kind:"资料", type:"evidence", id:row.id})
            })
            App.projects.reports.forEach(function(row) { source.push({name:row.path, kind:"报告", type:"report", id:row.id}) })
            matches = source.filter(function(row) { return row.name.toLowerCase().includes(text) })
        }
        ColumnLayout {
            anchors.fill: parent
            RowLayout {
                Layout.fillWidth: true
                TextField { id: keyword; Layout.fillWidth: true; placeholderText: "输入设备、资料或报告名称"; onAccepted: findDialog.search() }
                TouchButton { text: "查找"; autoWidth: true; onClicked: findDialog.search() }
            }
            DataTable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                items: findDialog.matches
                columns: [{key:"kind",title:"类别",width:70}, {key:"name",title:"名称",width:0}]
                onRowDoubleClicked: function(index, row) {
                    findDialog.close()
                    pageProject.openResource(row.type, row.id)
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
            text: "1. 打开工程，在设备台账登记设备。\n\n"
                + "2. 在「巡检执行」派发任务、填写巡检记录，并把现场资料归到相应设备与测点。\n\n"
                + "3. 从工程资料发起融合诊断，再在事件台账复核结果、关联巡检记录和缺陷。\n\n"
                + "4. 在「报告归档」查看正式巡检报告或导出事件分析报告。"
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
