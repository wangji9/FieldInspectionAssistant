import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
Window{
    id:win;objectName:"inspectionDetailWindow";title:"设备巡检记录";visible:false;color:Colors.background
    property var ownerPage
    property string kind:"records"
    property string identity:""
    property var detailData:({})
    property int tab:0
    property string message:""
    property bool showDeleted:false
    property string pendingImage:""
    property var selectedEntry:({})
    AnalysisReviewDialog { id: inspectionReview }
    readonly property var projectEvents: App.projects.events.filter(row =>
        !!row.record_id && row.record_id === (detailData.record || {}).id)
    onClosing:{entryEditor.visible=false;imagePicker.close();imagePreview.close();if(App.input.targetWindow===win)App.input.cancel()}
    property alias form:entryEditor
    readonly property int fixedWidth:Math.min(900,Screen.desktopAvailableWidth-32)
    readonly property int fixedHeight:Math.min(690,Screen.desktopAvailableHeight-40)
    width:fixedWidth;height:fixedHeight;minimumWidth:fixedWidth;maximumWidth:fixedWidth;minimumHeight:fixedHeight;maximumHeight:fixedHeight
    modality:Qt.WindowModal
    transientParent:ownerPage?ownerPage.Window.window:null;flags:Qt.Dialog|Qt.WindowTitleHint|Qt.WindowSystemMenuHint|Qt.WindowCloseButtonHint
    function openFor(type,id){kind=type;identity=id;message="";tab=0;showDeleted=false;refresh();show();raise();requestActivate()}
    function refresh(){if(!identity||!App.session.loggedIn)return;var r=App.workflow.detail(kind,identity);if(!r.ok){message=r.error;detailData={};return}detailData=r}
    function date(value){return value?Qt.formatDateTime(new Date(value),"yyyy-MM-dd HH:mm:ss"):"—"}
    function stage(v){return ({observation:"检查记录",action:"整改措施",review:"复核",conclusion:"结论"})[v]||v}
    function editEntry(row){selectedEntry=row||{};pendingImage="";entryEditor.open(row&&row.id?"编辑处理记录":"新增处理记录",[{key:"title",label:"记录标题"},{key:"stage",label:"阶段",type:"choice",options:[{id:"observation",name:"检查记录"},{id:"action",name:"整改措施"},{id:"review",name:"复核"},{id:"conclusion",name:"结论"}]},{key:"state",label:"状态",type:"choice",options:[{id:"pending",name:"待完成"},{id:"completed",name:"已完成"}]},{key:"content",label:"过程、措施与结果",type:"long"}],row&&row.id?row:{stage:"observation",state:"pending"})}
    function writeEntry(v){v.image=pendingImage;var r=App.workflow.saveEntry((detailData.record||{}).id||"",v);if(!r.ok){entryEditor.message=r.error;return}entryEditor.visible=false;refresh()}
    function trashEntry(row,deleted){var r=App.workflow.setEntryDeleted(row.id,deleted,row.revision);message=r.ok?"":r.error;refresh()}
    Connections{target:App.workflow;function onChanged(){if(win.visible)win.refresh()}}
    Connections{target:App.workspace;function onDataChanged(){if(win.visible)win.refresh()}}
    Connections{target:App.defects;function onDefectsChanged(){if(win.visible)win.refresh()}}
    Connections{target:App.session;function onSessionChanged(){if(!App.session.loggedIn){win.close();win.detailData={};win.identity=""}}}
    BoardFilePicker{id:imagePicker;onPicked:function(path){win.pendingImage=path;entryEditor.setValue("remove_image",false)}}
    AssistantDialog{id:imagePreview;title:"巡检图片";width:Math.min(760,win.width-32);height:Math.min(530,win.height-32);anchors.centerIn:parent;property string imagePath:"";contentItem:Image{source:imagePreview.imagePath?"file://"+imagePreview.imagePath:"";fillMode:Image.PreserveAspectFit;asynchronous:true}}
    ColumnLayout{anchors.fill:parent;anchors.margins:16;spacing:10
        RowLayout{Layout.fillWidth:true
            Text{Layout.fillWidth:true;text:(win.detailData.asset||{}).name||"巡检记录";font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text;elide:Text.ElideRight}
            Text{text:win.detailData.state_text||"待完成";font.pixelSize:Typography.body;color:win.detailData.state==="completed"?Colors.success:Colors.normal}
        }
        Text{Layout.fillWidth:true;text:((win.detailData.task||{}).task_code||"")+"  "+((win.detailData.task||{}).name||"");font.pixelSize:Typography.body;color:Colors.textSecondary;elide:Text.ElideRight}
        InspectionFlow {
            Layout.fillWidth:true; Layout.preferredHeight:102
            steps: win.detailData.flow || []
            defectMode: win.kind === "defects"
            defect: win.detailData.selected_defect || ({})
            heading: defectMode ? "缺陷整改闭环" : "巡检执行流程"
            onStageClicked: function(stage) { win.tab = defectMode || stage==="remediate" || stage==="review" ? 3 : stage==="record" ? 1 : 0 }
        }
        RowLayout{Layout.fillWidth:true
            Repeater{model:["巡检内容","处理过程","图片","缺陷与整改"];TouchButton{text:modelData;autoWidth:true;height:32;variant:win.tab===index?"tonal":"ghost";onClicked:win.tab=index}}
            Item{Layout.fillWidth:true}
            TouchButton{objectName:"addWorkflowEntry";text:"新增处理记录";autoWidth:true;height:32;variant:"primary";enabled:!!win.detailData.can_write;onClicked:{win.tab=1;win.editEntry({})}}
            TouchButton {
                text:"AI 分析"; autoWidth:true; height:32
                enabled:!!win.detailData.ok
                onClicked: inspectionReview.review(win.kind==="tasks" ? "task" : win.kind==="defects" ? "defect" : "inspection", win.identity)
            }
            TouchButton{text:"执行 / 查看";autoWidth:true;height:32;enabled:!!(win.detailData.task||{}).id;onClicked:{win.close();ownerPage.openTask(win.detailData.task)}}
        }
        Text{visible:win.message.length>0;Layout.fillWidth:true;wrapMode:Text.Wrap;text:win.message;color:Colors.danger;font.pixelSize:Typography.body}
        StackLayout{Layout.fillWidth:true;Layout.fillHeight:true;currentIndex:win.tab
            ScrollView{id:recordScroll;clip:true;contentWidth:availableWidth
                ColumnLayout{width:recordScroll.availableWidth;spacing:12
                    Text{Layout.fillWidth:true;text:"负责人："+((win.detailData.task||{}).assignee_name||"未分派")+"   计划时间："+win.date((win.detailData.task||{}).planned_at);wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
                    Text{Layout.fillWidth:true;text:"巡检结论："+((win.detailData.record||{}).result||"待填写")+"\n"+((win.detailData.record||{}).answer||"");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
                    Repeater{model:win.detailData.checks||[];Rectangle{Layout.fillWidth:true;implicitHeight:checkLabel.implicitHeight+22;radius:8;color:Colors.panel;border.color:Colors.border;Text{id:checkLabel;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:11;text:modelData.item_name+" · "+({normal:"正常",abnormal:"异常",skip:"跳过"}[modelData.result]||"待填写")+"  "+(modelData.value_text||"")+" "+(modelData.unit||"")+"\n"+(modelData.note||"");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}}}
                    Text{visible:!(win.detailData.record||{}).id;Layout.fillWidth:true;text:"任务尚未开始；开始巡检后生成本次记录。";font.pixelSize:Typography.body;color:Colors.textSecondary}
                    Text{Layout.fillWidth:true;text:"正式报告版本："+(win.detailData.reports||[]).length;font.pixelSize:Typography.body;color:Colors.textSecondary}
                    TouchButton {
                        visible: win.projectEvents.length > 0
                        text: "查看关联分析事件（" + win.projectEvents.length + "）"
                        autoWidth: true
                        onClicked: {
                            const record = win.detailData.record.id
                            win.close()
                            App.nav.push("events", {record_id:record})
                        }
                    }
                }
            }
            ColumnLayout{spacing:8
                AppCheckBox{text:"显示已删除记录";checked:win.showDeleted;onToggled:win.showDeleted=checked}
                ListView{id:entryList;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:9;model:(win.showDeleted?win.detailData.all_entries:win.detailData.entries)||[];ScrollBar.vertical:ScrollBar{}
                    delegate:Rectangle{required property var modelData;width:entryList.width;height:entryColumn.implicitHeight+20;color:Colors.panel;border.color:Colors.border;radius:10
                        ColumnLayout{id:entryColumn;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:10;spacing:8
                            Text{Layout.fillWidth:true;text:modelData.title+" · "+win.stage(modelData.stage)+" · "+(modelData.deleted_at?"已删除":modelData.state==="completed"?"已完成":"待完成");wrapMode:Text.Wrap;font.pixelSize:Typography.body;font.weight:Font.Medium;color:Colors.text}
                            Text{Layout.fillWidth:true;text:modelData.content;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
                            Image{visible:!!modelData.path;source:modelData.path?"file://"+modelData.path:"";Layout.preferredWidth:200;Layout.preferredHeight:visible?120:0;fillMode:Image.PreserveAspectFit;asynchronous:true;MouseArea{anchors.fill:parent;onClicked:{imagePreview.imagePath=modelData.path;imagePreview.open()}}}
                            RowLayout{Text{Layout.fillWidth:true;text:modelData.creator+" · "+Qt.formatDateTime(new Date(modelData.created_at),"yyyy-MM-dd HH:mm:ss")+" · v"+modelData.revision;elide:Text.ElideRight;font.pixelSize:12;color:Colors.textSecondary}
                                TouchButton{text:"编辑";height:28;autoWidth:true;variant:"ghost";visible:!modelData.deleted_at;enabled:!!modelData.editable;onClicked:win.editEntry(modelData)}
                                TouchButton{text:modelData.deleted_at?"恢复":"删除";height:28;autoWidth:true;variant:"ghost";enabled:!!modelData.editable;onClicked:win.trashEntry(modelData,!modelData.deleted_at)}
                            }
                        }
                    }
                    Text{visible:entryList.count===0;anchors.centerIn:parent;text:"暂无处理记录";color:Colors.textSecondary;font.pixelSize:Typography.body}
                }
            }
            ScrollView{id:photoScroll;clip:true;contentWidth:availableWidth
                Flow{width:photoScroll.availableWidth;spacing:12;Repeater{model:win.detailData.photos||[];Rectangle{width:240;height:185;color:Colors.panel;border.color:Colors.border;radius:9;Image{anchors.fill:parent;anchors.margins:8;source:modelData.path?"file://"+modelData.path:"";fillMode:Image.PreserveAspectFit;asynchronous:true}MouseArea{anchors.fill:parent;onClicked:{imagePreview.imagePath=modelData.path;imagePreview.open()}}}}}
            }
            ScrollView{id:defectScroll;clip:true;contentWidth:availableWidth
                ColumnLayout{width:defectScroll.availableWidth;spacing:10
                    Repeater{model:win.detailData.defects||[];Rectangle{Layout.fillWidth:true;implicitHeight:dc.implicitHeight+20;color:Colors.panel;border.color:Colors.border;radius:9
                        ColumnLayout{id:dc;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:10;spacing:8
                            Text{Layout.fillWidth:true;text:modelData.defect_code+" · "+modelData.title+" · "+(modelData.status==="closed"?"已完成":"待完成");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
                            Text{Layout.fillWidth:true;text:(modelData.description||"")+"\n整改结果："+(modelData.feedback||"待填写");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                            TouchButton{text:"查看 / 处理缺陷";autoWidth:true;height:30;variant:"tonal";onClicked:{win.close();ownerPage.openDefect(modelData)}}
                        }
                    }}
                    Repeater{model:win.detailData.events||[];Text{Layout.fillWidth:true;text:modelData.defect_title+" · "+modelData.status+" · "+(modelData.note||"")+" · "+win.date(modelData.created_at);wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}}
                    Flow{visible:win.kind==="defects";Layout.fillWidth:true;spacing:8;Repeater{model:[{id:"assign",name:"分派"},{id:"start",name:"开始处理"},{id:"feedback",name:"提交整改"},{id:"pass",name:"复核通过"},{id:"reject",name:"退回"}];TouchButton{text:modelData.name;autoWidth:true;height:32;onClicked:{win.close();ownerPage.manageDefect(modelData.id)}}}}
                }
            }
        }
    }
    ProductEditor{id:entryEditor;attachmentLabel:win.pendingImage||win.selectedEntry.path&&!values.remove_image?"更换图片":"添加图片";attachmentRemovable:!!(win.pendingImage||win.selectedEntry.path&&!values.remove_image);onAttachmentRequested:imagePicker.openFor("image");onAttachmentRemoveRequested:{win.pendingImage="";setValue("remove_image",true)}onSubmitted:function(v){win.writeEntry(v)}}
}
