import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FieldInspection
import "../theme"

ColumnLayout {
    id: pane
    spacing: 5
    readonly property var service: App.faceAccess
    property var devices: []
    property var observed: []
    property var selected: null
    readonly property var people: service.people.filter(row => {
        const term = search.text.trim().toLowerCase()
        return !term || row.name.toLowerCase().includes(term) || row.code.toLowerCase().includes(term)
    })

    function scan() {
        const previous = source.currentText
        devices = App.visionLive.cameras()
        const index = devices.indexOf(previous)
        const usb = devices.findIndex(name => name.startsWith("/dev/video1 ") || name === "/dev/video1")
        source.currentIndex = index >= 0 ? index : usb >= 0 ? usb : devices.length ? 0 : -1
    }
    function formatTime(value) { return String(value || "").replace("T", " ").slice(0, 23) }
    function angle(value) { return value === undefined ? "—" : Number(value).toFixed(1) + "°" }
    function openEditor(row) {
        editor.personId = row ? row.id : ""
        code.text = row ? row.code : ""
        nameField.text = row ? row.name : ""
        active.checked = row ? row.active : true
        replaceFeature.checked = !row
        captureView.currentIndex = 0
        editor.feedback = ""
        editor.open()
    }
    onVisibleChanged: {
        if (visible) { scan(); service.refresh() }
        else { service.stop(); selected = null; editor.close(); removeDialog.close() }
    }
    Connections {
        target: pane.service
        function onObservationsChanged() {
            if (!snapshot.running) snapshot.start()
        }
    }
    Timer { id: snapshot; interval: 100; onTriggered: pane.observed = pane.service.faces }

    RowLayout {
        Layout.fillWidth: true
        spacing: 5
        AppComboBox {
            id: source
            Layout.preferredWidth: 162; Layout.preferredHeight: 28
            model: pane.devices; enabled: !pane.service.running
            displayText: currentIndex < 0 ? "无摄像头" : currentText
        }
        TouchButton { text: "扫描"; autoWidth: true; height: 28; enabled: !pane.service.running; onClicked: pane.scan() }
        TouchButton {
            objectName: "faceAccessStart"
            text: "开始识别"; autoWidth: true; height: 28; variant: "primary"
            enabled: !!App.session.permissions.inspect && source.currentIndex >= 0 && !pane.service.running
                && !App.ai.busy && !App.agent.busy && !App.visionLive.running && !App.videoUnderstanding.running
            onClicked: if (App.voiceAssistant.suspendForActivity()) pane.service.start(source.currentText)
        }
        TouchButton {
            objectName: "faceAccessStop"
            text: "停止"; autoWidth: true; height: 28
            enabled: pane.service.running
            onClicked: pane.service.stop()
        }
        Text { Layout.fillWidth: true; text: pane.service.status; color: Colors.textSecondary; elide: Text.ElideRight; font.pixelSize: Typography.small }
    }

    RowLayout {
        Layout.fillWidth: true; Layout.fillHeight: true
        spacing: 6
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: pane.width * .61; Layout.minimumWidth: 140
            spacing: 0
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 28; color: "#223443"
                Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "摄像头 · 人脸与视线"; color: "#f2f6fa"; font.pixelSize: Typography.small }
                Text {
                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                    visible: pane.service.running && App.visionLive.summary.cameraFps > 0
                    text: visible ? App.visionLive.summary.cameraFps.toFixed(1) + " FPS" : ""
                    color: "#ccdae6"; font.pixelSize: Typography.small
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true; color: "#16222d"
                CameraPreview { anchors.fill: parent; frame: App.visionLive.previewFrame; fillCrop: false; visible: pane.service.running }
                LiveVisionView { anchors.fill: parent; source: App.visionLive; fillCrop: false; visible: pane.service.running }
                Text {
                    anchors.centerIn: parent
                    visible: !pane.service.running || !App.visionLive.hasPreview
                    text: pane.service.running ? "等待摄像头画面" : "摄像头预览"
                    color: "#cbd8e3"; font.pixelSize: Typography.body
                }
            }
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 24; color: "#223443"
                Row {
                    anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                    Text { text: "视线"; color: "#f7da86"; font.pixelSize: Typography.small }
                    Text { text: "头部朝向"; color: "#9dcbfa"; font.pixelSize: Typography.small }
                    Text { text: "面部关键点"; color: "#63e7bc"; font.pixelSize: Typography.small }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: pane.width * .39; Layout.minimumWidth: 258
            spacing: 4
            Rectangle {
                Layout.fillWidth: true; implicitHeight: 28; color: "#223443"
                Text { anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "实时人脸分析"; color: "#f2f6fa"; font.pixelSize: Typography.small }
                Text { anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter; text: "RK1828 / C++"; color: "#ccdae6"; font.pixelSize: Typography.small }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: Math.min(132, Math.max(75, pane.height * .23)); color: "#edf3f8"; border.color: Colors.border
                ListView {
                    anchors.fill: parent; anchors.margins: 2; clip: true
                    model: pane.observed
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ColumnLayout {
                        required property var modelData
                        width: ListView.view.width - 15
                        spacing: 2
                        Text {
                            Layout.fillWidth: true; Layout.leftMargin: 6; Layout.topMargin: 5
                            text: modelData.name + (modelData.code ? "  ·  " + modelData.code : "")
                            textFormat: Text.PlainText; elide: Text.ElideRight; color: Colors.text; font.pixelSize: Typography.body; font.bold: true
                        }
                        Text {
                            Layout.fillWidth: true; Layout.leftMargin: 6
                            text: pane.formatTime(modelData.time) + "  ·  " + (modelData.pointCount || 0) + " 个关键点"
                                + (modelData.identityHeld ? "  ·  跟踪中" : "")
                            color: Colors.textSecondary; font.pixelSize: Typography.small; elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true; Layout.leftMargin: 6
                            text: "头部  偏航 " + pane.angle(modelData.headYaw) + "  俯仰 " + pane.angle(modelData.headPitch) + "  横滚 " + pane.angle(modelData.headRoll)
                            color: Colors.info; font.pixelSize: Typography.small; elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true; Layout.leftMargin: 6; Layout.bottomMargin: 5
                            text: "视线  水平 " + pane.angle(modelData.gazeYaw) + "  垂直 " + pane.angle(modelData.gazePitch)
                            color: Colors.warning; font.pixelSize: Typography.small; elide: Text.ElideRight
                        }
                    }
                }
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 9; anchors.verticalCenter: parent.verticalCenter
                    visible: pane.observed.length === 0; text: pane.service.running ? "等待人脸进入画面" : "尚无实时结果"
                    color: Colors.textSecondary; font.pixelSize: Typography.small
                }
            }

            TabBar {
                id: views
                Layout.fillWidth: true
                DesktopTab { objectName: "faceHistoryTab"; text: "识别记录" }
                DesktopTab { objectName: "facePeopleTab"; text: "人员库"; enabled: pane.service.canManage }
                DesktopTab { objectName: "faceDirectionTab"; text: "三维方向" }
            }
            RowLayout {
                visible: views.currentIndex === 1 && pane.service.canManage
                Layout.fillWidth: true; spacing: 4
                TextField { id: search; Layout.fillWidth: true; Layout.minimumWidth: 55; placeholderText: "姓名 / 编号"; font.pixelSize: Typography.small; onTextChanged: pane.selected = null }
                TouchButton { objectName: "faceAdd"; text: "新增"; height: 28; autoWidth: true; onClicked: pane.openEditor(null) }
                TouchButton { objectName: "faceEdit"; text: "编辑"; height: 28; autoWidth: true; enabled: !!pane.selected; onClicked: pane.openEditor(pane.selected) }
                TouchButton { objectName: "faceRemove"; text: "删除"; height: 28; autoWidth: true; enabled: !!pane.selected; onClicked: removeDialog.open() }
            }
            Rectangle {
                visible: views.currentIndex !== 2
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 54
                color: Colors.panel; border.color: Colors.border
                ListView {
                    id: records
                    anchors.fill: parent; anchors.margins: 1; clip: true
                    model: views.currentIndex === 0 ? pane.service.history : pane.people
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        readonly property bool directory: views.currentIndex === 1
                        objectName: directory ? "facePerson_" + index : "faceEvent_" + index
                        width: records.width - 12; height: 46
                        color: pane.selected && directory && pane.selected.id === modelData.id ? Colors.infoDark : index % 2 ? Colors.panelAlt : Colors.panel
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 6; spacing: 5
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 1
                                Text { Layout.fillWidth: true; text: modelData.name + (modelData.code ? "  ·  " + modelData.code : ""); textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: Typography.small; color: Colors.text }
                                Text { Layout.fillWidth: true; text: pane.formatTime(directory ? modelData.updated : modelData.time) + (directory ? "   侧脸 " + modelData.sideCount + "/4" : ""); elide: Text.ElideRight; font.pixelSize: Typography.small; color: Colors.textSecondary }
                            }
                            Text { text: directory ? (modelData.active ? "启用" : "停用") : modelData.result === "match" ? "匹配" : "未匹配"; color: (directory ? modelData.active : modelData.result === "match") ? Colors.success : Colors.warning; font.pixelSize: Typography.small }
                        }
                        MouseArea { anchors.fill: parent; enabled: directory; onClicked: pane.selected = modelData }
                    }
                }
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 8; anchors.top: parent.top; anchors.topMargin: 8
                    visible: records.count === 0
                    text: views.currentIndex === 0 ? "暂无识别记录" : "暂无匹配档案"
                    color: Colors.textSecondary; font.pixelSize: Typography.small
                }
            }
            Rectangle {
                visible: views.currentIndex === 2
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 92
                color: Colors.panel; border.color: Colors.border
                FaceDirectionPlot {
                    objectName: "faceDirectionPlot"
                    anchors.fill: parent; anchors.margins: 8
                    observation: pane.observed.length === 1 ? pane.observed[0] : null
                }
            }
            Text { Layout.fillWidth: true; visible: text.length > 0; text: pane.service.notice; wrapMode: Text.Wrap; color: Colors.textSecondary; font.pixelSize: Typography.small }
        }
    }

    Dialog {
        id: editor
        objectName: "faceEditor"
        anchors.centerIn: parent
        modal: true
        width: Math.min(420, pane.width - 16)
        property string personId: ""
        property string feedback: ""
        readonly property int side: captureView.currentIndex
        title: personId ? "编辑人员档案" : "录入人脸"
        contentItem: ColumnLayout {
            spacing: 8
            TextField { id: code; objectName: "facePersonCode"; Layout.fillWidth: true; placeholderText: "人员编号"; maximumLength: 40 }
            TextField { id: nameField; objectName: "facePersonName"; Layout.fillWidth: true; placeholderText: "姓名"; maximumLength: 80 }
            AppCheckBox { id: active; text: "启用识别" }
            AppComboBox {
                id: captureView
                objectName: "faceCaptureView"
                visible: editor.personId.length > 0
                Layout.fillWidth: true
                model: ["编辑档案 / 正脸模板", "朝画面左侧 · 30–55°", "朝画面右侧 · 30–55°",
                    "朝画面左侧 · 60–85°", "朝画面右侧 · 60–85°"]
            }
            AppCheckBox { id: replaceFeature; visible: editor.personId.length > 0 && !editor.side; text: "重新采集正脸" }
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                visible: !editor.personId || replaceFeature.checked || editor.side > 0
                readonly property bool ready: pane.service.enrollmentView === editor.side
                text: ready ? "已采集稳定人脸，可保存" : editor.side > 0
                    ? "由一人按所选方向和角度转头，保持片刻"
                    : "请开始识别，由一人面向摄像头"
                color: ready ? Colors.success : Colors.textSecondary; font.pixelSize: Typography.small
            }
            Text { Layout.fillWidth: true; visible: text.length > 0; text: editor.feedback; wrapMode: Text.Wrap; color: Colors.danger; font.pixelSize: Typography.small }
        }
        footer: DialogButtonBox {
            Button { text: "取消"; onClicked: editor.close() }
            Button {
                objectName: "faceSave"
                text: editor.side > 0 ? "保存侧脸" : "保存"
                enabled: pane.service.canManage && code.text.trim().length > 0 && nameField.text.trim().length > 0
                    && (editor.side > 0 ? pane.service.enrollmentView === editor.side
                        : (!replaceFeature.checked && editor.personId.length > 0) || pane.service.canEnroll)
                onClicked: {
                    const saved = editor.side > 0 ? pane.service.saveSide(editor.personId, editor.side)
                        : pane.service.save(editor.personId, code.text, nameField.text, active.checked, replaceFeature.checked)
                    if (saved) {
                        pane.selected = null
                        editor.close()
                    } else editor.feedback = pane.service.notice
                }
            }
        }
    }
    Dialog {
        id: removeDialog
        anchors.centerIn: parent; modal: true; title: "删除人员档案"
        width: Math.min(300, pane.width - 16)
        standardButtons: Dialog.Cancel | Dialog.Ok
        contentItem: Text { text: "删除此人的人脸模板和档案？"; color: Colors.text }
        onAccepted: if (pane.selected && pane.service.remove(pane.selected.id)) pane.selected = null
    }
}
