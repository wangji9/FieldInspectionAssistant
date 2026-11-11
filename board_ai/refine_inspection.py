from pathlib import Path
p=Path('FieldInspectionAssistant/qml/pages/InspectionPage.qml')
s=p.read_text(encoding='utf8').replace('import QtQuick.Layouts','import QtQuick.Controls\nimport QtQuick.Layouts')
s=s.replace('    property string taskId:', '    property bool readOnlyTask: false\n    property string taskId:',1)
s=s.replace('        root.items = App.inspection.templateItems()', '''        var tasks = App.inspection.tasks()
        for (var k = 0; k < tasks.length; k++) if (tasks[k].id === root.taskId) root.readOnlyTask = tasks[k].status === "completed"
        root.items = App.inspection.templateItems()''',1)
s=s.replace('        root.valueText = ""\n        root.noteText = ""\n        root.message = ""','        loadItem()\n        root.message = ""',1)
s=s.replace('        root.progressMap = {}\n        var list', '        var map = {}\n        var list',1).replace('root.progressMap[list[i].key] = list[i]', 'map[list[i].key] = list[i]\n        root.progressMap = map',1)
s=s.replace('    function savedFor(key)', '''    onCurrentItemIndexChanged: loadItem()
    function loadItem() {
        var it = currentItem(), saved = it ? progressMap[it.key] : null
        valueText = saved ? saved.value || "" : ""
        noteText = saved ? saved.note || "" : ""
    }
    function savedFor(key)''',1)
