import QtQuick
import "../theme"

Item {
    id: root
    property string title: "暂无数据"
    property string icon: "archive"
    implicitWidth: 240; implicitHeight: 170
    Column {
        anchors.centerIn:parent; width:Math.max(100,Math.min(420,parent.width-32)); spacing:12
        Text { width:parent.width; horizontalAlignment:Text.AlignHCenter; wrapMode:Text.Wrap; text:root.title; font.pixelSize:Typography.body; color:Colors.textSecondary }
    }
}
