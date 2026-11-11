import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import "../theme"
Item {
 id:root;objectName:"voiceAssistantPane"
 readonly property var voice:App.voiceAssistant
 property string displayedOwner:""
 property var historyRows:[]
 property int historyPage:0
 property int historyTotal:0
 property string historyQuery:""
 property string historyError:""
 readonly property bool wide:width>930
 function openSettings(){settingsWindow.show();settingsWindow.raise();settingsWindow.requestActivate()}
 function openHistory(){reloadHistory(0);historyWindow.show();historyWindow.raise();historyWindow.requestActivate()}
 function reloadHistory(page){var r=App.workspace.query("conversations",{mode:"voice",keyword:historyQuery},Math.max(0,page),20);historyError=r.ok?"":r.error;if(r.ok){historyRows=r.rows;historyPage=r.page;historyTotal=r.total}}
 function exportConversation(){var rows=voice.exportRows();if(rows.length)exporter.open(rows)}
 onVisibleChanged:if(visible&&App.session.loggedIn)Qt.callLater(function(){if(root.visible)voice.enable()})
 Component.onCompleted:if(visible&&App.session.loggedIn)Qt.callLater(function(){if(root.visible)voice.enable()})
 Connections{target:App.session;function onSessionChanged(){var owner=App.session.userId,changed=owner!==root.displayedOwner;root.displayedOwner=owner;root.historyRows=[];root.historyQuery="";settingsWindow.close();historyWindow.close();if(changed&&root.visible&&App.session.loggedIn)Qt.callLater(function(){if(root.visible)voice.enable()})}}
 ConversationExporter{id:exporter;mode:"voice"}
 Window {
  id:settingsWindow;objectName:"voiceAssistantSettings";title:"语音助手设置";visible:false;color:Colors.background
  readonly property int fixedWidth:Math.min(560,Screen.desktopAvailableWidth-32);readonly property int fixedHeight:Math.min(620,Screen.desktopAvailableHeight-40)
  width:fixedWidth;height:fixedHeight;minimumWidth:fixedWidth;maximumWidth:fixedWidth;minimumHeight:fixedHeight;maximumHeight:fixedHeight;modality:Qt.WindowModal;transientParent:root.Window.window;flags:Qt.Dialog|Qt.WindowTitleHint|Qt.WindowSystemMenuHint|Qt.WindowCloseButtonHint
  ColumnLayout{anchors.fill:parent;anchors.margins:22;spacing:14
   Text{text:"语音助手";font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}
   ScrollView{id:settingsScroll;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;contentWidth:availableWidth
    ColumnLayout{width:settingsScroll.availableWidth;spacing:16
     Text{Layout.fillWidth:true;text:"进入后自动倾听，停顿后发送；说“结束对话”即可结束。";wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
     AppCheckBox{objectName:"voiceContinuousToggle";text:"回答后继续听我说";checked:voice.continuous;onToggled:voice.continuous=checked}
     AppCheckBox{objectName:"voiceMemoryToggle";text:"记住当前对话的上下文";checked:App.workspace.memoryPreferences.voice===true;enabled:!voice.busy;onToggled:{App.workspace.setMemoryEnabled("voice",checked);checked=Qt.binding(function(){return App.workspace.memoryPreferences.voice===true})}}
     Text{text:"停顿多久后发送";font.pixelSize:Typography.body;color:Colors.text}
     AppComboBox{objectName:"voiceSilenceChoice";Layout.fillWidth:true;model:[{name:"快速 · 约 0.5 秒",value:500},{name:"均衡 · 约 0.7 秒",value:700},{name:"从容 · 约 1 秒",value:1000},{name:"较长停顿 · 约 1.4 秒",value:1400}];textRole:"name";currentIndex:model.findIndex(function(r){return r.value===voice.silenceMs});onActivated:voice.silenceMs=model[currentIndex].value}
     RowLayout{Layout.fillWidth:true;Text{text:"语音音量";font.pixelSize:Typography.body;color:Colors.text}Item{Layout.fillWidth:true}Text{text:voice.volume+"%";font.pixelSize:Typography.body;color:Colors.textSecondary}}
     Slider{objectName:"voiceVolumeSlider";Layout.fillWidth:true;from:5;to:60;stepSize:1;value:voice.volume;onMoved:voice.volume=Math.round(value)}
     Text{Layout.fillWidth:true;text:"默认低音量。播报期间可点按圆球打断，再继续提问。其他采集任务使用麦克风时会自动暂停。";wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
     Flow{Layout.fillWidth:true;spacing:8
      TouchButton{text:"新对话";autoWidth:true;height:34;enabled:!voice.busy;onClicked:{voice.newConversation();settingsWindow.close()}}
      TouchButton{text:"历史对话";autoWidth:true;height:34;enabled:!voice.busy;onClicked:{settingsWindow.close();root.openHistory()}}
      TouchButton{text:"导出对话";autoWidth:true;height:34;enabled:!voice.busy;onClicked:{settingsWindow.close();root.exportConversation()}}
     }
     Text{visible:Object.keys(voice.metrics).length>0;text:"本轮响应";font.pixelSize:Typography.body;color:Colors.text}
     Text{visible:Object.keys(voice.metrics).length>0;Layout.fillWidth:true;text:"识别 "+(voice.metrics.asr_ms===undefined?"—":Math.round(voice.metrics.asr_ms)+" ms")+"　首字 "+(voice.metrics.first_text_ms===undefined?"—":Math.round(voice.metrics.first_text_ms)+" ms")+"\n播放启动 "+(voice.metrics.first_audio_ms===undefined?"—":Math.round(voice.metrics.first_audio_ms)+" ms");font.pixelSize:Typography.body;color:Colors.textSecondary;wrapMode:Text.Wrap}
    }
   }
   TouchButton{Layout.alignment:Qt.AlignRight;text:"完成";height:34;onClicked:settingsWindow.close()}
  }
 }
 Window {
  id:historyWindow;objectName:"voiceAssistantHistory";title:"语音助手记录";visible:false;color:Colors.background
  readonly property int fixedWidth:Math.min(640,Screen.desktopAvailableWidth-32);readonly property int fixedHeight:Math.min(580,Screen.desktopAvailableHeight-40)
  width:fixedWidth;height:fixedHeight;minimumWidth:fixedWidth;maximumWidth:fixedWidth;minimumHeight:fixedHeight;maximumHeight:fixedHeight;modality:Qt.WindowModal;transientParent:root.Window.window;flags:Qt.Dialog|Qt.WindowTitleHint|Qt.WindowSystemMenuHint|Qt.WindowCloseButtonHint
  ColumnLayout{anchors.fill:parent;anchors.margins:18;spacing:10
   Text{text:"历史对话";font.pixelSize:21;color:Colors.text}
   TextField{Layout.fillWidth:true;placeholderText:"搜索我的语音助手";selectByMouse:true;onTextEdited:{root.historyQuery=text;root.reloadHistory(0)}}
   Text{visible:!!root.historyError;Layout.fillWidth:true;text:root.historyError;color:Colors.danger;wrapMode:Text.Wrap;font.pixelSize:Typography.body}
   ListView{id:historyList;Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:6;model:root.historyRows;delegate:ItemDelegate{required property var modelData;width:historyList.width;height:64;text:modelData.title+"\n"+String(modelData.updated_at).replace("T"," ").replace("Z","");onClicked:{voice.restore(modelData.id);historyWindow.close()}}}
   RowLayout{Layout.fillWidth:true;Text{text:root.historyTotal+" 个会话";font.pixelSize:Typography.body;color:Colors.textSecondary}Item{Layout.fillWidth:true}TouchButton{text:"上一页";height:32;autoWidth:true;enabled:root.historyPage>0;onClicked:root.reloadHistory(root.historyPage-1)}TouchButton{text:"下一页";height:32;autoWidth:true;enabled:(root.historyPage+1)*20<root.historyTotal;onClicked:root.reloadHistory(root.historyPage+1)}TouchButton{text:"关闭";height:32;autoWidth:true;onClicked:historyWindow.close()}}
  }
 }
 GridLayout {
  anchors.fill:parent;columns:root.wide?2:1;columnSpacing:14;rowSpacing:10
  Rectangle{
   Layout.preferredWidth:root.wide?280:-1;Layout.fillWidth:!root.wide;Layout.fillHeight:root.wide;Layout.preferredHeight:root.wide?-1:225;Layout.minimumHeight:160;radius:22;color:"#EDF2FD";border.color:"#E3EAF6"
   ColumnLayout{anchors.fill:parent;anchors.margins:root.wide?20:10;spacing:root.wide?20:6
    Item{Layout.fillHeight:root.wide;Layout.fillWidth:true;Layout.minimumHeight:root.wide?150:94
     Rectangle{id:halo;anchors.centerIn:parent;width:orb.width+34;height:width;radius:width/2;color:voice.muted?"#DDE2EC":"#DCE6FF";opacity:.6}
     Rectangle{id:orb;objectName:"voiceOrb";anchors.centerIn:parent;width:(root.wide?146:86)+(voice.state==="listening"?voice.level*15:0);height:width;radius:width/2
      gradient:Gradient{GradientStop{position:0;color:voice.state==="error"?"#E795A2":voice.muted?"#B7C0D0":"#6D87FF"}GradientStop{position:1;color:voice.state==="speaking"?"#25BFCB":voice.muted?"#8792A7":"#475BE3"}}
      Behavior on width{NumberAnimation{duration:70}}
      SequentialAnimation on scale{running:voice.active&&!voice.muted;loops:Animation.Infinite;NumberAnimation{from:1;to:1.045;duration:1100;easing.type:Easing.InOutSine}NumberAnimation{from:1.045;to:1;duration:1100;easing.type:Easing.InOutSine}}
      Row{anchors.centerIn:parent;spacing:5;Repeater{model:5;Rectangle{width:5;height:voice.state==="listening"?12+voice.level*[18,32,44,30,16][index]:[12,24,34,24,12][index];radius:3;color:"white";anchors.verticalCenter:parent.verticalCenter;Behavior on height{NumberAnimation{duration:65}}}}}
      MouseArea{anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:voice.talk()}
      Accessible.role:Accessible.Button;Accessible.name:voice.busy?"打断并说话":"点击说话";Accessible.onPressAction:voice.talk()
     }
    }
    Text{Layout.fillWidth:true;text:voice.status;horizontalAlignment:Text.AlignHCenter;color:Colors.text;font.pixelSize:root.wide?23:18;font.weight:Font.Medium}
    Text{Layout.fillWidth:true;text:voice.error?voice.error:voice.active?(voice.muted?"已暂停麦克风":voice.state==="armed"?("说完停顿后，我会回应"):voice.state==="listening"?"说完稍作停顿，即可发送":"点按圆球可打断并提问"):"开始后直接说话，无需唤醒词";horizontalAlignment:Text.AlignHCenter;color:voice.error?Colors.danger:Colors.textSecondary;font.pixelSize:Typography.body;wrapMode:Text.Wrap;maximumLineCount:3;elide:Text.ElideRight}
    RowLayout{Layout.alignment:Qt.AlignHCenter;spacing:8
     TouchButton{objectName:"enableVoiceAssistant";text:voice.engaged?"结束对话":"开始对话";autoWidth:true;height:34;variant:voice.engaged?"secondary":"primary";enabled:!voice.busy||voice.engaged;onClicked:voice.engaged?voice.disable():voice.enable()}
     ToolButton{objectName:"muteVoiceAssistant";text:voice.muted?"恢复":"暂停";visible:voice.engaged;onClicked:voice.muted=!voice.muted;Accessible.name:voice.muted?"恢复麦克风":"暂停麦克风"}
    }
    Item{Layout.preferredHeight:root.wide?20:0}
   }
  }
  Rectangle{Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:140;color:Colors.panel;radius:20;border.color:Colors.border
   ColumnLayout{anchors.fill:parent;anchors.margins:root.wide?14:8;spacing:8
    Item{Layout.fillWidth:true;Layout.fillHeight:true
     ListView{id:chat;objectName:"voiceAssistantConversation";anchors.fill:parent;clip:true;spacing:4;model:voice.messages;cacheBuffer:450;reuseItems:true;property bool followTail:true
      onMovementStarted:followTail=false
      onMovementEnded:if(contentY+height>=contentHeight-60)followTail=true
      onContentHeightChanged:if(followTail)Qt.callLater(positionViewAtEnd)
      onCountChanged:if(followTail)Qt.callLater(positionViewAtEnd)
      ScrollBar.vertical:ScrollBar{}
      delegate:ChatBubble{body:model.body;outgoing:model.outgoing;kind:model.kind;status:model.status;streaming:model.streaming;audioPath:model.audioPath;systemEvidenceJson:model.systemEvidenceJson||"";width:chat.width;caption:outgoing?"我":"巡检助手";externalPlaying:audioPath.length>0&&voice.playbackPath===audioPath;actionsEnabled:!voice.busy;onPlayRequested:voice.replay(index);showActions:false}
     }
     Text{visible:chat.count===0;anchors.centerIn:parent;text:"今天需要我帮你什么？";font.pixelSize:root.wide?22:18;color:"#8C95AB"}
     TouchButton{visible:!chat.followTail;anchors.right:parent.right;anchors.bottom:parent.bottom;text:"回到最新";height:30;autoWidth:true;variant:"tonal";onClicked:{chat.followTail=true;chat.positionViewAtEnd()}}
    }
    AssistantComposer{id:composer;objectName:"voiceAssistantComposer";Layout.fillWidth:true;placeholder:"输入文字也可以对话…";attachmentsAvailable:false;busy:voice.busy;recording:false;canVoice:true;canSend:!voice.busy;onSendRequested:{voice.sendText(text);if(voice.busy)text=""}onVoiceRequested:voice.talk();onStopRequested:voice.interrupt()}
   }
  }
 }
}
