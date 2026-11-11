import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
Window {
    id:win;objectName:"newInspectionWindow";title:"新增巡检";visible:false;color:Colors.background
    property var ownerPage
    property var device:({})
    property var tasks:[]
    property var people:[]
    property string taskId:""
    property bool creating:false
    property string message:""
    property string taskName:""
    property string assignee:""
    property string planned:""
    property bool submitting:false
    readonly property bool canAssign:App.session.permissions.assign===true
    readonly property var task:tasks.find(function(t){return t.id===win.taskId})||({})
    readonly property int fixedWidth:Math.min(660,Screen.desktopAvailableWidth-32)
    readonly property int fixedHeight:Math.min(600,Screen.desktopAvailableHeight-40)
    width:fixedWidth;height:fixedHeight;minimumWidth:fixedWidth;maximumWidth:fixedWidth;minimumHeight:fixedHeight;maximumHeight:fixedHeight
    modality:Qt.WindowModal;transientParent:ownerPage?ownerPage.Window.window:null
    flags:Qt.Dialog|Qt.WindowTitleHint|Qt.WindowSystemMenuHint|Qt.WindowCloseButtonHint
    function reload(){tasks=App.inspection.inspectionCandidates(device.id||"");if(!tasks.some(function(t){return t.id===taskId})){var next=tasks.filter(function(t){return t.can_start})[0]||tasks[0];taskId=next?next.id:""}}
    function openFor(asset){if(!asset.id){ownerPage.message="请先选择设备";return}device=asset;message="";submitting=false;taskId="";reload();creating=canAssign&&tasks.length===0;people=canAssign?App.workspace.options("users"):[];taskName=device.name+"现场巡检";assignee=App.session.userId;planned=Qt.formatDateTime(new Date(Date.now()+300000),"yyyy-MM-dd HH:mm");show();raise();requestActivate()}
    function createTask(){
        if(submitting||!creating||!canAssign)return;message="";var date=new Date(planned.replace(" ","T"));if(!taskName.trim()||!isFinite(date.getTime())){message="请填写任务名称和有效计划时间";return}
        submitting=true;var result=App.workspace.save("tasks",{name:taskName.trim(),asset_id:device.id,assignee_id:assignee,planned_at:date.toISOString()});submitting=false
        if(!result.ok){message=result.error;return}taskId=result.id;reload();creating=false;message="任务已派发，请核对后开始巡检。";ownerPage.refresh()
    }
    function begin(){
        if(submitting||creating||!task.can_start)return;submitting=true;var result=App.inspection.beginInspection(device.id,task.id);submitting=false
        if(!result.ok){message=result.error;reload();return}close();App.nav.push("inspection",{taskId:result.task_id,taskName:result.task_name,taskCode:result.task_code})
    }
    Connections{target:App.session;function onSessionChanged(){if(!App.session.loggedIn){win.close();win.tasks=[];win.people=[];win.device={};win.taskId=""}}}
    Connections{target:App.inspection;function onTasksChanged(){if(win.visible&&!win.submitting)win.reload()}}
    onClosing:if(App.input.targetWindow===win)App.input.cancel()
    ColumnLayout {anchors.fill:parent;anchors.margins:22;spacing:14
        Text{text:"新增巡检";font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}
        Text{Layout.fillWidth:true;text:win.device.name+" · "+win.device.code;elide:Text.ElideRight;font.pixelSize:Typography.body;color:Colors.textSecondary}
        RowLayout{Layout.fillWidth:true
            TouchButton{objectName:"chooseInspectionTask";text:"选择已有任务";autoWidth:true;height:34;variant:!win.creating?"tonal":"ghost";onClicked:{win.creating=false;win.message="";win.reload()}}
            TouchButton{objectName:"createInspectionTask";visible:win.canAssign;text:"新建并派发任务";autoWidth:true;height:34;variant:win.creating?"tonal":"ghost";onClicked:{win.creating=true;win.message=""}}
        }
        ScrollView{id:body;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;contentWidth:availableWidth
            ColumnLayout{width:body.availableWidth;spacing:13
                ColumnLayout{visible:!win.creating;Layout.fillWidth:true;spacing:14
                    AppComboBox{objectName:"newInspectionTaskChoice";Layout.fillWidth:true;height:38;visible:win.tasks.length>0;model:win.tasks;textRole:"display";currentIndex:win.tasks.findIndex(function(t){return t.id===win.taskId});displayText:win.task.code?(win.task.code+" · "+win.task.name):"请选择任务";onActivated:{win.taskId=model[currentIndex].id;win.message=""}}
                    Text{objectName:"inspectionNoTaskHint";visible:win.tasks.length===0;Layout.fillWidth:true;text:win.canAssign?"当前设备暂无待办任务。请先新建并派发任务，再开始巡检。":"当前设备暂无分派给你的待办任务，请联系任务管理人员派发。";wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
                    Rectangle{visible:!!win.task.id;Layout.fillWidth:true;implicitHeight:taskInfo.implicitHeight+28;color:Colors.panel;border.color:Colors.border;radius:12
                        ColumnLayout{id:taskInfo;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:14;spacing:10
                            Text{Layout.fillWidth:true;text:"负责人："+(win.task.assignee||"—");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
                            Text{Layout.fillWidth:true;text:"计划时间："+(win.task.planned_at?Qt.formatDateTime(new Date(win.task.planned_at),"yyyy-MM-dd HH:mm"):"—");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                            Text{Layout.fillWidth:true;text:win.task.reason||(win.task.record_id?"已有巡检草稿，将继续填写原记录。":"开始后将创建本次巡检草稿，并进入检查项目。");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:win.task.reason?Colors.danger:Colors.textSecondary}
                        }
                    }
                }
                ColumnLayout{visible:win.creating;Layout.fillWidth:true;spacing:9
                    Text{text:"任务名称";font.pixelSize:Typography.body;color:Colors.textSecondary}
                    FieldInput{objectName:"newInspectionName";Layout.fillWidth:true;height:38;text:win.taskName;onTextEdited:function(value){win.taskName=value}}
                    Text{text:"负责人";font.pixelSize:Typography.body;color:Colors.textSecondary}
                    AppComboBox{objectName:"newInspectionAssignee";Layout.fillWidth:true;height:38;model:win.people;textRole:"name";currentIndex:win.people.findIndex(function(u){return u.id===win.assignee});onActivated:win.assignee=model[currentIndex].id}
                    Text{text:"计划时间";font.pixelSize:Typography.body;color:Colors.textSecondary}
                    FieldInput{objectName:"newInspectionPlanned";Layout.fillWidth:true;height:38;text:win.planned;placeholder:"yyyy-MM-dd HH:mm";onTextEdited:function(value){win.planned=value}}
                }
            }
        }
        Text{objectName:"newInspectionMessage";Layout.fillWidth:true;visible:win.message.length>0;text:win.message;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
        RowLayout{Layout.fillWidth:true
            TouchButton{text:"查看任务列表";autoWidth:true;height:36;variant:"ghost";onClicked:{win.close();win.ownerPage.setView("tasks")}}
            Item{Layout.fillWidth:true}
            TouchButton{text:"取消";autoWidth:true;height:36;variant:"ghost";onClicked:win.close()}
            TouchButton{objectName:"confirmNewInspection";text:win.submitting?"处理中…":win.creating?"创建任务":win.task.record_id?"继续巡检":"开始巡检";autoWidth:true;height:36;variant:"primary";enabled:!win.submitting&&(win.creating?win.canAssign:!!win.task.can_start);onClicked:if(win.creating)win.createTask();else win.begin()}
        }
    }
}
