pragma Singleton
import QtQuick

QtObject {
    readonly property int appWidth: 1280
    readonly property int appHeight: 720
    property real windowWidth: 1280
    property real windowHeight: 720
    readonly property bool compact: windowWidth < 980
    readonly property int headerHeight: 0
    readonly property int detailWidth: 300
    readonly property int radius: 0
    readonly property int buttonHeight: 30
    readonly property int spacing: 8
    readonly property int gap: 6
    readonly property int pagePadding: compact ? 6 : 8
}
