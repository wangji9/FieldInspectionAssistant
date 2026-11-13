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
    required property var spectrogram
    property string profileId: ""
    property real viewSpan: 1
    property real viewStart: 0
    property int selectedIndex: -1
    readonly property int axesRevision: chart.axesRevision
    signal selected(int index)

    implicitHeight: Math.min(570, chart.chartHeight+31)
    color: "#ffffff"
    border.color: Colors.border
    function changeZoom(factor) {
        const oldSpan = viewSpan
        const minSpan = Math.min(1, 6000 / Math.max(1, Number(metrics.audio_ms || 0)))
        viewSpan = Math.max(minSpan, Math.min(1, oldSpan * factor))
        viewStart = Math.max(0, Math.min(1 - viewSpan, viewStart + (oldSpan - viewSpan) / 2))
    }

    function segmentColor(index) {
        return chart.segmentColor(index)
    }

    function revealSegment(row) {
        const duration = Math.max(1000, Number(metrics.audio_ms || 0))
        const offset = Number(metrics.offset_ms || 0)
        const begin = (row.start_ms - offset) / duration
        const end = (row.end_ms - offset) / duration
        if (begin < viewStart || end > viewStart + viewSpan)
            viewStart = Math.max(0, Math.min(1 - viewSpan, (begin + end - viewSpan) / 2))
        const y = chart.segmentPosition(selectedIndex)
        if (y >= 0 && y + 24 > viewport.contentY + viewport.height)
            viewport.contentY = Math.min(viewport.contentHeight - viewport.height, y + 24 - viewport.height)
        else if (y >= 0 && y < viewport.contentY)
            viewport.contentY = y
    }

    onSamplesChanged: {
        if (!samples.length) {
            viewSpan = 1
            viewStart = 0
            viewport.contentY = 0
        }
    }

    Row {
        id: toolbar
        anchors { top: parent.top; left: parent.left; right: parent.right; margins: 3 }
        spacing: 5
        TouchButton {
            objectName: "speakerZoomOut"
            text: "−"; width: 32; height: 24; enabled: timeline.viewSpan < 1
            Accessible.name: "缩小时间轴"
            onClicked: timeline.changeZoom(2)
        }
        TouchButton {
            objectName: "speakerZoomIn"
            text: "+"; width: 32; height: 24; enabled: Number(timeline.metrics.audio_ms || 0) * timeline.viewSpan > 6001
            Accessible.name: "放大时间轴"
            onClicked: timeline.changeZoom(0.5)
        }
        TouchButton {
            text: "全览"; width: 56; height: 24; enabled: timeline.viewSpan < 1
            onClicked: {
                timeline.viewSpan = 1
                timeline.viewStart = 0
            }
        }
        Slider {
            width: Math.max(0, Math.min(240, toolbar.width - 145)); height: 24
            from: 0; to: 1 - timeline.viewSpan
            value: timeline.viewStart
            enabled: timeline.viewSpan < 1
            Accessible.name: "平移时间轴"
            onMoved: timeline.viewStart = value
        }
    }
    Flickable {
        id: viewport
        anchors { top: toolbar.bottom; bottom: parent.bottom; left: parent.left; right: parent.right }
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
            spectrogram: timeline.spectrogram
            profileId: timeline.profileId
            viewStart: timeline.viewStart
            viewSpan: timeline.viewSpan
            selectedIndex: timeline.selectedIndex
            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                property int hoveredRow: -1
                onPositionChanged: function(event) { hoveredRow = chart.segmentAt(event.x, event.y) }
                onExited: hoveredRow = -1
                onClicked: function(event) { timeline.selected(chart.segmentAt(event.x, event.y)) }
                ToolTip.visible: hoveredRow >= 0
                ToolTip.text: {
                    const row = timeline.segments[hoveredRow]
                    return row ? [row.range,row.speaker || row.status,row.overlap ? "含同讲" : "",row.transcript || ""]
                                 .filter(Boolean).join("\n") : ""
                }
            }
        }
    }
}
