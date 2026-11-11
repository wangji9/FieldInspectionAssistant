import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Rectangle {
    id: panel
    property string title: ""
    default property alias contents: body.data

    color: Colors.panel
    border.color: Colors.border

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        Rectangle {
            visible: panel.title.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: 27
            color: Colors.panelAlt

            Label {
                anchors.fill: parent
                anchors.leftMargin: 8
                text: panel.title
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font.family: Typography.uiFamily
                font.pixelSize: Typography.body
                font.bold: true
                color: Colors.text
            }
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Colors.border
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 8
            spacing: 6
        }
    }
}
