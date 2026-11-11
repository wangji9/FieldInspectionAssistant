import QtQuick
import "../theme"

Rectangle {
    id: root
    property string text: ""
    property string icon: ""
    property int iconSize: 16
    property string variant: "secondary"
    property bool autoWidth: false
    signal clicked()
    implicitWidth: Math.max(64, content.implicitWidth + 24)
    implicitHeight: Dimensions.buttonHeight
    height: Dimensions.buttonHeight
    width: autoWidth ? implicitWidth : 112
    radius: 2
    color: !enabled ? Colors.panelAlt : pointer.pressed ? Colors.primaryPressed
        : pointer.containsMouse ? Colors.primaryHover : variant === "tonal" ? Colors.infoDark
        : variant === "ghost" ? "transparent" : Colors.background
    border.width: variant === "ghost" && !pointer.containsMouse && !activeFocus ? 0 : 1
    border.color: activeFocus || (enabled && variant === "primary") ? Colors.normal : Colors.border
    gradient: Gradient {
        GradientStop { position: 0.0; color: pointer.pressed ? root.color : Qt.lighter(root.color, 1.10) }
        GradientStop { position: 0.47; color: Qt.lighter(root.color, 1.04) }
        GradientStop { position: 0.53; color: root.color }
        GradientStop { position: 1.0; color: Qt.darker(root.color, 1.05) }
    }
    activeFocusOnTab: enabled
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: if (root.enabled) root.clicked()
    Row {
        id: content; anchors.centerIn: parent; spacing: root.icon.length ? 6 : 0
        AppIcon {
            visible: root.icon.length > 0; width: visible ? root.iconSize : 0; height: root.iconSize
            anchors.verticalCenter: parent.verticalCenter; name: root.icon
            color: !root.enabled ? Colors.muted : root.variant === "danger" ? Colors.danger : Colors.textSecondary
        }
        Text {
            visible:root.text.length>0; text: root.text; font.pixelSize: Typography.button; font.family: Typography.uiFamily
            color: !root.enabled ? Colors.muted : root.variant === "danger" ? Colors.danger : Colors.text
        }
    }
    MouseArea { id: pointer; anchors.fill: parent; enabled: root.enabled; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.clicked() }
}
