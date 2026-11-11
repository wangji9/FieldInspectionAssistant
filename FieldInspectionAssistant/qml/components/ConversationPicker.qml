import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
AssistantDialog {
    id:root;objectName:"graphConversationPicker";title:"选择已有对话";anchors.centerIn:parent;width:Math.min(parent.width-16,700);height:Math.min(parent.height-16,520)
    property var catalogResult:({rows:[],total:0,page:0})
    property string query:""
    signal picked(var conversation)
    function reload(page){catalogResult=App.knowledge.conversationChoices(query,page||0,30)}
    onOpened:reload(0)
    Connections {target:App.session;function onSessionChanged(){root.close();root.query="";search.text="";root.catalogResult={rows:[],total:0,page:0}}}
    Timer {id:delay;interval:200;onTriggered:root.reload(0)}
    contentItem:ColumnLayout {
        spacing:10
        TextField {id:search;objectName:"graphConversationSearch";Layout.fillWidth:true;placeholderText:"搜索文字、图片、语音或智能体对话";selectByMouse:true;font.pixelSize:Typography.body;onTextEdited:{root.query=text;delay.restart()}onAccepted:root.reload(0)}
        ListView {Layout.fillWidth:true;Layout.fillHeight:true;clip:true;model:root.catalogResult.rows||[];spacing:4;ScrollBar.vertical:ScrollBar{}
            delegate:ItemDelegate {required property var modelData;width:ListView.view.width;height:50
                contentItem:Column {spacing:3;Text {width:parent.width;text:modelData.display;elide:Text.ElideRight;color:Colors.text;font.pixelSize:Typography.body}Text {text:Qt.formatDateTime(new Date(modelData.updated_at),"yyyy-MM-dd HH:mm:ss");font.pixelSize:Typography.body;color:Colors.textSecondary}}
                onClicked:{root.picked(modelData);root.close()}
            }
            Text {anchors.centerIn:parent;visible:!(root.catalogResult.rows||[]).length;text:root.catalogResult.ok===false?root.catalogResult.error:root.query?"没有匹配的对话":"当前账号暂无已保存对话";font.pixelSize:Typography.body;color:Colors.textSecondary}
        }
        RowLayout {Layout.fillWidth:true;Item{Layout.fillWidth:true}TouchButton {text:"上一页";height:30;autoWidth:true;variant:"ghost";enabled:(root.catalogResult.page||0)>0;onClicked:root.reload(root.catalogResult.page-1)}Text {text:((root.catalogResult.page||0)+1)+" / "+Math.max(1,Math.ceil((root.catalogResult.total||0)/30));font.pixelSize:Typography.body;color:Colors.textSecondary}TouchButton {text:"下一页";height:30;autoWidth:true;variant:"ghost";enabled:((root.catalogResult.page||0)+1)*30<(root.catalogResult.total||0);onClicked:root.reload(root.catalogResult.page+1)}Item{Layout.fillWidth:true}}
    }
}
