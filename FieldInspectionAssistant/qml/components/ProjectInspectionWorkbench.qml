import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FieldInspection
import "../theme"

Dialog {
    id: desk
    objectName: "projectInspectionWorkbench"
    required property var workspace
    property string initialDeviceId: ""
    signal evidenceRequested(string id)
    signal manageRequested()
    signal reportsRequested()
    signal tasksRequested()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1200, parent.width - 24)
    height: Math.min(780, parent.height - 24)
    title: "工程巡检工作台"
    modal: true
    closePolicy: Popup.NoAutoClose
    padding: 7

    property string deviceId: ""
    property string taskId: ""
    property string recordId: ""
    property string anchorId: ""
    property string eventId: ""
    property string notice: ""
    property bool problem: false
    property var cameraDevices: []
    property var thermalPorts: []
    property var pending: ({})
    property bool cameraOwned: false
    property int businessRevision: 0
    property string reportId: ""
    readonly property var inference: App.projectInference
    readonly property string projectId: workspace.project.id || ""
    readonly property var device: workspace.devices.find(row => row.id === deviceId) || ({})
    readonly property bool canInspect: App.session.loggedIn && App.session.can("inspect")
    readonly property bool canCapture: canInspect && !!deviceId && !!point.text.trim()
    readonly property bool canReview: App.session.loggedIn && App.session.can("review")
    readonly property bool capturePending: Object.keys(pending).length > 0
    readonly property var scopeEvidence: workspace.evidence.filter(row =>
        row.device_id === deviceId && (!point.text.trim() || row.point === point.text.trim())
        && (row.record_id || "") === recordId)
    readonly property var scopeEvents: workspace.events.filter(row =>
        row.device_id === deviceId && (!point.text.trim() || row.point === point.text.trim())
        && (!recordId || row.record_id === recordId))
    readonly property var event: scopeEvents.find(row => row.id === eventId) || ({})
    readonly property var timeline: event.id ? workspace.eventTimeline(event.id) : []
    readonly property var alignment: event.id ? workspace.fusionInfo(event.id) : ({})
    readonly property var reportRows: workspace.reports.filter(row => scopeEvents.some(item => item.id === row.event_id))
    readonly property var taskChoices: {
        void businessRevision
        return device.asset_id && visible ? App.inspection.inspectionCandidates(device.asset_id).filter(row => row.can_start) : []
    }
    readonly property var checks: {
        void businessRevision
        return taskId && recordId ? App.inspection.templateItems(taskId) : []
    }
    readonly property var savedChecks: {
        void businessRevision
        return recordId ? App.inspection.progress(recordId) : []
    }
    readonly property var modalities: [
        {id:"image", label:"照片", color:"#407caa"}, {id:"video", label:"视频", color:"#6465a1"},
        {id:"audio", label:"音频", color:"#207e7c"}, {id:"thermal", label:"热像", color:"#a85c2d"},
        {id:"signal", label:"定时信号", color:"#657b37"}, {id:"text", label:"文字", color:"#657383"}
    ]
    property var selectedKinds: ({})

    function label(kind) { return modalities.find(row => row.id === kind)?.label || kind }
    function inform(text, error) { notice = text; problem = !!error }
    function showResult(value, message) {
        inform(value.ok ? message : value.error || "操作未完成", !value.ok)
        return !!value.ok
    }
    function refreshInputs() {
        cameraDevices = App.evidence.cameras()
        thermalPorts = thermal.ports()
        App.analysis.refreshDevices()
    }
    function setDevice(id) {
        if (deviceId === id) return
        deviceId = id
        taskId = ""; recordId = ""; anchorId = ""; eventId = ""; reportId = ""
        selectedKinds = ({})
        const recent = workspace.evidence.find(row => row.device_id === id)
        point.text = recent ? recent.point : ""
    }
    function chooseEvidence(id) {
        const row = workspace.evidence.find(item => item.id === id)
        if (!row) return
        anchorId = id
        const scoped = workspace.evidence.filter(item => item.device_id === row.device_id
            && item.point === row.point && (item.record_id || "") === (row.record_id || ""))
        let next = {}
        scoped.forEach(item => { if (item.kind !== "text") next[item.kind] = true })
        next[row.kind] = true
        selectedKinds = next
    }
    function chooseEvent(id) {
        eventId = id
        const item = scopeEvents.find(row => row.id === id)
        if (item) anchorId = item.anchor_id
        reportId = ""
    }
    function beginRecord() {
        const task = taskChoice.currentValue
        const value = App.inspection.beginInspection(device.asset_id, task || "")
        if (!showResult(value, "已打开巡检草稿")) return
        taskId = value.task_id
        recordId = value.record_id
        anchorId = ""; eventId = ""; reportId = ""; selectedKinds = ({})
    }
    function captureContext() {
        return {project:projectId, user:App.session.userId, device:deviceId, point:point.text.trim(), record:recordId}
    }
    function setPending(kind, context) {
        let next = Object.assign({}, pending)
        if (context) next[kind] = context
        else delete next[kind]
        pending = next
    }
    function archivePending(kind, path, time, duration) {
        const ctx = pending[kind]
        if (!ctx) return
        setPending(kind, null)
        if (ctx.project !== projectId || ctx.user !== App.session.userId) return
        const value = workspace.importLiveCapture(path, ctx.device, ctx.point, kind, time, duration, ctx.record)
        if (showResult(value, label(kind) + "已归档")) {
            if (ctx.device === deviceId && ctx.record === recordId) chooseEvidence(value.id)
        }
        else inform((value.error || "归档失败") + "  原文件 " + path, true)
    }
    function photo() {
        setPending("image", captureContext())
        if (!App.camera.savePhotoAsync("JPG",92)) {
            setPending("image", null)
            inform(App.camera.status || "无法拍照", true)
        }
    }
    function video() {
        if (pending.video) { App.camera.stopVideo(); return }
        setPending("video", captureContext())
        if (!App.camera.startVideo(15,300,4)) {
            setPending("video", null)
            inform(App.camera.status || "无法录像", true)
        }
    }
    function audio() {
        if (pending.audio) { App.analysis.stop(); return }
        const context = captureContext()
        App.analysis.start(audioChoice.currentValue || "")
        if (App.analysis.running) setPending("audio", context)
        else inform(App.analysis.message, true)
    }
    function saveThermal() {
        const captured = thermal.capturedMs
        if (!captured || Date.now() - captured > 3000) {
            inform("请等待新的热像帧", true)
            return
        }
        const context = captureContext()
        const paths = thermal.saveInspectionFrame(App.dataDir + "/staging")
        if (!paths.length) { inform("热像保存失败", true); return }
        let errors = [], matrixId = "", imageId = ""
        for (const path of paths) {
            const value = workspace.importLiveCapture(path,context.device,context.point,"thermal",captured,0,context.record)
            if (!value.ok) errors.push(value.error)
            else if (path.endsWith("-celsius.csv")) matrixId = value.id
            else imageId = value.id
        }
        inform(errors.length ? errors.join(" · ") : "热像预览和温度矩阵已归档", !!errors.length)
        if (matrixId || imageId) chooseEvidence(matrixId || imageId)
    }
    function closeInputs() {
        if (pending.audio && App.analysis.running) App.analysis.stop()
        if (pending.video && App.camera.recording) App.camera.stopVideo()
        if (cameraOwned) { App.evidence.closeCamera(); cameraOwned = false }
        if (thermal.connected) thermal.stop()
    }
    function selectCaptureDevice() {
        const path = cameraChoice.currentText
        const formats = App.camera.resolutions(path)
        if (!formats.includes(App.camera.resolution)) {
            const size = (formats[0] || "").split("×")
            if (size.length < 2 || !App.camera.configure(Number(size[0]),Number(size[1]),Math.min(App.camera.fps,30))) {
                inform("所选相机没有可用的 MJPEG 格式", true)
                return
            }
        }
        cameraOwned = App.evidence.openCamera(path)
        if (!cameraOwned) inform(App.camera.status, true)
    }
    function runFusion() {
        const kinds = modalities.filter(item => selectedKinds[item.id]).map(item => item.id)
        const value = workspace.fuseSelected(anchorId, kinds, tolerance.value)
        if (showResult(value, "融合已保存")) chooseEvent(value.id)
    }
    function openEvidence(id) {
        if (capturePending) return
        close()
        Qt.callLater(() => evidenceRequested(id))
    }
    function updateCheck() {
        const value = App.inspection.saveItem(recordId, checkChoice.currentValue || "",
            checkState.currentValue, readout.text, checkNote.text)
        showResult(value, "检查项已保存")
    }
    function loadCheck() {
        const saved = savedChecks.find(item => item.key === checkChoice.currentValue)
        readout.text = saved?.value || ""
        checkNote.text = saved?.note || ""
        const index = checkState.model.findIndex(row => row.id === saved?.result)
        checkState.currentIndex = index >= 0 ? index : 0
    }

    onOpened: {
        setDevice(initialDeviceId || workspace.devices[0]?.id || "")
        deviceChoice.currentIndex = workspace.devices.findIndex(row => row.id === deviceId)
        refreshInputs()
        inference.refresh()
    }
    onClosed: closeInputs()
    onProjectIdChanged: {
        closeInputs()
        setDevice("")
        notice = ""
        if (opened) close()
    }
    onDeviceIdChanged: deviceChoice.currentIndex = workspace.devices.findIndex(row => row.id === deviceId)
    Connections {
        target: App.session
        function onSessionEnding() {
            desk.closeInputs()
            desk.pending = ({})
            desk.close()
        }
    }
    Connections {
        target: App.inspection
        function onTasksChanged() { desk.businessRevision++ }
        function onRecordChanged(id) { desk.businessRevision++ }
    }
    Connections {
        target: App.camera
        function onFrameReady(frame) { if (desk.visible && desk.cameraOwned) cameraPreview.frame = frame }
        function onCaptureSaved(kind,path,time,duration) { desk.archivePending(kind,path,time,duration) }
        function onError(message) {
            if (!desk.cameraOwned) return
            if (!App.camera.photoSaving) desk.setPending("image", null)
            if (!App.camera.recording && !App.camera.finalizing) desk.setPending("video", null)
            desk.inform(message, true)
        }
    }
    Connections {
        target: App.analysis
        function onCaptureSaved(path,time,duration) { desk.archivePending("audio",path,time,duration) }
        function onChanged() {
            if (desk.pending.audio && !App.analysis.running) {
                desk.setPending("audio",null)
                desk.inform(App.analysis.message, true)
            }
        }
    }

    FileDialog {
        id: importDialog
        property var context: ({})
        property string kind: ""
        property string timestamp: ""
        title: "选择巡检资料"
        onAccepted: {
            if (context.project !== desk.projectId || context.user !== App.session.userId) return
            const value = desk.workspace.importEvidence(selectedFile,context.device,context.point,kind,timestamp,context.record)
            if (desk.showResult(value,"资料已归档")) desk.chooseEvidence(value.id)
        }
    }
    Dialog {
        id: bindAsset
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: "登记台账设备到工程"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: assetChoice.model = App.assets.workAssets()
        ComboBox {
            id: assetChoice
            width: 320
            textRole: "name"; valueRole: "id"
            displayText: currentIndex < 0 ? "台账中暂无可巡检设备" : currentText
        }
        onAccepted: {
            if (assetChoice.currentIndex < 0) return
            const value = desk.workspace.registerAsset(assetChoice.currentValue)
            if (desk.showResult(value,"设备已登记到工程")) desk.setDevice(value.id)
        }
    }
    FileDialog {
        id: pdfDialog
        property string exportId: ""
        property string project: ""
        fileMode: FileDialog.SaveFile
        nameFilters: ["PDF (*.pdf)"]
        defaultSuffix: "pdf"
        onAccepted: {
            if (project === desk.projectId)
                desk.showResult(desk.workspace.exportReportPdf(exportId,selectedFile,"evidence"),"PDF 已导出")
        }
    }

    header: Rectangle {
        implicitHeight: 34
        color: "#e4eaf1"
        border.color: "#b4bfc9"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 9
            anchors.rightMargin: 5
            Label { Layout.fillWidth: true; font.bold: true; text: desk.title }
            Button {
                objectName: "closeProjectInspectionWorkbench"
                text: "关闭"
                enabled: !desk.capturePending
                onClicked: desk.close()
            }
        }
    }
    contentItem: ColumnLayout {
        spacing: 5
        GridLayout {
            Layout.fillWidth: true
            columns: desk.width > 770 ? 7 : 3
            columnSpacing: 5
            rowSpacing: 3
            Label { text: "工程设备" }
            ComboBox {
                id: deviceChoice
                objectName: "projectWorkbenchDevice"
                Layout.fillWidth: true
                model: desk.workspace.devices
                textRole: "name"; valueRole: "id"
                enabled: !desk.capturePending
                onActivated: desk.setDevice(currentValue || "")
            }
            TextField {
                id: point
                objectName: "projectWorkbenchPoint"
                Layout.fillWidth: true
                placeholderText: "测点名称"
                maximumLength: 120
                enabled: !desk.capturePending
                onTextEdited: { desk.anchorId = ""; desk.eventId = ""; desk.reportId = ""; desk.selectedKinds = ({}) }
            }
            ComboBox {
                id: taskChoice
                Layout.fillWidth: true
                model: desk.taskChoices
                textRole: "display"; valueRole: "id"
                displayText: currentIndex < 0 ? "选择已派发任务" : currentText
                enabled: !desk.capturePending
            }
            Button { objectName: "workbenchBeginRecord"; text: desk.recordId ? "切换草稿" : "开始巡检"; enabled: taskChoice.currentIndex >= 0 && desk.canInspect && !desk.capturePending; onClicked: desk.beginRecord() }
            Button { text: "登记设备…"; enabled: desk.canInspect && !desk.capturePending; onClicked: bindAsset.open() }
            Button { text: "任务管理"; enabled: !desk.capturePending; onClicked: { desk.close(); desk.tasksRequested() } }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [
                    {label:"现场资料",value:desk.scopeEvidence.length, accent:"#25719c"},
                    {label:"融合事件",value:desk.scopeEvents.length, accent:"#386f91"},
                    {label:"已分析",value:desk.scopeEvents.filter(row => row.analyzed).length, accent:"#287c61"},
                    {label:"检查进度",value:desk.recordId ? desk.savedChecks.length + "/" + desk.checks.length : "—", accent:"#7a6236"},
                    {label:"已归档报告",value:desk.reportRows.length, accent:"#5b6292"}
                ]
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    color: "#f7f9fc"
                    border.color: "#bfc8d2"
                    Rectangle { width: 3; height: parent.height; color: modelData.accent }
                    Column {
                        anchors.centerIn: parent
                        spacing: 1
                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.value; font.pixelSize: 21; font.bold: true; color: modelData.accent }
                        Label { anchors.horizontalCenter: parent.horizontalCenter; text: modelData.label; color: Colors.textSecondary }
                    }
                }
            }
        }
        TabBar {
            id: stages
            Layout.fillWidth: true
            DesktopTab { objectName: "workbenchCaptureTab"; text: "采集" }
            DesktopTab { objectName: "workbenchFusionTab"; text: "时间融合" }
            DesktopTab { objectName: "workbenchAnalysisTab"; text: "大模型分析" }
            DesktopTab { objectName: "workbenchRecordTab"; text: "巡检记录" }
            DesktopTab { objectName: "workbenchManageTab"; text: "事件与管理" }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: stages.currentIndex

            ColumnLayout {
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    GroupBox {
                        title: "相机"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        padding: 5
                        ColumnLayout {
                            anchors.fill: parent
                            ComboBox { id: cameraChoice; Layout.fillWidth: true; model: desk.cameraDevices; displayText: currentIndex < 0 ? "未发现相机" : currentText; enabled: !desk.cameraOwned }
                            Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 120
                                color: "#192431"
                                CameraPreview { id: cameraPreview; anchors.fill: parent; visible: desk.cameraOwned && App.camera.hasFrame }
                                Label { anchors.centerIn: parent; visible: !cameraPreview.visible; color: "#d5dfe7"; text: "相机预览" }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Button {
                                    objectName: "workbenchCameraConnect"
                                    text: desk.cameraOwned && App.evidence.cameraConnected ? "关闭" : "连接"
                                    enabled: !App.camera.opening && !App.camera.closing && !desk.pending.video
                                        && !desk.pending.image && cameraChoice.currentIndex >= 0 && desk.canInspect
                                    onClicked: {
                                        if (desk.cameraOwned && App.evidence.cameraConnected) {
                                            App.evidence.closeCamera()
                                            desk.cameraOwned = false
                                        } else if (App.evidence.cameraConnected) desk.inform("相机正在其他采集页面使用",true)
                                        else desk.selectCaptureDevice()
                                    }
                                }
                                Button { objectName: "workbenchPhoto"; text: "拍照归档"; enabled: desk.canCapture && desk.cameraOwned && App.camera.hasFrame && !App.camera.photoSaving && !App.camera.recording && !App.camera.finalizing; onClicked: desk.photo() }
                                Button { text: desk.pending.video ? "停止录像" : "录像"; enabled: desk.pending.video ? App.camera.recording : desk.canCapture && desk.cameraOwned && App.camera.hasFrame && !App.camera.photoSaving && !App.camera.finalizing; onClicked: desk.video() }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }
                    GroupBox {
                        title: "热像"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        padding: 5
                        ColumnLayout {
                            anchors.fill: parent
                            ComboBox { id: thermalChoice; Layout.fillWidth: true; model: desk.thermalPorts; textRole: "name"; valueRole: "path"; displayText: currentIndex < 0 ? "未发现热像仪" : currentText; enabled: !thermal.connected }
                            Rectangle {
                                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 120
                                color: "#192431"
                                ThermalCapture { id: thermal; anchors.fill: parent }
                                Label { anchors.centerIn: parent; visible: !thermal.hasFrame; color: "#d5dfe7"; text: "热像预览" }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Button { objectName: "workbenchThermalConnect"; text: thermal.connected ? "关闭" : "连接"; enabled: desk.canInspect && (thermal.connected || thermalChoice.currentIndex >= 0); onClicked: thermal.connected ? thermal.stop() : thermal.start(thermalChoice.currentValue || "",false) }
                                Button { objectName: "workbenchThermalSave"; text: "归档热像"; enabled: desk.canCapture && thermal.connected && thermal.hasFrame; onClicked: desk.saveThermal() }
                                Label { Layout.fillWidth: true; elide: Text.ElideRight; text: thermal.connected && thermal.hasFrame ? "最高 " + Number(thermal.stats.MAX).toFixed(1) + " °C" : thermal.status }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "音频源" }
                    ComboBox {
                        id: audioChoice
                        Layout.fillWidth: true
                        model: App.analysis.devices
                        textRole: "name"; valueRole: "id"
                        enabled: !App.analysis.running
                        displayText: currentIndex < 0 ? "未发现音频设备" : currentText
                        onCountChanged: {
                            const index = App.analysis.devices.findIndex(row => row.id === "ListenGo_raw_six")
                            if (!App.analysis.running && index >= 0) currentIndex = index
                        }
                    }
                    Button {
                        objectName: "workbenchAudioCapture"
                        text: desk.pending.audio ? "停止并归档" : "开始录音"
                        enabled: desk.pending.audio || desk.canCapture && !App.analysis.running && audioChoice.currentIndex >= 0
                        onClicked: desk.audio()
                    }
                    Button { text: "刷新采集源"; enabled: !desk.capturePending; onClicked: desk.refreshInputs() }
                }
                Label { Layout.fillWidth: true; text: App.analysis.running || desk.pending.audio ? App.analysis.message : "接入振动、电流或其他测量时序数据，请导入首列为 time_s 或 time_ms 的 CSV。"; elide: Text.ElideRight; color: Colors.textSecondary }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "导入资料" }
                    ComboBox { id: importKind; model: desk.modalities; textRole: "label"; valueRole: "id" }
                    TextField {
                        id: importTime
                        Layout.fillWidth: true
                        placeholderText: "ISO 8601 采集起点，可留空使用文件时间估算"
                    }
                    Button {
                        objectName: "workbenchImportEvidence"
                        text: "选择文件…"
                        enabled: desk.canCapture && !desk.capturePending
                        onClicked: {
                            importDialog.context = desk.captureContext()
                            importDialog.kind = importKind.currentValue
                            importDialog.timestamp = importTime.text
                            importDialog.open()
                        }
                    }
                }
                DataTable {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    rowHeight: 24
                    items: desk.scopeEvidence.slice(0,12).map(row => Object.assign({},row,{modality:desk.label(row.kind)}))
                    selectedIndex: items.findIndex(row => row.id === desk.anchorId)
                    columns: [{key:"modality",title:"模态",width:76},{key:"title",title:"本测点已归档资料",width:0},{key:"captured",title:"采集时间",width:155}]
                    onRowClicked: function(index,row) { desk.chooseEvidence(row.id) }
                    onRowDoubleClicked: function(index,row) { desk.openEvidence(row.id) }
                }
            }
            ColumnLayout {
                spacing: 4
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "参考证据" }
                    ComboBox {
                        id: fusionAnchor
                        objectName: "workbenchFusionAnchor"
                        Layout.fillWidth: true
                        model: desk.scopeEvidence.map(row => ({id:row.id,display:desk.label(row.kind) + " · " + row.title + " · " + row.captured}))
                        textRole: "display"; valueRole: "id"
                        currentIndex: model.findIndex(row => row.id === desk.anchorId)
                        onActivated: desk.chooseEvidence(currentValue || "")
                        displayText: currentIndex < 0 ? "选择参考资料" : currentText
                    }
                    Label { text: "容差 (ms)" }
                    SpinBox { id: tolerance; from: 0; to: 60000; value: 2000; stepSize: 100; editable: true }
                    Button { objectName: "workbenchRunFusion"; text: "生成融合事件"; enabled: desk.canInspect && !!desk.anchorId; onClicked: desk.runFusion() }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 8
                    Repeater {
                        model: desk.modalities
                        CheckBox {
                            required property var modelData
                            text: modelData.label
                            checked: !!desk.selectedKinds[modelData.id]
                            onToggled: {
                                let next = Object.assign({},desk.selectedKinds)
                                next[modelData.id] = checked
                                desk.selectedKinds = next
                            }
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Colors.textSecondary
                    text: desk.alignment.method ? "融合 " + desk.alignment.aligned.length + " 路 · 最大间隙 "
                        + desk.alignment.max_gap_ms + " ms · "
                        + (desk.alignment.has_common_time ? "全部模态在时间轴上有交集" : "全部模态无共同时间点")
                        : "每种模态选择同设备、同测点、同巡检记录的最近时间区间。区间相交时，间隙为 0 ms。"
                }
                GroupBox {
                    title: "时间对齐 · 相对参考证据"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    padding: 6
                    ColumnLayout {
                        id: alignmentChart
                        anchors.fill: parent
                        readonly property var aligned: desk.alignment.aligned || []
                        readonly property real minMs: Math.min(-1,...aligned.map(row => row.offset_ms))
                        readonly property real maxMs: Math.max(1,...aligned.map(row => row.offset_ms + row.duration_ms))
                        readonly property real span: maxMs - minMs
                        RowLayout {
                            Layout.fillWidth: true
                            visible: alignmentChart.aligned.length > 0
                            Label { Layout.preferredWidth: 92; text: "信号" }
                            Item {
                                Layout.fillWidth: true; Layout.preferredHeight: 19
                                Label { anchors.left: parent.left; text: Number(alignmentChart.minMs/1000).toFixed(3) + " s" }
                                Label { anchors.right: parent.right; text: "+" + Number(alignmentChart.maxMs/1000).toFixed(3) + " s" }
                            }
                            Label { Layout.preferredWidth: 142; text: "起点偏移 / 间隙 (ms)" }
                        }
                        Repeater {
                            model: alignmentChart.aligned
                            RowLayout {
                                id: track
                                required property var modelData
                                Layout.fillWidth: true
                                Label { Layout.preferredWidth: 92; text: desk.label(track.modelData.kind) }
                                Rectangle {
                                    Layout.fillWidth: true; Layout.preferredHeight: 19
                                    color: "#edf2f6"; border.color: "#c7d0d9"
                                    readonly property var axis: alignmentChart
                                    Rectangle { height: parent.height; width: 1; x: parent.width * -parent.axis.minMs / parent.axis.span; color: "#6b7d92" }
                                    Rectangle {
                                        x: Math.min(parent.width - 4,parent.width * (track.modelData.offset_ms - parent.axis.minMs) / parent.axis.span)
                                        y: 4; height: 11
                                        width: Math.max(4,Math.min(parent.width - x,parent.width * track.modelData.duration_ms / parent.axis.span))
                                        color: desk.modalities.find(row => row.id === track.modelData.kind).color
                                    }
                                }
                                Label { Layout.preferredWidth: 142; text: (track.modelData.offset_ms >= 0 ? "+" : "") + track.modelData.offset_ms + "  /  " + track.modelData.gap_ms }
                            }
                        }
                        DataTable {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 100
                            rowHeight: 25
                            items: desk.timeline
                            emptyText: "生成融合事件后显示时间戳、偏移和采集指标"
                            columns: [{key:"modality",title:"模态",width:74},{key:"timeUtc",title:"UTC 采集起点",width:190},
                                {key:"clockLabel",title:"时钟来源",width:128},{key:"observationLabel",title:"指标 / 观测",width:0}]
                            onRowDoubleClicked: function(index,row) { desk.openEvidence(row.id) }
                        }
                    }
                }
            }
            ColumnLayout {
                spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; elide: Text.ElideRight; text: desk.event.id ? desk.event.title + " · " + desk.event.point : "请先生成或选中融合事件" }
                    Button { text: "检查服务"; enabled: !desk.inference.busy; onClicked: desk.inference.refresh() }
                    Button {
                        objectName: "workbenchRunAnalysis"
                        text: desk.inference.busy ? "分析中…" : "运行大模型分析"
                        enabled: !!desk.event.id && desk.canInspect && desk.inference.ready && !desk.inference.busy
                        onClicked: desk.inference.analyze(desk.eventId)
                    }
                    Button { visible: desk.inference.queued; text: "取消排队"; onClicked: desk.inference.cancelQueued() }
                }
                Label { Layout.fillWidth: true; text: desk.inference.status; wrapMode: Text.Wrap; color: Colors.textSecondary }
                DataTable {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 135
                    rowHeight: 25
                    items: desk.timeline
                    columns: [{key:"modality",title:"模态",width:82},{key:"title",title:"证据",width:200},
                              {key:"offsetLabel",title:"偏移",width:90},{key:"observationLabel",title:"输入指标",width:0}]
                    onRowDoubleClicked: function(index,row) { desk.openEvidence(row.id) }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        objectName: "workbenchDiagnosis"
                        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                        placeholderText: "分析完成后，诊断建议和证据引用会保存到该事件。"
                        text: desk.inference.eventId === desk.eventId && desk.inference.answer
                            ? desk.inference.answer : (desk.workspace.events, desk.eventId ? desk.workspace.findingForEvent(desk.eventId) : "")
                    }
                }
            }
            ColumnLayout {
                spacing: 5
                Label {
                    Layout.fillWidth: true
                    text: desk.recordId ? "当前巡检记录   " + desk.recordId : "选择工程设备对应的已派发任务后开始巡检。"
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true
                    ComboBox {
                        id: checkChoice
                        Layout.fillWidth: true
                        model: desk.checks; textRole: "name"; valueRole: "key"
                        onActivated: desk.loadCheck()
                        onCurrentValueChanged: desk.loadCheck()
                    }
                    ComboBox {
                        id: checkState
                        textRole: "label"; valueRole: "id"
                        model: [{id:"normal",label:"正常"},{id:"abnormal",label:"异常"},{id:"skip",label:"跳过"}]
                    }
                    TextField { id: readout; Layout.fillWidth: true; placeholderText: "读数" }
                    TextField { id: checkNote; Layout.fillWidth: true; placeholderText: "检查说明" }
                    Button { objectName: "workbenchSaveCheck"; text: "保存检查项"; enabled: desk.canInspect && !!desk.taskId && checkChoice.currentIndex >= 0; onClicked: desk.updateCheck() }
                }
                DataTable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    rowHeight: 26
                    items: desk.checks.map(item => {
                        const saved = desk.savedChecks.find(row => row.key === item.key) || ({})
                        return {key:item.key,name:item.name,unit:item.unit || "",value:saved.value || "",
                                state:{normal:"正常",abnormal:"异常",skip:"跳过"}[saved.result] || "待检查",note:saved.note || ""}
                    })
                    columns: [{key:"name",title:"检查项",width:0},{key:"state",title:"结果",width:84},
                              {key:"value",title:"读数",width:90},{key:"unit",title:"单位",width:57},{key:"note",title:"说明",width:0}]
                    onRowClicked: function(index,row) { checkChoice.currentIndex = index; desk.loadCheck() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { id: workflowNote; Layout.fillWidth: true; placeholderText: "巡检过程记录或人工复核意见" }
                    Button {
                        text: "保存过程记录"
                        enabled: desk.canInspect && !!desk.taskId && !!workflowNote.text.trim()
                        onClicked: {
                            const value = App.workflow.saveEntry(desk.recordId,{title:desk.event.id ? "融合事件 " + desk.eventId : "现场巡检",content:workflowNote.text,stage:"observation",state:"completed"})
                            if (desk.showResult(value,"过程记录已保存")) workflowNote.clear()
                        }
                    }
                    Button {
                        text: "登记检查项缺陷"
                        enabled: !!desk.taskId && App.session.can("register_defect") &&
                            desk.savedChecks.some(row => row.key === checkChoice.currentValue && row.result === "abnormal")
                        onClicked: {
                            const item = desk.checks[checkChoice.currentIndex]
                            const value = App.defects.createDefect(desk.recordId,desk.device.asset_id,
                                item.name + "异常",checkNote.text,"medium",item.key)
                            desk.showResult(value,"缺陷已登记，可在事件与整改中处理")
                        }
                    }
                    Button { text: "提交巡检…"; enabled: desk.canInspect && !!desk.taskId && !desk.capturePending; onClicked: submitDialog.open() }
                }
            }
            ColumnLayout {
                spacing: 4
                DataTable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    rowHeight: 26
                    items: desk.scopeEvents.map(row => Object.assign({},row,{analysis:row.analyzed ? "已完成" : "待分析"}))
                    selectedIndex: items.findIndex(row => row.id === desk.eventId)
                    columns: [{key:"observed",title:"事件时间",width:150},{key:"title",title:"融合事件",width:0},
                              {key:"point",title:"测点",width:130},{key:"classificationLabel",title:"判定",width:91},
                              {key:"analysis",title:"分析",width:86}]
                    onRowClicked: function(index,row) { desk.chooseEvent(row.id) }
                    onRowDoubleClicked: function(index,row) { desk.chooseEvent(row.id); stages.currentIndex = 2 }
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: 4
                    Button { text: "查看分析"; enabled: !!desk.event.id; onClicked: stages.currentIndex = 2 }
                    ComboBox {
                        id: reviewChoice
                        textRole: "label"; valueRole: "id"
                        model: [{id:"unknown",label:"待复测"},{id:"normal",label:"正常"},{id:"abnormal",label:"异常"}]
                    }
                    Button { text: "保存人工判定"; enabled: desk.canReview && !!desk.event.id; onClicked: desk.showResult(desk.workspace.reviewEvent(desk.eventId,reviewChoice.currentValue),"人工判定已保存") }
                    Button {
                        objectName: "workbenchArchiveReport"
                        text: "归档证据报告"
                        enabled: desk.canInspect && !!desk.event.id && !desk.inference.busy
                        onClicked: {
                            const value = desk.workspace.exportReport(desk.eventId)
                            if (desk.showResult(value,"证据报告已归档")) desk.reportId = value.id
                        }
                    }
                    Button { text: "事件与整改"; enabled: !desk.capturePending; onClicked: { desk.close(); desk.manageRequested() } }
                    Button { text: "报告管理"; enabled: !desk.capturePending; onClicked: { desk.close(); desk.reportsRequested() } }
                }
                DataTable {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 116
                    rowHeight: 25
                    items: desk.reportRows.map(row => Object.assign({},row,{eventTitle:desk.scopeEvents.find(event => event.id === row.event_id)?.title || row.event_id}))
                    selectedIndex: items.findIndex(row => row.id === desk.reportId)
                    columns: [{key:"created_at",title:"归档时间",width:168},{key:"eventTitle",title:"报告关联事件",width:0}]
                    onRowClicked: function(index,row) { desk.reportId = row.id }
                }
                Button {
                    text: "导出所选 PDF…"
                    enabled: !!desk.reportId && desk.reportRows.some(row => row.id === desk.reportId)
                    onClicked: { pdfDialog.exportId = desk.reportId; pdfDialog.project = desk.projectId; pdfDialog.open() }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            color: desk.problem ? Colors.danger : Colors.textSecondary
            text: desk.notice || (desk.capturePending ? "正在采集或保存，完成后可关闭工作台。" : desk.recordId ? "资料归入当前巡检草稿" : "资料归入工程，选择任务后可归入巡检草稿")
            wrapMode: Text.Wrap
        }
    }
    Dialog {
        id: submitDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        title: "提交巡检任务"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        Label { text: "提交后将锁定检查结果。系统会检查必填项和异常缺陷关联。" }
        onAccepted: {
            const value = App.inspection.completeTask(desk.taskId)
            if (desk.showResult(value,"巡检已提交并归档")) desk.taskId = ""
        }
    }
}
