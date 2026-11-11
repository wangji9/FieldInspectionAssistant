import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Item {
    id: root;objectName:"recordingManager"
    property var catalog:App.analysis.recordingPage
    property string searchText:""
    property bool trash:false
    property int originIndex:0
    property string feedback:""
    property bool failed:false
    property var selected:({})
    property var events:[]
    property bool editing:false
    property string dialogError:""
    readonly property bool busy:App.analysis.running||App.analysis.recordingBusy||App.analysis.exportBusy
    signal importRequested()
    signal analyzeRequested()
    function query(page){App.analysis.queryRecordings({search:searchText,trash:trash,origin:["","sensor","imported"][originIndex]},page||0)}
    function showRecord(id,edit){
        var result=App.analysis.recordingDetail(id)
        if(!result.ok){feedback=result.error;failed=true;return}
        selected=result.record;events=result.events;editing=edit;dialogError=""
        nameInput.text=selected.display_name;notesInput.text=selected.notes||"";recordDialog.open()
    }
    function requestDelete(id){
        var r=App.analysis.recordingDetail(id)
        if(!r.ok){feedback=r.error;failed=true;return}
        selected=r.record;dialogError="";deleteDialog.open()
    }
    function applyResult(result,message){
        failed=!result.ok;feedback=result.ok?message:result.error;return result.ok
    }
    function restore(id){var r=App.analysis.recordingDetail(id);if(!r.ok){applyResult(r,"");return}applyResult(App.analysis.restoreRecording(id,r.record.revision),"录音已恢复")}
    onVisibleChanged:if(visible&&App.session.loggedIn)query(0)
    Connections {target:App.session;function onSessionChanged(){if(!App.session.loggedIn){searchText="";searchInput.text="";trash=false;originIndex=0;originChoice.currentIndex=0;selected={};events=[];feedback="";recordDialog.close();deleteDialog.close()}}}
    Connections {target:App.analysis;function onRecordingOperationFinished(ok,message){root.failed=!ok;root.feedback=message}}
    Timer {id:searchTimer;interval:220;onTriggered:root.query(0)}
    ColumnLayout {
        anchors.fill:parent;spacing:10
        RowLayout {
            Layout.fillWidth:true;spacing:8
            Text {text:(root.trash?"回收站":"原始录音")+" · "+(root.catalog.total||0)+" 条";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text;Layout.fillWidth:true;elide:Text.ElideRight}
            TouchButton {objectName:"recordingImport";text:App.analysis.recordingBusy?"正在读取…":"导入 WAV";height:32;autoWidth:true;variant:"tonal";enabled:!root.busy&&App.session.permissions.inspect;onClicked:root.importRequested()}
            ToolButton {objectName:"recordingRefresh";text:"↻";font.pixelSize:22;width:32;height:32;Accessible.name:"刷新录音列表";onClicked:root.query(root.catalog.page||0);background:Rectangle{radius:8;color:parent.hovered?Colors.infoDark:"transparent"}}
        }
        RowLayout {
            Layout.fillWidth:true;spacing:8
            TextField {id:searchInput;objectName:"recordingSearch";Layout.fillWidth:true;Layout.minimumWidth:70;height:36;placeholderText:"搜索名称、备注或日期";selectByMouse:true;font.pixelSize:Typography.body;color:Colors.text;onTextEdited:{root.searchText=text;searchTimer.restart()}onAccepted:{searchTimer.stop();root.searchText=text;root.query(0)}background:Rectangle{radius:9;color:Colors.panel;border.color:searchInput.activeFocus?Colors.normal:Colors.border}}
            AppComboBox {id:originChoice;objectName:"recordingOrigin";Layout.preferredWidth:root.width<600?100:126;height:36;model:["全部来源","传感器实采","导入录音"];onActivated:{root.originIndex=currentIndex;root.query(0)}}
            ToolButton {objectName:"recordingTrashToggle";text:root.trash?"返回列表":"回收站";font.pixelSize:Typography.body;height:36;onClicked:{root.trash=!root.trash;root.query(0)}background:Rectangle{radius:9;color:root.trash?Colors.infoDark:"transparent"}}
        }
        Text {objectName:"recordingFeedback";visible:text.length>0;Layout.fillWidth:true;text:root.feedback||(root.catalog.ok===false?root.catalog.error:"");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:root.failed||root.catalog.ok===false?Colors.danger:Colors.success}
        ListView {
            id:list;objectName:"recordingList";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:8;model:App.analysis.recordings
            ScrollBar.vertical:ScrollBar{}
            delegate:Rectangle {
                required property var modelData
                width:list.width;height:root.width<620?100:86;radius:12;color:Colors.panel;border.color:Colors.border
                RowLayout {
                    anchors.fill:parent;anchors.margins:12;spacing:8
                    ColumnLayout {
                        Layout.fillWidth:true;spacing:5
                        Text {Layout.fillWidth:true;text:modelData.display_name;font.pixelSize:Typography.body;font.weight:Font.Medium;color:Colors.text;elide:Text.ElideRight}
                        Text {Layout.fillWidth:true;text:Qt.formatDateTime(new Date(modelData.started*1000),"yyyy-MM-dd HH:mm:ss")+" · "+modelData.channels+" 通道 · "+Math.round(modelData.duration)+" s";font.pixelSize:Typography.body;color:Colors.textSecondary;elide:Text.ElideRight}
                        Text {visible:!modelData.file_exists||!modelData.complete;Layout.fillWidth:true;text:!modelData.file_exists?"原始文件缺失":"尚未完成";color:Colors.warning;font.pixelSize:Typography.body;elide:Text.ElideRight}
                    }
                    TouchButton {text:root.trash?"恢复":"分析";height:32;autoWidth:true;variant:"tonal";enabled:!root.busy&&(root.trash?modelData.can_edit:!!modelData.complete&&!!modelData.file_exists);onClicked:{if(root.trash)root.restore(modelData.id);else{App.analysis.selectRecording(modelData.id);root.analyzeRequested()}}}
                    ToolButton {
                        text:"⋯";font.pixelSize:22;width:32;height:32;Accessible.name:"录音操作";onClicked:actions.open()
                        background:Rectangle{radius:8;color:parent.hovered?Colors.infoDark:"transparent"}
                        Menu {
                            id:actions
                            MenuItem {text:"详情";onTriggered:root.showRecord(modelData.id,false)}
                            MenuItem {text:"编辑信息";visible:!root.trash;enabled:modelData.can_edit&&!root.busy;onTriggered:root.showRecord(modelData.id,true)}
                            MenuItem {text:"移入回收站";visible:!root.trash;enabled:modelData.can_edit&&!root.busy;onTriggered:root.requestDelete(modelData.id)}
                        }
                    }
                }
            }
            Text {anchors.centerIn:parent;visible:list.count===0;text:root.trash?"回收站为空":root.searchText.length||root.originIndex?"没有符合条件的录音":"暂无录音";color:Colors.textSecondary;font.pixelSize:Typography.body}
        }
        RowLayout {
            Layout.fillWidth:true;spacing:12
            Item {Layout.fillWidth:true}
            TouchButton {objectName:"recordingPrevious";text:"上一页";autoWidth:true;height:30;variant:"ghost";enabled:(root.catalog.page||0)>0;onClicked:root.query(root.catalog.page-1)}
            Text {text:((root.catalog.page||0)+1)+" / "+Math.max(1,Math.ceil((root.catalog.total||0)/20));color:Colors.textSecondary;font.pixelSize:Typography.body}
            TouchButton {objectName:"recordingNext";text:"下一页";autoWidth:true;height:30;variant:"ghost";enabled:((root.catalog.page||0)+1)*20<(root.catalog.total||0);onClicked:root.query(root.catalog.page+1)}
            Item {Layout.fillWidth:true}
        }
    }
    AssistantDialog {
        id:recordDialog;objectName:"recordingDetails";anchors.centerIn:parent;width:Math.min(root.width-16,600);height:Math.min(root.height-16,570);title:root.editing?"编辑录音信息":"录音详情"
        contentItem:ScrollView {
            id:detailsScroll;contentWidth:availableWidth;clip:true
            ColumnLayout {
                width:detailsScroll.availableWidth;spacing:12
                Text {text:"名称";color:Colors.textSecondary;font.pixelSize:Typography.body}
                TextField {id:nameInput;objectName:"recordingName";Layout.fillWidth:true;readOnly:!root.editing;maximumLength:120;selectByMouse:true;font.pixelSize:Typography.body;color:Colors.text;background:Rectangle{radius:8;color:root.editing?Colors.panel:Colors.panelAlt;border.color:nameInput.activeFocus&&root.editing?Colors.normal:Colors.border}}
                Text {Layout.fillWidth:true;text:(root.selected.source||"")+"\n"+Qt.formatDateTime(new Date((root.selected.started||0)*1000),"yyyy-MM-dd HH:mm:ss")+"\n"+(root.selected.fs||0)+" Hz · "+(root.selected.channels||0)+" 通道 · "+Math.round(root.selected.duration||0)+" s";wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                Text {text:"备注";color:Colors.textSecondary;font.pixelSize:Typography.body}
                TextArea {id:notesInput;objectName:"recordingNotes";Layout.fillWidth:true;Layout.preferredHeight:100;readOnly:!root.editing;selectByMouse:true;wrapMode:TextEdit.Wrap;font.pixelSize:Typography.body;color:Colors.text;background:Rectangle{radius:8;color:Colors.panelAlt;border.color:Colors.border}}
                Text {Layout.fillWidth:true;text:root.selected.path||"";wrapMode:Text.WrapAnywhere;font.pixelSize:Typography.body;color:Colors.textSecondary}
                Text {Layout.fillWidth:true;text:root.selected.file_exists?"原始文件 · "+((root.selected.bytes||0)/1048576).toFixed(2)+" MB":"原始文件缺失";font.pixelSize:Typography.body;color:root.selected.file_exists?Colors.textSecondary:Colors.warning}
                Repeater {model:root.events;Text {Layout.fillWidth:true;text:({"capture":"开始采集","import":"导入","update":"编辑信息","delete":"移入回收站","restore":"恢复"}[modelData.action]||modelData.action)+" · "+Qt.formatDateTime(new Date(modelData.at),"yyyy-MM-dd HH:mm:ss");font.pixelSize:Typography.body;color:Colors.textSecondary;elide:Text.ElideRight}}
                Text {visible:text.length>0;Layout.fillWidth:true;text:root.dialogError;wrapMode:Text.Wrap;color:Colors.danger;font.pixelSize:Typography.body}
            }
        }
        footer:Item {
            implicitHeight:60
            Row {anchors.right:parent.right;anchors.rightMargin:20;anchors.verticalCenter:parent.verticalCenter;spacing:8
                TouchButton {text:root.editing?"取消":"关闭";height:34;autoWidth:true;variant:"ghost";onClicked:recordDialog.close()}
                TouchButton {objectName:"recordingSave";visible:root.editing;text:"保存";height:34;autoWidth:true;variant:"primary";enabled:!root.busy;onClicked:{var r=App.analysis.updateRecording(root.selected.id,nameInput.text,notesInput.text,root.selected.revision);if(!r.ok){root.dialogError=r.error;return}root.applyResult(r,"录音信息已保存");recordDialog.close()}}
            }
        }
    }
    AssistantDialog {
        id:deleteDialog;objectName:"recordingDeleteDialog";anchors.centerIn:parent;width:Math.min(root.width-16,440);height:Math.min(root.height-16,300);title:"移入回收站"
        contentItem:ColumnLayout {
            Text {Layout.fillWidth:true;text:root.selected.display_name||"";color:Colors.text;font.pixelSize:Typography.body;wrapMode:Text.Wrap}
            Text {Layout.fillWidth:true;text:"可以从回收站恢复，原始音频文件将保留。";color:Colors.textSecondary;font.pixelSize:Typography.body;wrapMode:Text.Wrap}
            Text {visible:text.length>0;Layout.fillWidth:true;text:root.dialogError;wrapMode:Text.Wrap;color:Colors.danger;font.pixelSize:Typography.body}
            Item {Layout.fillHeight:true}
        }
        footer:Item {implicitHeight:60
            Row {anchors.right:parent.right;anchors.rightMargin:20;anchors.verticalCenter:parent.verticalCenter;spacing:8
                TouchButton {text:"取消";height:34;autoWidth:true;variant:"ghost";onClicked:deleteDialog.close()}
                TouchButton {objectName:"recordingConfirmDelete";text:"移入回收站";height:34;autoWidth:true;variant:"danger";enabled:!root.busy;onClicked:{var r=App.analysis.deleteRecording(root.selected.id,root.selected.revision);if(!r.ok){root.dialogError=r.error;return}root.applyResult(r,"录音已移入回收站");deleteDialog.close()}}
            }
        }
    }
}
