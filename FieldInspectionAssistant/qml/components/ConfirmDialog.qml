import QtQuick
import QtQuick.Layouts
import "../theme"

Item {
    id: root
    anchors.fill: parent
    visible: false
    property string title: "确认"
    property string message: ""
    property string acceptText: "确认"
    property string cancelText: "取消"
    property bool showCancel: true
    signal accepted()
    signal rejected()

    function openDialog(t, m, accept, cancel) {
        if (t !== undefined) title = t
        if (m !== undefined) message = m
        if (accept !== undefined) acceptText = accept
        if (cancel !== undefined) cancelText = cancel
        visible = true
    }
    function closeDialog() { visible = false }

    Rectangle {
        anchors.fill: parent
        color: "#99000000"
        MouseArea { anchors.fill: parent; onClicked: root.closeDialog() }
    }
    Rectangle {
        id: box
        anchors.centerIn: parent
        width: 440
        height: dialogColumn.implicitHeight + 40
        color: Colors.panel
        radius: Dimensions.radius
        border.color: Colors.border
        border.width: 1
        Column {
            id: dialogColumn
            anchors.fill: parent
            anchors.margins: 20
            spacing: Dimensions.spacing
            Text {
                text: root.title
                font.pixelSize: Typography.h2
                font.weight: Font.Bold
                color: Colors.text
            }
            Text {
                width: parent.width
                text: root.message
                font.pixelSize: Typography.body
                color: Colors.textSecondary
                wrapMode: Text.WordWrap
            }
            Row {
                Layout.alignment: Qt.AlignRight
                spacing: Dimensions.gap
                TouchButton {
                    visible: root.showCancel
                    text: root.cancelText
                    variant: "ghost"
                    autoWidth: true
                    onClicked: root.closeDialog()
                }
                TouchButton {
                    text: root.acceptText
                    variant: "primary"
                    autoWidth: true
                    onClicked: { root.closeDialog(); root.accepted() }
                }
            }
        }
    }
}
