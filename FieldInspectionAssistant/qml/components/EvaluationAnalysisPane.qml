import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import "../theme"
Item {
 id:root;objectName:"evaluationAnalysisPane"
 property var panel
 property var formValues:({})
 property bool historyVisible:false
 property real previewZoom:1
 readonly property string recordingId:String(App.analysis.summary.recording||"")
 readonly property var currentReport:App.evaluation.report
 function editForm(){editorWindow.kind="form";formEditor.open("报告资料",[{key:"device",label:"设备名称（必填）"},{key:"device_code",label:"设备编号"},{key:"site",label:"现场位置（必填）"},{key:"point",label:"测点、距离与安装方式（必填）"},{key:"condition",label:"工况、转速及负载"},{key:"environment",label:"环境、背景及干扰"},{key:"instrument",label:"仪器型号、编号"},{key:"calibration",label:"校准、溯源与前后核查"},{key:"baseline",label:"可比健康基准及评价准则"},{key:"method",label:"测量方法、偏离及不确定度"},{key:"client",label:"委托方"},{key:"organization",label:"报告单位"},{key:"organization_address",label:"报告单位地址"}],formValues);editorWindow.show();editorWindow.raise();editorWindow.requestActivate()}
 function editConclusion(review){editorWindow.kind=review?"review":"revise";formEditor.open(review?"审核报告":"更正报告",[{key:"conclusion",label:review?"审核确认结论":"更正后的结论",type:"long"},{key:"note",label:review?"复核说明":"更正原因",type:"long"}],{conclusion:currentReport.editorial_conclusion||(currentReport.result||{}).conclusion||""});editorWindow.show();editorWindow.raise();editorWindow.requestActivate()}
 onVisibleChanged:if(visible&&App.session.loggedIn)App.evaluation.refresh()
 Connections{target:App.session;function onSessionChanged(){root.formValues={};editorWindow.close();root.historyVisible=false}}
 Window {
  id:editorWindow;objectName:"acousticReportEditor";property string kind:"form";visible:false;title:formEditor.title;color:Colors.background
  readonly property int fixedWidth:Math.min(700,Screen.desktopAvailableWidth-32)
  readonly property int fixedHeight:Math.min(680,Screen.desktopAvailableHeight-40)
  width:fixedWidth;height:fixedHeight;minimumWidth:fixedWidth;maximumWidth:fixedWidth;minimumHeight:fixedHeight;maximumHeight:fixedHeight
  modality:Qt.WindowModal;transientParent:root.Window.window;flags:Qt.Dialog|Qt.WindowTitleHint|Qt.WindowSystemMenuHint|Qt.WindowCloseButtonHint
  ProductEditor{id:formEditor;onVisibleChanged:if(!visible)editorWindow.close();onSubmitted:function(v){if(editorWindow.kind==="form"){if(!v.device||!v.site||!v.point){message="请填写设备、现场位置与测点";return}root.formValues=v}else if(editorWindow.kind==="review")App.evaluation.review(v.conclusion||"",v.note||"");else App.evaluation.revise(v.conclusion||"",v.note||"");editorWindow.close()}}
  onClosing:{formEditor.visible=false;if(App.input.targetWindow===editorWindow)App.input.cancel()}
 }
 FileDialog{id:pdfDialog;objectName:"acousticPdfSaveDialog";title:"保存声学评价报告 PDF";fileMode:FileDialog.SaveFile;defaultSuffix:"pdf";nameFilters:["PDF 报告 (*.pdf)"];onAccepted:App.evaluation.exportPdf(selectedFile.toString())}
 ColumnLayout {
  anchors.fill:parent;spacing:8
  Flow {Layout.fillWidth:true;spacing:6
   AppComboBox{objectName:"evaluationRecordingChoice";width:Math.min(330,root.width-8);height:32;model:App.analysis.recordings;textRole:"display_name";valueRole:"id";currentIndex:App.analysis.recordings.findIndex(function(r){return r.id===root.recordingId});displayText:currentIndex<0?"选择录音":currentText;enabled:!App.evaluation.busy&&!App.analysis.running&&!App.analysis.recordingBusy;onActivated:App.analysis.selectRecording(currentValue)}
   TouchButton{text:"录音管理";autoWidth:true;height:32;enabled:!App.evaluation.busy;onClicked:panel.tab=4}
   TouchButton{objectName:"evaluationForm";text:"报告资料";autoWidth:true;height:32;enabled:!App.evaluation.busy;onClicked:root.editForm()}
   TouchButton{objectName:"generateAcousticReport";text:"生成评价报告";autoWidth:true;height:32;variant:"primary";enabled:!App.evaluation.busy&&root.recordingId.length>0&&!App.analysis.running&&!App.analysis.recordingBusy;onClicked:{if(!root.formValues.device||!root.formValues.site||!root.formValues.point){root.editForm();return}App.evaluation.generateRecording(root.recordingId,root.formValues)}}
   TouchButton{visible:App.evaluation.busy;text:"停止";autoWidth:true;height:32;onClicked:App.evaluation.stop()}
   TouchButton{objectName:"acousticReportHistory";text:"历史与版本";autoWidth:true;height:32;onClicked:{root.historyVisible=!root.historyVisible;App.evaluation.refresh()}}
   TouchButton{objectName:"reviseAcousticReport";text:"更正";autoWidth:true;height:32;visible:!!currentReport.id&&currentReport.owner===App.session.userId;enabled:!App.evaluation.busy;onClicked:root.editConclusion(false)}
   TouchButton{objectName:"reviewAcousticReport";text:"审核";autoWidth:true;height:32;visible:!!currentReport.id&&App.session.can("review")&&currentReport.owner!==App.session.userId&&currentReport.status!=="reviewed";enabled:!App.evaluation.busy;onClicked:root.editConclusion(true)}
   TouchButton{objectName:"exportAcousticPdf";text:"导出 PDF";autoWidth:true;height:32;enabled:!App.evaluation.busy&&App.evaluation.pages.length>0;onClicked:pdfDialog.open()}
  }
  Text{visible:!!(App.evaluation.error||App.evaluation.status);Layout.fillWidth:true;text:App.evaluation.error||App.evaluation.status;color:App.evaluation.error?Colors.danger:Colors.textSecondary;wrapMode:Text.Wrap;font.pixelSize:Typography.body}
  RowLayout{Layout.fillWidth:true;Layout.fillHeight:true;spacing:8
   Rectangle{visible:root.historyVisible;Layout.preferredWidth:Math.min(300,root.width*.32);Layout.fillHeight:true;color:Colors.panel;border.color:Colors.border;radius:10
    ListView{id:historyList;objectName:"acousticHistoryList";anchors.fill:parent;anchors.margins:8;clip:true;spacing:6;model:App.evaluation.history
     delegate:ItemDelegate{required property var modelData;width:historyList.width;height:98;enabled:!App.evaluation.busy;contentItem:Text{text:modelData.device+"\n"+modelData.id+"\nV"+modelData.revision+" · "+(modelData.status==="reviewed"?"已审核":"待审核")+" · "+modelData.author;color:Colors.text;font.pixelSize:Typography.body;wrapMode:Text.Wrap;elide:Text.ElideRight}onClicked:App.evaluation.open(modelData.id,modelData.revision)}
    }
   }
   Rectangle{Layout.fillWidth:true;Layout.fillHeight:true;radius:10;color:"#E8EBF0";clip:true
    Text{visible:!App.evaluation.pages.length&&!App.evaluation.busy;anchors.centerIn:parent;text:"选择一条录音，生成完整评价报告";font.pixelSize:Typography.body;color:Colors.textSecondary}
    ColumnLayout{anchors.fill:parent;anchors.margins:8;spacing:5
     RowLayout{visible:App.evaluation.pages.length>0;Layout.fillWidth:true
      Text{Layout.fillWidth:true;text:(currentReport.id||"")+" / V"+(currentReport.revision||1)+" · "+(currentReport.status==="reviewed"?"已审核":"待审核");font.pixelSize:Typography.body;color:Colors.text;elide:Text.ElideMiddle}
      ToolButton{text:"−";width:30;height:28;onClicked:root.previewZoom=Math.max(.6,root.previewZoom-.2)}
      ToolButton{text:"适应";height:28;onClicked:root.previewZoom=1}
      ToolButton{text:"+";width:30;height:28;onClicked:root.previewZoom=Math.min(3,root.previewZoom+.2)}
     }
     Flickable{id:preview;objectName:"acousticReportPreview";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;contentWidth:Math.max(width,width*root.previewZoom);contentHeight:pageColumn.height;boundsBehavior:Flickable.StopAtBounds;ScrollBar.vertical:ScrollBar{} ScrollBar.horizontal:ScrollBar{}
      Column{id:pageColumn;width:preview.contentWidth;spacing:12
       Repeater{model:App.evaluation.pages;Image{required property string modelData;source:modelData;width:pageColumn.width;height:width*297/210;fillMode:Image.PreserveAspectFit;asynchronous:true;cache:false}}
      }
     }
    }
   }
  }
 }
}
