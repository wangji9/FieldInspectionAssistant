import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Flickable {
    id: pane
    required property var projectPage
    clip: true
    contentWidth: width
    contentHeight: Math.max(height, layout.implicitHeight + 10)
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

    ColumnLayout {
        id: layout
        width: Math.max(0, pane.width - 12)
        spacing: 7

        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: 5
            TouchButton {
                objectName: "diagnosisStart"
                text: pane.projectPage.inference.busy ? "正在分析…" : "运行工程诊断"
                autoWidth: true; height: 29; variant: "primary"
                enabled: !!pane.projectPage.activeEvent.id && pane.projectPage.inference.ready && !pane.projectPage.inference.busy
                onClicked: pane.projectPage.inference.analyze(pane.projectPage.eventId)
            }
            TouchButton {
                text: "查看基准资料"; autoWidth: true; height: 29
                enabled: !!pane.projectPage.activeEvent.anchor_id
                onClicked: pane.projectPage.openEvidence(pane.projectPage.activeEvent.anchor_id)
            }
            TouchButton {
                text: "打开事件"; autoWidth: true; height: 29
                enabled: pane.projectPage.canOpenActiveEvent
                onClicked: App.nav.push("events", {event_id:pane.projectPage.eventId})
            }
            TouchButton {
                text: "检查推理"; autoWidth: true; height: 29
                enabled: !pane.projectPage.inference.busy
                onClicked: pane.projectPage.inference.refresh()
            }
            TouchButton {
                visible: !pane.projectPage.activeEvent.id
                text: "打开工程资料"; autoWidth: true; height: 29
                onClicked: pane.projectPage.showEvidence("")
            }
        }

        Label {
            Layout.fillWidth: true
            text: pane.projectPage.inference.status
            wrapMode: Text.WrapAnywhere
            color: Colors.textSecondary
        }

        DataTable {
            objectName: "diagnosisInputTable"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(130, Math.min(250, pane.height * .38))
            spreadsheet: true
            items: pane.projectPage.visibleEvents
            emptyText: "暂无诊断对象，请先从工程资料生成融合事件"
            selectedIndex: items.findIndex(row => row.id === pane.projectPage.eventId)
            columns: [{key:"title", title:"诊断对象", width:0}, {key:"device", title:"设备", width:95},
                      {key:"point", title:"测点", width:80}, {key:"classificationLabel", title:"判定", width:58}]
            onRowClicked: function(index, item) { pane.projectPage.eventId = item.id }
        }

        Label {
            Layout.fillWidth: true
            text: pane.projectPage.hasDiagnosis ? "诊断结果" : "诊断依据"
            font.bold: true
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(145, pane.height - 255)
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            TextArea {
                objectName: "diagnosisResultText"
                padding: 10
                text: pane.projectPage.diagnosisText
                font.pixelSize: Typography.body
                textFormat: TextEdit.PlainText
                readOnly: true; selectByMouse: true
                wrapMode: TextEdit.Wrap
            }
        }
    }
}
