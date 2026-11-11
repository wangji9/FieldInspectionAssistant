import QtQuick
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: root
    property string assetCode: ""
    property string assetName: ""
    property string status: "todo"   // todo / done / current
    property string progressText: ""
    property int answered: 0
    property int total: 0
    signal clicked()

    height: 54
    radius: Dimensions.radius
    color: {
        if (status === "current") return Colors.infoDark
        if (status === "done") return Colors.panelAlt
        return Colors.panel
    }
    border.color: status === "current" ? Colors.normal : Colors.border
    border.width: status === "current" ? 1 : 0

    RowLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 9
        Rectangle {
            width:24; height:24; radius:8; color:root.status === "done" ? Colors.normalDark : Colors.infoDark
            AppIcon { anchors.centerIn:parent; width:15; height:15; name:root.status === "done" ? "check" : "devices"; color:root.status === "done" ? Colors.success : Colors.normal }
        }
        Column {
            Layout.fillWidth:true; spacing:2
            Text { width:parent.width; text:root.assetCode; font.pixelSize:Typography.body; font.weight:Font.Medium; color:Colors.text; elide:Text.ElideRight }
            Text { width:parent.width; text:root.assetName; font.pixelSize:Typography.body; color:Colors.textSecondary; elide:Text.ElideRight }
        }
        Text { text:root.progressText.length ? root.progressText : root.answered+"/"+root.total; font.pixelSize:Typography.body; font.family:Typography.numericFamily; color:Colors.textSecondary }
    }
    MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; onClicked:root.clicked() }
}
