import QtQuick
import "../theme"

Item {
    id: root
    property string title: ""
    height: 40
    Text { anchors.left:parent.left; y:0; text:root.title; font.pixelSize:Typography.title; font.weight:Font.DemiBold; color:Colors.text }
}
