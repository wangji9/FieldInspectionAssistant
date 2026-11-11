import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
Column {
 id:root
 property string evidenceJson:""
 property var evidence:{try{return evidenceJson?JSON.parse(evidenceJson):{}}catch(e){return {}}}
 property bool expanded:false
 property string error:""
 readonly property var sources:evidence.sources||[]
 visible:sources.length>0
 spacing:5
 ToolButton {text:"系统依据 · "+root.sources.length+(root.expanded?" ▴":" ▾");height:28;font.pixelSize:Typography.body;onClicked:root.expanded=!root.expanded}
 Column {visible:root.expanded;width:parent.width;spacing:6
  Repeater {model:root.sources
   RowLayout {required property var modelData;width:parent.width;spacing:4
    Text {Layout.fillWidth:true;text:"["+modelData.citation+"] "+modelData.title+(modelData.at?"\n"+modelData.at:"");color:Colors.textSecondary;font.pixelSize:Typography.body;wrapMode:Text.Wrap}
    ToolButton {text:"查看";font.pixelSize:Typography.body;visible:["asset","record","defect","task","overview","analysis"].indexOf(modelData.entity)>=0;onClicked:{var r=App.systemContext.destination(modelData.entity,modelData.id);if(r.ok)App.nav.push(r.page,r.params);else root.error=r.error}}
   }
  }
 }
 Text {visible:!!root.error;width:parent.width;text:root.error;color:Colors.danger;wrapMode:Text.Wrap;font.pixelSize:Typography.body}
}
