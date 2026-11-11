import QtQuick
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id:root;objectName:"taskListPage";color:Colors.background
    property string filter:""
    property string keyword:""
    property string message:""
    property var allTasks:[]
    property var filteredTasks:[]
    property var selected:({})
    property int selectedIndex:-1
    function refresh() {allTasks=App.inspection.tasks();applyFilter()}
    function applyFilter() {
        var rows=[],index=-1,kw=keyword.trim().toLowerCase()
        for(var i=0;i<allTasks.length;i++) {
            var row=allTasks[i]
            if(filter&&row.status!==filter)continue
            if(kw&&(row.code+" "+row.name+" "+row.assetName).toLowerCase().indexOf(kw)<0)continue
            if(row.id===selected.id)index=rows.length
            rows.push(row)
        }
        filteredTasks=rows;selectedIndex=index>=0?index:rows.length?0:-1;selected=selectedIndex>=0?rows[selectedIndex]:({})
    }
    function openSelected() {
        if(!selected.id)return
        var result=App.inspection.startTask(selected.id)
        if(!result.ok){message=result.error;return}
        App.nav.push("inspection",{taskId:selected.id,taskName:selected.name,taskCode:selected.code})
    }
    function viewSelected() {if(selected.id)App.nav.push("inspection",{taskId:selected.id,taskName:selected.name,taskCode:selected.code})}
    onVisibleChanged:if(visible)refresh()
    Component.onCompleted:refresh()
    Connections {target:App.inspection;function onTasksChanged(){if(root.visible)root.refresh()}}
    ColumnLayout {
        anchors.fill:parent;anchors.margins:Dimensions.pagePadding;spacing:12
        RowLayout {Layout.fillWidth:true;PageHeading {Layout.fillWidth:true;title:"巡检任务"}TouchButton{text:"计划与交接";height:34;autoWidth:true;onClicked:App.nav.push("product",{section:"tasks"})}Text{text:App.workspace.reminders>0?"待检 / 超期 "+App.workspace.reminders:"";font.pixelSize:Typography.body;color:Colors.danger}}
        Flow {
            Layout.fillWidth:true;spacing:8
            Repeater {
                model:[{label:"全部",value:""},{label:"待开始",value:"pending"},{label:"进行中",value:"in_progress"},{label:"已完成",value:"completed"}]
                TouchButton{text:modelData.label;autoWidth:true;height:36;variant:root.filter===modelData.value?"tonal":"ghost";onClicked:{root.filter=modelData.value;root.applyFilter()}}
            }
            TouchButton {objectName:"startInspectionButton";text:root.selected.status==="in_progress"?"继续巡检":"开始巡检";icon:"play";autoWidth:true;height:36;variant:"primary";enabled:!!root.selected.id&&root.selected.status!=="completed";onClicked:root.openSelected()}
            TouchButton {text:"查看";autoWidth:true;height:36;variant:"ghost";visible:root.selected.status==="completed";onClicked:root.viewSelected()}
        }
        Flow {
            Layout.fillWidth:true;spacing:8
            FieldInput {width:Math.min(280,parent.width);height:36;placeholder:"搜索任务 / 设备";onTextEdited:{root.keyword=value;root.applyFilter()}}
            TouchButton {objectName:"newTaskButton";text:"新增任务";enabled:App.session.permissions.assign;icon:"plus";autoWidth:true;height:36;variant:"primary";onClicked:App.nav.push("product",{section:"tasks"})}
            TouchButton {objectName:"editTaskButton";text:"编辑";autoWidth:true;height:36;enabled:!!root.selected.id&&root.selected.status!=="completed"&&App.session.permissions.assign;onClicked:App.nav.push("product",{section:"tasks"})}
            TouchButton {objectName:"deleteTaskButton";text:"删除";autoWidth:true;height:36;variant:"ghost";enabled:!!root.selected.id&&App.session.permissions.assign;onClicked:removeDialog.openDialog("删除任务","将未提交任务及草稿移入回收站？已提交档案和关联缺陷会阻止此操作。","删除","取消")}
        }
        Text {visible:root.message.length>0;Layout.fillWidth:true;text:root.message;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.danger}
        Rectangle {
            Layout.fillWidth:true;Layout.fillHeight:true;color:Colors.panel;radius:12;border.color:Colors.border
            DataTable {
                anchors.fill:parent;items:root.filteredTasks;selectedIndex:root.selectedIndex
                columns:[{key:"code",title:"任务编号",width:160},{key:"name",title:"任务名称",width:200},{key:"assetName",title:"设备",width:130},{key:"progressText",title:"进度",width:80},{key:"statusText",title:"状态",width:90},{key:"plannedAt",title:"计划时间",width:150,format:"datetime"}]
                onRowClicked:function(index,item){root.selectedIndex=index;root.selected=item;root.message=""}
                onRowDoubleClicked:function(index,item){root.selectedIndex=index;root.selected=item;if(item.status==="completed")root.viewSelected()}
            }
        }
    }
    EntityEditor {id:taskEditor;objectName:"taskEditor";kind:"task";onSaved:function(id){root.selected={id:id};root.refresh();root.message=""}}
    ConfirmDialog {id:removeDialog;z:210;onAccepted:{var r=App.inspection.deleteTask(root.selected.id);if(!r.ok)root.message=r.error;else{root.selected=({});root.refresh()}}}
}
