import QtQuick
import "../theme"

Rectangle {
    id: root
    property string text: ""
    property string placeholder: ""
    property string label: ""
    property color background: Colors.panel
    property color borderColor: Colors.border
    property alias font: input.font
    property alias readOnly: input.readOnly
    property alias inputMethodHints: input.inputMethodHints
    property alias maximumLength: input.maximumLength
    property alias echoMode: input.echoMode
    signal editingFinished(string value)
    signal textEdited(string value)

    height: 40
    radius: Dimensions.radius
    color: root.background
    border.color: input.activeFocus ? Colors.normal : root.borderColor
    border.width: 1

    Text {
        visible: root.label.length > 0
        text: root.label
        font.pixelSize: Typography.small
        color: Colors.textSecondary
        anchors.top: parent.top
        anchors.topMargin: -18
        anchors.left: parent.left
    }
    TextInput {
        id: input
        objectName: "fieldTextInput"
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        font.pixelSize: Typography.body
        color: Colors.text
        selectByMouse: true
        verticalAlignment: TextInput.AlignVCenter
        onEditingFinished: root.editingFinished(text)
        onTextChanged: if(root.text!==text) { root.text = text; root.textEdited(text) }
    }
    Text {
        visible: input.text.length === 0
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: root.placeholder
        font.pixelSize: input.font.pixelSize
        color: Colors.textSecondary
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
}
