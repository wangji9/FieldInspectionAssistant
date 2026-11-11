import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FieldInspection
import "../theme"
import "../components"

Rectangle {
    id: page
    objectName: "projectPage"
    color: Colors.background
    readonly property var workspace: App.projects
    readonly property var inference: App.projectInference
    readonly property string projectId: workspace.project.id || ""
    property string message: ""
    property string evidenceId: ""
    property string eventId: ""
    property string deviceId: ""
    property string reportId: ""
    property string evidenceKind: ""
    property string eventFilter: ""
    property string eventDeviceId: ""
    property string inspectionFilter: ""
    property string evidenceRecordId: ""
    property string evidenceAssetId: ""
    property string graphDeviceId: ""
    readonly property string route: App.nav.page
    readonly property bool eventMode: route === "events"
    readonly property bool diagnosisMode: route === "projectDiagnosis"
    readonly property var activeEvent: workspace.events.find(row => row.id === page.eventId) || ({})
    readonly property bool canOpenActiveEvent: isRegisteredEvent(activeEvent)
    readonly property string liveDiagnosis: inference.eventId === eventId ? inference.answer : ""
    readonly property bool hasDiagnosis: !!liveDiagnosis || !!activeEvent.analyzed
    readonly property string diagnosisText: liveDiagnosis || (activeEvent.analyzed
        ? workspace.findingForEvent(eventId) : activeEvent.id
        ? (workspace.analysisInput(eventId).summary || activeEvent.note || "")
        : "选择诊断对象后，可核对其关联证据。")
    readonly property var visibleEvents: workspace.events.filter(function(row) {
        return (!page.eventDeviceId || row.device_id === page.eventDeviceId)
            && (!page.eventMode || page.isRegisteredEvent(row))
            && (!page.eventMode || !page.eventFilter || page.eventFilter === row.classification)
            && (!page.eventMode || !page.inspectionFilter || page.inspectionFilter === row.record_id)
    })
    readonly property var reportableEvents: workspace.events.filter(row => page.isRegisteredEvent(row))
    readonly property var activeReport: workspace.reports.find(row => row.id === reportId) || workspace.reports[0] || ({})
    readonly property string selectedReport: activeReport.id || ""
    readonly property var reportRows: workspace.reports.map(function(report) {
        const event = workspace.events.find(row => row.id === report.event_id)
        return Object.assign({}, report, {eventTitle: event ? event.title : "—"})
    })
    readonly property var kinds: [{id:"image",label:"照片"}, {id:"audio",label:"音频"}, {id:"thermal",label:"热像"},
                                  {id:"signal",label:"信号"}, {id:"video",label:"视频"}, {id:"text",label:"文字"}]
    readonly property var visibleEvidence: workspace.evidence.filter(function(row) {
        return (!page.deviceId || row.device_id === page.deviceId) && (!page.evidenceKind || row.kind === page.evidenceKind)
            && (!page.evidenceRecordId || row.record_id === page.evidenceRecordId)
    }).map(function(row) {
        const kind = page.kinds.find(function(entry) { return entry.id === row.kind })
        return Object.assign({}, row, {kindLabel: kind ? kind.label : row.kind})
    })
    signal previewReportRequested()
    signal exportReportRequested()
    AnalysisReviewDialog { id: reportReview }

    onProjectIdChanged: {
        message = ""
        evidenceId = ""
        eventId = ""
        deviceId = ""
        reportId = ""
        evidenceKind = ""
        eventFilter = ""
        eventDeviceId = ""
        inspectionFilter = ""
        evidenceRecordId = ""
        evidenceAssetId = ""
        graphDeviceId = ""
        pointField.text = ""
        timeField.text = ""
        captureDialog.close()
        inspectionDialog.close()
        reportSource.close()
        manualEvent.close()
        defectDialog.close()
        reportReview.close()
    }
    function isRegisteredEvent(event) {
        return !!event && !!event.id && !!(event.analyzed || event.source || event.review_result || event.record_id)
    }
    function newProject() { App.nav.push("projects"); Qt.callLater(function() { createDialog.open() }) }
    function openProject() { App.nav.push("projects"); Qt.callLater(function() { openFolder.open() }) }
    function importProject() { App.nav.push("projects"); Qt.callLater(function() { importFolder.open() }) }
    function openDemoProject() { App.nav.push("projects"); showResult(workspace.openDemoProject(), "演示工程已打开") }
    function showEvidence(kind) {
        App.nav.push("projectEvidence", {kind:kind || ""})
    }
    function attachCapture(paths, kind) {
        if (!projectId || !paths || !paths.length) return
        captureDialog.paths = typeof paths === "string" ? [paths] : paths
        captureDialog.kind = kind
        captureDialog.project = projectId
        App.nav.push("projectEvidence")
        Qt.callLater(function() {
            if (captureDialog.project === page.projectId) captureDialog.open()
        })
    }
    function openResource(type, id) {
        if (type === "device") {
            const device = workspace.devices.find(row => row.id === id)
            if (device && device.asset_id) App.nav.push("devices", {asset_id:device.asset_id})
            else App.nav.push("projectEvidence", {device_id:id})
        } else if (type === "evidence") {
            showEvidence("")
            evidenceId = id
            Qt.callLater(function() { page.openEvidence(id) })
        } else if (type === "event") {
            const item = workspace.events.find(row => row.id === id)
            const recorded = isRegisteredEvent(item)
            App.nav.push(recorded ? "events" : "projectDiagnosis", {event_id:id})
        } else if (type === "report") {
            reportId = id
            previewReportRequested()
        }
    }
    function workflowStep(step, device) {
        const asset = device.asset_id || ""
        const projectDevice = device.id || ""
        if (step === "device") App.nav.push("devices", {asset_id:asset})
        else if (step === "task") App.nav.push("tasks", {asset_id:asset})
        else if (step === "evidence") {
            if (projectDevice) App.nav.push("projectEvidence", {device_id:projectDevice})
            else showEvidence("")
        } else if (step === "diagnosis") App.nav.push("projectDiagnosis", {device_id:projectDevice})
        else if (step === "event") App.nav.push("events", {device_id:projectDevice})
        else if (step === "report") App.nav.push("reports")
    }
    function openEvidence(id) {
        const file = workspace.evidenceFile(id)
        if (!file.url) {
            message = "文件缺失或不可访问"
            return
        }
        evidenceDialog.file = file
        evidenceDialog.evidenceId = id
        evidenceDialog.playbackError = ""
        evidenceDialog.open()
    }
    function newReport(sourceEventId) {
        reportSource.eventId = sourceEventId || ""
        App.nav.push("reports")
        Qt.callLater(function() {
            if (page.reportableEvents.length) reportSource.open()
        })
    }
    function showSection() {
        if (route === "projectEvidence") {
            deviceId = App.nav.param("device_id") || ""
            evidenceRecordId = App.nav.param("capture_record_id") || ""
            evidenceAssetId = App.nav.param("asset_id") || ""
            evidenceKind = App.nav.param("kind") || ""
            evidenceId = ""
        }
        if (route === "projectGraph") graphDeviceId = App.nav.param("device_id") || ""
        if (diagnosisMode) inference.refresh()
        if (diagnosisMode || eventMode) {
            eventDeviceId = App.nav.param("device_id") || ""
            if (route === "events") {
                eventFilter = ""
                inspectionFilter = App.nav.param("record_id") || ""
            }
            const requested = App.nav.param("event_id")
            if (requested) eventId = requested
            if (!visibleEvents.some(row => row.id === eventId))
                eventId = visibleEvents.length ? visibleEvents[0].id : ""
        }
    }
    onVisibleChanged: if (visible) { showSection(); inference.refresh() }
    Connections {
        target: App.nav
        function onPageChanged() { page.showSection() }
    }

    function showResult(result, successText) {
        message = result.ok ? (result.warning || successText) : result.error
        return result.ok
    }
    function makeEvent() {
        const result = workspace.fuse(evidenceId)
        if (showResult(result, "已生成诊断对象")) {
            eventId = result.id
            App.nav.push("projectDiagnosis", {event_id:result.id})
        }
    }
    function setEventFilter(value) {
        eventFilter = value
        if (!visibleEvents.some(row => row.id === eventId))
            eventId = visibleEvents.length ? visibleEvents[0].id : ""
    }
    function openEventRecord() {
        if (activeEvent.record_id && activeEvent.asset_id)
            App.nav.push("history", {asset_id:activeEvent.asset_id, source_id:activeEvent.record_id})
    }
    function registerAnalysis(source, useSelectedEvent) {
        if (!projectId) return
        manualEvent.sourceLabel = source
        manualEvent.anchorId = useSelectedEvent ? (activeEvent.anchor_id || "") : ""
        manualEvent.open()
    }

    Dialog {
        id: manualEvent
        objectName: "manualAnalysisEvent"
        title: "登记分析事件"
        parent: Overlay.overlay
        anchors.centerIn: parent; modal: true
        width: Math.min(515, (parent ? parent.width : page.width) - 16)
        property string sourceLabel: ""
        property string anchorId: ""
        property string feedback: ""
        onOpened: {
            manualEvidence.currentIndex = workspace.evidence.findIndex(row => row.id === anchorId)
            manualTime.text = new Date().toISOString()
            manualTitle.text = ""; manualNote.text = ""; manualClass.currentIndex = 0
            feedback = workspace.evidence.length ? "" : "请先在工程资料中归档相关证据。"
        }
        contentItem: ColumnLayout {
            Label { text: "分析来源  " + manualEvent.sourceLabel }
            AppComboBox {
                id: manualEvidence; Layout.fillWidth: true
                model: workspace.evidence.map(row => ({id:row.id, name:row.device + " / " + row.point + " / " + row.title}))
                textRole: "name"; valueRole: "id"; displayText: currentIndex < 0 ? "选择工程证据" : currentText
            }
            TextField { id: manualTitle; Layout.fillWidth:true; placeholderText:"事件标题"; maximumLength:100 }
            Label { text:"发生时间 · ISO 8601，含时区"; font.pixelSize:Typography.small }
            TextField { id: manualTime; Layout.fillWidth:true; maximumLength:40 }
            AppComboBox {
                id: manualClass; Layout.fillWidth:true; textRole:"label"; valueRole:"id"
                model:[{id:"unknown",label:"待判定"},{id:"normal",label:"正常"},{id:"abnormal",label:"异常"}]
            }
            ScrollView {
                Layout.fillWidth:true; Layout.preferredHeight:100
                TextArea { id:manualNote; placeholderText:"填写观察内容或读数依据"; wrapMode:TextEdit.Wrap }
            }
            Label { Layout.fillWidth:true; wrapMode:Text.Wrap; text:manualEvent.feedback; visible:!!text }
        }
        footer: DialogButtonBox {
            Button { objectName:"manualEventCancel"; text:"取消"; onClicked:manualEvent.close() }
            Button {
                text:"登记"
                enabled:manualEvidence.currentIndex>=0 && !!manualTitle.text.trim() && !!manualNote.text.trim()
                onClicked: {
                    const result = workspace.addAnalysisEvent(manualEvidence.currentValue, manualEvent.sourceLabel,
                        manualTime.text, manualTitle.text, manualNote.text, manualClass.currentValue)
                    if (!result.ok) { manualEvent.feedback = result.error; return }
                    manualEvent.close()
                    App.nav.push("events", {event_id:result.id})
                }
            }
        }
    }

    Dialog {
        id: defectDialog
        title: "关联同次巡检的缺陷"
        anchors.centerIn: parent; modal: true; width: Math.min(435, page.width - 16)
        property string feedback: ""
        onOpened: {
            defectChoice.model = App.workflow.detail("records", page.activeEvent.record_id).defects || []
            defectChoice.currentIndex = -1
            feedback = defectChoice.model.length ? "" : "请先在该巡检记录下登记缺陷。"
        }
        contentItem: ColumnLayout {
            AppComboBox { id:defectChoice; Layout.fillWidth:true; textRole:"title"; valueRole:"id" }
            Label { Layout.fillWidth:true; wrapMode:Text.Wrap; text:defectDialog.feedback; visible:!!text }
        }
        footer: DialogButtonBox {
            Button { text:"取消"; onClicked:defectDialog.close() }
            Button {
                text:"关联"; enabled:defectChoice.currentIndex>=0
                onClicked: {
                    const result = workspace.linkDefect(page.eventId, defectChoice.currentValue)
                    if (result.ok) { page.message = "已关联缺陷"; defectDialog.close() }
                    else defectDialog.feedback = result.error
                }
            }
        }
    }

    FolderDialog {
        id: openFolder
        title: "选择 .miproj 工程目录"
        onAccepted: page.showResult(workspace.openProject(selectedFolder), "工程已打开")
    }
    FolderDialog {
        id: parentFolder
        title: "选择新工程的存放位置"
        onAccepted: createDialog.folder = selectedFolder
    }
    FolderDialog {
        id: importFolder
        title: "导入 .miproj 目录为本地副本"
        onAccepted: page.showResult(workspace.importProject(selectedFolder), "已导入并打开本地工程副本")
    }
    FileDialog {
        id: evidenceFile
        title: "选择证据文件"
        onAccepted: {
            const result = workspace.importEvidence(selectedFile, page.deviceId, pointField.text,
                                                    kindField.currentValue, timeField.text, page.evidenceRecordId)
            if (page.showResult(result, "证据已导入")) {
                page.evidenceKind = ""
                page.evidenceId = result.id
            }
        }
    }
    Dialog {
        id: captureDialog
        objectName: "projectCaptureDialog"
        title: "采集资料归入工程"
        anchors.centerIn: parent
        width: Math.min(430, page.width - 16)
        modal: true
        property var paths: []
        property string kind: ""
        property string project: ""
        property string feedback: ""
        onOpened: {
            capturePath.currentIndex = paths.length ? 0 : -1
            const index = captureDevice.model.findIndex(row => row.id === page.deviceId)
            captureDevice.currentIndex = index >= 0 ? index : captureDevice.model.length === 1 ? 0 : -1
            capturePoint.text = pointField.text
            feedback = ""
        }
        contentItem: ColumnLayout {
            AppComboBox {
                id: capturePath; Layout.fillWidth: true
                model: Array.from(captureDialog.paths, path => path.split(/[\\/]/).pop())
            }
            AppComboBox {
                id: captureDevice; Layout.fillWidth: true
                model: workspace.devices
                textRole: "name"; valueRole: "id"
                displayText: currentIndex < 0 ? "选择工程设备" : currentText
            }
            Label { visible: !workspace.devices.length; text: "请先在设备台账登记到工程。" }
            TextField { id: capturePoint; Layout.fillWidth: true; placeholderText: "测点"; maximumLength: 120 }
            Label { Layout.fillWidth: true; visible: !!captureDialog.feedback; text: captureDialog.feedback; wrapMode: Text.Wrap }
        }
        footer: DialogButtonBox {
            Button { text: "取消"; onClicked: captureDialog.close() }
            Button {
                text: "归入工程"
                enabled: capturePath.currentIndex >= 0 && captureDevice.currentIndex >= 0 && !!capturePoint.text.trim()
                    && captureDialog.project === page.projectId
                onClicked: {
                    const result = workspace.importCaptured(captureDialog.paths[capturePath.currentIndex],
                        captureDevice.currentValue, capturePoint.text, captureDialog.kind, page.evidenceRecordId)
                    if (!result.ok) { captureDialog.feedback = result.error; return }
                    page.deviceId = captureDevice.currentValue
                    pointField.text = capturePoint.text
                    page.evidenceKind = ""
                    page.evidenceId = result.id
                    page.message = "已归入工程资料"
                    captureDialog.close()
                }
            }
        }
    }
    Dialog {
        id: textDialog
        objectName: "projectTextDialog"
        title: "新增现场文字"
        anchors.centerIn: parent
        width: Math.min(520, page.width - 16)
        modal: true
        property string feedback: ""
        onOpened: { textTitle.text = ""; textBody.text = ""; feedback = "" }
        contentItem: ColumnLayout {
            TextField { id: textTitle; Layout.fillWidth: true; placeholderText: "标题"; maximumLength: 80 }
            ScrollView {
                Layout.fillWidth: true; Layout.preferredHeight: 145
                TextArea { id: textBody; placeholderText: "记录读数、工况或观察结果"; wrapMode: TextEdit.Wrap }
            }
            Label { Layout.fillWidth: true; text: textDialog.feedback; visible: !!text; wrapMode: Text.Wrap }
        }
        footer: DialogButtonBox {
            Button { text: "取消"; onClicked: textDialog.close() }
            Button {
                text: "保存"
                enabled: !!textTitle.text.trim() && !!textBody.text.trim()
                onClicked: {
                    const result = workspace.addTextEvidence(page.deviceId, pointField.text, textTitle.text, textBody.text, page.evidenceRecordId)
                    if (!result.ok) { textDialog.feedback = result.error; return }
                    page.evidenceKind = ""
                    page.evidenceId = result.id
                    page.message = "现场文字已保存"
                    textDialog.close()
                }
            }
        }
    }
    Dialog {
        id: inspectionDialog
        objectName: "projectInspectionLinkDialog"
        title: "关联巡检记录"
        anchors.centerIn: parent
        width: Math.min(440, page.width - 16)
        modal: true
        property var choices: []
        property string feedback: ""
        onOpened: {
            choices = workspace.inspectionChoices(page.eventId)
            const index = choices.findIndex(row => row.id === page.activeEvent.record_id)
            inspectionChoice.currentIndex = index >= 0 ? index : -1
            feedback = choices.length ? "" : "没有可关联的巡检记录"
        }
        contentItem: ColumnLayout {
            Label { Layout.fillWidth: true; text: "工程设备  " + (page.activeEvent.device || ""); elide: Text.ElideRight }
            AppComboBox {
                id: inspectionChoice; Layout.fillWidth: true
                model: inspectionDialog.choices; textRole: "name"; valueRole: "id"
                displayText: currentIndex < 0 ? "选择同一设备的巡检记录" : currentText
            }
            Label { Layout.fillWidth: true; visible: !!text; text: inspectionDialog.feedback; wrapMode: Text.Wrap }
        }
        footer: DialogButtonBox {
            Button { text: "取消"; onClicked: inspectionDialog.close() }
            Button {
                text: "关联"
                enabled: inspectionChoice.currentIndex >= 0
                onClicked: {
                    const result = workspace.linkInspection(page.eventId, inspectionChoice.currentValue)
                    if (!result.ok) { inspectionDialog.feedback = result.error; return }
                    page.message = "已关联巡检记录"
                    inspectionDialog.close()
                }
            }
        }
    }
    Dialog {
        id: reportSource
        title: "生成事件分析报告"
        property string eventId: ""
        anchors.centerIn: parent
        width: Math.min(440, page.width - 16)
        modal: true
        onOpened: {
            const index = page.reportableEvents.findIndex(row => row.id === reportSource.eventId)
            reportEvent.currentIndex = index >= 0 ? index : page.reportableEvents.length === 1 ? 0 : -1
        }
        contentItem: AppComboBox {
            id: reportEvent
            model: page.reportableEvents.map(row => ({id:row.id, name:row.device + " · " + row.point + " · " + row.title}))
            textRole: "name"; valueRole: "id"
            displayText: currentIndex < 0 ? "选择事件" : currentText
        }
        footer: DialogButtonBox {
            Button { text: "取消"; onClicked: reportSource.close() }
            Button {
                text: "生成"
                enabled: reportEvent.currentIndex >= 0
                onClicked: {
                    const result = workspace.exportReport(reportEvent.currentValue)
                    if (page.showResult(result, "工程报告已生成")) {
                        page.reportId = result.id
                        reportSource.close()
                    }
                }
            }
        }
    }
    Dialog {
        id: createDialog
        property url folder
        title: "新建工程"
        anchors.centerIn: parent
        width: Math.min(380, page.width - 20)
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { nameField.text = ""; folder = "" }
        onAccepted: page.showResult(workspace.createProject(nameField.text, folder), "工程已创建")
        ColumnLayout {
            width: parent.width
            TextField { id: nameField; Layout.fillWidth: true; placeholderText: "工程名称"; maximumLength: 80 }
            TouchButton { text: "选择目录"; autoWidth: true; onClicked: parentFolder.open() }
            Label { Layout.fillWidth: true; text: createDialog.folder.toString() || "默认使用应用的工程存储目录"; wrapMode: Text.WrapAnywhere }
        }
    }
    Dialog {
        id: evidenceDialog
        property var file: ({})
        property string evidenceId: ""
        property string playbackError: ""
        title: file.title || "查看原始资料"
        anchors.centerIn: parent
        width: Math.min(820, page.width - 12)
        height: Math.min(570, page.height - 12)
        modal: true
        standardButtons: Dialog.Close
        onClosed: file = ({})
        ColumnLayout {
            anchors.fill: parent
            Image {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !!evidenceDialog.file.image
                source: evidenceDialog.file.image ? evidenceDialog.file.url : ""
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 1440
                sourceSize.height: 1080
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: evidenceDialog.file.text !== undefined
                clip: true
                TextArea {
                    text: evidenceDialog.file.text || ""
                    textFormat: TextEdit.PlainText
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                }
            }
            Label {
                Layout.fillWidth: true
                visible: !evidenceDialog.file.image && evidenceDialog.file.text === undefined
                text: evidenceDialog.file.wave ? "可回听音频，或将 WAV 导入信号分析。"
                                               : evidenceDialog.file.kind === "video" ? "可在独立窗口播放原始视频。"
                                                                                       : "此文件可从导出的工程中读取。"
                wrapMode: Text.Wrap
            }
            Flow {
                Layout.fillWidth: true
                spacing: 5
                TouchButton {
                    text: App.speech.playing ? "停止回听" : "回听"
                    autoWidth: true; visible: !!evidenceDialog.file.wave
                    onClicked: {
                        if (App.speech.playing) App.speech.stopSpeech()
                        else App.speech.playAudio(evidenceDialog.file.localPath)
                    }
                }
                TouchButton {
                    text: "在信号分析中读取"; autoWidth: true
                    visible: !!evidenceDialog.file.wave
                    enabled: !!App.session.permissions.inspect && !App.analysis.running
                    onClicked: {
                        App.analysis.importFile(evidenceDialog.file.localPath)
                        evidenceDialog.close()
                        App.nav.push("analysis", {analysis_tab:0})
                    }
                }
                TouchButton {
                    text: "播放视频"; autoWidth: true
                    visible: evidenceDialog.file.kind === "video"
                    onClicked: if (!workspace.playEvidenceVideo(evidenceDialog.evidenceId)
                                   && !Qt.openUrlExternally(evidenceDialog.file.url))
                        evidenceDialog.playbackError = "无法启动视频播放器"
                }
            }
            Label {
                Layout.fillWidth: true
                visible: !!evidenceDialog.file.wave || evidenceDialog.playbackError.length > 0
                text: evidenceDialog.playbackError || App.speech.message
                elide: Text.ElideRight
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 4
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            currentIndex: page.route === "projectEvidence" ? 1
                : page.route === "reports" ? 3 : page.route === "projectGraph" ? 4
                : page.diagnosisMode ? 5 : page.eventMode ? 2 : 0
            ProjectOverview {
                workspace: page.workspace
                message: page.message
                onRecentRequested: function(folder) { page.showResult(workspace.openProject(folder), "工程已打开") }
                onStepRequested: function(step, device) { page.workflowStep(step, device) }
                onEvidenceRequested: function(kind) { page.showEvidence(kind) }
                onDeviceRequested: function(device) {
                    if (device.asset_id) App.nav.push("devices", {asset_id:device.asset_id})
                    else page.openResource("device", device.id)
                }
                onPreviewRequested: function(id) { page.openEvidence(id) }
            }
            ColumnLayout {
                Flow {
                    visible: !!page.evidenceRecordId
                    Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height
                    spacing: 5
                    TouchButton {
                        text: "查看本次巡检"; autoWidth: true; height: 28
                        onClicked: App.nav.push("history", {asset_id:page.evidenceAssetId, source_id:page.evidenceRecordId})
                    }
                    TouchButton { text: "查看全部工程资料"; autoWidth: true; height: 28; onClicked: page.showEvidence("") }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 5
                    TouchButton {
                        text: "到台账登记设备"; autoWidth: true; enabled: !!page.projectId
                        visible: !page.evidenceRecordId
                        onClicked: App.nav.push("devices")
                    }
                    AppComboBox {
                        id: devices
                        width: 130; height: 30
                        model: workspace.devices; textRole: "name"; valueRole: "id"
                        currentIndex: workspace.devices.findIndex(function(row) { return row.id === page.deviceId })
                        displayText: currentIndex < 0 ? "选择设备" : currentText
                        enabled: !page.evidenceRecordId
                        onActivated: { page.deviceId = currentValue; page.evidenceId = "" }
                    }
                    FieldInput { id: pointField; width: 110; height: 30; placeholder: "测点"; maximumLength: 120 }
                    AppComboBox {
                        id: kindField
                        width: 85; height: 30; textRole: "label"; valueRole: "id"
                        model: page.kinds
                    }
                    FieldInput {
                        id: timeField
                        width: 170; height: 30; placeholder: "采集时间（默认文件时间）"
                        maximumLength: 35
                    }
                    TouchButton {
                        text: "导入文件"; autoWidth: true
                        enabled: page.deviceId.length > 0 && pointField.text.trim().length > 0
                        onClicked: evidenceFile.open()
                    }
                    TouchButton {
                        text: "新增文字"; autoWidth: true
                        enabled: page.deviceId.length > 0 && pointField.text.trim().length > 0
                        onClicked: textDialog.open()
                    }
                    TouchButton { text: "查看原始资料"; autoWidth: true; enabled: !!page.evidenceId; onClicked: page.openEvidence(page.evidenceId) }
                    TouchButton { objectName: "createFusionDiagnosis"
                        text: "生成融合诊断"; autoWidth: true; variant: "primary"; enabled: !!page.evidenceId; onClicked: page.makeEvent() }
                    TouchButton { text: "多模态图谱"; autoWidth: true; enabled: !!page.projectId; onClicked: App.nav.push("projectGraph", {device_id:page.deviceId}) }
                    TouchButton {
                        text: "清除筛选"; autoWidth: true; visible: !page.evidenceRecordId && (!!page.deviceId || !!page.evidenceKind)
                        onClicked: { page.deviceId = ""; page.evidenceKind = ""; page.evidenceId = "" }
                    }
                }
                DataTable {
                    spreadsheet: true
                    Layout.fillWidth: true; Layout.fillHeight: true
                    items: page.visibleEvidence
                    selectedIndex: items.findIndex(function(row) { return row.id === page.evidenceId })
                    columns: [{key:"title",title:"证据",width:0}, {key:"device",title:"设备",width:100},
                              {key:"point",title:"测点",width:110}, {key:"kindLabel",title:"模态",width:70},
                              {key:"captured",title:"采集时间",width:128}]
                    onRowClicked: function(index, item) { page.evidenceId = item.id }
                    onRowDoubleClicked: function(index, item) { page.openEvidence(item.id) }
                }
            }
            ColumnLayout {
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 4
                    AppComboBox {
                        visible: page.eventMode
                        width: 115; height: 30
                        textRole: "label"; valueRole: "id"
                        model: [{id:"",label:"全部判定"}, {id:"normal",label:"正常"},
                                {id:"abnormal",label:"异常"}, {id:"unknown",label:"待判定"}]
                        currentIndex: model.findIndex(row => row.id === page.eventFilter)
                        onActivated: page.setEventFilter(currentValue)
                    }
                    TouchButton {
                        visible: page.eventMode && !!page.inspectionFilter
                        text: "显示全部巡检"; autoWidth: true
                        onClicked: App.nav.push("events", {event_id:page.eventId})
                    }
                    TouchButton {
                        text: "查看诊断"; autoWidth: true; enabled: !!page.eventId
                        onClicked: App.nav.push("projectDiagnosis", {event_id:page.eventId})
                    }
                    TouchButton {
                        text: "查看基准资料"; autoWidth: true; enabled: !!page.activeEvent.anchor_id
                        onClicked: page.openEvidence(page.activeEvent.anchor_id)
                    }
                    TouchButton {
                        visible: page.eventMode
                        text: "关联巡检记录"; autoWidth: true; enabled: !!page.eventId && !!App.session.permissions.inspect
                        onClicked: inspectionDialog.open()
                    }
                    TouchButton {
                        visible: page.eventMode
                        text: "打开巡检记录"; autoWidth: true; enabled: !!page.activeEvent.record_id
                        onClicked: page.openEventRecord()
                    }
                    TouchButton {
                        visible: page.eventMode && page.activeEvent.classification === "abnormal"
                        text: "关联缺陷"; autoWidth: true
                        enabled: !!page.activeEvent.record_id && !!App.session.permissions.register_defect
                        onClicked: defectDialog.open()
                    }
                    TouchButton {
                        visible: page.eventMode && !!page.activeEvent.defect_id
                        text: "打开缺陷"; autoWidth: true
                        onClicked: App.nav.push("defects", {asset_id:page.activeEvent.asset_id,source_id:page.activeEvent.defect_id})
                    }
                    TouchButton {
                        visible: page.eventMode
                        text: "生成事件分析报告…"; autoWidth: true; enabled: !!page.eventId
                        onClicked: page.newReport(page.eventId)
                    }
                    AppComboBox {
                        visible: page.eventMode && !!App.session.permissions.review
                        enabled: !!page.eventId
                        width: 112; height: 30
                        model: [{id:"normal",label:"设为正常"}, {id:"abnormal",label:"设为异常"}, {id:"unknown",label:"设为待判定"}]
                        textRole: "label"; valueRole: "id"; currentIndex: -1
                        displayText: "人工复核"
                        onActivated: {
                            if (page.showResult(workspace.reviewEvent(page.eventId, currentValue), "判定已保存"))
                                page.setEventFilter(page.eventFilter)
                            currentIndex = -1
                        }
                    }
                }
                DataTable {
                    objectName: "projectEventsTable"
                    spreadsheet: true
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.minimumHeight: 125
                    Layout.preferredHeight: 240
                    items: page.visibleEvents
                    selectedIndex: items.findIndex(function(row) { return row.id === page.eventId })
                    columns: [{key:"title",title:"事件",width:0}, {key:"device",title:"设备",width:100},
                              {key:"point",title:"测点",width:85}, {key:"classificationLabel",title:"判定",width:68},
                              {key:"observed",title:"发生时间",width:130}]
                    onRowClicked: function(index, item) { page.eventId = item.id }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 72
                    Layout.preferredHeight: Math.min(205, Math.max(88, page.height * .29))
                    clip: true
                    contentWidth: availableWidth
                    TextArea {
                        text: page.eventMode && inference.eventId === page.eventId && inference.answer
                            ? inference.answer
                            : page.activeEvent.analyzed
                                ? workspace.findingForEvent(page.eventId)
                                : page.activeEvent.note
                                    ? page.activeEvent.source + "\n" + page.activeEvent.note
                                : page.activeEvent.id
                                    ? (workspace.analysisInput(page.eventId).summary || "")
                                    : "事件台账显示已分析或手动登记的事件。选择事件可查看依据、复核判定或关联整改。"
                        textFormat: TextEdit.PlainText
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                    }
                }
            }
            ColumnLayout {
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 4
                    TouchButton {
                        text: "从事件生成报告…"; autoWidth: true; variant: "primary"
                        enabled: page.reportableEvents.length > 0; onClicked: page.newReport()
                    }
                    TouchButton { text: "预览"; autoWidth: true; enabled: !!page.selectedReport; onClicked: page.previewReportRequested() }
                    TouchButton { text: "导出 PDF…"; autoWidth: true; enabled: !!page.selectedReport; onClicked: page.exportReportRequested() }
                    TouchButton { text: "AI 复核"; autoWidth: true; enabled: !!page.selectedReport; onClicked: reportReview.review("projectReport", page.selectedReport) }
                    TouchButton {
                        text: "打开来源事件"; autoWidth: true
                        enabled: page.reportableEvents.some(row => row.id === page.activeReport.event_id)
                        onClicked: App.nav.push("events", {event_id:page.activeReport.event_id})
                    }
                }
                DataTable {
                    spreadsheet: true
                    Layout.fillWidth: true; Layout.fillHeight: true
                    objectName: "eventReportTable"
                    emptyText: "暂无事件分析报告。请先在事件台账选择要报告的事件。"
                    items: page.reportRows
                    selectedIndex: items.findIndex(function(row) { return row.id === page.selectedReport })
                    onRowClicked: function(index, item) { page.reportId = item.id }
                    onRowDoubleClicked: function(index, item) {
                        page.reportId = item.id
                        page.previewReportRequested()
                    }
                    columns: [{key:"eventTitle",title:"来源事件",width:0}, {key:"path",title:"报告文件",width:190},
                              {key:"created_at",title:"导出时间",width:138,format:"datetime"}]
                }
            }
            ColumnLayout {
                RowLayout {
                    Layout.fillWidth: true
                    TouchButton {
                        text: "返回工程资料"; autoWidth: true
                        onClicked: App.nav.push("projectEvidence", {device_id:page.deviceId,
                            capture_record_id:page.evidenceRecordId, asset_id:page.evidenceAssetId, kind:page.evidenceKind})
                    }
                    AppComboBox {
                        Layout.preferredWidth: 170; height: 30
                        model: [{id:"", name:"全部设备"}].concat(workspace.devices)
                        textRole: "name"; valueRole: "id"
                        currentIndex: model.findIndex(row => row.id === page.graphDeviceId)
                        onActivated: { page.graphDeviceId = currentValue; graph.selectedId = "" }
                    }
                    TouchButton { text: "适应窗口"; autoWidth: true; onClicked: graph.fit() }
                    TouchButton { text: "放大"; autoWidth: true; onClicked: graph.zoomIn() }
                    TouchButton { text: "缩小"; autoWidth: true; onClicked: graph.zoomOut() }
                    Item { Layout.fillWidth: true }
                    TouchButton {
                        text: "打开节点"; autoWidth: true
                        enabled: /^(evidence|event|device):/.test(graph.selectedId) && !graph.selectedId.includes("/point:")
                        onClicked: {
                            const split = graph.selectedId.indexOf(":")
                            page.openResource(graph.selectedId.slice(0, split), graph.selectedId.slice(split + 1))
                        }
                    }
                }
                KnowledgeGraphView {
                    id: graph; objectName: "projectEvidenceGraph"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    dynamicLayout: false
                    graph: page.route === "projectGraph" && workspace.evidence.length
                        ? workspace.evidenceGraph(page.graphDeviceId) : ({})
                }
                Label {
                    Layout.fillWidth: true
                    readonly property var node: (graph.graph.nodes || []).find(item => item.id === graph.selectedId)
                    text: node ? node.type + "  ·  " + node.name : "工程、设备、测点与原始资料的关系 · 当前设备筛选下最近 36 条资料"
                    elide: Text.ElideRight
                }
            }
            ProjectDiagnosisPane { projectPage: page }
        }
        Label {
            Layout.fillWidth: true
            visible: page.route !== "projects" && page.route !== "dashboard" && !!page.message
            text: page.message; elide: Text.ElideRight
        }
    }
}
