import QtQuick
import QtQuick.Controls
import "../theme"

CheckBox {
    id: control
    spacing: 8
    font.pixelSize: Typography.small
    indicator: Rectangle {
        implicitWidth: 17; implicitHeight: 17
        x: control.leftPadding; y: (control.height-height)/2
        color: Colors.panel
        border.color: control.activeFocus ? Colors.normal : Colors.border
        AppIcon { anchors.centerIn: parent; name: "check"; width: 13; height: 13; color: Colors.text; visible: control.checked }
    }
    contentItem: Text { text: control.text; font: control.font; color: control.enabled ? Colors.textSecondary : Colors.muted; verticalAlignment: Text.AlignVCenter; leftPadding: control.indicator.width+control.spacing }
}
