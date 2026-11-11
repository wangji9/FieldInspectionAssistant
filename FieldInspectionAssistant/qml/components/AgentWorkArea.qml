import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"

Rectangle {
    id:root;objectName:"agentWorkArea";color:"transparent";radius:12
    property string imagePath:""
    property string analysisContext:""
    property int analysisChartCount:0
    function importAnalysis(bundle,question){analysisContext=bundle.context;analysisChartCount=bundle.images.length;imagePath=bundle.imagePath;taskInput.text=question}
    property string audioPath:""
    property var tools:[]
    property string memoryError:""
    property string inputError:""
    property bool ownsRecording:false
    property string displayedConversation:""
    property string displayedOwner:""
    // App.agent.messages is a QVariantList: binding it directly resets the
    // entire ListView for every streamed token. Keep stable delegate rows and
    // update only the active last row or newly appended rows.
    ListModel {id:displayMessages}
    function syncMessages(){
        syncTimer.stop()
        if(!App.session.loggedIn){displayMessages.clear();displayedConversation="";displayedOwner="";return}
        var rows=App.agent.messages,id=App.agent.conversationId,owner=App.session.userId
        if(displayedOwner!==owner||displayedConversation!==id||rows.length<displayMessages.count){displayMessages.clear();displayedOwner=owner;displayedConversation=id}
        var start=Math.max(0,displayMessages.count-1)
        for(var i=start;i<rows.length;i++){
            var r=rows[i],next={role:r.role||"",kind:r.kind||"text",body:r.body||"",imagePath:r.imagePath||"",audioPath:r.audioPath||"",streaming:!!r.streaming,systemEvidenceJson:JSON.stringify(r.systemEvidence||{})}
            if(i>=displayMessages.count){displayMessages.append(next);continue}
            var old=displayMessages.get(i)
            for(var key in next)if(old[key]!==next[key])displayMessages.setProperty(i,key,next[key])
        }
    }
    Timer{id:syncTimer;interval:90;onTriggered:root.syncMessages()}
    Timer{id:followAgent;interval:90;onTriggered:if(chat.followTail)chat.positionViewAtEnd()}
    Connections {target:App.agent;function onChanged(){if(root.visible&&!syncTimer.running)syncTimer.start()}}
    function setTool(id,on){var list=App.agent.enabledTools.slice(),at=list.indexOf(id);if(on&&at<0)list.push(id);else if(!on&&at>=0)list.splice(at,1);var r=App.agent.configureTools(list);memoryError=r.ok?"":r.error}
    function recordAudio(){if(App.agent.enabledTools.indexOf("audio_transcription")<0){inputError="请先启用语音识别工具";return}ownsRecording=true;inputError="";App.speech.startRecording(false)}
    Connections {target:App.speech;function onRecordingReady(path){if(root.ownsRecording){root.audioPath=path;root.ownsRecording=false}}function onChanged(){if(root.ownsRecording&&!App.speech.recording&&App.speech.error){root.inputError=App.speech.error;root.ownsRecording=false}}}
    Connections {target:App.session;function onSessionChanged(){if(!App.session.loggedIn){syncTimer.stop();displayMessages.clear();root.displayedOwner="";root.displayedConversation="";if(root.ownsRecording)App.speech.cancelRecording();root.ownsRecording=false;root.imagePath="";root.audioPath="";root.analysisContext="";taskInput.text="";root.inputError="";root.memoryError="";root.closeSettings()}else if(root.visible)root.syncMessages()}}
    onVisibleChanged:{if(!visible&&ownsRecording){App.speech.cancelRecording();ownsRecording=false}if(visible)root.syncMessages()}
    Component.onCompleted:if(visible)root.syncMessages()
    function openSettings(){agentSettings.open()}
    function closeSettings(){agentSettings.close();agentFilePicker.close();agentImagePreview.close();toolDialog.close()}
    function chooseFile(kind){agentFilePicker.openFor(kind)}
    function closeTools(){toolDialog.close()}
    Dialog{id:toolDialog;title:"智能体工具检查";modal:true;width:Math.min(root.width-24,650);height:Math.min(root.height-24,460);anchors.centerIn:parent;standardButtons:Dialog.Close
        contentItem:ScrollView{id:toolScroll;clip:true;contentWidth:availableWidth
            ColumnLayout{width:toolScroll.availableWidth;spacing:12
                Repeater{model:root.tools
                    ColumnLayout{Layout.fillWidth:true;spacing:4
                        Text{Layout.fillWidth:true;text:(modelData.ready?"✓ ":"○ ")+modelData.id;wrapMode:Text.Wrap;font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:modelData.ready?Colors.success:Colors.textSecondary}
                        Text{Layout.fillWidth:true;text:modelData.description+" · "+modelData.state;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                    }
                }
            }
        }
    }
    function send(){inputError="";App.agent.submit(taskInput.text,imagePath,audioPath,analysisContext);if(App.agent.busy){taskInput.text="";imagePath="";audioPath="";analysisContext="";chat.followTail=true;root.syncMessages();followAgent.start()}else inputError=App.agent.status}
    function exportChat(){var rows=[];for(var row of App.agent.messages)if(!row.streaming)rows.push({role:row.role==="user"?"user":"assistant",text:row.body});exporter.open(rows)}
    ConversationExporter{id:exporter;mode:"text"}
    BoardFilePicker {id:agentFilePicker;onPicked:function(path){if(kind==="image"){root.analysisContext="";root.imagePath=path;}else root.audioPath=path;root.inputError=""}}
    AssistantDialog {id:agentImagePreview;title:"图片";anchors.centerIn:parent;width:Math.min(root.width-16,800);height:Math.min(root.height-16,600);contentItem:Image {id:previewImage;fillMode:Image.PreserveAspectFit;asynchronous:true}}
    AssistantDialog {
        id:agentSettings;objectName:"agentSettingsDialog";title:"智能体设置";modal:true;anchors.centerIn:parent
        width:Math.min(root.width-24,540);height:Math.min(root.height-24,610)
        contentItem:ScrollView {id:agentSettingsScroll;clip:true;contentWidth:availableWidth
        ColumnLayout {
            width:agentSettingsScroll.availableWidth;spacing:10
            AppCheckBox {objectName:"agentMemoryToggle";text:"记忆（当前会话）";height:30;checked:App.workspace.memoryPreferences.agent===true;enabled:App.session.loggedIn&&!App.agent.busy&&App.session.permissions.inspect;onToggled:{var r=App.workspace.setMemoryEnabled("agent",checked);root.memoryError=r.ok?"":r.error;checked=Qt.binding(function(){return App.workspace.memoryPreferences.agent===true})}ToolTip.visible:hovered;ToolTip.text:"用于任务规划和最终回答；与 Graph RAG 分别控制。"}
            Text {visible:root.memoryError.length>0;Layout.fillWidth:true;text:root.memoryError;color:Colors.danger;font.pixelSize:Typography.body;wrapMode:Text.Wrap}
            Text {text:"允许使用的工具";font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text}
            Repeater {model:App.agent.auditTools()
                AppCheckBox {required property var modelData;objectName:"agentTool_"+modelData.id;Layout.fillWidth:true;text:modelData.description+(modelData.id==="graph_search"?" · Graph RAG":"");checked:App.agent.enabledTools.indexOf(modelData.id)>=0;enabled:App.session.loggedIn&&!App.agent.busy;onToggled:{root.setTool(modelData.id,checked);checked=Qt.binding(function(){return App.agent.enabledTools.indexOf(modelData.id)>=0})}}
            }
            TouchButton{objectName:"checkAgentTools";text:"工具检查";autoWidth:true;height:30;variant:"ghost";onClicked:{root.tools=App.agent.auditTools();toolDialog.open()}}
            AppComboBox{id:historyChoice;Layout.fillWidth:true;height:34;model:App.agent.conversations;textRole:"title";displayText:currentIndex<0?"历史对话":currentText;enabled:!App.agent.busy;onActivated:App.agent.openConversation(App.agent.conversations[currentIndex].id)}
            TouchButton{text:"新对话";autoWidth:true;height:30;variant:"ghost";enabled:!App.agent.busy;onClicked:{App.agent.newConversation();root.imagePath="";root.audioPath="";root.analysisContext=""}}
            TouchButton{text:"导出";autoWidth:true;height:30;variant:"ghost";enabled:App.agent.messages.length>0&&!App.agent.busy;onClicked:root.exportChat()}
            TouchButton{text:"停止任务";autoWidth:true;height:30;variant:"ghost";enabled:App.agent.busy;onClicked:App.agent.cancel()}
            Text {text:"本账号存储目录";font.pixelSize:Typography.body;color:Colors.text}
            TextEdit {Layout.fillWidth:true;readOnly:true;selectByMouse:true;wrapMode:TextEdit.WrapAnywhere;text:App.workspace.assistantPaths.user||"";color:Colors.textSecondary;font.pixelSize:Typography.body}
        }
        }
    }
    Menu {
        id:attachmentMenu
        MenuItem{text:"添加图片";enabled:!App.agent.busy;onTriggered:root.chooseFile("image")}
        MenuItem{text:"最近拍照";enabled:!App.agent.busy&&App.camera.lastPhoto.length>0;onTriggered:{root.analysisContext="";root.imagePath=App.camera.photoForQuestion()}}
        MenuItem{text:"添加录音";enabled:!App.agent.busy;onTriggered:root.chooseFile("audio")}
        MenuItem{text:"录音作为任务附件";enabled:!App.agent.busy&&!App.ai.busy&&!App.speech.recording&&!App.dictation.active;onTriggered:root.recordAudio()}
        MenuItem{text:"工具与记忆";onTriggered:root.openSettings()}
        MenuItem{text:"新对话";enabled:!App.agent.busy;onTriggered:{App.agent.newConversation();root.imagePath="";root.audioPath="";root.analysisContext=""}}
        MenuItem{text:"关联本次巡检";enabled:!App.agent.busy&&App.agent.messages.length>0;onTriggered:{var answer="";for(var row of App.agent.messages)if(row.role==="assistant"&&row.kind==="text"&&!row.streaming)answer=row.body;if(answer)App.nav.push("product",{section:"evidence",kind:"ai",content:answer,metadata:{agent_conversation_id:App.agent.conversationId,graph_evidence:App.agent.graphEvidence}})}}
        MenuItem{text:"导出对话";enabled:!App.agent.busy;onTriggered:root.exportChat()}
    }
    ColumnLayout {
        anchors.fill:parent;anchors.margins:12;spacing:8
        RowLayout {
            Layout.fillWidth:true;Layout.fillHeight:true;spacing:10
            Rectangle {
                Layout.fillWidth:true;Layout.fillHeight:true;radius:10;color:"transparent"
                ListView {
                    id:chat;objectName:"agentConversation";anchors.fill:parent;anchors.margins:12;model:displayMessages;spacing:10;clip:true;reuseItems:true;cacheBuffer:320
                    property bool followTail:true
                    onContentHeightChanged:if(followTail&&!followAgent.running)followAgent.start()
                    onMovementStarted:followTail=false
                    onMovementEnded:followTail=atYEnd
                    ScrollBar.vertical:ScrollBar{onPressedChanged:if(pressed)chat.followTail=false;else if(chat.atYEnd)chat.followTail=true}
                    delegate:ChatBubble {
                        width:chat.width;outgoing:model.role==="user";kind:model.kind==="error"?"error":"text";body:model.body
                        imagePath:model.imagePath;audioPath:model.audioPath;transcribable:false;systemEvidenceJson:model.systemEvidenceJson
                        caption:model.role==="user"?App.session.userName:model.kind==="plan"?"执行计划 · Qwen3.5":model.kind==="tool"?"工具观测":"巡检智能体"
                        streaming:model.streaming;showActions:model.role==="assistant"&&model.kind==="text"&&!model.streaming;allowEdit:false
                        onReadRequested:App.speech.speak(body)
                        onPlayRequested:if(active)App.speech.stopSpeech();else App.speech.playAudio(audioPath)
                        onImageRequested:function(path){previewImage.source="file://"+path;agentImagePreview.open()}
                    }
                    onCountChanged:if(followTail&&!followAgent.running)followAgent.start()
                }
            }
            Rectangle {
                visible:false;Layout.preferredWidth:240;Layout.fillHeight:true;radius:10;color:Colors.panelAlt
                ScrollView{id:stepScroll;anchors.fill:parent;anchors.margins:12;contentWidth:availableWidth;clip:true
                    ColumnLayout{width:stepScroll.availableWidth;spacing:12
                        Text{text:"执行流程";font.pixelSize:Typography.body;font.weight:Font.DemiBold;color:Colors.text}
                        Repeater{model:App.agent.steps
                            Rectangle{Layout.fillWidth:true;Layout.preferredHeight:stepText.implicitHeight+28;radius:9;color:Colors.panel;border.color:modelData.state==="running"?Colors.normal:Colors.border
                                Text{id:stepText;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:12;text:(modelData.state==="done"?"✓ ":modelData.state==="running"?"● ":"○ ")+(index+1)+". "+modelData.instruction+"\n"+modelData.tool;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:modelData.state==="done"?Colors.success:Colors.text}
                            }
                        }
                    }
                }
            }
        }
        Text {Layout.fillWidth:true;visible:root.inputError.length>0;text:root.inputError;wrapMode:Text.Wrap;color:Colors.danger;font.pixelSize:Typography.body}
        Text {Layout.fillWidth:true;visible:App.agent.busy||root.ownsRecording;text:root.ownsRecording?"录音中 · "+App.speech.seconds+" s":App.agent.status;elide:Text.ElideRight;color:Colors.textSecondary;font.pixelSize:Typography.body}
        AssistantComposer {
            id:taskInput;inputName:"agentTaskInput";objectName:"agentComposer"
            Layout.fillWidth:true;Layout.preferredHeight:implicitHeight;Layout.maximumWidth:1000;Layout.alignment:Qt.AlignHCenter
            placeholder:"发消息，或添加图片、录音…";busy:App.agent.busy;recording:App.dictation.active||root.ownsRecording;attachmentCanSend:true
            searchAvailable:true;searchEnabled:App.workspace.searchPreferences.agent===true
            onSearchToggled:function(enabled){var r=App.workspace.setSearchEnabled("agent",enabled);root.inputError=r.ok?"":r.error}
            imagePath:root.imagePath;attachmentText:root.analysisContext?"已选 "+root.analysisChartCount+" 张分析图":root.audioPath.split("/").pop()
            canSend:!App.agent.busy&&!App.ai.busy&&!App.dictation.active;canVoice:!App.agent.busy&&!App.ai.busy&&!App.dictation.active&&!App.speech.speechBusy
            onSendRequested:root.send()
            onStopRequested:if(App.agent.busy)App.agent.cancel();else if(root.ownsRecording)App.speech.stopRecording();else App.dictation.cancel()
            onVoiceRequested:App.input.open(taskInput.editor,true)
            onAttachmentsRequested:attachmentMenu.popup()
            onRemoveAttachment:{root.imagePath="";root.audioPath="";root.analysisContext=""}
        }
    }
}
