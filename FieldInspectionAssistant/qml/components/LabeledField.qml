import QtQuick
import QtQuick.Layouts
import "../theme"

RowLayout {
    id: root
    property string label: ""
    property string text: ""
    property string placeholder: ""
    property bool readOnly: false
    property alias inputMethodHints: field.inputMethodHints
    signal valueEdited(string value)
    spacing: 12
    implicitHeight: 44
    Text { text: root.label; Layout.preferredWidth: 92; font.pixelSize: Typography.body; color: Colors.textSecondary }
    FieldInput {
        id: field
        Layout.fillWidth: true; height: 40
        text: root.text; placeholder: root.placeholder; readOnly: root.readOnly
        onTextEdited: root.valueEdited(value)
    }
}
