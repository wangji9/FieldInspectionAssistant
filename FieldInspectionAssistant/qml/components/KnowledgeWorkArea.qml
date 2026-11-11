import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import FieldInspection 1.0
import "../theme"

Rectangle {
    id:root;objectName:"knowledgeWorkArea";color:Colors.panel;border.color:Colors.border;radius:12
    function openSettings(){graphSettings.open()}
    function closeSettings(){graphSettings.close();conversationPicker.close();documentPicker.close();candidateDialog.close();textImport.close()}
    function closeReview(){candidateDialog.close()}
    property int tab:0
    property bool allowEvolve:true
    readonly property bool hasPending:Object.keys(App.knowledge.pending).length>0
    readonly property int retainedCount:(App.knowledge.pending.relations||[]).filter(function(r){return r.selected!==false}).length
    function retained(){var r=App.knowledge.applyPending();root.showResult(r);if(r.ok){root.selectedRevision=0;root.tab=0;root.nodeDetails={};graphView.fit()}}
    function selectAll(value){for(var i=0;i<(App.knowledge.pending.relations||[]).length;i++)App.knowledge.selectCandidate(i,value)}
    property int selectedRevision:0
    property bool onlyChanges:false
    property var nodeDetails:({})
    property var searchResult:({})
    property string localMessage:""
    property var ontologyDraft:({entity_types:[],relation_types:[]})
    property bool ontologyCode:false
    property bool includeDeleted:false
    property string recordQuery:""
    property string editKind:"source"
    property string editId:""
    property var selectedConversation:({})
    ListModel {id:candidateRows;dynamicRoles:true}
    function syncCandidates(){
        var rows=App.knowledge.pending.relations||[]
        while(candidateRows.count>rows.length)candidateRows.remove(candidateRows.count-1)
        for(var i=0;i<rows.length;i++){
            var r=rows[i],item={sourceName:r.from||"",predicate:r.predicate||"",targetName:r.to||"",quote:r.evidence||"",retained:r.selected!==false}
            if(i>=candidateRows.count)candidateRows.append(item)
            else for(var key in item)if(candidateRows.get(i)[key]!==item[key])candidateRows.setProperty(i,key,item[key])
        }
    }
    Connections {target:App.session;function onSessionChanged(){root.selectedConversation={};root.localMessage="";if(!App.session.loggedIn){sourceInput.text="";root.nodeDetails={};root.closeSettings()}}}
    ConversationPicker {id:conversationPicker;onPicked:function(row){root.selectedConversation=row}}
    BoardFilePicker {id:documentPicker;onPicked:function(path){root.localMessage="";App.knowledge.buildFile(path,root.allowEvolve)}}
    function resetOntology(){try{ontologyDraft=JSON.parse(App.knowledge.ontologyJson);ontologyInput.text=App.knowledge.ontologyJson}catch(e){}}
    function writeDraft(){ontologyInput.text=JSON.stringify(ontologyDraft,null,2)}
    function addType(){var name=typeNameInput.text.trim();if(!name.length)return;var types=(ontologyDraft.entity_types||[]).slice();if(types.indexOf(name)>=0)return;types.push(name);ontologyDraft={entity_types:types,relation_types:ontologyDraft.relation_types||[]};typeNameInput.text="";writeDraft()}
    function addRelation(){var name=relationNameInput.text.trim();if(!name.length||relationFrom.currentIndex<0||relationTo.currentIndex<0)return;var rules=(ontologyDraft.relation_types||[]).slice();rules.push({name:name,from:relationFrom.currentText,to:relationTo.currentText});ontologyDraft={entity_types:ontologyDraft.entity_types,relation_types:rules};relationNameInput.text="";writeDraft()}
    onWidthChanged:if(width>=1000)detailDialog.close()
    function showResult(r){localMessage=r.ok?"已完成":r.error||"操作失败"}
    function reloadDetails(id){
        if(!hasPending){nodeDetails=App.knowledge.details(id,selectedRevision);return}
        var node=(App.knowledge.previewGraph.nodes||[]).filter(function(n){return n.id===id})[0]
        if(!node){nodeDetails={};return}
        var facts=[];for(var i=0;i<(App.knowledge.pending.relations||[]).length;i++){var r=App.knowledge.pending.relations[i];if((r.from===node.name&&r.from_type===node.type)||(r.to===node.name&&r.to_type===node.type))facts.push(Object.assign({},r,{quote:r.evidence,candidate_index:i}))}
        nodeDetails=Object.assign({},node,{facts:facts})
    }
    function inspect(id){reloadDetails(id);if(width<1000)detailDialog.open()}
    onSelectedRevisionChanged:if(nodeDetails.id)reloadDetails(nodeDetails.id)
    onVisibleChanged:if(visible){App.knowledge.refresh();resetOntology()}
    Connections{target:App.knowledge;function onChanged(){root.syncCandidates();if(!App.knowledge.busy){root.resetOntology();if(root.nodeDetails.id)root.reloadDetails(root.nodeDetails.id)}}}
    FileDialog{id:importFile;title:"选择图谱资料";nameFilters:["文字资料 (*.txt *.md *.csv *.json *.html *.htm *.pdf)"];onAccepted:{root.localMessage="";App.knowledge.buildFile(decodeURIComponent(selectedFile.toString().replace(/^file:\/\//,"")),root.allowEvolve)}}
    FileDialog{id:exportFile;title:"导出知识图谱";fileMode:FileDialog.SaveFile;defaultSuffix:"json";nameFilters:["知识图谱 JSON (*.json)"];onAccepted:root.showResult(App.knowledge.exportGraph(decodeURIComponent(selectedFile.toString().replace(/^file:\/\//,""))))}
    Dialog{id:renameDialog;title:"重命名";modal:true;width:Math.min(root.width-24,460);height:180;anchors.centerIn:parent;standardButtons:Dialog.Ok|Dialog.Cancel
        contentItem:FieldInput{id:recordName;height:38;placeholder:"名称"}
        onAccepted:root.showResult(root.editKind==="source"?App.knowledge.renameSource(root.editId,recordName.text):App.knowledge.renameRevision(Number(root.editId),recordName.text))}
    Dialog{id:deleteDialog;title:"删除记录";modal:true;width:Math.min(root.width-24,460);height:240;anchors.centerIn:parent;standardButtons:Dialog.Ok|Dialog.Cancel
        contentItem:Text{text:root.editKind==="source"?"此来源支持的事实将从当前图谱和检索中撤回。历史版本保留，可在已删除列表恢复。":"移除这条版本记录，保留来源与历史图谱；可在已删除列表恢复。";wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
        onAccepted:root.showResult(root.editKind==="source"?App.knowledge.setSourceDeleted(root.editId,true):App.knowledge.setRevisionDeleted(Number(root.editId),true))}
    Dialog{id:editSourceDialog;title:"编辑资料并重新抽取";modal:true;width:Math.min(root.width-24,720);height:Math.min(root.height-24,520);anchors.centerIn:parent;standardButtons:Dialog.Ok|Dialog.Cancel
        contentItem:ColumnLayout{spacing:10
            FieldInput{id:editSourceTitle;Layout.fillWidth:true;height:36}
            ScrollView{Layout.fillWidth:true;Layout.fillHeight:true;clip:true
                TextArea{id:editSourceBody;selectByMouse:true;wrapMode:TextEdit.Wrap;font.pixelSize:Typography.body;color:Colors.text;background:null}}
        }
        onAccepted:{root.tab=2;App.knowledge.updateSource(root.editId,editSourceTitle.text,editSourceBody.text,root.allowEvolve)}}
    Dialog{id:detailDialog;title:root.nodeDetails.name||"实体详情";modal:true;width:Math.min(root.width-24,600);height:Math.min(root.height-24,470);anchors.centerIn:parent;standardButtons:Dialog.Close;contentItem:Loader{sourceComponent:detailsContent}}
    Dialog{id:sourceDialog;property string sourceText:"";title:"原始资料";modal:true;width:Math.min(root.width-24,720);height:Math.min(root.height-24,540);anchors.centerIn:parent;standardButtons:Dialog.Close
        contentItem:ScrollView{clip:true;TextArea{text:sourceDialog.sourceText;readOnly:true;selectByMouse:true;wrapMode:TextEdit.Wrap;font.pixelSize:Typography.body;background:null;color:Colors.text}}
    }
    Component{id:detailsContent
        ScrollView{id:detailScroll;clip:true;contentWidth:availableWidth
            ColumnLayout{width:detailScroll.availableWidth;spacing:10
                Text{Layout.fillWidth:true;text:root.nodeDetails.name||"选择实体查看来源";wrapMode:Text.Wrap;font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
                Text{Layout.fillWidth:true;visible:!!root.nodeDetails.id&&!root.hasPending;text:(root.nodeDetails.type||"")+" · 创建 v"+root.nodeDetails.created+" · 更新 v"+root.nodeDetails.updated;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                Repeater{model:root.nodeDetails.facts||[]
                    Rectangle{Layout.fillWidth:true;implicitHeight:factColumn.implicitHeight+20;radius:9;color:Colors.panelAlt;border.color:Colors.border
                        ColumnLayout{id:factColumn;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:10;spacing:7
                            Text{Layout.fillWidth:true;text:modelData.from+" → "+modelData.predicate+" → "+modelData.to;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text;font.weight:Font.Medium}
                            Text{Layout.fillWidth:true;text:modelData.quote;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                            TouchButton{visible:!!modelData.source_id;Layout.fillWidth:true;text:modelData.source+" · v"+modelData.revision;height:30;variant:"ghost";onClicked:{var r=App.knowledge.sourceDetail(modelData.source_id);sourceDialog.sourceText=r.content||"";sourceDialog.open()}}
                        }
                    }
                }
            }
        }
    }
    AssistantDialog {
        id:graphSettings;objectName:"graphSettingsDialog";title:"图谱设置与管理";modal:true;anchors.centerIn:parent
        width:Math.min(root.width-24,620);height:Math.min(root.height-24,470)
        contentItem:ScrollView{clip:true;contentWidth:availableWidth
            Column{spacing:16
        Flow{width:graphSettings.width-48;spacing:6
            AppCheckBox {text:"允许从候选关系扩展本体";checked:root.allowEvolve;enabled:!App.knowledge.busy&&!root.hasPending;onToggled:root.allowEvolve=checked}
            Repeater{model:["图谱","本体","抽取","版本与来源"]
                TouchButton{objectName:"knowledgeTab"+index;text:modelData;height:30;autoWidth:true;variant:root.tab===index?"tonal":"ghost";onClicked:{root.tab=index;graphSettings.close()}}}
            TouchButton{text:"导出";height:30;autoWidth:true;variant:"ghost";enabled:App.knowledge.revision>0;onClicked:exportFile.open()}
            Text{height:30;verticalAlignment:Text.AlignVCenter;text:"v"+App.knowledge.revision+" · "+(App.knowledge.graph.nodes||[]).length+" 实体 · "+(App.knowledge.graph.edges||[]).length+" 关系";color:Colors.textSecondary;font.pixelSize:Typography.body}
        }
                Flow{width:graphSettings.width-48;spacing:6
                    FieldInput{id:queryInput;objectName:"graphSearchInput";width:Math.min(270,root.width-50);height:32;placeholder:"检索实体或问题";onEditingFinished:function(value){root.searchResult=App.knowledge.search(value,10)}}
                    TouchButton{text:"检索";height:32;autoWidth:true;variant:"tonal";onClicked:root.searchResult=App.knowledge.search(queryInput.text,10)}
                    AppComboBox{id:versionChoice;width:132;height:32;model:[{name:"最新版本",id:0}].concat(App.knowledge.revisions.map(function(r){return {name:"版本 "+r.id,id:r.id}}));textRole:"name";currentIndex:root.selectedRevision===0?0:model.findIndex(function(r){return r.id===root.selectedRevision});onActivated:root.selectedRevision=model[currentIndex].id}
                    AppCheckBox{text:"仅本次变更";height:32;checked:root.onlyChanges;onToggled:root.onlyChanges=checked}
                    TouchButton{text:"适应";height:32;autoWidth:true;variant:"ghost";onClicked:graphView.fit()}
                    TouchButton{text:"−";height:32;width:32;variant:"ghost";onClicked:graphView.zoomOut()}
                    TouchButton{text:"+";height:32;width:32;variant:"ghost";onClicked:graphView.zoomIn()}
                }
                Flow{width:graphSettings.width-48;spacing:14
                    Repeater{model:[{t:"新增",c:"#159B77"},{t:"更新 / 补充证据",c:"#7760DA"},{t:"已有内容",c:"#9CA7BD"},{t:"检索命中",c:"#ECA143"}]
                        Row{spacing:5;Rectangle{width:9;height:9;radius:4.5;color:modelData.c;anchors.verticalCenter:parent.verticalCenter}Text{text:modelData.t;color:Colors.textSecondary;font.pixelSize:Typography.body}}}
                }
            }
        }
    }
    AssistantDialog {
        id:textImport;objectName:"graphTextImportDialog";title:"从文本抽取";anchors.centerIn:parent
        width:Math.min(root.width-24,660);height:Math.min(root.height-24,430)
        contentItem:ColumnLayout{spacing:10
            ScrollView{Layout.fillWidth:true;Layout.fillHeight:true;clip:true;contentWidth:availableWidth
                TextArea{id:sourceInput;objectName:"knowledgeSourceInput";placeholderText:"输入待抽取的文本";selectByMouse:true;wrapMode:TextEdit.Wrap;font.pixelSize:Typography.body;color:Colors.text;background:null}}
            TouchButton{objectName:"buildKnowledgeGraph";text:"开始抽取";height:34;autoWidth:true;variant:"primary";enabled:sourceInput.text.trim().length>=4&&!App.knowledge.busy&&!root.hasPending;onClicked:{root.localMessage="";root.tab=0;App.knowledge.buildText("",sourceInput.text,root.allowEvolve);textImport.close()}}
        }
    }
    AssistantDialog {
        id:candidateDialog;objectName:"graphCandidateDialog";title:"选择保留的关系";anchors.centerIn:parent
        width:Math.min(root.width-24,720);height:Math.min(root.height-24,540)
        contentItem:ColumnLayout{spacing:10
            RowLayout{TouchButton{text:"全选";autoWidth:true;height:30;variant:"ghost";onClicked:root.selectAll(true)}TouchButton{text:"全不选";autoWidth:true;height:30;variant:"ghost";onClicked:root.selectAll(false)}}
            ListView{id:candidateList;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:10;model:candidateRows;ScrollBar.vertical:ScrollBar{}
                delegate:Rectangle{
                    required property int index;required property string sourceName;required property string predicate;required property string targetName;required property string quote;required property bool retained
                    width:candidateList.width;height:candidateColumn.implicitHeight+16;color:Colors.panelAlt;radius:9;border.color:Colors.border
                    ColumnLayout{id:candidateColumn;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:8;spacing:6
                        AppCheckBox{objectName:"graphCandidate_"+index;Layout.fillWidth:true;height:Math.max(34,contentItem.implicitHeight+12);checked:retained;text:sourceName+" → "+predicate+" → "+targetName;contentItem:Text{text:parent.text;wrapMode:Text.Wrap;leftPadding:28;verticalAlignment:Text.AlignVCenter;font.pixelSize:Typography.body;color:Colors.text}onToggled:root.showResult(App.knowledge.selectCandidate(index,checked))}
                        Text{Layout.fillWidth:true;text:quote;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                    }
                }
            }
            RowLayout{TouchButton{text:"保留所选（"+root.retainedCount+"）";autoWidth:true;height:34;variant:"primary";enabled:root.retainedCount>0;onClicked:{root.retained();if(!root.hasPending)candidateDialog.close()}}TouchButton{text:"返回预览";autoWidth:true;height:34;variant:"ghost";onClicked:candidateDialog.close()}}
        }
    }
    ColumnLayout{
        anchors.fill:parent;anchors.margins:12;spacing:8
        Flow{visible:root.tab===0||root.tab===2;Layout.fillWidth:true;spacing:6
            TouchButton{objectName:"importGraphDocument";text:"导入资料";height:32;autoWidth:true;variant:"tonal";enabled:App.knowledge.canEdit&&!App.knowledge.busy&&!root.hasPending;onClicked:documentPicker.openFor("document")}
            TouchButton{objectName:"chooseGraphConversation";text:{var value=root.selectedConversation.display||"选择对话",limit=Math.max(4,Math.floor((width-24)/16));return value.length>limit?value.slice(0,limit-1)+"…":value}width:Math.min(300,Math.max(150,root.width*.34));height:32;variant:"ghost";enabled:!App.knowledge.busy&&!root.hasPending;onClicked:conversationPicker.open()}
            TouchButton{objectName:"extractGraphConversation";text:"抽取对话";height:32;autoWidth:true;variant:"tonal";enabled:App.knowledge.canEdit&&!App.knowledge.busy&&!!root.selectedConversation.id&&!root.hasPending;onClicked:{root.localMessage="";App.knowledge.buildConversation(root.selectedConversation.id,root.allowEvolve)}}
            TouchButton{objectName:"graphTextImport";text:"输入文本";height:32;autoWidth:true;variant:"ghost";enabled:App.knowledge.canEdit&&!App.knowledge.busy&&!root.hasPending;onClicked:textImport.open()}
            TouchButton{objectName:"stopGraphExtraction";text:"停止";visible:App.knowledge.busy;height:32;autoWidth:true;variant:"ghost";onClicked:App.knowledge.cancel()}
        }
        StackLayout{currentIndex:root.tab===2?0:root.tab;Layout.fillWidth:true;Layout.fillHeight:true
            ColumnLayout{spacing:8
                Item{Layout.fillWidth:true;Layout.fillHeight:true
                    Rectangle{id:graphPanel;objectName:"knowledgeGraphPanel";anchors.left:parent.left;anchors.top:parent.top;anchors.bottom:parent.bottom;width:parent.width-(detailPanel.visible?280:0);color:"#FBFCFF";border.color:Colors.border;radius:10;clip:true
                        KnowledgeGraphView{id:graphView;objectName:"knowledgeGraphView";anchors.fill:parent;anchors.margins:1;graph:{var candidate=App.knowledge.previewGraph;var live=App.knowledge.graph;return root.hasPending?candidate:root.selectedRevision?App.knowledge.graphAtRevision(root.selectedRevision):live}
                            hits:App.knowledge.hits;highlightRevision:root.hasPending?(App.knowledge.previewGraph.revision||0):root.selectedRevision||App.knowledge.revision;changesOnly:!root.hasPending&&root.onlyChanges;onNodeSelected:function(id){root.inspect(id)}}
                    }
                    Rectangle{id:detailPanel;objectName:"knowledgeDetailPanel";visible:root.width>=1000&&!!root.nodeDetails.id;anchors.right:parent.right;anchors.top:parent.top;anchors.bottom:parent.bottom;width:270;color:Colors.panel;border.color:Colors.border;radius:10
                        Loader{anchors.fill:parent;anchors.margins:12;sourceComponent:detailsContent}}
                }
                ScrollView{id:resultScroll;visible:(root.searchResult.facts||[]).length>0;Layout.fillWidth:true;Layout.preferredHeight:110;clip:true;contentWidth:availableWidth
                    ColumnLayout{width:resultScroll.availableWidth;spacing:4
                        Repeater{model:root.searchResult.facts||[]
                            Text{Layout.fillWidth:true;text:"["+modelData.citation+"] "+modelData.from+" → "+modelData.predicate+" → "+modelData.to+" · "+modelData.source;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text;MouseArea{anchors.fill:parent;onClicked:{sourceDialog.sourceText=App.knowledge.sourceDetail(modelData.source_id).content||"";sourceDialog.open()}}}}
                    }
                }
                Text{visible:!!root.searchResult.ok&&(root.searchResult.facts||[]).length===0;Layout.fillWidth:true;text:"没有检索到相关证据";font.pixelSize:Typography.body;color:Colors.textSecondary}
            }
            ColumnLayout{spacing:8
                Flow{Layout.fillWidth:true;spacing:8
                    Text{height:32;verticalAlignment:Text.AlignVCenter;text:"本体结构";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
                    TouchButton{objectName:"saveGraphOntology";text:"保存本体";height:32;autoWidth:true;variant:"primary";enabled:App.knowledge.canEdit&&!App.knowledge.busy&&Object.keys(App.knowledge.pending).length===0;onClicked:root.showResult(App.knowledge.saveOntology(ontologyInput.text))}
                    TouchButton{text:"恢复已保存";height:32;autoWidth:true;variant:"ghost";onClicked:root.resetOntology()}
                    TouchButton{text:root.ontologyCode?"表单编辑":"JSON 编辑";height:32;autoWidth:true;variant:"ghost";onClicked:{if(root.ontologyCode){try{root.ontologyDraft=JSON.parse(ontologyInput.text)}catch(e){root.localMessage="本体 JSON 格式错误";return}}root.ontologyCode=!root.ontologyCode}}
                }
                ScrollView{id:ontologyFormScroll;visible:!root.ontologyCode;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;contentWidth:availableWidth;enabled:App.knowledge.canEdit&&!App.knowledge.busy
                    ColumnLayout{width:ontologyFormScroll.availableWidth;spacing:12
                        Text{text:"实体类型";font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text}
                        Flow{Layout.fillWidth:true;spacing:6
                            Repeater{model:root.ontologyDraft.entity_types||[]
                                Rectangle{height:34;width:typeLabel.implicitWidth+50;color:Colors.panelAlt;border.color:Colors.border;radius:17
                                    Text{id:typeLabel;anchors.left:parent.left;anchors.leftMargin:12;anchors.verticalCenter:parent.verticalCenter;text:modelData;font.pixelSize:Typography.body;color:Colors.text}
                                    TouchButton{anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;width:30;height:28;text:"×";variant:"ghost";onClicked:{var t=root.ontologyDraft.entity_types.slice();t.splice(index,1);root.ontologyDraft={entity_types:t,relation_types:root.ontologyDraft.relation_types};root.writeDraft()}}
                                }
                            }
                        }
                        Flow{Layout.fillWidth:true;spacing:6
                            FieldInput{id:typeNameInput;objectName:"ontologyTypeName";width:180;height:34;placeholder:"新增实体类型";onEditingFinished:root.addType()}
                            TouchButton{objectName:"addOntologyType";text:"添加类型";autoWidth:true;height:34;variant:"tonal";onClicked:root.addType()}
                        }
                        Text{text:"关系约束";font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text}
                        Flow{Layout.fillWidth:true;spacing:6
                            AppComboBox{id:relationFrom;objectName:"ontologyRelationFrom";width:140;height:34;model:root.ontologyDraft.entity_types||[]}
                            FieldInput{id:relationNameInput;objectName:"ontologyRelationName";width:160;height:34;placeholder:"关系名称"}
                            AppComboBox{id:relationTo;objectName:"ontologyRelationTo";width:140;height:34;model:root.ontologyDraft.entity_types||[]}
                            TouchButton{objectName:"addOntologyRelation";text:"添加关系";autoWidth:true;height:34;variant:"tonal";onClicked:root.addRelation()}
                        }
                        Repeater{model:root.ontologyDraft.relation_types||[]
                            Rectangle{Layout.fillWidth:true;implicitHeight:Math.max(42,ruleLabel.implicitHeight+16);radius:9;color:Colors.panelAlt;border.color:Colors.border
                                Text{id:ruleLabel;anchors.left:parent.left;anchors.right:ruleRemove.left;anchors.verticalCenter:parent.verticalCenter;anchors.margins:12;text:modelData.from+" → "+modelData.name+" → "+modelData.to;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
                                TouchButton{id:ruleRemove;anchors.right:parent.right;anchors.rightMargin:6;anchors.verticalCenter:parent.verticalCenter;width:56;height:30;text:"移除";variant:"ghost";onClicked:{var r=root.ontologyDraft.relation_types.slice();r.splice(index,1);root.ontologyDraft={entity_types:root.ontologyDraft.entity_types,relation_types:r};root.writeDraft()}}
                            }
                        }
                    }
                }
                Rectangle{visible:root.ontologyCode;Layout.fillWidth:true;Layout.fillHeight:true;radius:10;color:Colors.panelAlt;border.color:Colors.border
                    ScrollView{anchors.fill:parent;anchors.margins:10;clip:true
                        TextArea{id:ontologyInput;objectName:"ontologyEditor";text:App.knowledge.ontologyJson;selectByMouse:true;wrapMode:TextEdit.Wrap;font.pixelSize:Typography.body;font.family:Typography.uiFamily;color:Colors.text;background:null;enabled:App.knowledge.canEdit&&!App.knowledge.busy}}
                }
            }
            Item {}
            ScrollView{id:historyScroll;clip:true;contentWidth:availableWidth
                ColumnLayout{width:historyScroll.availableWidth;spacing:10
                    Flow{Layout.fillWidth:true;spacing:8
                        FieldInput{width:220;height:34;placeholder:"搜索版本或来源";onTextEdited:function(value){root.recordQuery=value}}
                        AppCheckBox{text:"显示已删除";checked:root.includeDeleted;height:34;onToggled:root.includeDeleted=checked}
                        TouchButton{text:"新增来源";autoWidth:true;height:34;variant:"tonal";onClicked:root.tab=2}
                    }
                    Text{text:"更新记录";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
                    Repeater{model:App.knowledge.revisions.filter(function(r){return (root.includeDeleted||!r.deleted)&&r.title.toLowerCase().indexOf(root.recordQuery.toLowerCase())>=0})
                        Rectangle{Layout.fillWidth:true;implicitHeight:revisionColumn.implicitHeight+20;color:Colors.panelAlt;radius:9;border.color:Colors.border
                            ColumnLayout{id:revisionColumn;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:10;spacing:6
                                Text{Layout.fillWidth:true;text:"v"+modelData.id+" · "+modelData.title+" · "+modelData.time.replace("T"," ").replace("Z","");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
                                Text{Layout.fillWidth:true;text:modelData.changes.action?modelData.changes.action+" · "+modelData.changes.entity:modelData.changes.ontology?"本体结构更新":"新增 "+(modelData.changes.nodes_added||0)+" 实体 / "+(modelData.changes.edges_added||0)+" 关系；补充 "+(modelData.changes.nodes_updated||0)+" 实体 / "+(modelData.changes.edges_updated||0)+" 关系";wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                                Flow{Layout.fillWidth:true;spacing:6
                                    TouchButton{text:"查看图谱";autoWidth:true;height:30;variant:"ghost";onClicked:{root.selectedRevision=modelData.id;root.tab=0}}
                                    TouchButton{text:"重命名";autoWidth:true;height:30;variant:"ghost";enabled:App.knowledge.canEdit;onClicked:{root.editKind="revision";root.editId=String(modelData.id);recordName.text=modelData.title;renameDialog.open()}}
                                    TouchButton{text:modelData.deleted?"恢复":"删除记录";autoWidth:true;height:30;variant:"ghost";enabled:App.knowledge.canEdit;onClicked:{root.editKind="revision";root.editId=String(modelData.id);if(modelData.deleted)root.showResult(App.knowledge.setRevisionDeleted(modelData.id,false));else deleteDialog.open()}}
                                }
                            }
                        }
                    }
                    Text{text:"资料来源";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
                    Repeater{model:App.knowledge.sources.filter(function(r){return (root.includeDeleted||!r.deleted)&&r.title.toLowerCase().indexOf(root.recordQuery.toLowerCase())>=0})
                        Rectangle{Layout.fillWidth:true;implicitHeight:sourceActions.implicitHeight+20;radius:9;color:Colors.panelAlt;border.color:Colors.border
                            ColumnLayout{id:sourceActions;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:10;spacing:8
                                Text{Layout.fillWidth:true;text:modelData.title+" · v"+modelData.revision+(modelData.deleted?" · 已删除":"");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
                                Flow{Layout.fillWidth:true;spacing:6
                                    TouchButton{text:"查看";autoWidth:true;height:30;variant:"ghost";onClicked:{sourceDialog.sourceText=App.knowledge.sourceDetail(modelData.id).content||"";sourceDialog.open()}}
                                    TouchButton{text:"编辑并抽取";autoWidth:true;height:30;variant:"ghost";enabled:App.knowledge.canEdit&&!modelData.deleted;onClicked:{root.editId=modelData.id;var r=App.knowledge.sourceDetail(root.editId);editSourceTitle.text=r.title;editSourceBody.text=r.content;editSourceDialog.open()}}
                                    TouchButton{text:"重命名";autoWidth:true;height:30;variant:"ghost";enabled:App.knowledge.canEdit;onClicked:{root.editKind="source";root.editId=modelData.id;recordName.text=modelData.title;renameDialog.open()}}
                                    TouchButton{text:modelData.deleted?"恢复":"删除";autoWidth:true;height:30;variant:"ghost";enabled:App.knowledge.canEdit;onClicked:{root.editKind="source";root.editId=modelData.id;if(modelData.deleted)root.showResult(App.knowledge.setSourceDeleted(modelData.id,false));else deleteDialog.open()}}
                                }
                            }
                        }
                    }
                }
            }
        }
        Flow{visible:(root.tab===0||root.tab===2);Layout.fillWidth:true;spacing:6
            TouchButton{objectName:"applyKnowledgeGraph";text:"保留所选（"+root.retainedCount+"）";visible:root.hasPending;height:32;autoWidth:true;variant:"primary";enabled:!!App.knowledge.pending.ready&&root.retainedCount>0;onClicked:root.retained()}
            TouchButton{objectName:"reviewGraphCandidates";text:"筛选候选";visible:root.hasPending;height:32;autoWidth:true;variant:"tonal";enabled:!!App.knowledge.pending.ready;onClicked:candidateDialog.open()}
            TouchButton{objectName:"discardKnowledgeGraph";text:"放弃";visible:root.hasPending;height:32;autoWidth:true;variant:"ghost";enabled:!App.knowledge.busy;onClicked:{App.knowledge.discard();root.nodeDetails={};root.localMessage=""}}
            TouchButton{text:"适应";height:32;autoWidth:true;variant:"ghost";onClicked:graphView.fit()}
            TouchButton{text:"−";height:32;width:32;variant:"ghost";onClicked:graphView.zoomOut()}
            TouchButton{text:"+";height:32;width:32;variant:"ghost";onClicked:graphView.zoomIn()}
            AppCheckBox{objectName:"graphDynamicsToggle";text:"动态布局";height:32;checked:graphView.dynamicLayout;onToggled:graphView.dynamicLayout=checked}
        }
        Text{Layout.fillWidth:true;visible:root.localMessage.length>0||App.knowledge.busy||App.knowledge.status!=="就绪";text:root.localMessage||App.knowledge.status;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
    }
}
