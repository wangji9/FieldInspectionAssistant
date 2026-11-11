import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FieldInspection
import "../theme"
import "../components"

Rectangle {
    id: root
    objectName: "inspectionPage"
    color: Colors.background

    property bool readOnlyTask: false
    property string taskId: ""
    property string taskName: ""
    property string taskCode: ""
    property var assets: []
    property var items: []
    property int currentAssetIndex: -1
    property int currentItemIndex: 0
    property string recordId: ""
    property var progressMap: ({})
    property string valueText: ""
    property string noteText: ""
    property string message: ""
    property string priority: "medium"
    property int evidenceStamp: 0
    property var attachmentsList: []
    property string pendingPhotoRecord: ""
    Connections {
        target:App.camera
        function onStateChanged(){if(root.pendingPhotoRecord&&App.camera.hasFrame){var same=root.visible&&root.pendingPhotoRecord===root.recordId;root.pendingPhotoRecord="";if(same)Qt.callLater(root.captureNow)}}
        function onError(message){if(root.pendingPhotoRecord){root.pendingPhotoRecord="";root.message=message}}
    }

    function currentAsset() { return currentAssetIndex >= 0 ? root.assets[currentAssetIndex] : null }
    function currentItem() { return root.items.length ? root.items[currentItemIndex] : null }

    function init() {
        root.taskId = App.nav.param("taskId") || ""
        root.taskName = App.nav.param("taskName") || ""
        root.taskCode = App.nav.param("taskCode") || ""
        if (!root.taskId) { App.nav.push("tasks"); return }
        root.readOnlyTask=false
        root.recordId="";root.progressMap=({})
        var tasks = App.inspection.tasks()
        var found=false
        for (var k = 0; k < tasks.length; k++) if (tasks[k].id === root.taskId) {found=true;root.readOnlyTask = tasks[k].status === "completed"}
        if(!found){App.nav.push("tasks");return}
        root.items = App.inspection.templateItems(root.taskId)
        root.assets = App.inspection.taskAssets(root.taskId)
        root.currentAssetIndex = 0
        selectAsset(0)
    }
    onVisibleChanged: {if (visible) init();else pendingPhotoRecord=""}
    Component.onCompleted: if (App.nav.page === "inspection") init()

    function selectAsset(idx) {
        if (idx < 0 || idx >= root.assets.length) return
        root.currentAssetIndex = idx
        var a = root.assets[idx]
        var rec = App.inspection.recordFor(root.taskId, a.id)
        if (!rec.recordId) {
            root.recordId = ""
            root.progressMap = ({})
            root.attachmentsList = []
            root.valueText = ""
            root.noteText = ""
            root.message = rec.error || "无法读取本次巡检记录，请返回任务列表检查任务状态。"
            return
        }
        a.recordId = rec.recordId
        root.recordId = rec.recordId
        reloadProgress()
        root.currentItemIndex = 0
        loadItem()
        root.message = ""
    }
    function reloadProgress() {
        var map = {}
        var list = App.inspection.progress(root.recordId)
        for (var i = 0; i < list.length; i++) map[list[i].key] = list[i]
        root.progressMap = map
        root.reloadAttachments()
    }
    function reloadAttachments() {
        root.attachmentsList = App.evidence.attachments(root.recordId)
    }
    function openProjectEvidence() {
        const asset = currentAsset()
        if (!asset || !recordId) return
        let device = App.projects.devices.find(row => row.asset_id === asset.id)
        if (!device) {
            const result = App.projects.registerAsset(asset.id)
            if (!result.ok) { root.message = result.error; return }
            device = {id:result.id}
        }
        App.nav.push("projectEvidence", {device_id:device.id, asset_id:asset.id, capture_record_id:recordId})
    }
    onCurrentItemIndexChanged: loadItem()
    function loadItem() {
        var it = currentItem(), saved = it ? progressMap[it.key] : null
        valueText = saved ? saved.value || "" : ""
        noteText = saved ? saved.note || "" : ""
    }
    function savedFor(key) { return root.progressMap[key] }

    function saveResult(result) {
        if (root.readOnlyTask || !root.currentItem()) return
        var it = root.currentItem()
        var r = App.inspection.saveItem(root.recordId, it.key, result, root.valueText, root.noteText)
        if (!r.ok) { root.message = r.error; return }
        root.message = "已保存：" + (result === "normal" ? "正常" : result === "abnormal" ? "异常" : "跳过")
        reloadProgress()
        loadItem()
    }
    function nextItem() {
        if (root.currentItemIndex + 1 < root.items.length) root.currentItemIndex++
        else root.message = "该设备检查项已完成，可切换设备或提交任务"
    }
    function createDefectNow() {
        var it = root.currentItem()
        if (!it || !currentAsset() || root.readOnlyTask) return
        var r = App.defects.createDefect(root.recordId, currentAsset().id, it.name + "异常", root.noteText, root.priority, it.key)
        if (!r.ok) { root.message = r.error; return }
        root.message = "缺陷单已创建：" + r.code + "，可在缺陷中心分派处理"
        root.noteText = ""
    }
    function completeNow() {
        var r = App.inspection.completeTask(root.taskId)
        if (!r.ok) { root.message = r.error; return }
        App.nav.push("tasks")
    }
    function openCamera() {
        if (!App.evidence.cameraOpen()) {
            if(App.camera.opening)return true
            var devs = App.evidence.cameras()
            if (!devs.length) { root.message = "未检测到摄像头设备"; return false }
            if (!App.evidence.openCamera(devs[0])) { root.message = App.camera.status; return false }
        }
        return true
    }
    function captureNow() {
        if(root.readOnlyTask||!openCamera())return
        if(!App.camera.hasFrame){root.pendingPhotoRecord=root.recordId;root.message="正在等待摄像头画面…";return}
        var r = App.evidence.capture(root.recordId, "", root.noteText)
        if (!r.ok) { root.message = r.error; return }
        root.message = "照片已保存并关联记录：" + r.path.split("/").pop()
        root.noteText = ""
        root.evidenceStamp++
        root.reloadAttachments()
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 40; spacing: 12
            TouchButton { text: "返回任务"; autoWidth: true; height: 38; variant: "ghost"; onClicked: App.nav.push("tasks", {asset_id:(root.currentAsset() || {}).id || ""}) }
            Text { text: root.taskName; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: Typography.title; font.bold: true; color: Colors.text }
            Text { text: root.readOnlyTask ? "已完成" : root.taskCode; font.pixelSize: Typography.body; color: Colors.textSecondary }
            TouchButton {text:"本次证据";autoWidth:true;height:36;enabled:!!root.recordId;onClicked:App.nav.push("product",{section:"evidence",record_id:root.recordId,asset_id:root.currentAsset().id})}
            TouchButton { text:"工程资料"; autoWidth:true; height:36; enabled:!!root.recordId && !!App.projects.project.id; onClicked:root.openProjectEvidence() }
            TouchButton { text: "提交任务"; autoWidth: true; height: 40; variant: "primary"; enabled: !root.readOnlyTask; onClicked: completeNow() }
        }
        ResponsiveRow {
            Layout.fillWidth: true; Layout.fillHeight: true
            breakpoint: 930; paneCount: 3; minimumPaneHeight: 480
            Rectangle {
                id: assetPane
                width: parent.stacked ? parent.width : 210
                height: parent.stacked ? 240 : parent.height
                color: Colors.panel; border.color: Colors.border; radius: Dimensions.radius
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 10
                    Text { text: "设备清单"; font.pixelSize: Typography.h2; font.bold: true; color: Colors.text }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4; model: root.assets
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            width: ListView.view.width; height: 66; radius: 8
                            color: index === root.currentAssetIndex ? Colors.infoDark : Colors.panelAlt
                            Column { anchors.fill: parent; anchors.margins: 8; spacing: 5
                                Text { width: parent.width; elide: Text.ElideRight; text: modelData.code; font.pixelSize: Typography.body; font.bold: true; color: Colors.text }
                                Text { width: parent.width; elide: Text.ElideRight; text: modelData.name; font.pixelSize: Typography.body; color: Colors.textSecondary }
                            }
                            MouseArea { anchors.fill: parent; onClicked: root.selectAsset(index) }
                        }
                    }
                    Text { text: "设备 " + Math.max(0,root.currentAssetIndex + 1) + " / " + root.assets.length; font.pixelSize: Typography.body; color: Colors.textSecondary }
                }
            }
            Rectangle {
                id: checkPane
                width: parent.stacked ? parent.width : 230
                height: parent.stacked ? 260 : parent.height
                color: Colors.panel; border.color: Colors.border; radius: Dimensions.radius
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 10
                    Text { text: "检查项目"; font.pixelSize: Typography.h2; font.bold: true; color: Colors.text }
                    Text { Layout.fillWidth: true; text: root.currentAsset() ? root.currentAsset().name : ""; elide: Text.ElideRight; font.pixelSize: Typography.body; color: Colors.textSecondary }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4; model: root.items
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            width: ListView.view.width; height: 64; radius: 8
                            color: index === root.currentItemIndex ? Colors.infoDark : Colors.panelAlt
                            Column { anchors.fill: parent; anchors.margins: 8; spacing: 5
                                Text { width: parent.width; elide: Text.ElideRight; text: modelData.name; font.pixelSize: Typography.body; color: Colors.text }
                                Text { width: parent.width; elide: Text.ElideRight; text: { var saved = root.savedFor(modelData.key); return saved ? saved.resultText + (saved.value ? " · " + saved.value + (modelData.unit || "") : "") : "未填写" } font.pixelSize: Typography.body; color: { var s=root.savedFor(modelData.key); return s && s.abnormal ? Colors.danger : Colors.textSecondary } }
                            }
                            MouseArea { anchors.fill: parent; onClicked: { root.currentItemIndex = index; root.loadItem() } }
                        }
                    }
                }
            }
            Rectangle {
                width: parent.stacked ? parent.width : parent.width - assetPane.width - checkPane.width - Dimensions.spacing * 2
                height: parent.stacked ? 560 : parent.height
                color: Colors.panel; border.color: Colors.border; radius: Dimensions.radius
                ScrollView {
                    id: operationScroll; anchors.fill: parent; anchors.margins: 16; clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: operationScroll.availableWidth; spacing: 14
                        Text { Layout.fillWidth: true; text: root.currentItem() ? root.currentItem().name : "请选择检查项"; wrapMode: Text.Wrap; font.pixelSize: Typography.h2; font.bold: true; color: Colors.text }
                        Text { Layout.fillWidth: true; visible: text.length > 0; text: root.currentItem() ? root.currentItem().thresholdText || "" : ""; wrapMode: Text.Wrap; font.pixelSize: Typography.body; color: Colors.textSecondary }
                        LabeledField { Layout.fillWidth: true; visible: root.currentItem() && root.currentItem().type === "measure"; label: root.currentItem() && root.currentItem().unit ? "数值 / " + root.currentItem().unit : "数值"; text: root.valueText; readOnly: root.readOnlyTask; inputMethodHints: Qt.ImhFormattedNumbersOnly; onValueEdited: root.valueText = value }
                        Text { text: "现场备注"; font.pixelSize: Typography.body; color: Colors.textSecondary }
                        ScrollView {
                            Layout.fillWidth: true; Layout.preferredHeight: 84; clip: true
                            TextArea { objectName: "inspectionNoteInput"; text: root.noteText; onTextChanged: root.noteText = text; readOnly: root.readOnlyTask; placeholderText: "填写异常情况或跳过原因"; wrapMode: TextEdit.Wrap; font.pixelSize: Typography.body; color: Colors.text; background: Rectangle { color: Colors.panelAlt; border.color: Colors.border; radius: 8 } }
                        }
                        Flow {
                            Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 8
                            TouchButton { text: "正常"; autoWidth: true; height: 40; variant: "primary"; enabled: !root.readOnlyTask && !!root.currentItem(); onClicked: root.saveResult("normal") }
                            TouchButton { text: "异常"; autoWidth: true; height: 40; variant: "danger"; enabled: !root.readOnlyTask && !!root.currentItem(); onClicked: root.saveResult("abnormal") }
                            TouchButton { text: "跳过"; autoWidth: true; height: 40; variant: "ghost"; enabled: !root.readOnlyTask && !!root.currentItem(); onClicked: root.saveResult("skip") }
                            TouchButton { text: "下一项"; autoWidth: true; height: 40; variant: "ghost"; enabled: root.currentItemIndex + 1 < root.items.length; onClicked: root.nextItem() }
                        }
                        Text { Layout.fillWidth: true; visible: text.length > 0; text: root.message; wrapMode: Text.Wrap; font.pixelSize: Typography.body; color: root.message.indexOf("已") === 0 ? Colors.normal : Colors.danger }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Colors.border }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "现场照片"; Layout.fillWidth: true; font.pixelSize: Typography.h2; font.bold: true; color: Colors.text }
                            TouchButton { text: "连接相机"; autoWidth: true; height: 38; variant: "ghost"; enabled: !root.readOnlyTask && !App.evidence.cameraConnected; onClicked: root.openCamera() }
                            TouchButton { text: "拍照"; width: 64; height: 38; variant: "ghost"; enabled: !root.readOnlyTask && App.evidence.cameraConnected; onClicked: root.captureNow() }
                        }
                        CameraPreview { id: camPreview; Layout.fillWidth: true; Layout.preferredHeight: 200; visible: App.evidence.cameraConnected; Connections { target: App.camera; function onFrameReady(frame) { camPreview.frame = frame } } }
                        ListView { Layout.fillWidth: true; Layout.preferredHeight: 120; visible: root.attachmentsList.length > 0; clip: true; orientation: ListView.Horizontal; spacing: 8; model: root.attachmentsList
                            delegate: Image { width: 160; height: 116; fillMode: Image.PreserveAspectFit; source: modelData.path ? "file://" + modelData.path : "" }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Colors.border }
                        Text { text: "缺陷登记"; font.pixelSize: Typography.h2; font.bold: true; color: Colors.text }
                        RowLayout {
                            Layout.fillWidth: true; spacing: 8
                            Repeater { model: [{label:"高",value:"high"},{label:"中",value:"medium"},{label:"低",value:"low"}]
                                TouchButton { text: "优先级" + modelData.label; autoWidth: true; height: 36; enabled: !root.readOnlyTask; variant: root.priority === modelData.value ? "primary" : "ghost"; onClicked: root.priority = modelData.value }
                            }
                            Item { Layout.fillWidth: true }
                        }
                        TouchButton { text: "登记缺陷"; autoWidth: true; height: 40; variant: "danger"; enabled: !root.readOnlyTask && !!root.currentItem(); onClicked: root.createDefectNow() }
                    }
                }
            }
        }
    }
}
