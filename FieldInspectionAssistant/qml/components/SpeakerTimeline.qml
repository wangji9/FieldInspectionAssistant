import QtQuick
import QtQuick.Controls
import FieldInspection
import "../theme"

Rectangle {
    id: timeline
    required property var samples
    required property var segments
    required property var profiles
    required property var metrics
    property int selectedIndex: -1
    signal selected(int index)

    implicitHeight: Math.min(270, chart.chartHeight)
    color: "#ffffff"
    border.color: Colors.border
    Flickable {
        anchors.fill: parent
        anchors.margins: 1
        contentWidth: width
        contentHeight: chart.chartHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        SpeakerTimelineView {
            id: chart
            width: parent.width; height: chartHeight
            samples: timeline.samples
            segments: timeline.segments
            profiles: timeline.profiles
            metrics: timeline.metrics
            selectedIndex: timeline.selectedIndex
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                property int hoveredRow: -1
                onPositionChanged: function(event) { hoveredRow = chart.segmentAt(event.x) }
                onExited: hoveredRow = -1
                onClicked: function(event) { timeline.selected(chart.segmentAt(event.x)) }
                ToolTip.visible: hoveredRow >= 0
                ToolTip.text: {
                    const row = timeline.segments[hoveredRow]
                    return row ? [row.range,row.state==="matched"?row.speaker:row.status,row.score].join("　·　") : ""
                }
            }
        }
    }
}
