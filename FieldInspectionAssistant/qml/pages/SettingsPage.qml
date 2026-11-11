import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
import "../components"

Rectangle {
    id: root
    objectName: "settingsPage"
    color: Colors.background

    property string currentSection: "hardware"
    readonly property var sections: [
        {id:"hardware", title:"设备与性能"},
        {id:"assistant", title:"助手偏好"},
        {id:"standards", title:"检查标准"},
        {id:"users", title:"用户与权限"},
        {id:"data", title:"数据与维护"}
    ]
    readonly property int currentTab: sections.findIndex(section => section.id === currentSection)
    property var thresholdItems: []
    property var minValues: ({})
    property var maxValues: ({})
    property string templateId: ""
    property string thresholdMessage: ""

    function selectSection(section) {
        if (section === "deviceInfo") {
            currentSection = "hardware"
            hardware.openPerformance()
        } else if (section === "runtime") {
            currentSection = "data"
            maintenance.selectEntity("runtime")
        } else if (sections.some(item => item.id === section)) {
            currentSection = section
            if (section === "hardware") hardware.openConnections()
            if (section === "data") maintenance.selectEntity("maintenance")
        }
    }

    function showRequestedSection() {
        const section = App.nav.param("section")
        if (section) selectSection(section)
    }

    function refreshThresholds() {
        const templates = App.assets.templates()
        templateId = templates.length ? templates[0].id : ""
        thresholdItems = App.inspection.templateItems()
        const minimum = {}, maximum = {}
        for (const item of thresholdItems) {
            minimum[item.key] = item.min ?? ""
            maximum[item.key] = item.max ?? ""
        }
        minValues = minimum
        maxValues = maximum
        thresholdMessage = ""
    }

    function saveThreshold(key) {
        const item = thresholdItems.find(row => row.key === key)
        if (!item) return
        const result = App.assets.updateThreshold(templateId, key, String(minValues[key]), String(maxValues[key]), item.unit || "")
        thresholdMessage = result.ok ? "已保存阈值 · " + item.name : result.error
    }

    function updateMonitoring() {
        App.hardware.active = visible && App.session.loggedIn && currentSection === "hardware"
        if (App.hardware.active) App.ai.refreshStatus()
    }

    onCurrentSectionChanged: updateMonitoring()
    onVisibleChanged: {
        if (visible) showRequestedSection()
        updateMonitoring()
    }
    Connections {
        target: App.nav
        function onPageChanged() {
            if (App.nav.page === "settings") root.showRequestedSection()
        }
    }
    Connections {
        target: App.session
        function onSessionChanged() { root.updateMonitoring() }
    }
    Timer {
        interval: 3000
        repeat: true
        running: App.hardware.active && root.visible && root.currentSection === "hardware"
        onTriggered: App.ai.refreshStatus()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 4
        spacing: 4

        AppTabs {
            Layout.fillWidth: true
            objectName: "settingsTabs"
            objectPrefix: "settingsTab_"
            sections: root.sections
            selected: root.currentSection
            onNavigate: function(section) { root.selectSection(section) }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: root.currentTab

            HardwareConnectionsPane { id: hardware }

            SettingsPanel {
                title: "问答显示"
                objectName: "settingsAssistantPanel"
                GridLayout {
                    Layout.alignment: Qt.AlignLeft | Qt.AlignTop
                    columns: 2
                    columnSpacing: 10
                    rowSpacing: 6

                    Label { text: "回答字数上限" }
                    AppComboBox {
                        objectName: "settingsAnswerLength"
                        Layout.preferredWidth: 164
                        textRole: "name"
                        valueRole: "value"
                        model: [{name:"短答 · 96 字",value:96}, {name:"常规 · 256 字",value:256},
                                {name:"详细 · 512 字",value:512}, {name:"长篇 · 1024 字",value:1024}]
                        currentIndex: model.findIndex(item => item.value === App.ai.answerLengthLimit)
                        displayText: currentIndex < 0 ? App.ai.answerLengthLimit + " 字" : currentText
                        onActivated: App.ai.answerLengthLimit = currentValue
                    }

                    Label { text: "答案字号" }
                    AppComboBox {
                        objectName: "settingsAnswerFont"
                        Layout.preferredWidth: 164
                        textRole: "name"
                        valueRole: "value"
                        model: [{name:"紧凑 · 14",value:14}, {name:"常规 · 16",value:16}, {name:"舒适 · 18",value:18}]
                        currentIndex: model.findIndex(item => item.value === App.ai.answerFontSize)
                        displayText: currentIndex < 0 ? String(App.ai.answerFontSize) : currentText
                        onActivated: App.ai.answerFontSize = currentValue
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: "适用于文字问答和图片问答，修改自动保存。"
                    color: Colors.textSecondary
                    wrapMode: Text.Wrap
                }
                Item { Layout.fillHeight: true }
            }

            SettingsPanel {
                id: standards
                objectName: "settingsStandardsPanel"
                title: "检查项目阈值"
                readonly property real typeWidth: width < 550 ? 62 : 94
                readonly property real limitWidth: width < 550 ? 65 : 98
                readonly property real unitWidth: width < 550 ? 38 : 62
                readonly property int actionWidth: 64
                readonly property int columnGap: 6

                function nameWidth(availableWidth) {
                    return Math.max(0, availableWidth - typeWidth - 2 * limitWidth
                                    - unitWidth - actionWidth - 5 * columnGap)
                }

                onVisibleChanged: if (visible) root.refreshThresholds()

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 28
                    color: Colors.panelAlt
                    border.color: Colors.border
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        spacing: standards.columnGap
                        Label { width: standards.nameWidth(parent.width); height: parent.height; text: "检查项目"; verticalAlignment: Text.AlignVCenter; font.bold: true }
                        Label { width: standards.typeWidth; height: parent.height; text: "方式"; verticalAlignment: Text.AlignVCenter }
                        Label { width: standards.limitWidth; height: parent.height; text: "下限"; verticalAlignment: Text.AlignVCenter }
                        Label { width: standards.limitWidth; height: parent.height; text: "上限"; verticalAlignment: Text.AlignVCenter }
                        Label { width: standards.unitWidth; height: parent.height; text: "单位"; verticalAlignment: Text.AlignVCenter }
                        Item { width: standards.actionWidth; height: parent.height }
                    }
                }
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ListView {
                        objectName: "settingsThresholdList"
                        anchors.fill: parent
                        clip: true
                        model: root.thresholdItems
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            readonly property bool editable: modelData.type === "measure" && !!App.session.permissions.manage_assets
                            width: ListView.view.width
                            height: 36
                            color: index % 2 ? Colors.panelAlt : Colors.panel
                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                spacing: standards.columnGap
                                Label {
                                    width: standards.nameWidth(parent.width); height: parent.height
                                    text: modelData.name; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                                }
                                Label {
                                    width: standards.typeWidth; height: parent.height
                                    text: modelData.type === "measure" ? "测量" : "目视检查"
                                    verticalAlignment: Text.AlignVCenter; color: Colors.textSecondary
                                }
                                FieldInput {
                                    y: 4; width: standards.limitWidth; height: 28
                                    readOnly: !editable
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    text: String(root.minValues[modelData.key] ?? "")
                                    placeholder: "—"
                                    onTextEdited: function(value) { root.minValues[modelData.key] = value }
                                }
                                FieldInput {
                                    y: 4; width: standards.limitWidth; height: 28
                                    readOnly: !editable
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    text: String(root.maxValues[modelData.key] ?? "")
                                    placeholder: "—"
                                    onTextEdited: function(value) { root.maxValues[modelData.key] = value }
                                }
                                Label {
                                    width: standards.unitWidth; height: parent.height
                                    text: modelData.unit || "—"; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
                                }
                                TouchButton {
                                    y: 4; width: standards.actionWidth; height: 28
                                    text: "保存"
                                    enabled: editable && !!root.templateId
                                    onClicked: root.saveThreshold(modelData.key)
                                }
                            }
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: !root.thresholdItems.length
                        text: "暂无检查项目"
                        color: Colors.textSecondary
                    }
                }
                Text { Layout.fillWidth: true; text: root.thresholdMessage; visible: text.length > 0; font.pixelSize: Typography.body; color: Colors.info; wrapMode: Text.Wrap }
            }

            UserManagementPage {}

            SettingsPanel {
                objectName: "settingsMaintenancePanel"
                ProductPage {
                    id: maintenance
                    objectName: "settingsDataManagement"
                    embedded: true
                    showHeading: false
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Component.onCompleted: setup({section:"data"})
                }
            }
        }
    }
}
