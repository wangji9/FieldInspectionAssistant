import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
Item {
    id:form;anchors.fill:parent;visible:false;z:230;objectName:"productEditor"
    property string title:""
    property var fields:[]
    property var values:({})
    property string attachmentLabel:""
    property bool attachmentRemovable:false
    signal attachmentRequested()
    signal attachmentRemoveRequested()
    property string message:""
    signal submitted(var values)
    function open(t,defs,data){title=t;fields=defs;values=JSON.parse(JSON.stringify(data||{}));message="";visible=true}
    function setValue(key,v){if(values[key]===v)return;var copy=Object.assign({},values);copy[key]=v;values=copy}
    function collect(){var copy=Object.assign({},values);for(var i=0;i<fieldRepeater.count;i++){var field=fieldRepeater.itemAt(i);if(field)copy[field.modelData.key]=field.readValue()}values=copy;return copy}
    Rectangle{anchors.fill:parent;color:"#770f172a";MouseArea{anchors.fill:parent;onClicked:form.visible=false}}
    Rectangle{
        anchors.centerIn:parent;width:Math.min(620,parent.width-24);height:Math.min(640,parent.height-24);color:Colors.panel;radius:16;border.color:Colors.border
        ColumnLayout{
            anchors.fill:parent;anchors.margins:20;spacing:12
            Text{text:form.title;font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}
            ScrollView{
                id:scroll;Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
                ColumnLayout{
                    width:scroll.availableWidth;spacing:12
                    Repeater{
                        id:fieldRepeater
                        model:form.fields
                        ColumnLayout{
                            id:field;required property var modelData;Layout.fillWidth:true;spacing:5
                            function readValue(){return modelData.type==="choice"?choice.currentValue:modelData.type==="long"?longInput.text:lineInput.text}
                            Text{text:field.modelData.label;font.pixelSize:Typography.body;color:Colors.textSecondary}
                            FieldInput{
                                id:lineInput
                                visible:field.modelData.type!=="choice"&&field.modelData.type!=="long";Layout.fillWidth:true;height:38
                                objectName:"productField_"+field.modelData.key
                                enabled:!field.modelData.readOnly;text:String(form.values[field.modelData.key]===undefined?(field.modelData.value||""):form.values[field.modelData.key])
                                placeholder:field.modelData.placeholder||"";echoMode:field.modelData.type==="password"?TextInput.Password:TextInput.Normal
                                onTextEdited:function(v){form.setValue(field.modelData.key,v)}
                            }
                            AppComboBox{
                                id:choice;visible:field.modelData.type==="choice";Layout.fillWidth:true;objectName:"productChoice_"+field.modelData.key
                                enabled:!field.modelData.readOnly;model:field.modelData.options||[];textRole:"name";valueRole:"id"
                                currentIndex:{var rows=field.modelData.options||[];for(var i=0;i<rows.length;i++)if(String(rows[i].id)===String(form.values[field.modelData.key]))return i;return -1}
                                displayText:currentIndex<0?"请选择":currentText
                                onActivated:form.setValue(field.modelData.key,currentValue)
                            }
                            ScrollView{
                                id:longScroll;visible:field.modelData.type==="long";Layout.fillWidth:true;Layout.preferredHeight:120;clip:true;contentWidth:availableWidth
                                TextArea{
                                    id:longInput
                                    objectName:"productLong_"+field.modelData.key;width:longScroll.availableWidth;implicitWidth:0;implicitHeight:120;height:Math.max(120,contentHeight+topPadding+bottomPadding);text:String(form.values[field.modelData.key]||"");wrapMode:TextEdit.Wrap;selectByMouse:true;font.pixelSize:Typography.body;color:Colors.text
                                    onTextChanged:if(visible&&String(form.values[field.modelData.key]||"")!==text)form.setValue(field.modelData.key,text)
                                    background:Rectangle{color:Colors.panelAlt;radius:8;border.color:Colors.border}
                                }
                            }
                        }
                    }
                }
            }
            Text{Layout.fillWidth:true;visible:form.message.length>0;text:form.message;color:Colors.danger;font.pixelSize:Typography.body;wrapMode:Text.Wrap}
            RowLayout{Layout.fillWidth:true;TouchButton{visible:form.attachmentLabel.length>0;text:form.attachmentLabel;autoWidth:true;variant:"tonal";onClicked:form.attachmentRequested()}TouchButton{visible:form.attachmentRemovable;text:"移除图片";autoWidth:true;variant:"ghost";onClicked:form.attachmentRemoveRequested()}Item{Layout.fillWidth:true}TouchButton{text:"取消";autoWidth:true;variant:"ghost";onClicked:form.visible=false}TouchButton{objectName:"productSave";text:"保存";autoWidth:true;variant:"primary";onClicked:form.submitted(form.collect())}}
        }
    }
}
