import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"
import "../pages"
Window {
    id:win;objectName:"workflowSettingsWindow";visible:false;title:"巡检管理 · 设置与操作"
    property bool formOnly:false
    readonly property int fixedWidth:Math.min(formOnly?660:900,Screen.desktopAvailableWidth-32)
    readonly property int fixedHeight:Math.min(formOnly?600:640,Screen.desktopAvailableHeight-40)
    width:fixedWidth;height:fixedHeight;minimumWidth:fixedWidth;maximumWidth:fixedWidth;minimumHeight:fixedHeight;maximumHeight:fixedHeight;color:Colors.background
    flags:Qt.Dialog|Qt.WindowTitleHint|Qt.WindowSystemMenuHint|Qt.WindowCloseButtonHint
    property var ownerPage
    modality:Qt.WindowModal
    transientParent:ownerPage?ownerPage.Window.window:null
    readonly property string entity:ownerPage?ownerPage.view:"tasks"
    readonly property var asset:ownerPage?ownerPage.asset:({})
    readonly property var selected:entity==="device"?asset:ownerPage?ownerPage.selected:({})
    property string action:"save"
    property string message:""
    property bool advancedOpen:false
    property alias form:editor
    FileDialog {id:recordCsv;title:"导出当前设备巡检档案";fileMode:FileDialog.SaveFile;defaultSuffix:"csv";nameFilters:["CSV (*.csv)"];onAccepted:{var r=App.evidence.exportCsv(selectedFile.toString(),win.asset.id);win.message=r.ok?"已导出："+r.path:r.error}}
    function openEditor(section,creating){if(section==="records"&&creating){ownerPage.openInspection();return}if(!creating&&!ownerPage.selected.id){ownerPage.message="请先选择需要编辑的记录";return}formOnly=true;if(ownerPage.view!==section)ownerPage.setView(section);message="";show();raise();requestActivate();edit(creating)}
    function openLifecycle(kind){formOnly=true;show();raise();requestActivate();lifecycle(kind)}
    function openFor(section,creating){if(section==="records"&&creating){ownerPage.openInspection();return}formOnly=false;if(ownerPage.view!==section)ownerPage.setView(section);else ownerPage.refresh();message="";advancedOpen=false;show();raise();requestActivate();if(creating)Qt.callLater(function(){win.edit(true)})}
    function option(key,label,rows,readonly){return {key:key,label:label,type:"choice",options:rows,readOnly:!!readonly}}
    function users(){return App.workspace.options("users")}
    function edit(creating){
        var v=creating?{}:JSON.parse(JSON.stringify(selected)),fields=[];action="save"
        if(!creating&&!v.id)return
        if(entity==="device"){fields=[{key:"code",label:"设备编号",readOnly:!creating},{key:"name",label:"设备名称"},{key:"location",label:"安装位置"},{key:"model",label:"型号"}]}
        else if(entity==="tasks"){
            fields=[{key:"name",label:"任务名称"},option("asset_id","设备",[{id:asset.id,name:asset.name}],true),option("assignee_id","负责人",users(),!creating&&v.status==="in_progress"),{key:"planned_at",label:"计划时间",placeholder:"yyyy-MM-dd HH:mm"}]
            if(creating)v={asset_id:asset.id,assignee_id:App.session.userId,planned_at:Qt.formatDateTime(new Date(Date.now()+3600000),"yyyy-MM-dd HH:mm")}
            else v.planned_at=Qt.formatDateTime(new Date(v.planned_at),"yyyy-MM-dd HH:mm")
        }else if(entity==="defects"){
            fields=[{key:"title",label:"缺陷标题"},option("asset_id","来源设备",[{id:asset.id,name:asset.name}],true),option("priority","优先级",[{id:"low",name:"低"},{id:"medium",name:"中"},{id:"high",name:"高"}]),{key:"description",label:"缺陷描述",type:"long"}]
            if(creating){v={asset_id:asset.id,priority:"medium",record_id:""};var records=App.workspace.query("records",{asset_id:asset.id,status:"draft"},0,100).rows||[];fields.splice(2,0,option("record_id","发现记录（可选）",[{id:"",name:"现场手动登记"}].concat(records.map(function(r){return {id:r.id,name:(r.task_code||"")+" · "+Qt.formatDateTime(new Date(r.created_at),"MM-dd HH:mm")}}))))}
        }else if(entity==="records"){
            if(creating){win.close();ownerPage.openInspection();return}
            else fields=[{key:"result",label:"结果说明"},{key:"answer",label:"巡检备注",type:"long"}]
        }
        editor.open((creating?"新增":"编辑")+({device:"设备",tasks:"任务",defects:"缺陷",records:"巡检记录"})[entity],fields,v)
    }
    function after(r){if(ownerPage)ownerPage.message=r.ok?"":r.error||"操作失败";if(!r.ok){editor.message=r.error||"操作失败";message=r.error||"操作失败";return false}editor.visible=false;message="已保存";if(entity==="device")ownerPage.reloadAssets(r.id||asset.id);else{if(r.id)ownerPage.selected={id:r.id};ownerPage.refresh()}if(formOnly)close();return true}
    function submit(v){
        var r
        if(action==="assign")r=App.defects.assign(selected.id,v.assignee_id,v.due_at||"",v.note||"")
        else if(action==="start")r=App.defects.start(selected.id,v.note||"")
        else if(action==="feedback")r=App.defects.submitFeedback(selected.id,v.feedback||"")
        else if(action==="pass"||action==="reject")r=App.defects.review(selected.id,action==="pass",v.note||"")
        else if(entity==="device")r=v.id?App.assets.updateAsset(v.id,v.name||"",v.location||"",v.model||""):App.assets.createAsset(v.code||"",v.name||"",v.location||"",v.model||"")
        else if(entity==="tasks"){var date=new Date(String(v.planned_at||"").replace(" ","T"));if(!isFinite(date.getTime())){editor.message="请填写有效计划时间，例如 2026-10-30 09:00";return}v.planned_at=date.toISOString();r=App.workspace.save("tasks",v)}
        else if(entity==="defects")r=v.id?App.defects.saveDefect(v.id,asset.id,v.title||"",v.description||"",v.priority||"medium"):App.defects.createDefect(v.record_id||"",asset.id,v.title||"",v.description||"",v.priority||"medium")
        else r=App.inspection.saveArchive(v.id,asset.id,v.result||"",v.answer||"")
        after(r)
    }
    function removeSelected(){var r=entity==="device"?App.assets.deleteAsset(asset.id):entity==="tasks"?App.inspection.deleteTask(selected.id):entity==="defects"?App.defects.deleteDefect(selected.id):App.inspection.deleteArchive(selected.id);after(r)}
    function lifecycle(kind){action=kind;var fields=kind==="assign"?[option("assignee_id","处理人",users()),{key:"due_at",label:"处理期限",placeholder:"yyyy-MM-dd"},{key:"note",label:"分派说明",type:"long"}]:[{key:kind==="feedback"?"feedback":"note",label:kind==="feedback"?"整改结果":"处理说明",type:"long"}];editor.open(({assign:"分派整改",start:"开始处理",feedback:"提交整改",pass:"复核通过",reject:"退回复核"})[kind],fields,{assignee_id:App.session.userId})}
    function advanced(section){advancedOpen=true;advancedPage.setup({section:section,asset_id:asset.id||"",record_id:entity==="records"?selected.id||"":selected.record_id||""})}
    onClosing:{editor.visible=false;advancedOpen=false;if(App.input.targetWindow===win)App.input.cancel()}
    Rectangle {anchors.fill:parent;color:Colors.background
        ScrollView {id:settingsScroll;anchors.fill:parent;anchors.margins:16;clip:true;contentWidth:availableWidth;visible:!win.advancedOpen&&!win.formOnly
        ColumnLayout {width:settingsScroll.availableWidth;spacing:12
            RowLayout {Layout.fillWidth:true;Text {text:win.asset.name||"请先新增设备";Layout.fillWidth:true;elide:Text.ElideRight;font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}AppComboBox {objectName:"workflowManageSection";Layout.preferredWidth:150;model:[{id:"device",name:"设备管理"},{id:"tasks",name:"任务派发"},{id:"records",name:"档案管理"},{id:"defects",name:"缺陷管理"}];textRole:"name";currentIndex:model.findIndex(function(r){return r.id===win.entity});onActivated:{editor.visible=false;ownerPage.setView(model[currentIndex].id);win.message=""}}}
            Text {text:win.entity==="device"?win.asset.code||"":win.selected.task_code||win.selected.defect_code||win.selected.name||"未选择记录";Layout.fillWidth:true;elide:Text.ElideRight;color:Colors.textSecondary;font.pixelSize:Typography.body}
            Flow {Layout.fillWidth:true;spacing:8
                TouchButton {objectName:"workflowNew";text:win.entity==="tasks"?"新增并派发任务":"新增";autoWidth:true;height:36;variant:"primary";enabled:(win.entity==="device"?App.session.permissions.manage_assets:win.entity==="tasks"?App.session.permissions.assign:win.entity==="defects"?App.session.permissions.register_defect:App.session.permissions.inspect)&&(win.entity==="device"||!!win.asset.id);onClicked:win.edit(true)}
                TouchButton {objectName:"workflowEdit";text:"编辑";autoWidth:true;height:36;enabled:!!win.selected.id&&(win.entity==="device"?App.session.permissions.manage_assets:win.entity==="tasks"?App.session.permissions.assign&&win.selected.status!=="completed":win.entity==="records"?win.selected.status==="draft"&&App.session.permissions.inspect:!!App.defects.defect(win.selected.id).canEdit);onClicked:win.edit(false)}
                TouchButton {objectName:"workflowDelete";text:"移入回收站";autoWidth:true;height:36;variant:"ghost";enabled:!!win.selected.id&&App.session.permissions.assign;onClicked:confirmRemove.openDialog("移入回收站","将选中内容移入回收站？已有正式档案、缺陷或整改历史的内容会保留并阻止删除。","确认","取消")}
                TouchButton {text:"部件与测点";visible:win.entity==="device";height:36;autoWidth:true;enabled:!!win.asset.id;onClicked:win.advanced("devices")}
                TouchButton {text:"周期与交接";visible:win.entity==="tasks";height:36;autoWidth:true;onClicked:win.advanced("tasks")}
                TouchButton {text:"正式报告";visible:win.entity==="records";height:36;autoWidth:true;enabled:!!win.selected.id;onClicked:win.advanced("reports")}
                TouchButton {objectName:"workflowExportRecords";text:"导出 CSV";visible:win.entity==="records";height:36;autoWidth:true;enabled:!!win.asset.id;onClicked:recordCsv.open()}
                TouchButton {text:"证据档案";visible:win.entity==="records"||win.entity==="defects";height:36;autoWidth:true;enabled:!!win.selected.id;onClicked:win.advanced("evidence")}
                TouchButton {text:"执行 / 查看";visible:win.entity==="records";height:36;autoWidth:true;enabled:!!win.selected.task_id;onClicked:{win.close();ownerPage.inspectRecord()}}
            }
            Flow {visible:win.entity==="defects";Layout.fillWidth:true;spacing:8
                TouchButton {text:"分派整改";autoWidth:true;height:34;enabled:!!win.selected.id&&App.session.permissions.assign&&["unassigned","pending"].indexOf(win.selected.status)>=0;onClicked:win.lifecycle("assign")}
                TouchButton {text:"开始处理";autoWidth:true;height:34;enabled:!!win.selected.id&&win.selected.status==="pending"&&App.session.permissions.start_defect&&(win.selected.assignee_id===App.session.userId||App.session.permissions.assign);onClicked:win.lifecycle("start")}
                TouchButton {text:"提交整改";autoWidth:true;height:34;enabled:!!win.selected.id&&win.selected.status==="in_progress"&&App.session.permissions.start_defect&&(win.selected.assignee_id===App.session.userId||App.session.permissions.assign);onClicked:win.lifecycle("feedback")}
                TouchButton {text:"复核通过";autoWidth:true;height:34;enabled:!!win.selected.id&&win.selected.status==="review"&&App.session.permissions.review;onClicked:win.lifecycle("pass")}
                TouchButton {text:"退回";autoWidth:true;height:34;enabled:!!win.selected.id&&win.selected.status==="review"&&App.session.permissions.review;onClicked:win.lifecycle("reject")}
            }
            Text {Layout.fillWidth:true;visible:win.message.length>0;text:win.message;font.pixelSize:Typography.body;color:Colors.danger;wrapMode:Text.Wrap}
            Rectangle {Layout.fillWidth:true;height:1;color:Colors.border}
            Text {text:"列表筛选与显示";font.pixelSize:Typography.body;color:Colors.text;visible:win.entity!=="device"}
            RowLayout {visible:win.entity!=="device";Layout.fillWidth:true;FieldInput {Layout.fillWidth:true;height:36;placeholder:"搜索当前设备下的内容";text:ownerPage?ownerPage.keyword:"";onTextEdited:function(v){ownerPage.keyword=v;ownerPage.page=0;ownerPage.refresh()}}AppComboBox {Layout.preferredWidth:145;model:win.entity==="tasks"?[{id:"",name:"全部状态"},{id:"pending",name:"待开始"},{id:"overdue",name:"已超期"},{id:"in_progress",name:"进行中"},{id:"completed",name:"已完成"}]:win.entity==="records"?[{id:"",name:"全部状态"},{id:"draft",name:"草稿"},{id:"submitted",name:"已提交"}]:[{id:"",name:"全部状态"},{id:"unassigned",name:"未分派"},{id:"pending",name:"待处理"},{id:"in_progress",name:"处理中"},{id:"review",name:"待复核"},{id:"closed",name:"已关闭"}];textRole:"name";currentIndex:model.findIndex(function(r){return r.id===ownerPage.statusFilter});onActivated:{ownerPage.statusFilter=model[currentIndex].id;ownerPage.page=0;ownerPage.refresh()}}}
            Flow {visible:win.entity!=="device";Layout.fillWidth:true;spacing:10;Repeater {model:ownerPage?ownerPage.columns():[];AppCheckBox {required property var modelData;text:modelData.title;checked:(ownerPage.columnChoices[win.entity]||[]).indexOf(modelData.key)<0;onToggled:{var copy=Object.assign({},ownerPage.columnChoices),hidden=(copy[win.entity]||[]).slice(),at=hidden.indexOf(modelData.key);if(!checked&&at<0)hidden.push(modelData.key);if(checked&&at>=0)hidden.splice(at,1);if(hidden.length>=ownerPage.columns().length){win.message="至少保留一列";checked=true;return}copy[win.entity]=hidden;ownerPage.columnChoices=copy}}}}
            Text {Layout.fillWidth:true;wrapMode:Text.Wrap;text:win.entity==="records"?"记录由任务执行生成。草稿可更正；提交后的记录通过正式报告追加更正。":win.entity==="tasks"?"选择设备和负责人派发任务后，再开始巡检。进行中的任务通过交接变更负责人。":win.entity==="defects"?"缺陷始终归属于来源设备，可关联发现它的巡检记录；整改经复核后关闭。":"设备关联的任务、巡检记录和缺陷会统一保留。";font.pixelSize:Typography.body;color:Colors.textSecondary}
            Item {Layout.fillHeight:true}
        }
        }
        ProductPage {id:advancedPage;embedded:true;anchors.fill:parent;visible:win.advancedOpen;onBackRequested:win.advancedOpen=false}
        ProductEditor {id:editor;onVisibleChanged:if(!visible&&win.formOnly)win.close();onSubmitted:function(v){win.submit(v)}}
        ConfirmDialog {id:confirmRemove;onAccepted:win.removeSelected()}
    }
}
