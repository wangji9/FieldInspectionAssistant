import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

ColumnLayout {
    id: charts
    property var snapshot: ({ summary: {}, results: [], categories: [] })
    property var history: []
    property bool cameraMode: true
    readonly property var figure: visible ? buildFigure(selector.currentIndex) : ({})
    spacing: 4

    function buildFigure(mode) {
        const rows = snapshot.results
        if (mode === 0 && cameraMode) {
            if (!history.length)
                return {title: "推理处理耗时", error: "启动实时推理后记录耗时", limit: 20}
            const model = []
            const endToEnd = []
            let maximum = 0
            for (const sample of history) {
                model.push([sample[0], sample[1]])
                endToEnd.push([sample[0], sample[2]])
                maximum = Math.max(maximum, sample[1], sample[2])
            }
            return {title: "推理处理耗时", kind: "line", xLabel: "时间 / s", yLabel: "耗时 / ms",
                limit: Math.max(20, Math.ceil(maximum / 20) * 20),
                series: [{name: "工作进程", points: model}, {name: "预处理至返回", points: endToEnd}]}
        }
        const histogram = mode === (cameraMode ? 2 : 1)
        if (!rows.length)
            return {title: histogram ? "置信度分布" : "类别计数", error: "当前帧无可统计目标", limit: 4}
        const labels = []
        const points = []
        let maximum = 0
        if (histogram) {
            const bins = Array(10).fill(0)
            for (const row of rows)
                bins[Math.min(9, Math.max(0, Math.floor(row.score * 10)))]++
            for (let i = 0; i < bins.length; i++) {
                points.push([i * 10 + 5, bins[i]])
                maximum = Math.max(maximum, bins[i])
            }
        } else {
            const ordered = snapshot.categories.slice().sort((a, b) => b.count - a.count || a.label.localeCompare(b.label))
            const shown = ordered.slice(0, 5)
            if (ordered.length > 5)
                shown.push({label: "其他", count: ordered.slice(5).reduce((sum, item) => sum + item.count, 0)})
            for (const category of shown) {
                points.push([labels.length, category.count])
                labels.push(category.label)
                maximum = Math.max(maximum, category.count)
            }
        }
        return {title: histogram ? "置信度分布" : "类别计数", kind: "bar",
            xLabel: histogram ? "置信度 / %" : "类别", yLabel: "目标数 / 个",
            xRange: histogram ? [0, 100] : [-0.5, labels.length - 0.5],
            xTickLabels: labels, barWidth: histogram ? 8.5 : 0.65, integerY: true,
            limit: Math.max(4, Math.ceil(maximum / 4) * 4),
            series: [{name: "当前帧", points: points}]}
    }

    RowLayout {
        Layout.fillWidth: true
        AppComboBox {
            id: selector
            objectName: "visionChartType"
            Layout.preferredWidth: 146
            implicitHeight: 27
            model: charts.cameraMode ? ["推理处理耗时", "类别计数", "置信度分布"] : ["类别计数", "置信度分布"]
            onCurrentIndexChanged: if (plotItem) plotItem.resetZoom()
            delegate: ItemDelegate {
                required property int index
                objectName: "visionChartChoice-" + index
                width: selector.width
                height: 27
                text: selector.textAt(index)
                highlighted: selector.highlightedIndex === index
            }
        }
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            text: charts.cameraMode && selector.currentIndex === 0
                ? "最近 30 秒" : "当前帧 · N = " + charts.snapshot.results.length
            color: Colors.textSecondary
        }
    }
    SignalChart {
        id: plotItem
        objectName: "visionInspectionPlot"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 76
        plot: charts.figure
        showTitle: false
        manualY: true
        yMinimum: 0
        yMaximum: charts.figure.limit || 4
    }
    RowLayout {
        visible: selector.currentIndex === 0 && charts.cameraMode
        Layout.fillWidth: true
        spacing: 5
        Rectangle { width: 14; height: 2; color: "#0072BD" }
        Label { text: "工作进程"; color: Colors.textSecondary }
        Rectangle { width: 14; height: 2; color: "#D95319" }
        Label { text: "预处理至返回"; color: Colors.textSecondary }
        Item { Layout.fillWidth: true }
    }
}
