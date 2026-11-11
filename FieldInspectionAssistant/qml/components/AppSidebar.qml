import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../theme"

Rectangle {
    id: root
    color: Colors.sidebar; radius: 14
    property bool collapsed: Dimensions.compact
    property var navItems: [
        {page:"dashboard",label:"工作台",icon:"grid"}, {page:"operations",label:"巡检管理",icon:"tasks"},
        {page:"camera",label:"现场拍照",icon:"camera"}, {page:"analysis",label:"数据分析",icon:"chart"},
        {page:"ai",label:"智能助手",icon:"sparkles"},
        {page:"settings",label:"系统设置",icon:"settings"}
    ]
    ColumnLayout {
        anchors.fill: parent; anchors.margins: root.collapsed ? 5 : 8; spacing: 12
        Item {
            Layout.fillWidth: true; Layout.preferredHeight: root.collapsed ? 46 : 54
            Row {
                anchors.centerIn: parent; spacing: 7
                Rectangle {
                    width: 28; height: 32; radius: 9; color: Colors.normal
                    AppIcon { anchors.centerIn: parent; name: "sparkles"; width: 20; height: 20; color: "white" }
                }
                Column {
                    visible: !root.collapsed; anchors.verticalCenter: parent.verticalCenter; spacing: 2
                    Text { text:"巡检助手"; color:"#F2F3FF"; font.pixelSize:14; font.weight:Font.DemiBold }
                }
            }
        }
        ScrollView {
            id: navScroll; Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: availableWidth; clip: true; ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Column {
                width: navScroll.availableWidth; spacing: 5
                Repeater {
                    model: root.navItems
                    Rectangle {
                        id: navButton
                        readonly property bool selected: App.nav.page === modelData.page||(modelData.page==="operations"&&App.nav.page==="inspection")
                        width: parent.width; height: 40; radius: 8
                        color: selected ? "#363352" : navMouse.containsMouse ? "#222D43" : "transparent"
                        Behavior on color { ColorAnimation { duration: 120 } }
                        Rectangle { width: 3; height: 16; radius: 1.5; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; visible: navButton.selected; color:"#B8AFFF" }
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            x: root.collapsed ? (parent.width-width)/2 : 8; spacing: 7
                            AppIcon { name:modelData.icon; width:18; height:18; anchors.verticalCenter: parent.verticalCenter; color:navButton.selected ? "#D2C9FF" : "#8996B0" }
                            Text { visible:!root.collapsed; text:modelData.label; font.pixelSize:14; font.weight:navButton.selected ? Font.DemiBold : Font.Medium; color:navButton.selected ? "#ECE8FF" : "#B7C1D4" }
                        }
                        ToolTip.visible: navMouse.containsMouse && root.collapsed; ToolTip.text: modelData.label
                        Accessible.role: Accessible.Button; Accessible.name: modelData.label
                        Accessible.onPressAction: App.nav.push(modelData.page)
                        MouseArea { id:navMouse; anchors.fill:parent; hoverEnabled:true; cursorShape:Qt.PointingHandCursor; onClicked:App.nav.push(modelData.page) }
                    }
                }
            }
        }
        Rectangle { Layout.fillWidth:true; height:1; color:"#29334A" }
        Item {
            Layout.fillWidth:true; Layout.preferredHeight:44
            Row {
                anchors.centerIn:parent; spacing:7
                Rectangle { width:24; height:28; radius:8; color:"#2C3650"; Text { anchors.centerIn:parent; text:"巡"; color:"#C9D1E9"; font.pixelSize:14 } }
                Column {
                    visible:!root.collapsed; spacing:2
                    Text { text:App.session.userName; width:root.width-55;elide:Text.ElideRight;font.pixelSize:14; color:"#CED5E5" }
                    Row { spacing:5; Rectangle { width:5; height:5; radius:3; color:App.ai.available ? "#73CFAF" : "#8B94A8"; anchors.verticalCenter:parent.verticalCenter } Text { text:App.ai.available ? "在线" : "离线"; color:"#8D9AB2"; font.pixelSize:14 } }
                }
            }
        }
    }
}
