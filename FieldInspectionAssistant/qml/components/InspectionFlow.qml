import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: flow
    property var steps: []
    property bool defectMode: false
    property var defect: ({})
    property string heading: "巡检执行流程"
    readonly property var displayedSteps: {
        if (!defectMode) return steps
        const names = ["登记", "分派", "整改", "复核", "归档"]
        const current = ({unassigned:1, pending:2, in_progress:2, review:3, closed:5})[defect.status] ?? 0
        return names.map((name, index) => ({id:String(index), name:name,
            state:index < current ? "completed" : index === current && defect.id ? "active" : "pending",
            detail:index === 2 ? (defect.status === "pending" ? "待开始整改" : defect.assignee_name || "") : ""}))
    }
    signal stageClicked(string stage)
    implicitHeight: 105
    color: Colors.panel
    border.color: Colors.border
    visible: displayedSteps.length > 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 4
        Label {
            Layout.fillWidth: true
            text: flow.heading
            elide: Text.ElideRight
            font.bold: true
            font.pixelSize: Typography.small
        }
        Flickable {
            id: viewport
            Layout.fillWidth: true; Layout.fillHeight: true
            contentWidth: Math.max(width, flow.displayedSteps.length * 110)
            contentHeight: height
            clip: true; boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            Row {
                width: viewport.contentWidth
                height: viewport.height - (viewport.contentWidth > viewport.width ? 7 : 0)
                Repeater {
                    model: flow.displayedSteps
                    RowLayout {
                        id: stage
                        required property var modelData
                        required property int index
                        readonly property bool complete: modelData.state === "completed"
                        readonly property bool active: modelData.state === "active"
                        readonly property color accent: complete ? "#30724d" : active ? "#235f9a" : "#707d88"
                        width: viewport.contentWidth / flow.displayedSteps.length; height: parent.height
                        spacing: 0
                        Button {
                            Layout.fillWidth: true; Layout.fillHeight: true
                            padding: 4
                            onClicked: flow.stageClicked(stage.modelData.id)
                            contentItem: Column {
                                spacing: 3
                                Label {
                                    width: parent.width; elide: Text.ElideRight
                                    text: String(stage.index + 1).padStart(2, "0") + "  " + stage.modelData.name
                                    font.bold: true; font.pixelSize: Typography.small
                                }
                                Label {
                                    width: parent.width; elide: Text.ElideRight
                                    text: stage.complete ? "已完成" : stage.active ? "进行中" : "未到达"
                                    color: stage.accent; font.pixelSize: Typography.small
                                }
                                Label {
                                    visible: !!stage.modelData.detail
                                    width: parent.width; elide: Text.ElideRight
                                    text: stage.modelData.detail || ""
                                    color: Colors.textSecondary; font.pixelSize: Typography.small
                                }
                            }
                            background: Rectangle {
                                color: stage.active ? "#e8f2fa" : stage.complete ? "#edf5ef" : "#f5f6f8"
                                border.color: stage.accent
                                Rectangle { anchors.top: parent.top; width: parent.width; height: 3; color: stage.accent }
                            }
                        }
                        Item {
                            visible: stage.index < flow.displayedSteps.length - 1
                            Layout.preferredWidth: 15; Layout.fillHeight: true
                            Rectangle { anchors.verticalCenter: parent.verticalCenter; width: 12; height: 1; color: stage.accent }
                            Rectangle {
                                x: 8; anchors.verticalCenter: parent.verticalCenter
                                width: 5; height: 5; rotation: 45; color: stage.accent
                            }
                        }
                    }
                }
            }
        }
    }
}
