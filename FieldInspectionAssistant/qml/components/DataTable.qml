import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Item {
    id: root
    // columns: [{key, title, width, align}]
    property var columns: []
    // items: [{key: value}]
    property var items: []
    property var rowModel: null
    property bool showHeader: true
    property string emptyText: "暂无记录"
    property int rowHeight: 34
    property bool spreadsheet: false
    readonly property color gridColor: "#c5ccd4"
    property int selectedIndex: -1
    signal rowClicked(int index, var item)
    signal rowDoubleClicked(int index, var item)

    function availableWidth() { return Math.max(0, width - 24) }
    function columnWidth(column) {
        var total = 0, flexible = 0
        for (var i = 0; i < root.columns.length; i++) {
            if (root.columns[i].width === 0) flexible++
            else total += Number(root.columns[i].width || 100)
        }
        var available = root.availableWidth()
        if (flexible && available > total) return column.width === 0 ? (available - total) / flexible : Number(column.width || 100)
        var weight = column.width === 0 ? 140 : Number(column.width || 100)
        return available * weight / (total + flexible * 140)
    }
    function cellValue(column, item) {
        if (!item) return ""
        var value = item[column.key]
        if (value === undefined || value === null) return ""
        if (column.format === "datetime") {
            var date = new Date(String(value).replace(" ", "T"))
            if (!isNaN(date.getTime())) return Qt.formatDateTime(date, "MM-dd hh:mm")
        }
        return String(value)
    }

    Column {
        anchors.fill: parent
        Rectangle {
            visible: root.showHeader && root.columns.length > 0
            width: parent.width
            height: 31
            color: root.spreadsheet ? "#e8eef4" : Colors.panelAlt
            Row {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                Repeater {
                    model: root.columns
                    Item {
                        width: root.columnWidth(modelData)
                        height: parent.height
                        Rectangle { visible: root.spreadsheet; anchors.right: parent.right; height: parent.height; width: 1; color: root.gridColor }
                        Text {
                            width: parent.width - 8
                            elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left; anchors.leftMargin: root.spreadsheet ? 5 : 0
                            text: modelData.title
                            font.pixelSize: Typography.table
                            font.bold: root.spreadsheet
                            color: Colors.textSecondary
                        }
                    }
                }
            }
        }
        Rectangle { visible: root.showHeader && root.columns.length > 0; width: parent.width; height: 1; color: Colors.border }

        ListView {
            id: listView
            width: parent.width
            height: parent.height - (root.showHeader && root.columns.length > 0 ? 32 : 0)
            clip: true
            model: root.rowModel || root.items
            ScrollBar.vertical: ScrollBar { }
            delegate: Rectangle {
                id: row
                property int rowIndex: index
                property var rowData: root.rowModel ? model : modelData
                width: listView.width
                height: root.rowHeight
                color: rowIndex === root.selectedIndex ? Colors.infoDark : rowPointer.containsMouse ? Colors.primaryHover
                    : root.spreadsheet && rowIndex % 2 ? "#f7f9fb" : Colors.panel
                border.color: Colors.border
                border.width: 0
                Rectangle { anchors.bottom:parent.bottom; width:parent.width; height:1; color:root.spreadsheet ? root.gridColor : Colors.panelAlt }
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    Repeater {
                        model: root.columns
                        Item {
                            width: root.columnWidth(modelData)
                            height: parent.height
                            Rectangle { visible: root.spreadsheet; anchors.right: parent.right; height: parent.height; width: 1; color: root.gridColor }
                            Text {
                                width: parent.width - 8
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left; anchors.leftMargin: root.spreadsheet ? 5 : 0
                                text: root.cellValue(modelData, row.rowData)
                                font.family: Typography.uiFamily
                                font.pixelSize: Typography.table
                                color: Colors.text
                                elide: Text.ElideRight
                                maximumLineCount: 1
                            }
                        }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    id: rowPointer; hoverEnabled:true; cursorShape:Qt.PointingHandCursor
                    onClicked: if (row.rowData) root.rowClicked(row.rowIndex, row.rowData)
                    onDoubleClicked: if (row.rowData) root.rowDoubleClicked(row.rowIndex, row.rowData)
                }
            }
        }
    }
    Rectangle {
        visible: root.spreadsheet
        anchors.fill: parent
        color: "transparent"
        border.color: root.gridColor
    }

    EmptyState {
        anchors.fill: parent
        visible: listView.count === 0
        title: root.emptyText
    }
}
