import QtQuick
import QtQuick.Controls
import "../theme"

Dialog {
    id: sheet
    modal: true; padding: 20; standardButtons: Dialog.NoButton
    background: Rectangle { color: Colors.background; border.color: Colors.border }
    header: Item {
        implicitHeight: 58
        Text { anchors.left: parent.left; anchors.leftMargin: 20; anchors.verticalCenter: parent.verticalCenter; text: sheet.title; color: Colors.text; font.pixelSize: Typography.h2; font.weight: Font.DemiBold }
    }
    footer: Item {
        implicitHeight: 62
        TouchButton { anchors.right: parent.right; anchors.rightMargin: 20; anchors.verticalCenter: parent.verticalCenter; width: 88; height: 34; text: "完成"; variant: "tonal"; onClicked: sheet.close() }
    }
}
