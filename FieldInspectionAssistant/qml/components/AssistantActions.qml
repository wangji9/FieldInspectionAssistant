import QtQuick
import "../theme"

Item {
    id: actions
    objectName:"assistantActions"
    property var panel
    implicitWidth:222;implicitHeight:30
    Row {
        anchors.fill:parent;spacing:6
        TouchButton {objectName:"exportConversationButton";text:"导出";width:56;height:30;variant:"ghost";enabled:!App.ai.busy&&!App.speech.speechBusy;onClicked:actions.panel.exportConversation()}
        Rectangle {
            width:160;height:30;radius:8;color:"#EBEDF4"
            Row {
                anchors.fill:parent;anchors.margins:3;spacing:2
                Repeater {
                    model:["使用","性能","记录"]
                    Rectangle {
                        objectName:"assistantView"+index
                        signal clicked()
                        onClicked:actions.panel.selectView(index)
                        width:50;height:24;radius:6;color:actions.panel.view===index?Colors.panel:"transparent"
                        Text {anchors.centerIn:parent;text:modelData;font.pixelSize:Typography.body;font.weight:actions.panel.view===index?Font.DemiBold:Font.Normal;color:actions.panel.view===index?Colors.text:Colors.textSecondary}
                        Accessible.role:Accessible.Button;Accessible.name:modelData;Accessible.onPressAction:clicked()
                        MouseArea {anchors.fill:parent;cursorShape:Qt.PointingHandCursor;onClicked:parent.clicked()}
                    }
                }
            }
        }
    }
}
