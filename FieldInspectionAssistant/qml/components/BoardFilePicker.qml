import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
AssistantDialog {
    id:root;objectName:"boardFilePicker";title:"选择板端文件";anchors.centerIn:parent
    width:Math.min(parent.width-16,700);height:Math.min(parent.height-16,540)
    property string kind:"document"
    property string directory:""
    property string parentDirectory:""
    property var entries:[]
    property string errorText:""
    signal picked(string path)
    function openFor(type){kind=type;errorText="";navigate("");open()}
    function navigate(path){var r=App.workspace.inputFiles(path,kind);if(!r.ok){errorText=r.error;return}if(r.file){picked(r.file);close();return}directory=r.directory;parentDirectory=r.parent;entries=r.rows;pathInput.text=directory;errorText=r.truncated?"目录文件较多，请输入更具体的路径。":""}
    Connections {target:App.session;function onSessionChanged(){if(!App.session.loggedIn){root.close();root.entries=[];pathInput.text="";root.directory=""}}}
    contentItem:ColumnLayout {
        spacing:10
        RowLayout {Layout.fillWidth:true
            TouchButton {text:"上一级";autoWidth:true;height:34;variant:"ghost";enabled:root.directory!==root.parentDirectory;onClicked:root.navigate(root.parentDirectory)}
            TouchButton {text:"主目录";autoWidth:true;height:34;variant:"ghost";onClicked:root.navigate("")}
            Item {Layout.fillWidth:true}
        }
        TextField {id:pathInput;objectName:"boardFilePath";Layout.fillWidth:true;placeholderText:"输入板端目录或完整文件路径";selectByMouse:true;font.pixelSize:Typography.body;color:Colors.text;onAccepted:root.navigate(text)}
        Text {visible:text.length>0;Layout.fillWidth:true;text:root.errorText;font.pixelSize:Typography.body;color:Colors.danger;wrapMode:Text.Wrap}
        ListView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:root.entries;spacing:4;ScrollBar.vertical:ScrollBar{}
            delegate:ItemDelegate {
                required property var modelData
                width:ListView.view.width;height:42;text:(modelData.directory?"▸  ":"    ")+modelData.name
                contentItem:Text{text:parent.text;color:Colors.text;font.pixelSize:Typography.body;verticalAlignment:Text.AlignVCenter;elide:Text.ElideMiddle}
                onClicked:if(modelData.directory)root.navigate(modelData.path);else pathInput.text=modelData.path
                onDoubleClicked:if(!modelData.directory)root.navigate(modelData.path)
            }
            Text {anchors.centerIn:parent;visible:!root.entries.length;text:"此目录没有匹配的文件";font.pixelSize:Typography.body;color:Colors.textSecondary}
        }
    }
    footer:Item {implicitHeight:60;Row {anchors.right:parent.right;anchors.rightMargin:20;anchors.verticalCenter:parent.verticalCenter;spacing:8
        TouchButton {text:"取消";autoWidth:true;height:34;variant:"ghost";onClicked:root.close()}
        TouchButton {objectName:"boardFileConfirm";text:"打开 / 选择";autoWidth:true;height:34;variant:"primary";onClicked:root.navigate(pathInput.text)}
    }}
}
