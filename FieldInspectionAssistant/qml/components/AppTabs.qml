import QtQuick
import QtQuick.Controls
import "../theme"

Flickable {
    id: strip
    property var sections: []
    property string selected: ""
    property string objectPrefix: "navTab_"
    signal navigate(string route)
    implicitHeight: 29
    contentWidth: tabs.width + 6
    contentHeight: height
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    Rectangle {
        width: Math.max(strip.width, strip.contentWidth)
        height: 1
        y: strip.height - height
        color: Colors.border
    }
    Row {
        id: tabs
        x: 3
        y: 3
        spacing: 1
        Repeater {
            model: strip.sections
            DesktopTab {
                required property var modelData
                objectName: strip.objectPrefix + modelData.id
                width: implicitWidth
                height: 26
                checked: strip.selected === modelData.id
                text: modelData.title
                onClicked: strip.navigate(modelData.id)
            }
        }
    }
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
}
