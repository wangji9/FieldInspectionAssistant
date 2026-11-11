import QtQuick
import QtQuick.Controls
import "../theme"

// Reflow panes vertically on narrow windows; keep short windows scrollable.
ScrollView {
    id: viewport
    property int breakpoint: 820
    property int paneCount: 2
    property int minimumPaneHeight: 380
    readonly property bool stacked: availableWidth < breakpoint
    default property alias panes: grid.data
    function showPane(item) {
        if (stacked && contentItem && item)
            contentItem.contentY = Math.min(item.y, Math.max(0, grid.height - availableHeight))
    }
    clip: true
    contentWidth: availableWidth
    contentHeight: grid.height
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    contentData: Grid {
        id: grid
        readonly property bool stacked: viewport.stacked
        width: viewport.availableWidth
        height: viewport.stacked ? implicitHeight : Math.max(viewport.availableHeight, viewport.minimumPaneHeight)
        columns: viewport.stacked ? 1 : viewport.paneCount
        spacing: Dimensions.spacing
    }
}
