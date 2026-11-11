import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

// 缺陷中心：左侧缺陷列表 / 中间缺陷详情与证据 / 右侧处理属性与历史时间线
Rectangle {
    id: root
    objectName: "defectPage"
    color: Colors.background

    property string filter: "all"
    property string keyword: ""
    property var allDefects: []
    property var filteredDefects: []
    property string currentId: ""
    property var detail: ({})
    property var events: []
    property var evidence: []
    property var usersData: []
    property string message: ""
    property string noteText: ""
    property string feedbackText: ""
    property string assignNote: ""
    property string dueText: ""
    property int evidenceStamp: 0
    property string pendingPhotoDefect: ""
    function capturePhoto() {
        if(!root.currentId||root.detail.status==="closed")return
        if(!App.evidence.cameraOpen()&&!App.camera.opening){
            var devs=App.evidence.cameras()
            if(!devs.length){root.message="未检测到摄像头设备";return}
            if(!App.evidence.openCamera(devs[0])){root.message=App.camera.status;return}
        }
        if(!App.camera.hasFrame){root.pendingPhotoDefect=root.currentId;root.message="正在等待摄像头画面…";return}
        var r=App.evidence.capture("",root.currentId,root.noteText)
        if(!r.ok){root.message=r.error;return}
        root.message="证据已保存："+r.path.split("/").pop();root.noteText="";root.evidenceStamp++;root.evidence=App.evidence.attachments("",root.currentId)
    }
    Connections{target:App.camera
        function onStateChanged(){if(root.pendingPhotoDefect&&App.camera.hasFrame){var same=root.visible&&root.pendingPhotoDefect===root.currentId;root.pendingPhotoDefect="";if(same)Qt.callLater(root.capturePhoto)}}
        function onError(message){if(root.pendingPhotoDefect){root.pendingPhotoDefect="";root.message=message}}
    }

    function refresh() {
        root.allDefects = App.defects.defects()
        root.usersData = App.defects.users()
        applyFilter()
        if (!root.currentId && root.filteredDefects.length) selectDefect(root.filteredDefects[0].id)
        else if (root.currentId) reloadDetail()
    }
    function applyFilter() {
        var out = []
        for (var i = 0; i < root.allDefects.length; i++) {
            var d = root.allDefects[i]
            if (root.filter !== "all" && d.status !== root.filter) continue
            if (root.keyword.length) {
                var kw = root.keyword.toLowerCase()
                if (d.code.toLowerCase().indexOf(kw) < 0 && d.title.toLowerCase().indexOf(kw) < 0
                    && (d.assetCode || "").toLowerCase().indexOf(kw) < 0) continue
            }
            out.push(d)
        }
        root.filteredDefects = out
        var found = false
        for (var n = 0; n < out.length; n++) if (out[n].id === root.currentId) found = true
        if (!found) {
            if (out.length) root.selectDefect(out[0].id)
            else { root.currentId = ""; root.detail = ({}); root.events = []; root.evidence = []; root.message = "" }
        }
    }
    function selectDefect(id) {
        root.currentId = id
        reloadDetail()
    }
    function reloadDetail() {
        if (!root.currentId) return
        root.detail = App.defects.defect(root.currentId)
        root.events = App.defects.events(root.currentId)
        root.evidence = App.evidence.attachments("", root.currentId)
        root.feedbackText = ""
        root.assignNote = ""
        root.noteText = ""
        root.message = ""
        root.dueText = Qt.formatDate(new Date(Date.now() + 7 * 86400000), "yyyy-MM-dd")
    }
    function afterAction(r) {
        if (!r.ok) { root.message = r.error; return }
        root.message = ""
        App.nav.push("defects")
        refresh()
    }

    Connections {
        target: App.defects
        function onDefectsChanged() { if (root.visible) refresh() }
        function onDefectUpdated() { if (root.visible) reloadDetail() }
    }
    onVisibleChanged: {if(visible)refresh();else pendingPhotoDefect=""}
    Component.onCompleted: refresh()

    Row {
        anchors.right:parent.right;anchors.top:parent.top;anchors.margins:16;spacing:8;z:2
        TouchButton {objectName:"newDefectButton";text:"新增缺陷";icon:"plus";autoWidth:true;height:36;variant:"primary";onClicked:defectEditor.open({})}
        TouchButton {text:"编辑";autoWidth:true;height:36;enabled:!!root.currentId&&!!root.detail.canEdit;onClicked:defectEditor.open(root.detail)}
        TouchButton {text:"删除";visible:false;autoWidth:true;height:36;variant:"ghost";enabled:false;onClicked:removeDefect.openDialog("删除缺陷","删除此缺陷及其处理历史、附件关联？","删除","取消")}
    }
    PageHeading { id: pageHeading; title: "缺陷中心"; anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 16 }

    ResponsiveRow {
        id: defectPanes
        anchors.top: pageHeading.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Dimensions.pagePadding
        breakpoint: 900; paneCount: 3; minimumPaneHeight: 500

        // 左：缺陷列表
        Rectangle {
            id: defectListPanel
            width: parent.stacked ? parent.width : Math.min(300, Math.max(240, parent.width * 0.28))
            height: parent.stacked ? 340 : parent.height
            color: Colors.panel
            radius: Dimensions.radius
            border.color: Colors.border
            border.width: 1
            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: Dimensions.gap
                Flow {
                    width: parent.width
                    height: childrenRect.height
                    spacing: 5
                    Repeater {
                        model: [
                            { label: "全部", filter: "all" },
                            { label: "待分派", filter: "unassigned" },
                            { label: "待处理", filter: "pending" },
                            { label: "处理中", filter: "in_progress" },
                            { label: "待复核", filter: "review" },
                            { label: "已关闭", filter: "closed" }
                        ]
                        TouchButton {
                            text: modelData.label
                            variant: root.filter === modelData.filter ? "tonal" : "ghost"
                            autoWidth: true
                            height: 34
                            onClicked: { root.filter = modelData.filter; root.applyFilter() }
                        }
                    }
                }
                FieldInput {
                    width: parent.width
                    height: 36
                    placeholder: "搜索编号 / 标题 / 设备…"
                    onTextEdited: { root.keyword = value; root.applyFilter() }
                }
                ListView {
                    width: parent.width
                    height: parent.height - y
                    clip: true
                    model: root.filteredDefects
                    delegate: Rectangle {
                        width: parent.width
                        height: 76
                        radius: Dimensions.radius
                        color: root.currentId === modelData.id ? Colors.panelAlt : "transparent"
                        border.color: root.currentId === modelData.id ? Colors.normal : "transparent"
                        border.width: root.currentId === modelData.id ? 1 : 0
                        Column {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 3
                            Row {
                                width: parent.width
                                spacing: 8
                                Text {
                                    text: modelData.code
                                    font.pixelSize: Typography.body
                                    font.weight: Font.Medium
                                    color: Colors.text
                                }
                                StatusBadge {
                                    status: modelData.status
                                    text: modelData.statusText
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Item { width: 1; height: 1 }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "紧急：" + modelData.priorityText
                                    font.pixelSize: Typography.small
                                    color: modelData.priority === "high" ? Colors.danger
                                         : modelData.priority === "medium" ? Colors.warning
                                         : Colors.textSecondary
                                }
                            }
                            Text {
                                text: modelData.title
                                font.pixelSize: Typography.small
                                color: Colors.textSecondary
                                elide: Text.ElideRight
                                width: parent.width
                            }
                            Text {
                                width: parent.width
                                elide: Text.ElideRight
                                text: (modelData.assetCode || "") + " · " + (modelData.assigneeName || "未分派")
                                font.pixelSize: Typography.small
                                color: Colors.textSecondary
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { root.selectDefect(modelData.id); defectPanes.showPane(defectMiddlePanel) }
                        }
                    }
                }
            }
        }

        // 中：缺陷详情 + 证据
        Rectangle {
            id: defectMiddlePanel
            width: parent.stacked ? parent.width : parent.width - defectListPanel.width - defectActionColumn.width - 2 * Dimensions.spacing
            height: parent.stacked ? 420 : parent.height
            color: Colors.panel
            border.color: Colors.border
            border.width: 1
            Flickable {
                anchors.fill: parent
                contentHeight: middleCol.height + 40
                clip: true
                Column {
                    id: middleCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 18
                    width: parent.width
                    spacing: Dimensions.spacing

                    Text { width: parent.width; text: root.detail.code || "缺陷详情"; wrapMode: Text.Wrap; font.pixelSize: Typography.h2; font.bold: true; color: Colors.text }
                    Flow { width: parent.width; spacing: 8
                        StatusBadge { status: root.detail.status || ""; text: root.detail.statusText || "" }
                        Text { visible: !!root.currentId; text: "优先级：" + (root.detail.priorityText || "—"); font.pixelSize: Typography.body; color: Colors.warning }
                    }
                    Text {
                        text: root.detail.title || "暂无待处理缺陷"
                        font.pixelSize: Typography.h2
                        color: Colors.textSecondary
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }
                    Text {
                        visible: !!root.currentId
                        text: "设备：" + (root.detail.assetCode || "-") + " " + (root.detail.assetName || "")
                              + "   处理人：" + (root.detail.assigneeName || "未分派")
                              + "   截止：" + (root.detail.dueAt || "-")
                              + "   创建：" + (root.detail.createdAt || "")
                        font.pixelSize: Typography.small
                        color: Colors.textSecondary
                        wrapMode: Text.WordWrap
                        width: parent.width
                    }

                    Rectangle {
                        width: parent.width
                        height: Math.max(90, defectDescription.implicitHeight + 24)
                        color: Colors.panel
                        radius: Dimensions.radius
                        border.color: Colors.border
                        border.width: 1
                        Text {
                            anchors.fill: parent
                            anchors.margins: 12
                            id: defectDescription
                            text: root.currentId ? root.detail.description || "暂无描述" : ""
                            font.pixelSize: Typography.body
                            color: Colors.text
                            wrapMode: Text.WordWrap
                        }
                    }

                    // 证据采集
                    RowLayout {
                        width: parent.width
                        spacing: Dimensions.gap
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: "现场照片（" + root.evidence.length + " 张）"
                            font.pixelSize: Typography.h2
                            font.weight: Font.Bold
                            color: Colors.text
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Item { width: 1; height: 1; Layout.fillWidth: true }
                        TouchButton {
                            text: root.detail.status === "closed" ? "已锁定" : "拍照"
                            variant: root.detail.status === "closed" ? "ghost" : "primary"
                            enabled: root.currentId.length > 0 && root.detail.status !== "closed"
                            autoWidth: true
                            onClicked: root.capturePhoto()
                        }
                    }
                    ListView {
                        width: parent.width
                        height: 120
                        clip: true
                        orientation: ListView.Horizontal
                        model: root.evidence
                        delegate: Rectangle {
                            width: 140
                            height: 110
                            color: Colors.panelAlt
                            radius: 6
                            Image {
                                anchors.fill: parent
                                anchors.margins: 4
                                fillMode: Image.PreserveAspectCrop
                                source: modelData.path ? "file://" + modelData.path : ""
                                sourceSize.width: 280
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: 4
                                text: modelData.createdAt
                                width: parent.width
                                elide: Text.ElideMiddle
                                font.pixelSize: Typography.body
                                color: Colors.textSecondary
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: root.message
                        font.pixelSize: Typography.body
                        color: root.message.indexOf("已") === 0 ? Colors.normal : Colors.danger
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // 右：处理属性 + 时间线
        Column {
            id: defectActionColumn
            width: parent.stacked ? parent.width : Math.min(330, Math.max(260, parent.width * 0.30))
            height: parent.stacked ? 700 : parent.height
            spacing: Dimensions.spacing

            Rectangle {
                width: parent.width
                height: 330
                color: Colors.panel
                radius: Dimensions.radius
                border.color: Colors.border
                border.width: 1
                Flickable {
                    anchors.fill: parent; clip: true; contentHeight: actionForm.implicitHeight + 28
                    Column {
                    id: actionForm
                    x: 14; y: 14; width: parent.width - 28
                    spacing: Dimensions.gap
                    Text {
                        text: "处理操作"
                        font.pixelSize: Typography.h2
                        font.weight: Font.Bold
                        color: Colors.text
                    }
                    Text {
                        visible: !root.currentId
                        text: "请先选择一条缺陷"
                        font.pixelSize: Typography.small
                        color: Colors.textSecondary
                    }

                    // 待分派：选择处理人 + 截止日期
                    Column {
                        visible: root.detail.status === "unassigned"
                        width: parent.width
                        spacing: Dimensions.gap
                        Text {
                            text: "处理人"
                            font.pixelSize: Typography.small
                            color: Colors.textSecondary
                        }
                                                AppComboBox {
                            id: assigneePicker; width: parent.width; height: 40
                            model: root.usersData; textRole: "name"; font.pixelSize: Typography.body
                            property int selectedIndex: currentIndex
                        }
                        Text {
                            text: "处理期限"
                            font.pixelSize: Typography.small
                            color: Colors.textSecondary
                        }
                        FieldInput {
                            width: parent.width
                            text: root.dueText
                            placeholder: "yyyy-MM-dd"
                            onEditingFinished: root.dueText = value
                        }
                        Text {
                            text: "分派说明"
                            font.pixelSize: Typography.small
                            color: Colors.textSecondary
                        }
                        Rectangle {
                            width: parent.width
                            height: 56
                            color: Colors.panelAlt
                            radius: 6
                            border.color: Colors.border
                            border.width: 1
                            TextEdit {
                                id: assignNoteEdit
                                objectName: "assignNoteInput"
                                anchors.fill: parent
                                anchors.margins: 8
                                font.pixelSize: Typography.body
                                color: Colors.text
                                wrapMode: TextEdit.Wrap
                                text: root.assignNote
                                onTextChanged: root.assignNote = text
                            }
                            Text {
                                visible: assignNoteEdit.text.length === 0
                                anchors.fill: parent
                                anchors.margins: 8
                                text: "分派说明…"
                                font.pixelSize: Typography.body
                                color: Colors.textSecondary
                                wrapMode: Text.WordWrap
                                verticalAlignment: Text.AlignTop
                            }
                        }
                        TouchButton {
                            text: "确认分派"
                            variant: "primary"
                            width: parent.width
                            enabled: root.currentId.length > 0 && App.session.permissions.assign
                            onClicked: {
                                var user = root.usersData[assigneePicker.selectedIndex]
                                var r = App.defects.assign(root.currentId, user ? user.id : "", root.dueText, root.assignNote)
                                afterAction(r)
                            }
                        }
                        Text {
                            visible: !App.session.permissions.assign
                            text: "需要主管或管理员权限"
                            font.pixelSize: Typography.small
                            color: Colors.warning
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }
                    }

                    // 待处理：开始处理
                    Column {
                        visible: root.detail.status === "pending"
                        width: parent.width
                        spacing: Dimensions.gap
                        Text {
                            text: "已分派给 " + (root.detail.assigneeName || "-")
                            font.pixelSize: Typography.small
                            color: Colors.textSecondary
                        }
                        TouchButton {
                            text: "开始处理"
                            variant: "primary"
                            width: parent.width
                            enabled: App.session.permissions.start_defect || App.session.permissions.review
                            onClicked: {
                                var r = App.defects.start(root.currentId, "开始处理")
                                afterAction(r)
                            }
                        }
                    }

                    // 处理中：提交整改结果
                    Column {
                        visible: root.detail.status === "in_progress"
                        width: parent.width
                        spacing: Dimensions.gap
                        Text {
                            text: "整改结果（必填）"
                            font.pixelSize: Typography.small
                            color: Colors.textSecondary
                        }
                        Rectangle {
                            width: parent.width
                            height: 90
                            color: Colors.panelAlt
                            radius: 6
                            border.color: Colors.border
                            border.width: 1
                            TextEdit {
                                id: feedbackEdit
                                objectName: "feedbackInput"
                                anchors.fill: parent
                                anchors.margins: 8
                                font.pixelSize: Typography.body
                                color: Colors.text
                                wrapMode: TextEdit.Wrap
                                text: root.feedbackText
                                onTextChanged: root.feedbackText = text
                            }
                            Text {
                                visible: feedbackEdit.text.length === 0
                                anchors.fill: parent
                                anchors.margins: 8
                                text: "填写维修/整改措施与结果…"
                                font.pixelSize: Typography.body
                                color: Colors.textSecondary
                                wrapMode: Text.WordWrap
                                verticalAlignment: Text.AlignTop
                            }
                        }
                        TouchButton {
                            text: "提交整改"
                            variant: "primary"
                            width: parent.width
                            onClicked: {
                                var r = App.defects.submitFeedback(root.currentId, root.feedbackText)
                                afterAction(r)
                            }
                        }
                    }

                    // 待复核：复核通过 / 不通过
                    Column {
                        visible: root.detail.status === "review"
                        width: parent.width
                        spacing: Dimensions.gap
                        Text {
                            text: "复核失败须填写原因"
                            font.pixelSize: Typography.small
                            color: Colors.textSecondary
                        }
                        Rectangle {
                            width: parent.width
                            height: 56
                            color: Colors.panelAlt
                            radius: 6
                            border.color: Colors.border
                            border.width: 1
                            TextEdit {
                                id: reviewNoteEdit
                                objectName: "reviewNoteInput"
                                anchors.fill: parent
                                anchors.margins: 8
                                font.pixelSize: Typography.body
                                color: Colors.text
                                wrapMode: TextEdit.Wrap
                                text: root.noteText
                                onTextChanged: root.noteText = text
                            }
                            Text {
                                visible: reviewNoteEdit.text.length === 0
                                anchors.fill: parent
                                anchors.margins: 8
                                text: "复核意见…"
                                font.pixelSize: Typography.body
                                color: Colors.textSecondary
                                wrapMode: Text.WordWrap
                                verticalAlignment: Text.AlignTop
                            }
                        }
                        Row {
                            width: parent.width
                            spacing: Dimensions.gap
                            TouchButton {
                                text: "复核通过"
                                variant: "primary"
                                autoWidth: true
                                enabled: App.session.permissions.review
                                onClicked: {
                                    var r = App.defects.review(root.currentId, true, root.noteText)
                                    afterAction(r)
                                }
                            }
                            TouchButton {
                                text: "退回整改"
                                variant: "danger"
                                autoWidth: true
                                enabled: App.session.permissions.review
                                onClicked: {
                                    var r = App.defects.review(root.currentId, false, root.noteText)
                                    afterAction(r)
                                }
                            }
                        }
                    }

                    // 已关闭
                    Column {
                        visible: root.detail.status === "closed"
                        width: parent.width
                        spacing: Dimensions.gap
                        Text {
                            text: "缺陷已关闭 · " + (root.detail.closedAt || "")
                            font.pixelSize: Typography.body
                            color: Colors.normal
                        }
                        Text {
                            text: "已关闭缺陷的证据与状态不可再修改，仅可查询归档。"
                            font.pixelSize: Typography.small
                            color: Colors.textSecondary
                            wrapMode: Text.WordWrap
                            width: parent.width
                        }
                    }
                }
                }
            }

            Rectangle {
                width: parent.width
                height: parent.height - 342
                color: Colors.panel
                radius: Dimensions.radius
                border.color: Colors.border
                border.width: 1
                Column {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: Dimensions.gap
                    Text {
                        text: "处理记录"
                        font.pixelSize: Typography.h2
                        font.weight: Font.Bold
                        color: Colors.text
                    }
                    ListView {
                        width: parent.width
                        height: parent.height - 40
                        clip: true
                        model: root.events
                        delegate: Rectangle {
                            width: parent.width
                            height: 86
                            color: index % 2 === 0 ? "transparent" : Colors.panelAlt
                            Column {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2
                                Row {
                                    width: parent.width
                                    spacing: 8
                                    Text {
                                        text: modelData.eventType
                                        font.pixelSize: Typography.small
                                        font.weight: Font.Medium
                                        color: Colors.info
                                    }
                                    Text {
                                        text: modelData.statusText
                                        font.pixelSize: Typography.small
                                        color: Colors.textSecondary
                                    }
                                    Text {
                                        width: Math.max(60, parent.width - 150)
                                        horizontalAlignment: Text.AlignRight
                                        text: modelData.createdAt
                                        font.pixelSize: Typography.body
                                        color: Colors.textSecondary
                                        elide: Text.ElideMiddle
                                    }
                                }
                                Text {
                                    text: modelData.note
                                    font.pixelSize: Typography.small
                                    color: Colors.text
                                    elide: Text.ElideRight
                                    width: parent.width
                                }
                                Text {
                                    text: modelData.actorName || "系统"
                                    font.pixelSize: Typography.body
                                    color: Colors.textSecondary
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    EntityEditor {id:defectEditor;objectName:"defectEditor";kind:"defect";onSaved:function(id){root.currentId=id;root.refresh()}}
    ConfirmDialog {id:removeDefect;z:210;onAccepted:{var r=App.defects.deleteDefect(root.currentId);if(r.ok){root.currentId="";root.refresh()}else root.message=r.error}}
}
