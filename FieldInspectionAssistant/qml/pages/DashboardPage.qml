import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id: root
    objectName: "dashboardPage"
    color: Colors.background
    property var stats: ({})
    property var tasks: []

    function refresh() {
        stats = App.dashboardStats()
        tasks = App.inspection.tasks()
    }
    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: refresh()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Dimensions.pagePadding
        spacing: Dimensions.gap

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "巡检工作台"
                font.pixelSize: Typography.title
                color: Colors.text
            }
            Item { Layout.fillWidth: true }
            TouchButton { text: "查看任务"; autoWidth: true; onClicked: App.nav.push("tasks") }
            TouchButton { text: "热像分析"; autoWidth: true; onClicked: App.nav.push("thermal") }
        }

        GroupBox {
            title: "巡检任务"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle {
                anchors.fill: parent
                color: Colors.panel
                border.color: Colors.border
                DataTable {
                    anchors.fill: parent
                    anchors.margins: 1
                    items: root.tasks
                    emptyText: "暂无巡检任务"
                    columns: [
                        {key: "name", title: "任务名称", width: 0},
                        {key: "progressText", title: "巡检进度", width: 116},
                        {key: "statusText", title: "状态", width: 112},
                        {key: "plannedAt", title: "计划时间", width: 140, format: "datetime"}
                    ]
                    onRowClicked: function(index, item) { selectedIndex = index }
                    onRowDoubleClicked: App.nav.push("tasks")
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.width < 680 ? 190 : 105
            Layout.maximumHeight: Layout.preferredHeight
            columns: root.width < 680 ? 1 : 2
            columnSpacing: 8
            rowSpacing: 5

            GroupBox {
                title: "巡检统计"
                Layout.fillWidth: true
                Layout.fillHeight: true
                RowLayout {
                    anchors.fill: parent
                    spacing: 12
                    Repeater {
                        model: [
                            {label: "巡检任务", value: root.tasks.length},
                            {label: "今日记录", value: root.stats.recordCount || 0},
                            {label: "待处理缺陷", value: root.stats.pendingDefects || 0}
                        ]
                        Column {
                            Layout.fillWidth: true
                            spacing: 3
                            Text { text: modelData.label; font.pixelSize: Typography.small; color: Colors.textSecondary }
                            Text { text: modelData.value; font.pixelSize: Typography.value; color: Colors.text }
                        }
                    }
                }
            }
            GroupBox {
                title: "常用操作"
                Layout.fillWidth: true
                Layout.fillHeight: true
                GridLayout {
                    anchors.fill: parent
                    columns: 2
                    TouchButton { Layout.fillWidth: true; text: "现场拍照"; onClicked: App.nav.push("camera") }
                    TouchButton { Layout.fillWidth: true; text: "智能助手"; onClicked: App.nav.push("ai") }
                    TouchButton { Layout.fillWidth: true; text: "设备资料"; onClicked: App.nav.push("devices") }
                    TouchButton { Layout.fillWidth: true; text: "巡检档案"; onClicked: App.nav.push("history") }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 23
            color: Colors.panelAlt
            border.color: Colors.border
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: App.sync.online ? "同步已连接" : "离线工作中"
                color: Colors.textSecondary
                font.pixelSize: Typography.small
            }
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: Qt.formatDate(new Date(), "yyyy-MM-dd")
                color: Colors.textSecondary
                font.pixelSize: Typography.small
            }
        }
    }
}
