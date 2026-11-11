import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// 系统设置：摄像头、存储、同步、检查标准阈值、账号权限、系统诊断
Rectangle {
    id: root
    objectName: "settingsPage"
    color: Colors.background

    property int currentTab: 0
    property var permissionData: []
    property var cameras: []
    property var usersData: []
    property var outboxData: []
    property var thresholdItems: []
    property var minValues: ({})
    property var maxValues: ({})
    property string templateId: ""
    property string cameraMessage: ""
    property string storageMessage: ""
    property string syncMessage: ""
    property string thresholdMessage: ""
    property string accountMessage: ""

    function refresh() {
        root.cameras = App.evidence.cameras()
        root.usersData = App.session.users()
        root.outboxData = App.sync.outbox()
        root.thresholdItems = App.inspection.templateItems()
        var tmpls = App.assets.templates()
        root.templateId = tmpls.length > 0 ? tmpls[0].id : ""
        var m1 = {}, m2 = {}
        for (var i = 0; i < root.thresholdItems.length; i++) {
            var it = root.thresholdItems[i]
            m1[it.key] = it.min === undefined || it.min === null ? "" : String(it.min)
            m2[it.key] = it.max === undefined || it.max === null ? "" : String(it.max)
        }
        root.minValues = m1
        root.maxValues = m2
        root.permissionData = root.permissionRows()
    }
    function saveThreshold(key) {
        var it = root.thresholdItem(key)
        if (!it) return
        var r = App.assets.updateThreshold(root.templateId, key, root.minValues[key], root.maxValues[key], it.unit || "")
        root.thresholdMessage = r.ok ? "已保存阈值：" + it.name : r.error
    }
    function thresholdItem(key) {
        for (var i = 0; i < root.thresholdItems.length; i++)
            if (root.thresholdItems[i].key === key) return root.thresholdItems[i]
        return null
    }
    function openCameraAt(path) {
        root.cameraMessage = App.evidence.openCamera(path) ? "正在打开摄像头：" + path : App.camera.status
    }
    function flushSync() {
        var r = App.sync.flush()
        root.syncMessage = r.ok ? "同步完成：" + (r.sent !== undefined ? r.sent : "") : r.error
        root.outboxData = App.sync.outbox()
    }
    function exportCsv() {
        var d = new Date()
        var mm = ("0" + (d.getMonth() + 1)).slice(-2)
        var dd = ("0" + d.getDate()).slice(-2)
        var path = App.dataDir + "/export_" + d.getFullYear() + "-" + mm + "-" + dd + ".csv"
        var r = App.evidence.exportCsv(path)
        root.storageMessage = r.ok ? "已导出：" + path : r.error
    }
    function switchUser(name) {
        if (App.session.login(name)) {
            root.accountMessage = "已切换账户：" + name + "（" + App.session.roleText() + "）"
            root.usersData = App.session.users()
            root.permissionData = root.permissionRows()
        } else {
            root.accountMessage = "切换失败：账户不可用"
        }
    }
    function permissionRows() {
        var perms = [
            { key: "inspect", label: "巡检执行" },
            { key: "register_defect", label: "缺陷登记" },
            { key: "assign", label: "缺陷分派" },
            { key: "review", label: "缺陷复核" },
            { key: "start_defect", label: "缺陷处理" },
            { key: "manage_assets", label: "设备管理" },
            { key: "settings", label: "系统设置" }
        ]
        for (var i = 0; i < perms.length; i++) perms[i].allowed = App.session.can(perms[i].key)
        return perms
    }

    function showSection() {
        const sections = {hardware: 0, assistant: 1, standards: 2, users: 3, data: 4, runtime: 5, deviceInfo: 6}
        const tab = sections[App.nav.param("section")]
        if (tab !== undefined) currentTab = tab
    }
    onVisibleChanged: {updateMonitoring();if (visible) { refresh(); showSection() }}
    Component.onCompleted: refresh()

    Connections {target:App.nav;function onPageChanged(){if(App.nav.page==="settings")root.showSection()}}
    function updateMonitoring(){App.hardware.active=root.visible&&App.session.loggedIn&&[0,5,6].includes(root.currentTab);if(App.hardware.active)App.ai.refreshStatus()}
    onCurrentTabChanged:updateMonitoring()
    Timer{interval:3000;repeat:true;running:root.visible&&root.currentTab===0&&App.session.loggedIn;onTriggered:App.ai.refreshStatus()}
    Connections{target:App.session;function onSessionChanged(){root.updateMonitoring()}}
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12
        PageHeading {Layout.fillWidth:true;title:"系统设置"}
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Repeater {
                model: ["设备连接", "智能助手", "检查标准", "用户管理", "数据管理", "运行维护", "设备性能"]
                TouchButton { text: modelData; autoWidth: true; height: 36; variant: root.currentTab === index ? "tonal" : "ghost"; onClicked: root.currentTab = index }
            }
            Item { Layout.fillWidth: true }
        }
        StackLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: root.currentTab
            HardwareConnectionsPane {panel:root}
            Rectangle {
                color: Colors.panel; border.color: Colors.border; radius: Dimensions.radius
                ScrollView {
                    id: assistantSettingsScroll
                    anchors.fill: parent; anchors.margins: 20
                    contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: assistantSettingsScroll.availableWidth; spacing: 18
                    Text { text: "回答与显示"; font.pixelSize: Typography.h2; font.bold: true; color: Colors.text }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Colors.border }
                    Flow {
                        Layout.fillWidth: true; spacing: 12
                        Text { text: "回答长度上限"; width: 160; height:40;verticalAlignment:Text.AlignVCenter;font.pixelSize: Typography.body; color: Colors.text }
                        Repeater {
                            model: [{name: "短答 · 96字", count: 96}, {name: "常规 · 256字", count: 256}, {name: "详细 · 512字", count: 512}, {name: "长篇 · 1024字", count: 1024}]
                            TouchButton { text: modelData.name; autoWidth: true; height: 40; variant: App.ai.answerLengthLimit === modelData.count ? "primary" : "ghost"; onClicked: App.ai.answerLengthLimit = modelData.count }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 16
                        Text { text: "答案字号"; Layout.preferredWidth: 160; font.pixelSize: Typography.body; color: Colors.text }
                        Repeater {
                            model: [{name: "紧凑", size: 14}, {name: "常规", size: 16}, {name: "舒适", size: 18}]
                            TouchButton { text: modelData.name; width: 100; height: 40; variant: App.ai.answerFontSize === modelData.size ? "primary" : "ghost"; onClicked: App.ai.answerFontSize = modelData.size }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: Colors.border }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "回答方式"; Layout.preferredWidth: 176; font.pixelSize: Typography.body; color: Colors.text }
                        Text { text: "直接回答，边生成边显示"; font.pixelSize: Typography.body; color: Colors.textSecondary }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "适用模块"; Layout.preferredWidth: 176; font.pixelSize: Typography.body; color: Colors.text }
                        Text { text: "文字问答、图片问答"; font.pixelSize: Typography.body; color: Colors.textSecondary }
                        Item { Layout.fillWidth: true }
                    }
                    Item { Layout.fillHeight: true }
                    Text { text: "设置自动保存，下次回答生效。"; font.pixelSize: Typography.body; color: Colors.textSecondary }
                }
                }
            }
            Rectangle {
                color: Colors.panel; border.color: Colors.border; radius: Dimensions.radius
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 12
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 36; Layout.leftMargin: 6; Layout.rightMargin: 6; spacing: 12
                        Text { text: "检查项目"; Layout.fillWidth: true; font.pixelSize: Typography.body; font.bold: true; color: Colors.text }
                        Text { text: "方式"; Layout.preferredWidth: 100; font.pixelSize: Typography.body; color: Colors.textSecondary }
                        Text { text: "下限"; Layout.preferredWidth: 125; font.pixelSize: Typography.body; color: Colors.textSecondary }
                        Text { text: "上限"; Layout.preferredWidth: 125; font.pixelSize: Typography.body; color: Colors.textSecondary }
                        Text { text: "单位"; Layout.preferredWidth: 74; font.pixelSize: Typography.body; color: Colors.textSecondary }
                        Item { Layout.preferredWidth: 72 }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4; model: root.thresholdItems
                        ScrollBar.vertical: ScrollBar { }
                        delegate: Rectangle {
                            width: ListView.view.width; height: 54; color: index % 2 ? Colors.panelAlt : Colors.panel
                            RowLayout {
                                anchors.fill: parent; anchors.margins: 6; spacing: 12
                                Text { Layout.fillWidth: true; text: modelData.name; font.pixelSize: Typography.body; color: Colors.text; elide: Text.ElideRight }
                                Text { Layout.preferredWidth: 100; text: modelData.type === "measure" ? "测量" : "目视检查"; font.pixelSize: Typography.body; color: Colors.textSecondary }
                                FieldInput { Layout.preferredWidth: 125; height: 38; readOnly: modelData.type !== "measure"; inputMethodHints: Qt.ImhFormattedNumbersOnly; text: root.minValues[modelData.key] === undefined ? "" : String(root.minValues[modelData.key]); placeholder: "—"; onTextEdited: root.minValues[modelData.key] = value }
                                FieldInput { Layout.preferredWidth: 125; height: 38; readOnly: modelData.type !== "measure"; inputMethodHints: Qt.ImhFormattedNumbersOnly; text: root.maxValues[modelData.key] === undefined ? "" : String(root.maxValues[modelData.key]); placeholder: "—"; onTextEdited: root.maxValues[modelData.key] = value }
                                Text { Layout.preferredWidth: 74; text: modelData.unit || "—"; font.pixelSize: Typography.body; color: Colors.textSecondary }
                                TouchButton { Layout.preferredWidth: 72; text: "保存"; height: 38; variant: "ghost"; onClicked: root.saveThreshold(modelData.key) }
                            }
                        }
                    }
                    Text { Layout.fillWidth: true; text: root.thresholdMessage; visible: text.length > 0; font.pixelSize: Typography.body; color: Colors.info; wrapMode: Text.Wrap }
                }
            }
            UserManagementPage {}

            ColumnLayout {
                spacing:8
                Flow{Layout.fillWidth:true;spacing:8
                    TouchButton{text:"导出巡检记录";autoWidth:true;height:34;onClicked:root.exportCsv()}
                    Text{height:34;verticalAlignment:Text.AlignVCenter;text:"待同步："+App.sync.pending+" 条";font.pixelSize:Typography.body;color:Colors.textSecondary}
                    TouchButton{text:"立即同步";autoWidth:true;height:34;enabled:App.sync.configured&&App.sync.online&&App.session.permissions.settings;onClicked:root.flushSync()}
                    TouchButton{text:App.sync.online?"暂停同步":"允许同步";autoWidth:true;height:34;variant:"ghost";enabled:App.sync.configured&&App.session.permissions.settings;onClicked:App.sync.online=!App.sync.online}
                }
                Text{Layout.fillWidth:true;text:root.storageMessage||root.syncMessage||App.sync.status;visible:text.length>0;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                ProductPage{id:dataMaintenance;objectName:"settingsDataManagement";embedded:true;showHeading:false;Layout.fillWidth:true;Layout.fillHeight:true;Component.onCompleted:Qt.callLater(function(){setup({section:"data"})})}
            }
            ProductPage{id:runtimeMaintenance;objectName:"settingsRuntimeMaintenance";embedded:true;showHeading:false;Component.onCompleted:Qt.callLater(function(){setup({section:"maintenance"})})}
            RuntimeStatusPanel { objectName: "devicePerformancePanel" }

        }
    }
}
