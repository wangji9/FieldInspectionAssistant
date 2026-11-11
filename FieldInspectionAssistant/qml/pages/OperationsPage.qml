import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"
Rectangle {
    id:root;objectName:"operationsPage";color:Colors.background
    property var assets:[]
    property var asset:({})
    property var rows:[]
    property var selected:({})
    property var timeline:[]
    property string view:"records"
    property var summary:({})
    property alias deviceEditor:deviceForm
    property alias recordDetail:recordWindow
    property string deviceSearch:""
    property string keyword:""
    property string statusFilter:""
    property string message:""
    property int page:0
    property int total:0
    property var columnChoices:({})
    property alias newInspection:inspectionEntry
    property alias manager:management
    AnalysisReviewDialog { id: operationReview }
    function status(value){return ({pending:"待开始",overdue:"已超期",in_progress:view==="defects"?"处理中":"进行中",completed:"已完成",unassigned:"未分派",review:"待复核",closed:"已关闭",draft:"草稿",submitted:"已提交"})[value]||value}
    function columns(){
        var sets={tasks:[{key:"task_code",title:"任务编号",width:150},{key:"name",title:"任务",width:210},{key:"assignee_name",title:"负责人",width:110},{key:"status_text",title:"状态",width:90},{key:"planned_at",title:"计划时间",width:165,format:"datetime"}],records:[{key:"task_code",title:"所属任务",width:155},{key:"created_at",title:"巡检时间",width:165,format:"datetime"},{key:"result",title:"结果",width:120},{key:"status_text",title:"状态",width:90}],defects:[{key:"defect_code",title:"缺陷编号",width:145},{key:"title",title:"缺陷",width:210},{key:"task_code",title:"发现任务",width:155},{key:"status_text",title:"状态",width:90},{key:"assignee_name",title:"处理人",width:110}]};return sets[view]||[]
    }
    function visibleColumns(){var hidden=columnChoices[view]||[];return columns().filter(function(c){return hidden.indexOf(c.key)<0})}
    function reloadAssets(selectId){if(!App.session.loggedIn)return;assets=App.assets.assets(deviceSearch);if(!App.session.permissions.assign){var work=App.assets.workAssets().map(function(a){return a.id});assets.sort(function(a,b){return (work.indexOf(a.id)<0?1:0)-(work.indexOf(b.id)<0?1:0)})}var id=selectId||asset.id;var next=assets.filter(function(a){return a.id===id})[0]||{};if(next.id!==asset.id){page=0;selected={};statusFilter="";keyword=""}asset=next;refresh()}
    function refresh(){
        if(!App.session.loggedIn)return
        App.systemContext.selectAsset(asset.id||"")
        if(!asset.id){rows=[];selected={};total=0;timeline=[];return}
        summary=App.workflow.overview(asset.id)
        timeline=App.workspace.deviceTimeline(asset.id)
        if(view==="device"){rows=[];selected=asset;total=0;return}
        var r=App.workspace.query(view,{asset_id:asset.id,keyword:keyword,status:statusFilter},page,20)
        if(!r.ok){message=r.error;return}
        rows=r.rows.map(function(x){x.status_text=view==="records"?(summary.record_states||{})[x.id]||"待完成":root.status(x.status);return x});total=r.total
        if(page>0&&!rows.length){page--;refresh();return}
        selected=rows.filter(function(r){return r.id===root.selected.id})[0]||rows[0]||{}
    }
    function chooseAsset(row){asset=row;page=0;selected={};statusFilter="";keyword="";refresh()}
    function setView(value){view=value;page=0;selected={};keyword="";statusFilter="";refresh()}
    function setup(){var id=App.nav.param("asset_id"),next=App.nav.param("view")||"records";if(view!==next){page=0;selected={};keyword="";statusFilter=""}view=next;reloadAssets(id||"");var source=App.nav.param("source_id");if(source&&view!=="device"&&asset.id)Qt.callLater(function(){recordWindow.openFor(root.view,source)})}
    function openInspection(){inspectionEntry.openFor(root.asset)}
    function openManager(section,create){management.openFor(section||view,!!create)}
    function openTask(task){if(task.status==="completed"){App.nav.push("inspection",{taskId:task.id,taskName:task.name,taskCode:task.task_code||task.code});return}selected=task;begin()}
    function openDefect(d){setView("defects");selected=d;recordWindow.openFor("defects",d.id)}
    function manageDefect(action){if((recordWindow.detailData.selected_defect||{}).id)selected=recordWindow.detailData.selected_defect;management.openLifecycle(action)}
    function begin(){if(!selected.id)return;var r=App.inspection.beginInspection(asset.id,selected.id);if(!r.ok){message=r.error;return}App.nav.push("inspection",{taskId:r.task_id,taskName:r.task_name,taskCode:r.task_code})}
    function inspectRecord(){if(!selected.task_id){message="此记录为历史手工记录，请在设置中查看证据或正式报告";return}var tasks=App.inspection.tasks().filter(function(t){return t.id===root.selected.task_id});if(tasks.length)App.nav.push("inspection",{taskId:tasks[0].id,taskName:tasks[0].name,taskCode:tasks[0].code})}
    onVisibleChanged:if(visible&&App.session.loggedIn)setup()
    Connections {target:App.nav;function onPageChanged(){if(App.nav.page==="operations")root.setup()}}
    Connections {target:App.session;function onSessionChanged(){if(!App.session.loggedIn){management.close();deviceForm.close();recordWindow.close();inspectionEntry.close();root.asset={};root.assets=[];root.rows=[];root.selected={};root.timeline=[];root.message=""}else root.reloadAssets()}}
    Connections {target:App.assets;function onAssetsChanged(){if(root.visible)root.reloadAssets()}}
    Connections {target:App.workspace;function onDataChanged(){if(root.visible)root.reloadAssets()}}
    Connections {target:App.inspection;function onTasksChanged(){if(root.visible)root.refresh()}function onRecordChanged(id){if(root.visible)root.refresh()}}
    Connections {target:App.defects;function onDefectsChanged(){if(root.visible)root.refresh()}}
    WorkflowSettingsWindow {id:management;ownerPage:root}
    NewInspectionWindow{id:inspectionEntry;ownerPage:root}
    DeviceEditorWindow{id:deviceForm;ownerPage:root}
    InspectionDetailWindow{id:recordWindow;ownerPage:root}
    ConfirmDialog{id:removeConfirm;onAccepted:management.removeSelected()}
    RowLayout {
        anchors.fill:parent;anchors.margins:10;spacing:10
        Rectangle {
            Layout.preferredWidth:root.width<900?185:235;Layout.minimumWidth:140;Layout.fillHeight:true;color:Colors.panel;border.color:Colors.border
            ColumnLayout {anchors.fill:parent;anchors.margins:10;spacing:10
                RowLayout {Layout.fillWidth:true;Text {text:"设备";font.pixelSize:Typography.h2;color:Colors.text;Layout.fillWidth:true}TouchButton {objectName:"manageDevices";text:"管理";autoWidth:true;onClicked:root.openManager("device",false)}}
                FieldInput {objectName:"workflowDeviceSearch";Layout.fillWidth:true;height:34;placeholder:"搜索设备";onTextEdited:function(v){root.deviceSearch=v;root.reloadAssets()}}
                ListView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:root.assets;spacing:6;ScrollBar.vertical:ScrollBar{}
                    delegate:Rectangle {required property var modelData;width:ListView.view.width;height:60;color:root.asset.id===modelData.id?Colors.infoDark:Colors.panel
                        Column {anchors.fill:parent;anchors.margins:10;spacing:6;Text {width:parent.width;text:modelData.name;elide:Text.ElideRight;font.pixelSize:Typography.body;font.weight:Font.Medium;color:Colors.text}Text {width:parent.width;text:modelData.code;elide:Text.ElideRight;font.pixelSize:Typography.body;color:Colors.textSecondary}}
                        MouseArea {anchors.fill:parent;onClicked:root.chooseAsset(modelData)}
                    }
                    Text {anchors.centerIn:parent;visible:!root.assets.length;text:"暂无设备";font.pixelSize:Typography.body;color:Colors.textSecondary}
                }
                TouchButton {objectName:"workflowAddDevice";text:"新增设备";Layout.fillWidth:true;height:34;variant:"tonal";visible:App.session.permissions.manage_assets;onClicked:deviceForm.openDevice({})}
            }
        }
        Text{visible:!root.asset.id;Layout.fillWidth:true;Layout.fillHeight:true;text:"请先选择设备，或新增设备";horizontalAlignment:Text.AlignHCenter;verticalAlignment:Text.AlignVCenter;font.pixelSize:18;color:Colors.textSecondary}
        ColumnLayout {
            Layout.fillWidth:true;Layout.fillHeight:true;spacing:8;visible:!!root.asset.id
            RowLayout {Layout.fillWidth:true
                Text {Layout.fillWidth:true;text:root.asset.name||"巡检管理";elide:Text.ElideRight;font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
                TouchButton {
                    text:"登记到工程"; autoWidth:true; visible:root.view==="device"
                    enabled:!!App.projects.project.id
                    onClicked:{
                        const result = App.projects.registerAsset(root.asset.id)
                        if (result.ok) App.nav.push("projectEvidence", {device_id:result.id})
                        else root.message = result.error
                    }
                }
                TouchButton {objectName:"workflowSettingsButton";text:"管理与设置";autoWidth:true;onClicked:root.openManager(root.view,false)}
            }
            RowLayout {Layout.fillWidth:true;spacing:3;visible:root.view==="tasks"
                Item {Layout.fillWidth:true}
                TouchButton {objectName:"workflowStart";visible:root.view==="tasks";text:root.selected.status==="in_progress"?"继续巡检":"开始巡检";height:32;autoWidth:true;variant:"primary";enabled:!!root.selected.id&&root.selected.status!=="completed";onClicked:root.begin()}
            }
            InspectionFlow {
                Layout.fillWidth:true; Layout.preferredHeight:108
                heading: root.view === "defects" ? "缺陷整改闭环 · " + (root.selected.title || "选择一条缺陷查看进度") : "设备巡检流程"
                steps: root.summary.flow || []
                defectMode: root.view === "defects"
                defect: root.selected
                onStageClicked: function(stage) {
                    if (root.view === "defects") {
                        if (root.selected.id) recordWindow.openFor("defects", root.selected.id)
                    } else root.setView(stage==="dispatch"||stage==="inspect"?"tasks":stage==="remediate"||stage==="review"?"defects":"records")
                }
            }
            RowLayout{Layout.fillWidth:true;spacing:8
                FieldInput{Layout.fillWidth:true;height:34;placeholder:"搜索当前设备记录";text:root.keyword;onTextEdited:function(v){root.keyword=v;root.page=0;root.refresh()}}
                TouchButton{objectName:"directWorkflowNew";text:root.view==="tasks"?"派发任务":root.view==="records"?"新增巡检":root.view==="defects"?"登记缺陷":"编辑设备";autoWidth:true;height:34;variant:"primary";enabled:root.view==="device"?App.session.permissions.manage_assets:root.view==="tasks"?App.session.permissions.assign:root.view==="defects"?App.session.permissions.register_defect:App.session.permissions.inspect;onClicked:if(root.view==="device")deviceForm.openDevice(root.asset);else if(root.view==="records")root.openInspection();else management.openEditor(root.view,true)}
                TouchButton {
                    text:"AI 分析"; autoWidth:true; height:34; visible:root.view!=="device"; enabled:!!root.selected.id
                    onClicked: operationReview.review(root.view==="tasks" ? "task" : root.view==="defects" ? "defect" : "inspection", root.selected.id)
                }
                TouchButton{text:"编辑";visible:root.view!=="device";autoWidth:true;height:34;enabled:!!root.selected.id&&(root.view==="tasks"?App.session.permissions.assign&&root.selected.status!=="completed":root.view==="records"?root.selected.status==="draft":!!App.defects.defect(root.selected.id).canEdit);onClicked:management.openEditor(root.view,false)}
                TouchButton{text:"删除";autoWidth:true;height:34;variant:"ghost";enabled:!!root.selected.id&&App.session.permissions.assign;onClicked:removeConfirm.openDialog("移入回收站","选中记录将移入回收站。已关联的正式档案与整改历史会受到保护。","确认","取消")}
            }
            Text {visible:root.message.length>0;Layout.fillWidth:true;text:root.message;wrapMode:Text.Wrap;color:Colors.danger;font.pixelSize:Typography.body}
            Rectangle {
                Layout.fillWidth:true;Layout.fillHeight:true;color:Colors.panel;border.color:Colors.border
                DataTable {objectName:"workflowTable";visible:root.view!=="device";anchors.fill:parent;items:root.rows;columns:root.visibleColumns();selectedIndex:root.rows.findIndex(function(r){return r.id===root.selected.id});onRowClicked:function(index,item){root.selected=item;root.message="";recordWindow.openFor(root.view,item.id)}onRowDoubleClicked:function(index,item){root.selected=item;recordWindow.openFor(root.view,item.id)}}
                Column {visible:root.view==="device";anchors.fill:parent;anchors.margins:22;spacing:18
                    Text {text:root.asset.code||"";font.pixelSize:18;color:Colors.text}
                    Text {width:parent.width;text:"安装位置："+(root.asset.location||"—")+"\n设备型号："+(root.asset.model||"—");font.pixelSize:Typography.body;color:Colors.textSecondary;wrapMode:Text.Wrap;lineHeight:1.6}
                }
            }
            RowLayout {visible:root.total>20;Layout.fillWidth:true;Item{Layout.fillWidth:true}TouchButton {text:"上一页";height:28;autoWidth:true;variant:"ghost";enabled:root.page>0;onClicked:{root.page--;root.refresh()}}Text{text:(root.page+1)+" / "+Math.max(1,Math.ceil(root.total/20));font.pixelSize:Typography.body;color:Colors.textSecondary}TouchButton {text:"下一页";height:28;autoWidth:true;variant:"ghost";enabled:(root.page+1)*20<root.total;onClicked:{root.page++;root.refresh()}}Item{Layout.fillWidth:true}}

        }
    }
}
