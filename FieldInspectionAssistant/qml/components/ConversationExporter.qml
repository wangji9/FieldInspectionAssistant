import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import "../theme"

Item {
    id: exporter
    property string mode:"text"
    property var messages:[]
    property string audioPath:""
    property string feedback:""
    readonly property var formats:mode==="text"?["md","txt","html","json"]:["zip","md","txt","html","json"]
    function open(rows) {messages=rows;saveDialog.selectedNameFilter.index=0;saveDialog.selectedFile=App.exports.defaultFile(mode,formats[0]);saveDialog.open()}
    function audio(path) {audioPath=path;audioDialog.selectedFile=App.exports.defaultFile("asr","wav");audioDialog.open()}
    function complete(result) {feedback=result.ok?"已保存："+result.path:result.error;notice.open();dismiss.restart()}
    FileDialog {
        id:saveDialog;objectName:"conversationSaveDialog";title:"保存对话";fileMode:FileDialog.SaveFile;acceptLabel:"保存";rejectLabel:"取消"
        nameFilters:exporter.mode==="text"?["Markdown (*.md)","纯文本 (*.txt)","HTML (*.html)","JSON (*.json)"]:["完整对话与附件 (*.zip)","Markdown (*.md)","纯文本 (*.txt)","HTML (*.html)","JSON (*.json)"]
        defaultSuffix:exporter.formats[selectedNameFilter.index]||"md"
        onAccepted:exporter.complete(App.exports.save(selectedFile,exporter.mode,exporter.messages,exporter.formats[selectedNameFilter.index]))
    }
    Connections {target:saveDialog.selectedNameFilter;function onIndexChanged(){var value=saveDialog.selectedFile.toString();var dot=value.lastIndexOf(".");if(dot>value.lastIndexOf("/"))value=value.slice(0,dot);saveDialog.selectedFile=value+"."+exporter.formats[saveDialog.selectedNameFilter.index]}}
    FileDialog {id:audioDialog;title:"保存语音条";fileMode:FileDialog.SaveFile;acceptLabel:"保存";rejectLabel:"取消";nameFilters:["WAV 音频 (*.wav)"];defaultSuffix:"wav";onAccepted:exporter.complete(App.exports.saveAudio(selectedFile,exporter.audioPath))}
    Popup {
        id:notice;parent:Overlay.overlay;anchors.centerIn:parent;width:Math.min(600,parent.width-32);height:120;modal:false;padding:18
        background:Rectangle{radius:12;color:Colors.panel;border.color:Colors.border}
        Text {anchors.fill:parent;text:exporter.feedback;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}
    }
    Timer{id:dismiss;interval:6500;onTriggered:notice.close()}
}
