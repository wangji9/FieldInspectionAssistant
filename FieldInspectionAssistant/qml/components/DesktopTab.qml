import QtQuick
import QtQuick.Controls
import "../theme"

TabButton {
    id: control
    implicitWidth: Math.max(62, label.implicitWidth + 20)
    implicitHeight: 25
    padding: 5
    contentItem: Text {
        id: label
        text: control.text
        font.pixelSize: Typography.small
        color: control.enabled ? Colors.text : Colors.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
    background: Rectangle {
        radius: 2
        border.color: control.activeFocus ? Colors.normal : Colors.border
        gradient: Gradient {
            GradientStop { position: 0.0; color: control.checked ? Colors.panel : "#ffffff" }
            GradientStop { position: 1.0; color: control.checked ? Colors.panel : control.hovered ? Colors.infoDark : "#e5e5e5" }
        }
        Rectangle {
            visible: control.checked
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 1; rightMargin: 1 }
            height: 1
            color: Colors.panel
        }
    }
}
