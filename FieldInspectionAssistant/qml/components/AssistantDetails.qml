import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

StackLayout {
    id: details
    objectName:"assistantDetails"
    property int historyPage:0
    property int historyTotal:0
    property string historySearch:""
    property var historyRows:[]
    function reload(){var r=App.workspace.query("conversations",{mode:panel.mode,keyword:historySearch},historyPage,30);historyRows=r.ok?r.rows:[];historyTotal=r.ok?r.total:0}
    onVisibleChanged:if(visible){historyPage=0;reload()}
    Connections{target:App.workspace;function onConversationsChanged(){if(details.visible)details.reload()}}
    Connections{target:App.session;function onSessionChanged(){details.historySearch="";historySearchInput.text=Qt.binding(function(){return details.historySearch});details.historyRows=[];details.historyTotal=0;details.historyPage=0;if(details.visible&&App.session.loggedIn)details.reload()}}
    property var panel
    currentIndex:panel.view-1
    ScrollView {
        id:performance
        contentWidth:availableWidth;clip:true
        GridLayout {
            width:performance.availableWidth;columns:Math.max(1,Math.min(4,Math.floor(width/220)));columnSpacing:10;rowSpacing:10
            Repeater {
                model:details.panel.readings
                Rectangle {
                    Layout.fillWidth:true;Layout.preferredWidth:1;Layout.preferredHeight:116
                    color:Colors.panel;border.color:Colors.border;radius:10
                    Column {
                        anchors.fill:parent;anchors.margins:12;spacing:8
                        Text {width:parent.width;text:modelData.label;font.pixelSize:Typography.body;color:Colors.textSecondary;elide:Text.ElideRight}
                        Text {width:parent.width;text:modelData.value;font.pixelSize:Typography.value;font.family:Typography.numericFamily;font.weight:Font.DemiBold;color:Colors.text;elide:Text.ElideRight}
                        Text {width:parent.width;text:modelData.unit;font.pixelSize:Typography.body;color:Colors.textSecondary;elide:Text.ElideRight}
                    }
                }
            }
        }
    }
    ColumnLayout {
        spacing:8
        RowLayout {
            Layout.fillWidth:true;Layout.preferredHeight:30
            Text {Layout.fillWidth:true;text:"使用记录";font.pixelSize:Typography.h2;font.weight:Font.DemiBold;color:Colors.text}
            TouchButton {text:"刷新";autoWidth:true;height:30;Layout.maximumHeight:30;variant:"ghost";onClicked:details.reload()}
        }
        FieldInput{id:historySearchInput;objectName:"historySearchInput";Layout.fillWidth:true;height:34;text:details.historySearch;placeholder:"搜索我的完整会话";onTextEdited:function(v){details.historySearch=v;details.historyPage=0;details.reload()}}
        ListView {
            Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:6;model:details.historyRows
            ScrollBar.vertical:ScrollBar{}
            delegate:Rectangle {
                width:ListView.view.width;height:70;radius:9;color:Colors.panel;border.color:Colors.border
                ColumnLayout {
                    anchors.fill:parent;anchors.margins:10;spacing:5
                    RowLayout {
                        Layout.fillWidth:true;spacing:8
                        Text {text:details.panel.labels[modelData.mode]||"问答";font.pixelSize:Typography.body;font.weight:Font.Medium;color:Colors.text}
                        Text {Layout.fillWidth:true;Layout.minimumWidth:0;text:details.panel.modelNames[modelData.model]||"";font.pixelSize:Typography.body;color:Colors.textSecondary;elide:Text.ElideRight}
                        Text {text:"已保存";font.pixelSize:Typography.body;color:Colors.success}
                        Text {visible:parent.width>440;text:new Date(modelData.updated_at).toLocaleTimeString(Qt.locale("zh_CN"),"hh:mm:ss");font.pixelSize:Typography.body;color:Colors.textSecondary}
                    }
                    Text {Layout.fillWidth:true;Layout.minimumWidth:0;text:modelData.title;font.pixelSize:Typography.body;color:Colors.textSecondary;elide:Text.ElideRight}
                }
                MouseArea {anchors.fill:parent;cursorShape:Qt.PointingHandCursor;enabled:!App.ai.busy;onClicked:{var r=App.workspace.openConversation(modelData.id);if(r.ok)details.panel.restoreConversation(modelData.mode)}}
            }
        }
        RowLayout{Layout.fillWidth:true
            TouchButton{text:"上一页";autoWidth:true;height:30;enabled:details.historyPage>0;onClicked:{details.historyPage--;details.reload()}}
            Text{text:(details.historyPage+1)+" / "+Math.max(1,Math.ceil(details.historyTotal/30))+" · "+details.historyTotal+" 个会话";font.pixelSize:Typography.body;color:Colors.textSecondary}
            TouchButton{text:"下一页";autoWidth:true;height:30;enabled:(details.historyPage+1)*30<details.historyTotal;onClicked:{details.historyPage++;details.reload()}}
        }
    }
}
