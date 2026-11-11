import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id: root
    objectName: "devicePage"
    color: Colors.background
    property string keyword: ""
    property var allAssets: []
    property int selectedIndex: -1
    property var selected: ({})
    property string scanCode: ""
    property string editName: ""
    property string editLocation: ""
    property string editModel: ""
    property string message: ""
    property bool newPanelOpen: false
    property string newCode: ""
    property string newName: ""
    property string newLocation: ""
    property string newModel: ""
    property int detailTab: 0
    function selectRow(idx, item) {
        selectedIndex = idx; selected = item
        editName = item.name || ""; editLocation = item.location || ""; editModel = item.model || ""
        message = ""
    }
    function refresh() {
        allAssets = App.assets.assets(keyword)
        var idx = -1
        for (var i = 0; i < allAssets.length; i++) if (allAssets[i].id === selected.id) idx = i
        if (allAssets.length) selectRow(idx >= 0 ? idx : 0, allAssets[idx >= 0 ? idx : 0])
        else { selected = ({}); selectedIndex = -1 }
    }
    function locateByCode() {
        var result = App.assets.findByCode(scanCode.trim())
        if (!result.ok) { message = result.error; return }
        keyword = ""; searchBox.text = ""; allAssets = App.assets.assets()
        for (var i = 0; i < allAssets.length; i++) if (allAssets[i].id === result.asset.id) { selectRow(i, allAssets[i]); return }
    }
    function save() {
        var result = App.assets.updateAsset(selected.id, editName.trim(), editLocation.trim(), editModel.trim())
        if (result.ok) { refresh(); message = "设备资料已保存" }
        else message = result.error
    }
    function create() {
        var result = App.assets.createAsset(newCode.trim(), newName.trim(), newLocation.trim(), newModel.trim())
        if (!result.ok) { message = result.error; return }
        var code = newCode.trim(); newPanelOpen = false; keyword = ""; searchBox.text = ""
        allAssets = App.assets.assets()
        for (var i = 0; i < allAssets.length; i++) if (allAssets[i].code === code) selectRow(i, allAssets[i])
        newCode = ""; newName = ""; newLocation = ""; newModel = ""; message = "设备已添加"
    }
    Connections {target:App.assets;function onAssetsChanged(){if(root.visible)root.refresh()}}
    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: refresh()
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12
        Item {
            Layout.fillWidth:true; Layout.preferredHeight:40
            Text { id:deviceTitle; anchors.left:parent.left; anchors.verticalCenter:parent.verticalCenter; text:"设备中心"; font.pixelSize:Typography.title; font.weight:Font.DemiBold; color:Colors.text }
            TouchButton { anchors.left:deviceTitle.right;anchors.leftMargin:16;anchors.verticalCenter:parent.verticalCenter;text:"部件与测点";autoWidth:true;height:34;onClicked:App.nav.push("product",{section:"devices",asset_id:root.selected.id||""}) }
            TouchButton { anchors.right:parent.right; anchors.verticalCenter:parent.verticalCenter; text:"新增设备";enabled:App.session.permissions.manage_assets;icon:"plus";autoWidth:true;height:36;variant:"primary";onClicked:{root.message="";root.newPanelOpen=true} }
        }
        ResponsiveRow {
            id: devicePanes
            Layout.fillWidth: true; Layout.fillHeight: true
            minimumPaneHeight: 400
            Rectangle {
                id: deviceListPanel
                width: parent.stacked ? parent.width : Math.min(320, parent.width * 0.36)
                height: parent.stacked ? 300 : parent.height
                color: Colors.panel; border.color: Colors.border; radius: Dimensions.radius
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12; spacing: 10
                    FieldInput { id: searchBox; Layout.fillWidth: true; height: 40; placeholder: "搜索设备编号或名称"; onTextEdited: { root.keyword = value; root.refresh() } }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        FieldInput { Layout.fillWidth: true; height: 40; placeholder: "输入或扫描设备编号"; onTextEdited: root.scanCode = value; onEditingFinished: if (value.length) root.locateByCode() }
                        TouchButton { text: "查找"; width: 64; height: 40; variant: "ghost"; onClicked: root.locateByCode() }
                    }
                    DataTable {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        columns: [{key: "code", title: "设备编号", width: 132}, {key: "name", title: "设备名称", width: 0}]
                        items: root.allAssets; selectedIndex: root.selectedIndex
                        onRowClicked: function(index, item) { root.selectRow(index, item); devicePanes.showPane(deviceDetailPanel) }
                    }
                }
            }
            Rectangle {
                id: deviceDetailPanel
                width: parent.stacked ? parent.width : parent.width - deviceListPanel.width - Dimensions.spacing
                height: parent.stacked ? 410 : parent.height
                color: Colors.panel; border.color: Colors.border; radius: Dimensions.radius
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 16; spacing: 16
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: 36
                        Text { text: root.selected.name || "设备资料"; Layout.fillWidth: true; font.pixelSize: Typography.h2; font.bold: true; color: Colors.text; elide: Text.ElideRight }
                        TouchButton { text: "保存资料"; autoWidth: true; height: 38; variant: "primary"; visible: root.detailTab === 0; enabled: !!root.selected.id&&App.session.permissions.manage_assets; onClicked: root.save() }
                        TouchButton {text:"删除";autoWidth:true;height:36;variant:"ghost";enabled:!!root.selected.id&&App.session.permissions.manage_assets;onClicked:removeDevice.openDialog("删除设备","删除选中设备？请先删除关联任务、档案和缺陷。","删除","取消")}
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        TouchButton { text: "设备资料"; autoWidth: true; height: 36; variant: root.detailTab === 0 ? "tonal" : "ghost"; onClicked: root.detailTab = 0 }
                        TouchButton { text: "巡检项目"; autoWidth: true; height: 36; variant: root.detailTab === 1 ? "tonal" : "ghost"; onClicked: root.detailTab = 1 }
                        Item { Layout.fillWidth: true }
                        Text { text: root.selected.code || ""; font.pixelSize: Typography.body; color: Colors.textSecondary }
                    }
                    StackLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true; currentIndex: root.detailTab
                        ColumnLayout {
                            spacing: 14
                            LabeledField { Layout.fillWidth: true; label: "设备编号"; text: root.selected.code || ""; readOnly: true }
                            LabeledField { Layout.fillWidth: true; label: "设备名称"; text: root.editName; onValueEdited: root.editName = value }
                            LabeledField { Layout.fillWidth: true; label: "安装位置"; text: root.editLocation; onValueEdited: root.editLocation = value }
                            LabeledField { Layout.fillWidth: true; label: "设备型号"; text: root.editModel; onValueEdited: root.editModel = value }
                            Text { Layout.fillWidth: true; visible: text.length > 0; text: root.message; wrapMode: Text.Wrap; font.pixelSize: Typography.body; color: Colors.info }
                            Item { Layout.fillHeight: true }
                        }
                        ListView {
                            clip: true; spacing: 1; model: root.selected.id ? App.inspection.templateItems() : []
                            ScrollBar.vertical: ScrollBar { }
                            delegate: Rectangle {
                                width: ListView.view.width; height: 52; color: index % 2 ? Colors.panelAlt : Colors.panel
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 12; spacing: 12
                                    Text { text: modelData.name; Layout.fillWidth: true; font.pixelSize: Typography.body; color: Colors.text; elide: Text.ElideRight }
                                    Text { text: modelData.type === "measure" ? "测量" : "目视检查"; Layout.preferredWidth: 90; font.pixelSize: Typography.body; color: Colors.textSecondary }
                                    Text { text: modelData.thresholdText || ""; Layout.preferredWidth: 180; font.pixelSize: Typography.body; color: Colors.textSecondary; elide: Text.ElideRight }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Rectangle {
        anchors.fill: parent; visible: root.newPanelOpen; color: "#88000000"; z: 20
        MouseArea { anchors.fill: parent; onClicked: root.newPanelOpen = false }
        Rectangle {
            anchors.centerIn: parent; width: Math.min(560, parent.width - 24); height: Math.min(414, parent.height - 24); color: Colors.panel; border.color: Colors.border; radius: Dimensions.radius
            MouseArea { anchors.fill: parent }
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 22; spacing: 12
                Text { text: "新增设备"; font.pixelSize: Typography.h2; font.bold: true; color: Colors.text }
                LabeledField { Layout.fillWidth: true; label: "设备编号"; text: root.newCode; placeholder: "如 PUMP-009"; onValueEdited: root.newCode = value }
                LabeledField { Layout.fillWidth: true; label: "设备名称"; text: root.newName; placeholder: "如 9 号循环水泵"; onValueEdited: root.newName = value }
                LabeledField { Layout.fillWidth: true; label: "安装位置"; text: root.newLocation; placeholder: "如 A 区泵房"; onValueEdited: root.newLocation = value }
                LabeledField { Layout.fillWidth: true; label: "设备型号"; text: root.newModel; onValueEdited: root.newModel = value }
                Text { Layout.fillWidth: true; visible: text.length > 0; text: root.message; font.pixelSize: Typography.body; wrapMode: Text.Wrap; color: Colors.danger }
                Item { Layout.fillHeight: true }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    TouchButton { text: "取消"; width: 88; height: 40; variant: "ghost"; onClicked: root.newPanelOpen = false }
                    TouchButton { text: "保存设备"; enabled:App.session.permissions.manage_assets;width: 104; height: 40; variant: "primary"; onClicked: root.create() }
                }
            }
        }
    }
    ConfirmDialog {id:removeDevice;z:210;onAccepted:{var r=App.assets.deleteAsset(root.selected.id);if(r.ok)root.refresh();else root.message=r.error}}
}
