import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Item {
    id: editor
    anchors.fill:parent;visible:false;z:200
    property string kind:"task"
    property string editingId:""
    property string heading:""
    property string nameText:""
    property string noteText:""
    property string plannedText:""
    property string message:""
    property var assets:[]
    signal saved(string id)
    function open(item) {
        item=item||{}
        assets=(kind!=="task"&&!App.session.permissions.assign)?App.assets.workAssets():App.assets.assets();editingId=item.id||"";message=""
        heading=(editingId?"编辑":"新增")+(kind==="task"?"巡检任务":kind==="defect"?"缺陷":"巡检档案")
        nameText=kind==="record"?(item.result||"正常"):(item.name||item.title||"")
        noteText=item.description||item.answer||""
        plannedText=item.plannedAt?String(item.plannedAt).replace("T"," ").slice(0,16):Qt.formatDateTime(new Date(),"yyyy-MM-dd HH:mm")
        assetChoice.currentIndex=assets.length?0:-1
        for(var i=0;i<assets.length;i++)if(assets[i].id===item.assetId)assetChoice.currentIndex=i
        priorityChoice.currentIndex=Math.max(0,["low","medium","high"].indexOf(item.priority||"medium"))
        visible=true
    }
    function submit() {
        var asset=assetChoice.currentIndex>=0?assets[assetChoice.currentIndex].id:""
        var result=kind==="task"?App.inspection.saveTask(editingId,nameText,asset,plannedText)
            :kind==="defect"?App.defects.saveDefect(editingId,asset,nameText,noteText,["low","medium","high"][priorityChoice.currentIndex])
            :App.inspection.saveArchive(editingId,asset,nameText,noteText)
        if(!result.ok){message=result.error;return}
        visible=false;saved(result.id||editingId)
    }
    Rectangle {anchors.fill:parent;color:"#88000000";MouseArea{anchors.fill:parent;onClicked:editor.visible=false}}
    Rectangle {
        anchors.centerIn:parent;width:Math.min(600,parent.width-24);height:Math.min(520,parent.height-24);radius:12;color:Colors.panel;border.color:Colors.border
        MouseArea {anchors.fill:parent}
        ColumnLayout {
            anchors.fill:parent;anchors.margins:20;spacing:14
            Text {text:editor.heading;font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}
            ScrollView {
                id:formScroll;Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
                ColumnLayout {
                    width:formScroll.availableWidth;spacing:14
                    LabeledField {Layout.fillWidth:true;label:editor.kind==="record"?"巡检结果":editor.kind==="task"?"任务名称":"缺陷标题";text:editor.nameText;onValueEdited:editor.nameText=value}
                    RowLayout {
                        Layout.fillWidth:true
                        Text {text:"关联设备";Layout.preferredWidth:120;font.pixelSize:Typography.body;color:Colors.textSecondary}
                        AppComboBox {id:assetChoice;objectName:editor.objectName+"Asset";Layout.fillWidth:true;model:editor.assets;textRole:"name";enabled:!editor.editingId||editor.kind==="task";displayText:currentIndex<0?"请先新增设备":currentText}
                    }
                    LabeledField {visible:editor.kind==="task";Layout.fillWidth:true;label:"计划时间";text:editor.plannedText;placeholder:"yyyy-MM-dd HH:mm";onValueEdited:editor.plannedText=value}
                    RowLayout {
                        visible:editor.kind==="defect";Layout.fillWidth:true
                        Text {text:"优先级";Layout.preferredWidth:120;font.pixelSize:Typography.body;color:Colors.textSecondary}
                        AppComboBox {id:priorityChoice;Layout.fillWidth:true;model:["低","中","高"]}
                    }
                    Text {visible:editor.kind!=="task";text:editor.kind==="record"?"巡检说明":"缺陷描述";font.pixelSize:Typography.body;color:Colors.textSecondary}
                    ScrollView {
                        visible:editor.kind!=="task";Layout.fillWidth:true;Layout.preferredHeight:120;clip:true
                        TextArea {objectName:editor.objectName+"Notes";text:editor.noteText;onTextChanged:editor.noteText=text;wrapMode:TextEdit.Wrap;selectByMouse:true;font.pixelSize:Typography.body;color:Colors.text;background:Rectangle{radius:8;color:Colors.panelAlt;border.color:Colors.border}}
                    }
                    Text {visible:editor.message.length>0;Layout.fillWidth:true;text:editor.message;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.danger}
                }
            }
            RowLayout {Layout.fillWidth:true;Item{Layout.fillWidth:true}TouchButton{text:"取消";autoWidth:true;variant:"ghost";onClicked:editor.visible=false}TouchButton{objectName:editor.objectName+"Save";text:"保存";autoWidth:true;variant:"primary";onClicked:editor.submit()}}
        }
    }
}
