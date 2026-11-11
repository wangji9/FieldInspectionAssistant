import QtQuick
import "../theme"

Rectangle {
    id: root
    property string text: ""
    property string status: ""
    visible: root.text.length > 0
    height: 26
    width: badgeContent.implicitWidth + 20
    radius: Dimensions.radius
    color: {
        var s = root.status
        if (["closed", "normal", "completed", "submitted"].indexOf(s) >= 0) return Colors.normalDark
        if (["pending", "unassigned", "draft", "overdue"].indexOf(s) >= 0) return Colors.warningDark
        if (["review", "in_progress", "reopened"].indexOf(s) >= 0) return Colors.infoDark
        if (["abnormal", "error"].indexOf(s) >= 0) return Colors.dangerDark
        return Colors.panelAlt
    }
    Text {
        id: badgeContent
        anchors.centerIn: parent
        text: root.text
        font.pixelSize: Typography.small
        color: {
            var s = root.status
            if (["closed", "normal", "completed", "submitted"].indexOf(s) >= 0) return Colors.success
            if (["pending", "unassigned", "draft", "overdue"].indexOf(s) >= 0) return Colors.warning
            if (["review", "in_progress", "reopened"].indexOf(s) >= 0) return Colors.info
            if (["abnormal", "error"].indexOf(s) >= 0) return Colors.danger
            return Colors.textSecondary
        }
    }
}
