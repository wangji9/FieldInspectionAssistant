import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../theme"
import "../components"
Rectangle {
    id:root;objectName:"productPage";color:Colors.background
    property bool embedded:false
    property bool showHeading:true
    signal backRequested()
    property string section:"devices"
    property string entity:"components"
    property var items:[]
    property int page:0
    property int total:0
    property int selectedIndex:-1
    property var selected:({})
    property var reportData:({})
    AnalysisReviewDialog { id: formalReview }
    property string message:""
    property string keyword:""
    property string assetFilter:""
    property string recordFilter:""
    property string editingEntity:""
    property string editAction:""
    property string fileAction:""
    property string pendingAction:""
    property var pendingArguments:({})
    property string cleanupToken:""
    property string undoToken:""
    property string restoreToken:""
    property string releaseToken:""
    property var releases:[]
    property string maintenanceText:""
    property string conclusion:""
    property string correctionReason:""
    property int retentionDays:90
    readonly property var tabs:section==="devices"?[{key:"components",name:"部件"},{key:"points",name:"测点"},{key:"channels",name:"通道绑定"},{key:"calibrations",name:"标定记录"},{key:"baselines",name:"健康基准"}]:section==="tasks"?[{key:"tasks",name:"任务"},{key:"plans",name:"周期计划"},{key:"reminders",name:"漏检与超期"},{key:"handovers",name:"交接记录"}]:section==="evidence"?[{key:"evidence",name:"证据"},{key:"records",name:"巡检记录"}]:section==="reports"?[{key:"records",name:"巡检记录"},{key:"reports",name:"报告版本"}]:section==="data"?[{key:"maintenance",name:"数据维护"},{key:"recycle",name:"回收站"}]:[{key:"maintenance",name:"运行维护"}]
    function setup(options){var opts=options||{section:App.nav.param("section"),asset_id:App.nav.param("asset_id"),record_id:App.nav.param("record_id")};section=opts.section||"devices";assetFilter=opts.asset_id||"";recordFilter=opts.record_id||"";entity=tabs[0].key;page=0;selected=({});reportData=({});refresh()}
    function refresh(){if(!App.session.loggedIn)return;var f={keyword:keyword,asset_id:assetFilter,record_id:recordFilter};if(entity==="maintenance")return;var r=App.workspace.query(entity,f,page,30);if(!r.ok){message=r.error;return}items=r.rows;total=r.total;selectedIndex=-1;for(var i=0;i<items.length;i++)if(items[i].id===selected.id)selectedIndex=i;if(selectedIndex<0&&items.length)selectedIndex=0;selected=selectedIndex>=0?items[selectedIndex]:({});if(section==="reports"&&selected.id)loadReport()}
    function loadReport(){var id=entity==="reports"?selected.record_id:selected.id;if(id){reportData=entity==="reports"?App.workspace.reportVersion(selected.id):App.workspace.report(id);conclusion=(reportData.snapshot||{}).human_conclusion||"";correctionReason=""}}
    function choice(key,label,type,filter){return {key:key,label:label,type:"choice",options:(key==="point_id"?[{id:"",name:"不指定测点"}]:[]).concat(App.workspace.options(type,filter||{}))}}
    function editItem(creating){
        var data=creating?{}:JSON.parse(JSON.stringify(selected)),defs=[];editingEntity=entity;editAction="save"
        if(!creating&&!data.id)return
        if(entity==="components")defs=[choice("asset_id","设备","assets"),{key:"name",label:"部件名称"},{key:"position",label:"安装位置"}]
        else if(entity==="points")defs=[choice("component_id","所属部件","components",{asset_id:assetFilter}),{key:"code",label:"测点编号"},{key:"name",label:"测点名称"},{key:"position",label:"安装位置"},{key:"operating_condition",label:"工况"}]
        else if(entity==="channels"){defs=[choice("point_id","测点","points",{asset_id:assetFilter}),{key:"source",label:"采集源标识"},{key:"channel",label:"传感器通道（1—6）"},{key:"unit",label:"原始单位"},{key:"serial_number",label:"传感器序列号"}];if(creating)data={source:"ListenGo_raw_six",channel:"1",unit:"FS"}}
        else if(entity==="calibrations"){defs=[choice("channel_id","绑定通道","channels",{asset_id:assetFilter}),{key:"gain",label:"增益"},{key:"offset",label:"偏置"},{key:"unit",label:"标定后单位"},{key:"valid_from",label:"生效时间（ISO 8601）"},{key:"valid_until",label:"到期时间（ISO 8601）"},{key:"reference",label:"证书编号 / 标定依据"}];if(creating)data={gain:"1",offset:"0",valid_from:new Date().toISOString(),valid_until:new Date(Date.now()+365*86400000).toISOString()}}
        else if(entity==="baselines")defs=[choice("point_id","测点","points",{asset_id:assetFilter}),{key:"metric",label:"指标（例如 RMS）"},{key:"minimum",label:"下限（可留空）"},{key:"maximum",label:"上限（可留空）"},{key:"unit",label:"单位"},{key:"operating_condition",label:"工况"},{key:"reference",label:"基准依据 / 标准版本"}]
        else if(entity==="tasks"||entity==="plans"){
            defs=[{key:"name",label:"名称"},choice("asset_id","设备","assets"),choice("point_id","测点（可选）","points",{asset_id:assetFilter}),choice("assignee_id","负责人","users"),{key:entity==="tasks"?"planned_at":"next_due",label:"计划时间（ISO 8601）"}]
            if(entity==="plans")defs.push({key:"period_days",label:"周期天数"})
            if(creating){data={period_days:"7",enabled:true};data[entity==="tasks"?"planned_at":"next_due"]=new Date().toISOString()}
        }
        if(defs.length)editor.open((creating?"新增":"编辑")+tabs.filter(function(t){return t.key===entity})[0].name,defs,data)
    }
    function handover(){if(!selected.id)return;editingEntity="handovers";editAction="save";editor.open("任务交接",[choice("to_user","接收人","users"),{key:"reason",label:"交接原因与未完成事项",type:"long"}],{task_id:selected.id})}
    function addEvidence(){editAction="evidence";editingEntity="evidence";editor.open("关联巡检证据",[choice("record_id","本次巡检记录","records"),choice("point_id","测点（可选）","points",{asset_id:assetFilter}),{key:"kind",label:"证据类型",type:"choice",options:[{id:"photo",name:"照片"},{id:"audio",name:"录音"},{id:"chart",name:"分析图"},{id:"analysis",name:"分析数据"},{id:"reading",name:"人工读数"},{id:"ai",name:"AI建议"},{id:"conclusion",name:"人工结论"}]},{key:"title",label:"证据名称"},{key:"path",label:"附件路径（可选，使用下方选择附件）"},{key:"content",label:"内容 / 说明",type:"long"},{key:"metric",label:"读数指标"},{key:"value",label:"读数值"},{key:"unit",label:"读数单位"},{key:"operating_condition",label:"读数工况"}],{record_id:recordFilter,kind:App.nav.param("kind")||(App.nav.param("path")?"photo":App.nav.param("content")?"ai":"reading"),path:App.nav.param("path")||"",content:App.nav.param("content")||"",title:App.nav.param("path")?"现场照片":App.nav.param("content")?"AI对话建议":""})}
    function submitForm(v){
        var r
        if(editAction==="evidence"){v.metadata=Object.assign({},App.nav.param("metadata")||{}, {metric:v.metric||"",value:v.value||"",unit:v.unit||"",operating_condition:v.operating_condition||""});r=App.workspace.linkEvidence(v)}
        else if(editAction==="remove"){r=App.workspace.remove(entity,selected.id,v.reason||"")}
        else if(editAction==="reauth"){
            if(!App.session.authenticate(App.session.userName,v.password||"")){editor.message="密码验证失败";return}
            editor.visible=false;App.workspace.operation(pendingAction,pendingArguments);return
        }else r=App.workspace.save(editingEntity,v)
        if(!r.ok){editor.message=r.error||"保存失败";return}editor.visible=false;message=r.pending?"正在保存附件…":"已保存";refresh()
    }
    function authorizeAction(action,args){pendingAction=action;pendingArguments=args;editAction="reauth";editor.open("确认维护操作",[{key:"password",label:"当前账号密码",type:"password"}],{})}
    function removeItem(){editAction="remove";editor.open("停用 / 移入回收站",[{key:"reason",label:"原因",type:"long"}],{})}
    function chooseFile(action,save){fileAction=action;if(save)saveDialog.open();else openDialog.open()}
    function columns(){
        var c={components:["name","部件",160,"asset_name","设备",170,"position","安装位置",240],points:["code","编号",110,"name","测点",130,"component_name","部件",120,"position","安装位置",180,"operating_condition","工况",150],channels:["point_name","测点",150,"source","采集源",160,"channel","通道",80,"unit","单位",90,"serial_number","序列号",150],calibrations:["point_name","测点",120,"gain","增益",90,"offset","偏置",90,"unit","单位",80,"valid_until","有效期",180,"reference","依据",180],baselines:["point_name","测点",120,"metric","指标",90,"minimum","下限",90,"maximum","上限",90,"unit","单位",80,"operating_condition","工况",130],tasks:["task_code","任务编号",155,"name","任务",170,"assignee_name","负责人",110,"status","状态",100,"planned_at","计划时间",190],plans:["name","计划",160,"asset_name","设备",150,"assignee_name","负责人",110,"period_days","周期/天",100,"next_due","下次时间",190,"enabled","启用",70],reminders:["task_code","任务",160,"name","名称",160,"assignee_name","负责人",110,"planned_at","应检时间",190],handovers:["task_id","任务",190,"from_name","交出人",100,"to_name","接收人",100,"reason","交接事项",250,"created_at","时间",190],records:["task_code","任务",150,"asset_name","设备",140,"result","结果",90,"status","状态",90,"created_at","时间",190],evidence:["kind","类型",85,"title","证据",170,"creator","提交人",110,"created_at","时间",190],reports:["revision","版本",80,"confirmer","确认人",120,"created_at","时间",190,"reason","更正原因",240],recycle:["entity","类型",90,"entity_id","对象",220,"actor","操作人",120,"reason","原因",200,"created_at","时间",190]};var a=c[entity]||[],out=[];for(var i=0;i<a.length;i+=3)out.push({key:a[i],title:a[i+1],width:a[i+2]});return out
    }
    onVisibleChanged:if(visible&&!embedded&&App.nav.page==="product")setup()
    Connections{target:App.nav;function onPageChanged(){if(!root.embedded&&App.nav.page==="product")root.setup()}}
    Connections{target:App.workspace;function onDataChanged(){if(root.visible)root.refresh()}function onMaintenanceReady(action,result){root.maintenanceText=JSON.stringify(result,null,2);if(action==="cleanup_preview")root.cleanupToken=result.token||"";if(action==="cleanup")root.undoToken=result.undo_token||"";if(action==="restore_prepare")root.restoreToken=result.token||"";if(action==="stage_release"||action==="snapshot_release"||action==="prepare_rollback")root.releaseToken=result.token||"";if(action==="releases")root.releases=result.rows||[]}}
    ColumnLayout{
        anchors.fill:parent;anchors.margins:Dimensions.pagePadding;spacing:10
        RowLayout{
            visible:root.showHeading;Layout.fillWidth:true
            TouchButton{text:"返回";autoWidth:true;height:34;variant:"ghost";onClicked:if(root.embedded)root.backRequested();else App.nav.pop()}
            Text{text:({devices:"设备与测点",tasks:"任务计划与交接",evidence:"统一证据档案",reports:"正式报告",maintenance:"运行维护",data:"数据管理"})[root.section]||"业务管理";font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}
            Item{Layout.fillWidth:true}
            TouchButton{text:"刷新";autoWidth:true;height:34;onClicked:root.refresh()}
        }
        Flow{
            visible:root.tabs.length>1;Layout.fillWidth:true;spacing:6
            Repeater{model:root.tabs;TouchButton{text:modelData.name;autoWidth:true;height:34;variant:root.entity===modelData.key?"tonal":"ghost";onClicked:{root.entity=modelData.key;root.page=0;root.selected=({});root.refresh()}}}
        }
        RowLayout{
            visible:root.entity!=="maintenance";Layout.fillWidth:true
            FieldInput{Layout.fillWidth:true;Layout.maximumWidth:300;placeholder:"搜索";height:34;onTextEdited:function(v){root.keyword=v;root.page=0;root.refresh()}}
            TouchButton{text:"清除筛选";autoWidth:true;height:34;visible:!!root.assetFilter||!!root.recordFilter;onClicked:{root.assetFilter="";root.recordFilter="";root.refresh()}}
            TouchButton{objectName:"productNew";text:"新增";autoWidth:true;height:34;visible:["components","points","channels","calibrations","baselines","tasks","plans"].indexOf(root.entity)>=0&&App.session.permissions.manage_assets;onClicked:root.editItem(true)}
            TouchButton{text:"用于采集";autoWidth:true;height:34;visible:root.entity==="channels";enabled:!!root.selected.id;onClicked:{var r=App.workspace.channelProfile(root.selected.id);if(r.ok)App.nav.push("analysis",{channel_profile:r});else root.message=r.error}}
            TouchButton{text:"编辑";autoWidth:true;height:34;visible:["components","points","tasks","plans"].indexOf(root.entity)>=0&&App.session.permissions.manage_assets;enabled:!!root.selected.id;onClicked:root.editItem(false)}
            TouchButton{text:"交接";autoWidth:true;height:34;visible:root.entity==="tasks"&&App.session.permissions.assign;enabled:!!root.selected.id&&root.selected.status!=="completed";onClicked:root.handover()}
            TouchButton{text:"关联证据";autoWidth:true;height:34;visible:root.section==="evidence";onClicked:root.addEvidence()}
            TouchButton{text:"选择附件";autoWidth:true;height:34;visible:root.section==="evidence";onClicked:root.chooseFile("evidence",false)}
            TouchButton{text:"停用 / 回收";autoWidth:true;height:34;variant:"ghost";visible:["components","points","channels","plans","evidence"].indexOf(root.entity)>=0&&App.session.permissions.manage_assets;enabled:!!root.selected.id;onClicked:root.removeItem()}
            TouchButton{text:"恢复";autoWidth:true;height:34;visible:root.entity==="recycle";enabled:!!root.selected.id;onClicked:{var r=App.workspace.restore(root.selected.id);root.message=r.ok?"已恢复":r.error;root.refresh()}}
        }
        Text{Layout.fillWidth:true;visible:root.message.length>0;text:root.message;color:Colors.danger;font.pixelSize:Typography.body;wrapMode:Text.Wrap}
        Rectangle{
            visible:root.entity!=="maintenance";Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:100;color:Colors.panel;radius:10;border.color:Colors.border
            DataTable{anchors.fill:parent;items:root.items;selectedIndex:root.selectedIndex;columns:root.columns();onRowClicked:function(index,item){root.selectedIndex=index;root.selected=item;if(root.section==="reports")root.loadReport()}}
        }
        RowLayout{
            visible:root.entity!=="maintenance";Layout.fillWidth:true
            TouchButton{text:"上一页";autoWidth:true;height:32;enabled:root.page>0;onClicked:{root.page--;root.refresh()}}
            Text{text:(root.page+1)+" / "+Math.max(1,Math.ceil(root.total/30))+" · "+root.total+" 条";font.pixelSize:Typography.body;color:Colors.textSecondary}
            TouchButton{text:"下一页";autoWidth:true;height:32;enabled:(root.page+1)*30<root.total;onClicked:{root.page++;root.refresh()}}
            Item{Layout.fillWidth:true}
            TouchButton{text:"查看证据详情";autoWidth:true;height:32;visible:root.entity==="evidence";enabled:!!root.selected.id;onClicked:{editAction="view";editor.open("证据详情",[{key:"details",label:"完整信息",type:"long"}],{details:JSON.stringify(root.selected,null,2)})}}
        }
        ColumnLayout{
            visible:root.section==="reports";Layout.fillWidth:true;spacing:8
            RowLayout{
                Layout.fillWidth:true
                Text{text:root.reportData.confirmed?"已确认 · v"+root.reportData.revision:"报告草稿";font.pixelSize:Typography.body;color:Colors.text}
                TouchButton {
                    text:"AI 复核"; height:32; autoWidth:true; enabled:!!root.reportData.ok && !!root.selected.id
                    onClicked: formalReview.review(root.entity==="reports" ? "formalReport" : "reportDraft", root.selected.id)
                }
                TouchButton{text:"校验证据";height:32;autoWidth:true;enabled:!App.workspace.busy;onClicked:App.workspace.operation("integrity",{})}
                TouchButton{text:"查看明细";height:32;autoWidth:true;onClicked:reportPreview.open()}
                TouchButton{text:"PDF";height:32;autoWidth:true;enabled:!!root.reportData.confirmed;onClicked:root.chooseFile("report_pdf",true)}
                TouchButton{text:"HTML";height:32;autoWidth:true;enabled:!!root.reportData.confirmed;onClicked:root.chooseFile("report_html",true)}
                TouchButton{text:"证据包 ZIP";height:32;autoWidth:true;enabled:!!root.reportData.confirmed&&!App.workspace.busy;onClicked:root.chooseFile("report_zip",true)}
                TouchButton{text:"JSON";height:32;autoWidth:true;enabled:!!root.reportData.confirmed;onClicked:root.chooseFile("report_json",true)}
            }
            RowLayout{
                Layout.fillWidth:true
                FieldInput{Layout.fillWidth:true;height:38;placeholder:"人工复核结论";text:root.conclusion;onTextEdited:function(v){root.conclusion=v}}
                FieldInput{Layout.fillWidth:true;height:38;placeholder:"更正原因（后续版本必填）";text:root.correctionReason;onTextEdited:function(v){root.correctionReason=v}}
                TouchButton{text:root.reportData.confirmed?"保存更正版本":"确认正式报告";autoWidth:true;height:38;enabled:App.session.permissions.review&&!!root.selected.id;onClicked:{var id=root.entity==="reports"?root.selected.record_id:root.selected.id;var r=App.workspace.confirmReport(id,root.conclusion,root.correctionReason,root.reportData.revision||0);root.message=r.ok?"报告已确认":r.error;root.refresh()}}
            }
        }
        ScrollView{
            visible:root.entity==="maintenance";Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
            ColumnLayout{
                width:parent.width;spacing:14
                Flow{
                    Layout.fillWidth:true;spacing:8
                    TouchButton{visible:root.section!=="data";text:"设备自检";autoWidth:true;height:36;enabled:App.session.permissions.settings;onClicked:root.maintenanceText=JSON.stringify(App.workspace.diagnostics(),null,2)}
                    TouchButton{visible:root.section==="data";text:"附件完整性";autoWidth:true;height:36;enabled:App.session.permissions.inspect&&!App.workspace.busy;onClicked:App.workspace.operation("integrity",{})}
                    TouchButton{visible:root.section==="data";text:"备份";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!App.workspace.busy;onClicked:root.chooseFile("backup",true)}
                    TouchButton{visible:root.section==="data";text:"校验备份";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!App.workspace.busy;onClicked:root.chooseFile("verify_backup",false)}
                    TouchButton{visible:root.section==="data";text:"准备恢复";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!App.workspace.busy;onClicked:root.chooseFile("restore_prepare",false)}
                    TouchButton{visible:root.section==="data";text:"恢复并重启";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!App.workspace.busy&&!!root.restoreToken;onClicked:root.authorizeAction("restore_apply",{token:root.restoreToken})}
                }
                Flow{
                    visible:root.section==="data";Layout.fillWidth:true;spacing:8
                    SpinBox{from:7;to:3650;value:root.retentionDays;editable:true;onValueModified:root.retentionDays=value}
                    TouchButton{text:"清理预览";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!App.workspace.busy;onClicked:App.workspace.operation("cleanup_preview",{days:root.retentionDays})}
                    TouchButton{text:"清理暂存与缓存";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!!root.cleanupToken&&!App.workspace.busy;onClicked:root.authorizeAction("cleanup",{token:root.cleanupToken})}
                    TouchButton{text:"撤销清理";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!!root.undoToken&&!App.workspace.busy;onClicked:App.workspace.operation("cleanup_undo",{token:root.undoToken})}
                }
                Flow{
                    visible:root.section!=="data";Layout.fillWidth:true;spacing:8
                    TouchButton{text:"保存当前程序版本";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!App.workspace.busy;onClicked:App.workspace.operation("snapshot_release",{version:App.version})}
                    TouchButton{text:"校验升级包";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!App.workspace.busy;onClicked:root.chooseFile("stage_release",false)}
                    TouchButton{text:"应用已校验版本";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!!root.releaseToken&&!App.workspace.busy;onClicked:root.authorizeAction("apply_release",{token:root.releaseToken})}
                    TouchButton{text:"版本记录";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!App.workspace.busy;onClicked:App.workspace.operation("releases",{})}
                    AppComboBox{id:rollbackChoice;model:root.releases;textRole:"version";width:180;height:36;displayText:currentIndex<0?"选择历史版本":currentText}
                    TouchButton{text:"准备回滚";autoWidth:true;height:36;enabled:App.session.permissions.settings&&rollbackChoice.currentIndex>=0&&!App.workspace.busy;onClicked:App.workspace.operation("prepare_rollback",{release_id:root.releases[rollbackChoice.currentIndex].token})}
                    TouchButton{text:"恢复模型服务";autoWidth:true;height:36;enabled:App.session.permissions.settings&&!App.workspace.busy;onClicked:root.authorizeAction("restart_ai",{})}
                }
                Text{Layout.fillWidth:true;text:App.workspace.status;font.pixelSize:Typography.body;color:Colors.text;wrapMode:Text.Wrap}
                TextArea{Layout.fillWidth:true;Layout.preferredHeight:Math.max(260,implicitHeight);readOnly:true;selectByMouse:true;text:root.maintenanceText;font.family:Typography.uiFamily;font.pixelSize:Typography.body;wrapMode:TextEdit.Wrap;background:Rectangle{color:Colors.panel;radius:10;border.color:Colors.border}}
            }
        }
    }
    AssistantDialog{
        id:reportPreview;title:"报告复核";modal:true;anchors.centerIn:parent;width:Math.min(860,root.width-24);height:Math.min(700,root.height-24)
        contentItem:ScrollView{id:reportScroll;clip:true;contentWidth:availableWidth;Text{width:reportScroll.availableWidth;text:root.reportData.html||"";textFormat:Text.RichText;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}}
    }
    ProductEditor{id:editor;onSubmitted:function(v){if(root.editAction!=="view")root.submitForm(v);else visible=false}}
    FileDialog{id:openDialog;title:"选择文件";onAccepted:{var path=selectedFile.toString();if(root.fileAction==="evidence"){root.addEvidence();editor.setValue("path",path);editor.setValue("kind",/\.(png|jpg|jpeg|bmp|webp)$/i.test(path)?"photo":/\.(wav|mp3|flac|ogg)$/i.test(path)?"audio":"analysis")}else App.workspace.operation(root.fileAction,{path:path})}}
    FileDialog{id:saveDialog;title:"保存";fileMode:FileDialog.SaveFile;defaultSuffix:root.fileAction==="backup"?"zip":root.fileAction.slice(7);onAccepted:{var path=selectedFile.toString();if(root.fileAction==="report_zip"){var p=App.workspace.packageReport(root.reportData.id,path);root.message=p.ok?"正在打包报告和原始证据…":p.error}else if(root.fileAction.indexOf("report_")===0){var r=App.workspace.exportReport(root.reportData.id,path,root.fileAction.slice(7));root.message=r.ok?"已保存："+r.path:r.error}else App.workspace.operation(root.fileAction,{path:path})}}
}
