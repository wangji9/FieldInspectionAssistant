import QtQuick
import QtQuick.Layouts
import "../theme"

ColumnLayout {
    id: plot
    property var observation: null
    readonly property var head: observation ? observation.headVector : null
    readonly property var gaze: observation ? observation.gazeVector : null
    property real azimuth: -0.60
    property real elevation: 0.44
    spacing: 2

    function components(vector) {
        if (!vector || vector.length !== 3)
            return "—"
        return ["X", "Y", "Z"].map((axis, index) => axis + " " + Number(vector[index]).toFixed(3)).join("     ")
    }
    onHeadChanged: if (visible) chart.requestPaint()
    onGazeChanged: if (visible) chart.requestPaint()
    onVisibleChanged: if (visible) chart.requestPaint()
    onAzimuthChanged: chart.requestPaint()
    onElevationChanged: chart.requestPaint()

    Canvas {
        id: chart
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const scale = Math.max(1, Math.min(width, height) * 0.37)
            const ca = Math.cos(plot.azimuth), sa = Math.sin(plot.azimuth)
            const ce = Math.cos(plot.elevation), se = Math.sin(plot.elevation)
            function project(v) {
                return [width / 2 + scale * (ca * v[0] - sa * v[2]),
                        height / 2 + scale * (ce * v[1] - se * (sa * v[0] + ca * v[2]))]
            }
            function line(a, b, color, thickness) {
                ctx.strokeStyle = color
                ctx.lineWidth = thickness
                ctx.beginPath()
                ctx.moveTo(a[0], a[1])
                ctx.lineTo(b[0], b[1])
                ctx.stroke()
            }
            function dot(p, radius, color) {
                ctx.fillStyle = color
                ctx.beginPath()
                ctx.arc(p[0], p[1], radius, 0, Math.PI * 2)
                ctx.fill()
            }
            for (let i = -4; i <= 4; ++i) {
                const step = i / 4
                line(project([-1, 0, step]), project([1, 0, step]), "#dce4eb", 1)
                line(project([step, 0, -1]), project([step, 0, 1]), "#dce4eb", 1)
            }
            const origin = project([0, 0, 0])
            ctx.font = "11px sans-serif"
            const labels = ["X", "Y", "Z"]
            for (let axis = 0; axis < 3; ++axis) {
                let a = [0, 0, 0], b = [0, 0, 0], tag = [0, 0, 0]
                a[axis] = -1
                b[axis] = 1
                tag[axis] = 1.10
                line(project(a), project(b), "#7c8c9d", 1)
                const p = project(tag)
                ctx.fillStyle = "#3d5267"
                ctx.fillText(labels[axis], p[0] - 3, p[1] + 4)
            }
            function ray(vector, color) {
                if (!vector || vector.length !== 3)
                    return
                const end = project(vector)
                line(origin, end, color, 2.5)
                const dx = end[0] - origin[0], dy = end[1] - origin[1]
                const len = Math.hypot(dx, dy)
                if (len > 8) {
                    const ux = dx / len, uy = dy / len
                    line(end, [end[0] - 7 * ux - 3 * uy, end[1] - 7 * uy + 3 * ux], color, 2)
                    line(end, [end[0] - 7 * ux + 3 * uy, end[1] - 7 * uy - 3 * ux], color, 2)
                } else {
                    dot(end, 3, color)
                }
            }
            dot(origin, 2, "#596c7c")
            ray(plot.head, "#255f9d")
            ray(plot.gaze, "#9d5c00")
        }
        DragHandler {
            id: orbit
            target: null
            property point previous: Qt.point(0, 0)
            onActiveChanged: previous = Qt.point(0, 0)
            onActiveTranslationChanged: {
                if (!active)
                    return
                plot.azimuth += (activeTranslation.x - previous.x) * 0.012
                plot.elevation = Math.max(-1.40, Math.min(1.40, plot.elevation + (activeTranslation.y - previous.y) * 0.012))
                previous = activeTranslation
            }
        }
        TapHandler {
            onDoubleTapped: { plot.azimuth = -0.60; plot.elevation = 0.44 }
        }
    }
    GridLayout {
        columns: 2
        columnSpacing: 12; rowSpacing: 2
        Layout.fillWidth: true
        Text { text: "头部朝向"; color: "#255f9d"; font.pixelSize: Typography.small }
        Text { text: plot.components(plot.head); color: Colors.text; font.pixelSize: Typography.small }
        Text { text: "视线方向"; color: "#9d5c00"; font.pixelSize: Typography.small }
        Text { text: plot.components(plot.gaze); color: Colors.text; font.pixelSize: Typography.small }
    }
    Text {
        Layout.fillWidth: true
        text: "摄像头坐标  X 向右 · Y 向下 · Z 向场景\n拖动旋转 · 双击复位"
        color: Colors.textSecondary
        wrapMode: Text.Wrap
        font.pixelSize: Typography.small
    }
}
