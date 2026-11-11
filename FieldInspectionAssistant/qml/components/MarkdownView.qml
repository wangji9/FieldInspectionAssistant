import QtQuick
import QtQuick.Controls
import "../theme"

ScrollView {
    id: root
    property string markdown: ""
    property bool streaming: false
    property bool followOutput: true
    property string rendered: ""
    clip: true
    contentWidth: availableWidth
    contentHeight: body.height
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: ScrollBar.AsNeeded
    function render() { rendered = App.ai.renderMarkdown(markdown, availableWidth) }
    onMarkdownChanged: update.restart()
    onAvailableWidthChanged: update.restart()
    onStreamingChanged: if (streaming) followOutput = true
    Timer { id: update; interval: 45; onTriggered: root.render() }
    Connections { target: App.ai; function onPreferencesChanged() { root.render() } }
    TextArea {
        id: body
        width: root.availableWidth
        implicitHeight: contentHeight + topPadding + bottomPadding
        height: Math.max(root.availableHeight, implicitHeight)
        text: root.rendered
        textFormat: TextEdit.RichText
        readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
        font.pixelSize: App.ai.answerFontSize; color: Colors.text
        leftPadding: 8; rightPadding: 10; topPadding: 4; bottomPadding: 16
        background: Rectangle { color: Colors.panel }
        onLinkActivated: function(link) { if (/^https?:\/\//.test(link)) Qt.openUrlExternally(link) }
        onContentHeightChanged: if (root.streaming && root.followOutput) scrollToEnd.restart()
    }
    Timer {
        id: scrollToEnd; interval: 0
        onTriggered: root.contentItem.contentY = Math.max(0, root.contentItem.contentHeight - root.contentItem.height)
    }
    Connections {
        target: root.contentItem
        function onMovementEnded() { root.followOutput = root.contentItem.contentY >= root.contentItem.contentHeight - root.contentItem.height - 36 }
    }
}
