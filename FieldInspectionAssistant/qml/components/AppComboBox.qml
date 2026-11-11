import QtQuick
import QtQuick.Controls
import "../theme"

ComboBox {
    id: control
    property int maximumPopupHeight: 270
    implicitHeight: 30
    leftPadding: 12; rightPadding: 30
    font.pixelSize: Typography.small
    palette.text: Colors.text; palette.buttonText: Colors.text; palette.highlight: Colors.normal
    contentItem: Text {
        text: control.displayText; font: control.font; color: control.enabled ? Colors.text : Colors.muted
        verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
    }
    indicator: AppIcon { name: "chevron-down"; width: 16; height: 16; x: control.width - width - 10; y: (control.height-height)/2; color: control.enabled ? Colors.textSecondary : Colors.muted }
    background: Rectangle { color: control.enabled ? Colors.panel : Colors.panelAlt; border.color: control.activeFocus || control.hovered ? Colors.normal : Colors.border }
    delegate: ItemDelegate {
        width: control.width; height: 30
        contentItem: Text { text: control.textAt(index); font: control.font; color: highlighted ? Colors.normal : Colors.text; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
        highlighted: control.highlightedIndex === index
        background: Rectangle { color: parent.highlighted ? Colors.infoDark : "transparent" }
    }
    popup: Popup {
        y: control.height; width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 2, control.maximumPopupHeight)
        padding: 1
        background: Rectangle { color: Colors.panel; border.color: Colors.border }
        contentItem: ListView { clip: true; implicitHeight: contentHeight; model: control.popup.visible ? control.delegateModel : null; currentIndex: control.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
    }
}
