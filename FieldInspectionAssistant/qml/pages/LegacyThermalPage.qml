import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FieldInspection
import "../components"
import "../theme"

Rectangle {
    id: page
    objectName: "thermalAnalysisPage"
    color: Colors.background

    function close() { viewer.stop() }

    Connections {
        target: App.session
        function onSessionEnding() { viewer.stop() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 5

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "热像仪实时数据分析"
                color: Colors.text
                font.pixelSize: Typography.h2
            }
            Item { Layout.fillWidth: true }
            TouchButton {
                objectName: "thermalToggle"
                autoWidth: true
                text: viewer.active ? "关闭热像分析" : "打开热像分析"
                onClicked: viewer.active ? viewer.stop() : viewer.start()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Colors.panel
            border.color: Colors.border

            ThermalWindow {
                id: viewer
                objectName: "thermalViewer"
                anchors.fill: parent
                anchors.margins: 1
                anchors.rightMargin: vertical.visible ? vertical.width + 1 : 1
                anchors.bottomMargin: horizontal.visible ? horizontal.height + 1 : 1
                scrollX: horizontal.position * contentWidth
                scrollY: vertical.position * contentHeight
            }
            // Show both scrollbars when the analyzer's minimum geometry exceeds the viewport.
            readonly property bool needsScroll: viewer.embedded &&
                (viewer.minimumWidth > width - 2 || viewer.minimumHeight > height - 2)
            ScrollBar {
                id: vertical
                visible: parent.needsScroll
                orientation: Qt.Vertical
                anchors { top: parent.top; right: parent.right; bottom: horizontal.top }
                width: 16
                size: viewer.contentHeight > 0 ? viewer.height / viewer.contentHeight : 1
                policy: ScrollBar.AlwaysOn
            }
            ScrollBar {
                id: horizontal
                visible: parent.needsScroll
                orientation: Qt.Horizontal
                anchors { left: parent.left; right: vertical.left; bottom: parent.bottom }
                height: 16
                size: viewer.contentWidth > 0 ? viewer.width / viewer.contentWidth : 1
                policy: ScrollBar.AlwaysOn
            }
            Text {
                visible: !viewer.embedded
                anchors.centerIn: parent
                width: Math.min(460, parent.width - 28)
                text: viewer.status || "连接热像仪后，点击「打开热像分析」。"
                font.pixelSize: Typography.body
                color: Colors.textSecondary
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
