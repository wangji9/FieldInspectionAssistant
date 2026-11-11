from pathlib import Path
p=Path('FieldInspectionAssistant/qml/pages/InspectionPage.qml')
s=p.read_text(encoding='utf8').replace('onEdited: root.valueText = value', 'onValueEdited: root.valueText = value').replace('label: "测量值" + (root.currentItem() && root.currentItem().unit ? " ("+root.currentItem().unit+")" : "")', 'label: root.currentItem() && root.currentItem().unit ? "数值 / " + root.currentItem().unit : "数值"')
p.write_text(s,encoding='utf8')
p=Path('FieldInspectionAssistant/qml/pages/DefectPage.qml'); s=p.read_text(encoding='utf8')
s=s.replace('        root.filteredDefects = out','''        root.filteredDefects = out
        var found = false
        for (var n = 0; n < out.length; n++) if (out[n].id === root.currentId) found = true
        if (!found) {
            if (out.length) root.selectDefect(out[0].id)
            else { root.currentId = ""; root.detail = ({}); root.events = []; root.evidence = []; root.message = "" }
        }''',1)
# Replace the ad-hoc dropdown, whose popup was clipped by the scrolling form.
a=s.index('                        Rectangle {\n                            id: assigneePicker')
b=s.index('                        Text {\n                            text: "处理期限"',a)
s=s[:a]+'''                        ComboBox {
                            id: assigneePicker; width: parent.width; height: 40
                            model: root.usersData; textRole: "name"; font.pixelSize: 17
                            property int selectedIndex: currentIndex
                        }
'''+s[b:]
# Replace cramped defect title row with vertical summary.
a=s.index('                    Row {\n                        width: parent.width\n                        spacing: 10',s.index('id: middleCol'))
b=s.index('                    Text {\n                        text: root.detail.title',a)
s=s[:a]+'''                    Text { width: parent.width; text: root.detail.code || "缺陷详情"; wrapMode: Text.Wrap; font.pixelSize: 22; font.bold: true; color: Colors.text }
                    Flow { width: parent.width; spacing: 8
                        StatusBadge { status: root.detail.status || ""; text: root.detail.statusText || "" }
                        Text { visible: !!root.currentId; text: "优先级：" + (root.detail.priorityText || "—"); font.pixelSize: 16; color: Colors.warning }
                    }
'''+s[b:]
s=s.replace('                    Rectangle {\n                        width: parent.width\n                        height: 110','                    Rectangle {\n                        width: parent.width\n                        height: Math.max(90, defectDescription.implicitHeight + 24)',1)
s=s.replace('                            text: root.detail.description || "暂无描述"','                            id: defectDescription\n                            text: root.currentId ? root.detail.description || "暂无描述" : ""',1)
s=s.replace('                    Row {\n                        width: parent.width\n                        spacing: Dimensions.gap\n                        Text {\n                            text: "现场照片', '                    RowLayout {\n                        width: parent.width\n                        spacing: Dimensions.gap\n                        Text {\n                            Layout.fillWidth: true\n                            elide: Text.ElideRight\n                            text: "现场照片',1)
s=s.replace('text: root.detail.status === "closed" ? "已关闭·证据锁定" : "拍照"','text: root.detail.status === "closed" ? "已锁定" : "拍照"',1)
s=s.replace('height: 64\n                            color: index', 'height: 86\n                            color: index',1)
s=s.replace('text: modelData.createdAt\n                                font.pixelSize: 16','text: modelData.createdAt\n                                width: parent.width\n                                elide: Text.ElideMiddle\n                                font.pixelSize: 16',1)
s=s.replace('text: (modelData.assetCode || "") + " · " + (modelData.assigneeName || "未分派")','width: parent.width\n                                elide: Text.ElideRight\n                                text: (modelData.assetCode || "") + " · " + (modelData.assigneeName || "未分派")',1)
p.write_text(s,encoding='utf8')
