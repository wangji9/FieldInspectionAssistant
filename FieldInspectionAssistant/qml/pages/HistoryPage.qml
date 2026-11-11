import QtQuick
import QtQuick.Dialogs
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// 巡检档案：记录筛选、检索、现场照片追溯、CSV 导出
Rectangle {
    id: root
    objectName: "historyPage"
    color: Colors.background

    property string filter: ""
    property string keyword: ""
    property var records: []
    property int selectedIndex: -1
    property var selected: ({})
    property var attachmentsList: []
    property string message: ""
    property string previewPath: ""

    function refresh() { root.records = App.archiveRecords(root.filter) }
    function displayed() {
        var kw = root.keyword.trim().toLowerCase()
        if (!kw) return root.records
        var out = []
        for (var i = 0; i < root.records.length; i++) {
            var r = root.records[i]
            var hay = (r.assetCode + " " + r.assetName + " " + r.taskCode + " " + r.templateName).toLowerCase()
            if (hay.indexOf(kw) >= 0) out.push(r)
        }
        return out
    }
    function selectRow(idx, item) {
        root.selectedIndex = idx
        root.selected = item
        root.attachmentsList = item.id ? App.evidence.attachments(item.id) : []
        root.previewPath = ""
    }
    function exportCsv() {csvDialog.selectedFile=App.exports.defaultFile("archive","csv");csvDialog.open()}
    FileDialog {id:csvDialog;title:"保存巡检档案";fileMode:FileDialog.SaveFile;nameFilters:["CSV (*.csv)"];defaultSuffix:"csv";onAccepted:{var path=decodeURIComponent(selectedFile.toString().substring(7));var r=App.evidence.exportCsv(path);root.message=r.ok?"已保存："+path:r.error}}

    onVisibleChanged: if (visible) { root.refresh(); root.selectedIndex = -1; root.selected = ({}) }
    Component.onCompleted: refresh()

    Column {
        anchors.fill: parent
        anchors.margins: Dimensions.pagePadding
        spacing: Dimensions.gap

        RowLayout {width:parent.width;PageHeading {Layout.fillWidth:true;title:"巡检档案"}TouchButton{text:"正式报告";height:34;autoWidth:true;onClicked:App.nav.push("product",{section:"reports",record_id:root.selected.id||""})}TouchButton{text:"证据档案";height:34;autoWidth:true;onClicked:App.nav.push("product",{section:"evidence",record_id:root.selected.id||""})}}

        // 顶部：筛选 + 检索 + 导出
        Flow {
            width: parent.width
            height: implicitHeight
            spacing: Dimensions.gap

            TouchButton {
                text: "全部"
                variant: root.filter === "" ? "tonal" : "ghost"
                autoWidth: true
                height: 40
                onClicked: { root.filter = ""; root.refresh() }
            }

            TouchButton {
                text: "异常"
                variant: root.filter === "abnormal" ? "tonal" : "ghost"
                autoWidth: true
                height: 40
                onClicked: { root.filter = "abnormal"; root.refresh() }
            }

            TouchButton {
                text: "草稿"
                variant: root.filter === "draft" ? "tonal" : "ghost"
                autoWidth: true
                height: 40
                onClicked: { root.filter = "draft"; root.refresh() }
            }

            TouchButton {
                text: "已提交"
                variant: root.filter === "submitted" ? "tonal" : "ghost"
                autoWidth: true
                height: 40
                onClicked: { root.filter = "submitted"; root.refresh() }
            }

            FieldInput {
                width: Math.min(280, parent.width)
                placeholder: "按设备 / 任务 / 模板检索…"
                onTextEdited: root.keyword = value
            }

            TouchButton {
                text: "导出 CSV"
                variant: "primary"
                autoWidth: true
                height: 40
                onClicked: root.exportCsv()
            }
            TouchButton {objectName:"newArchiveButton";text:"新增记录";autoWidth:true;height:40;variant:"primary";onClicked:archiveEditor.open({})}
            TouchButton {text:"编辑";autoWidth:true;height:40;enabled:!!root.selected.id&&!!root.selected.canEdit;onClicked:archiveEditor.open(root.selected)}
            TouchButton {text:"删除";autoWidth:true;height:40;variant:"ghost";enabled:!!root.selected.id&&App.session.permissions.assign&&root.selected.status==="draft";onClicked:removeArchive.openDialog("删除档案","将未提交草稿移入回收站？正式档案和缺陷历史会保留。","删除","取消")}

        }

        Text {
            visible: root.message.length > 0
            text: root.message
            font.pixelSize: Typography.small
            color: Colors.normal
            elide: Text.ElideMiddle
            width: parent.width
        }

        ResponsiveRow {
            id: archivePanes
            width: parent.width
            height: parent.height - y
            breakpoint: 780; minimumPaneHeight: 380

            // 左侧：记录表
            Rectangle {
                id: archiveListPanel
                width: parent.stacked ? parent.width : Math.floor(parent.width * 0.60)
                height: parent.stacked ? 320 : parent.height
                color: Colors.panel
                radius: Dimensions.radius
                border.color: Colors.border
                border.width: 1

                DataTable {
                    anchors.fill: parent
                    anchors.margins: 10
                    columns: [
                        { key: "time", title: "时间", width: 130, format: "datetime" },
                        { key: "assetCode", title: "设备编号", width: 110 },
                        { key: "assetName", title: "名称", width: 110 },
                        { key: "result", title: "结果", width: 70 },
                        { key: "statusText", title: "状态", width: 70 }
                    ]
                    items: root.displayed()
                    selectedIndex: root.selectedIndex
                    onRowClicked: { root.selectRow(index, item); archivePanes.showPane(archiveDetailPanel) }
                }
            }

            // 右侧：记录详情 + 现场照片
            Rectangle {
                id: archiveDetailPanel
                width: parent.stacked ? parent.width : parent.width - archiveListPanel.width - Dimensions.spacing
                height: parent.stacked ? 380 : parent.height
                color: Colors.panel
                radius: Dimensions.radius
                border.color: Colors.border
                border.width: 1

                ScrollView {
                    id: historyDetails
                    contentWidth: availableWidth
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true

                    Column {
                        width: historyDetails.availableWidth
                        spacing: 14

                        Row {
                            width: parent.width
                            spacing: 8

                            Text {
                                width: Math.max(100, parent.width - 150)
                                text: (root.selected.assetCode || "未选择记录") + (root.selected.assetName ? " · " + root.selected.assetName : "")
                                font.pixelSize: Typography.h2
                                font.weight: Font.Bold
                                color: Colors.text
                                elide: Text.ElideRight
                            }

                            StatusBadge {
                                status: root.selected.status || ""
                                text: root.selected.statusText || ""
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            StatusBadge {
                                visible: root.selected.result === "异常" || root.selected.result === "缺陷"
                                status: "abnormal"
                                text: "异常"
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        Text {
                            width: parent.width
                            wrapMode: Text.WordWrap
                            text: root.selected.time ? ("时间：" + root.selected.time + "\n模板：" + (root.selected.templateName || "-") + "\n任务：" + (root.selected.taskCode || "-")) : ""
                            font.pixelSize: Typography.small
                            color: Colors.textSecondary
                        }

                        Rectangle { width: parent.width; height: 1; color: Colors.border }

                        Text {
                            text: "现场照片（" + root.attachmentsList.length + " 张）"
                            font.pixelSize: Typography.h2
                            font.weight: Font.Bold
                            color: Colors.text
                        }

                        Flow {
                            width: parent.width
                            spacing: 10

                            Repeater {
                                model: root.attachmentsList
                                Rectangle {
                                    width: parent.width
                                    height: 210
                                    color: Colors.panelAlt
                                    radius: 6
                                    border.color: Colors.border
                                    border.width: 1

                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: 4
                                        fillMode: Image.PreserveAspectFit
                                        source: modelData.path ? "file://" + modelData.path : ""
                                        sourceSize.width: 400
                                    }

                                    Text {
                                        anchors.bottom: parent.bottom
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.margins: 4
                                        padding: 2
                                        text: modelData.createdAt || ""
                                        font.pixelSize: Typography.body
                                        color: Colors.text
                                        Rectangle { anchors.fill: parent; z: -1; color: "#C0FFFFFF" }
                                        horizontalAlignment: Text.AlignHCenter
                                        elide: Text.ElideMiddle
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: root.previewPath = modelData.path
                                    }
                                }
                            }
                        }

                        EmptyState {
                            visible: root.attachmentsList.length === 0
                            title: root.selected.id ? "暂无现场照片" : "请选择记录"
                            height: 160
                            width: parent.width
                        }
                    }
                }
            }
        }
    }

    // 原图预览
    Rectangle {
        visible: root.previewPath.length > 0
        anchors.fill: parent
        color: "#D0000000"
        z: 50
        MouseArea { anchors.fill: parent; onClicked: root.previewPath = "" }

        Rectangle {
            width: Math.min(parent.width - 80, 1200)
            height: Math.min(parent.height - 80, 900)
            color: Colors.panel
            radius: Dimensions.radius
            border.color: Colors.border
            border.width: 1
            anchors.centerIn: parent

            Image {
                anchors.fill: parent
                anchors.margins: 8
                fillMode: Image.PreserveAspectFit
                source: root.previewPath ? "file://" + root.previewPath : ""
                sourceSize.width: 1600
            }

            TouchButton {
                text: "关闭"
                variant: "ghost"
                autoWidth: true
                height: 38
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 8
                onClicked: root.previewPath = ""
            }
        }
    }
    EntityEditor {id:archiveEditor;objectName:"archiveEditor";kind:"record";onSaved:function(id){root.refresh();var rows=root.displayed();for(var i=0;i<rows.length;i++)if(rows[i].id===id)root.selectRow(i,rows[i])}}
    ConfirmDialog {id:removeArchive;z:210;onAccepted:{var r=App.inspection.deleteArchive(root.selected.id);if(r.ok){root.selected=({});root.selectedIndex=-1;root.attachmentsList=[];root.refresh()}else root.message=r.error}}
}
