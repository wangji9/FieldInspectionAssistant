import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

ColumnLayout {
    id: tree
    required property var workspace
    signal resourceActivated(string type, string id)
    readonly property string projectId: workspace.project.id || ""
    property var expanded: ({project:true, devices:true, reports:true})
    property string selectedKey: ""
    spacing: 0

    onProjectIdChanged: {
        expanded = {project:true, devices:true, reports:true}
        selectedKey = ""
    }

    readonly property var rows: {
        if (!projectId) return []
        const result = [{key:"project", label:workspace.project.name, group:true, depth:0}]
        if (!expanded.project) return result
        const groups = [
            {key:"devices", label:"设备档案", type:"device", items:workspace.devices, labelKey:"name"},
            {key:"evidence", label:"原始资料", type:"evidence", items:workspace.evidence, labelKey:"title"},
            {key:"events", label:"融合结果", type:"event", items:workspace.events, labelKey:"point"},
            {key:"reports", label:"工程报告", type:"report", items:workspace.reports, labelKey:"path"}
        ]
        for (const group of groups) {
            result.push({key:group.key, label:group.label, group:true, depth:1,
                         count:group.items.length})
            if (!expanded[group.key]) continue
            for (const item of group.items) {
                const fullLabel = item[group.labelKey]
                const label = group.type === "report" ? fullLabel.split("/").pop() : fullLabel
                result.push({key:group.type + "/" + item.id, label:label, type:group.type,
                             id:item.id, depth:2, detail:item.device || ""})
            }
        }
        return result
    }

    Label {
        Layout.fillWidth: true
        text: "工程资源树"
        padding: 5
        background: Rectangle { color: Colors.panelAlt }
    }
    Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: "#d1d1d1" }
    ListView {
        id: nodes
        Layout.fillWidth: true
        Layout.fillHeight: true
        model: tree.rows
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Label {
            visible: !tree.projectId
            x: 7; y: 8; width: Math.max(0, nodes.width - 14)
            text: "未打开工程"
            color: Colors.muted
        }
        delegate: ItemDelegate {
            id: node
            required property var modelData
            objectName: "projectResource-" + modelData.key
            width: nodes.width
            height: 24
            padding: 0
            leftPadding: 6 + modelData.depth * 10
            rightPadding: 9
            highlighted: modelData.key === tree.selectedKey
            Accessible.name: modelData.label
            onClicked: {
                if (modelData.group) {
                    const next = Object.assign({}, tree.expanded)
                    next[modelData.key] = !next[modelData.key]
                    tree.expanded = next
                } else {
                    tree.selectedKey = modelData.key
                    tree.resourceActivated(modelData.type, modelData.id)
                }
            }
            ToolTip.visible: hovered && label.truncated
            ToolTip.delay: 450
            ToolTip.text: modelData.detail ? modelData.detail + " / " + modelData.label : modelData.label
            background: Rectangle {
                color: node.highlighted ? Colors.infoDark : node.hovered ? Colors.primaryHover : "white"
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width; height: 1
                    color: "#e7e7e7"
                    visible: node.modelData.depth === 0
                }
            }
            contentItem: RowLayout {
                spacing: 4
                Label {
                    visible: !!node.modelData.group
                    Layout.preferredWidth: 10
                    text: tree.expanded[node.modelData.key] ? "▾" : "▸"
                    color: Colors.textSecondary
                }
                Label {
                    id: label
                    Layout.fillWidth: true
                    text: node.modelData.label
                    font.bold: node.modelData.depth === 0
                    elide: Text.ElideRight
                }
                Label {
                    visible: node.modelData.count !== undefined && node.modelData.count > 0
                    text: node.modelData.count ?? ""
                    color: Colors.textSecondary
                }
            }
        }
    }
}