s=s.replace('        if (!root.currentItem()) return','        if (root.readOnlyTask || !root.currentItem()) return',1)
s=s.replace('        root.valueText = ""\n        root.noteText = ""\n        reloadProgress()', '        reloadProgress()\n        loadItem()',1)
s=s.replace('        var it = root.currentItem()\n        var r = App.defects', '        var it = root.currentItem()\n        if (!it || !currentAsset() || root.readOnlyTask) return\n        var r = App.defects',1)
s=s[:s.index('    // 顶部栏')]+'''    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 40; spacing: 12
            TouchButton { text: "返回任务"; autoWidth: true; height: 38; variant: "ghost"; onClicked: App.nav.push("tasks") }
            Text { text: root.taskName; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 24; font.bold: true; color: Colors.text }
            Text { text: root.readOnlyTask ? "已完成" : root.taskCode; font.pixelSize: 17; color: Colors.textSecondary }
            TouchButton { text: "提交任务"; autoWidth: true; height: 40; variant: "primary"; enabled: !root.readOnlyTask; onClicked: completeNow() }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 12
            Rectangle {
                Layout.preferredWidth: 230; Layout.fillHeight: true; color: Colors.panel; border.color: Colors.border; radius: 4
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 10
                    Text { text: "设备清单"; font.pixelSize: 20; font.bold: true; color: Colors.text }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4; model: root.assets
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            width: ListView.view.width; height: 66; radius: 3
                            color: index === root.currentAssetIndex ? Colors.infoDark : Colors.panelAlt
                            Column { anchors.fill: parent; anchors.margins: 8; spacing: 5
                                Text { width: parent.width; elide: Text.ElideRight; text: modelData.code; font.pixelSize: 17; font.bold: true; color: Colors.text }
                                Text { width: parent.width; elide: Text.ElideRight; text: modelData.name; font.pixelSize: 16; color: Colors.textSecondary }
                            }
                            MouseArea { anchors.fill: parent; onClicked: root.selectAsset(index) }
                        }
                    }
                    Text { text: "设备 " + Math.max(0,root.currentAssetIndex + 1) + " / " + root.assets.length; font.pixelSize: 16; color: Colors.textSecondary }
                }
            }
            Rectangle {
                Layout.preferredWidth: 250; Layout.fillHeight: true; color: Colors.panel; border.color: Colors.border; radius: 4
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 10
                    Text { text: "检查项目"; font.pixelSize: 20; font.bold: true; color: Colors.text }
                    Text { Layout.fillWidth: true; text: root.currentAsset() ? root.currentAsset().name : ""; elide: Text.ElideRight; font.pixelSize: 16; color: Colors.textSecondary }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4; model: root.items
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            width: ListView.view.width; height: 64; radius: 3
                            color: index === root.currentItemIndex ? Colors.infoDark : Colors.panelAlt
                            Column { anchors.fill: parent; anchors.margins: 8; spacing: 5
                                Text { width: parent.width; elide: Text.ElideRight; text: modelData.name; font.pixelSize: 17; color: Colors.text }
                                Text { width: parent.width; elide: Text.ElideRight; text: { var saved = root.savedFor(modelData.key); return saved ? saved.resultText + (saved.value ? " · " + saved.value + (modelData.unit || "") : "") : "未填写" }; font.pixelSize: 16; color: { var s=root.savedFor(modelData.key); return s && s.abnormal ? Colors.danger : Colors.textSecondary } }
                            }
                            MouseArea { anchors.fill: parent; onClicked: { root.currentItemIndex = index; root.loadItem() } }
                        }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; color: Colors.panel; border.color: Colors.border; radius: 4
                ScrollView {
                    id: operationScroll; anchors.fill: parent; anchors.margins: 16; clip: true; contentWidth: availableWidth
                    ColumnLayout {
                        width: operationScroll.availableWidth; spacing: 14
                        Text { Layout.fillWidth: true; text: root.currentItem() ? root.currentItem().name : "请选择检查项"; wrapMode: Text.Wrap; font.pixelSize: 22; font.bold: true; color: Colors.text }
                        Text { Layout.fillWidth: true; visible: text.length > 0; text: root.currentItem() ? root.currentItem().thresholdText || "" : ""; wrapMode: Text.Wrap; font.pixelSize: 17; color: Colors.textSecondary }
                        LabeledField { Layout.fillWidth: true; visible: root.currentItem() && root.currentItem().type === "measure"; label: "测量值" + (root.currentItem() && root.currentItem().unit ? " ("+root.currentItem().unit+")" : ""); text: root.valueText; readOnly: root.readOnlyTask; onEdited: root.valueText = value }
                        Text { text: "现场备注"; font.pixelSize: 17; color: Colors.textSecondary }
                        ScrollView {
                            Layout.fillWidth: true; Layout.preferredHeight: 84; clip: true
                            TextArea { text: root.noteText; onTextChanged: root.noteText = text; readOnly: root.readOnlyTask; placeholderText: "填写异常情况或跳过原因"; wrapMode: TextEdit.Wrap; font.pixelSize: 17; color: Colors.text; background: Rectangle { color: Colors.panelAlt; border.color: Colors.border; radius: 3 } }
                        }
                        Flow {
                            Layout.fillWidth: true; Layout.preferredHeight: childrenRect.height; spacing: 8
                            TouchButton { text: "正常"; autoWidth: true; height: 40; variant: "primary"; enabled: !root.readOnlyTask && !!root.currentItem(); onClicked: root.saveResult("normal") }
                            TouchButton { text: "异常"; autoWidth: true; height: 40; variant: "danger"; enabled: !root.readOnlyTask && !!root.currentItem(); onClicked: root.saveResult("abnormal") }
                            TouchButton { text: "跳过"; autoWidth: true; height: 40; variant: "ghost"; enabled: !root.readOnlyTask && !!root.currentItem(); onClicked: root.saveResult("skip") }
                            TouchButton { text: "下一项"; autoWidth: true; height: 40; variant: "ghost"; enabled: root.currentItemIndex + 1 < root.items.length; onClicked: root.nextItem() }
                        }
                        Text { Layout.fillWidth: true; visible: text.length > 0; text: root.message; wrapMode: Text.Wrap; font.pixelSize: 17; color: root.message.indexOf("已") === 0 ? Colors.normal : Colors.danger }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Colors.border }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "现场照片"; Layout.fillWidth: true; font.pixelSize: 20; font.bold: true; color: Colors.text }
                            TouchButton { text: "连接相机"; autoWidth: true; height: 38; variant: "ghost"; enabled: !root.readOnlyTask && !App.evidence.cameraConnected; onClicked: root.openCamera() }
                            TouchButton { text: "拍照"; width: 64; height: 38; variant: "ghost"; enabled: !root.readOnlyTask && App.evidence.cameraConnected; onClicked: root.captureNow() }
                        }
                        CameraPreview { id: camPreview; Layout.fillWidth: true; Layout.preferredHeight: 200; visible: App.evidence.cameraConnected; Connections { target: App.camera; function onFrameReady(frame) { camPreview.frame = frame } } }
                        ListView { Layout.fillWidth: true; Layout.preferredHeight: 120; visible: root.attachmentsList.length > 0; clip: true; orientation: ListView.Horizontal; spacing: 8; model: root.attachmentsList
                            delegate: Image { width: 160; height: 116; fillMode: Image.PreserveAspectFit; source: modelData.path ? "file://" + modelData.path : "" }
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: Colors.border }
                        Text { text: "缺陷登记"; font.pixelSize: 20; font.bold: true; color: Colors.text }
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
'''
p.write_text(s,encoding='utf8')
