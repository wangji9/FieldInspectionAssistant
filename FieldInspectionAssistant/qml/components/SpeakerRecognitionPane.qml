import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../theme"

Item {
    id: pane
    objectName: "speakerRecognitionPane"
    property string latestRecording: ""
    property url source: ""
    property string sourceLabel: ""
    property string profileId: ""
    property string feedback: ""
    property int selectedSegment: -1
    readonly property var service: App.speakerRecognition
    readonly property var profile: service.profiles.find(row => row.id === profileId) || ({})
    readonly property bool manager: App.session.permissions.settings
    readonly property var languages: [{id:"",name:"不设语种"}, {id:"zh",name:"中文"},
        {id:"en",name:"英语"}, {id:"de",name:"德语"}, {id:"fr",name:"法语"},
        {id:"es",name:"西班牙语"}, {id:"ja",name:"日语"}, {id:"ko",name:"韩语"}, {id:"ru",name:"俄语"}]

    function useRecording(path, name) {
        source = "file://" + path
        sourceLabel = name || path.split("/").pop()
        feedback = ""
    }
    function selectProfile(entry) {
        profileId = entry.id || ""
        nameField.text = entry.name || ""
        const index = languages.findIndex(lang => lang.id === (entry.language || ""))
        languageBox.currentIndex = Math.max(0, index)
    }
    function rangeStart() { return startTime.value }
    function rangeLength() { return duration.value }

    Connections {
        target: App.session
        function onSessionEnding() {
            pane.source = ""
            pane.sourceLabel = ""
            pane.feedback = ""
            pane.selectedSegment = -1
            pane.selectProfile({})
        }
    }

    FileDialog {
        id: audioFile
        title: "选择声纹分析音频"
        nameFilters: ["音频与视频 (*.wav *.mp3 *.flac *.ogg *.m4a *.aac *.mp4 *.mkv *.webm)", "所有文件 (*)"]
        onAccepted: {
            pane.source = selectedFile
            pane.sourceLabel = decodeURIComponent(selectedFile.toString().split("/").pop())
            pane.feedback = ""
        }
    }
    FileDialog {
        id: resultFile
        title: "导出发言时间段"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["JSON (*.json)"]
        onAccepted: pane.feedback = pane.service.exportResult(selectedFile) ? "识别结果已导出" : "导出失败"
    }
    ConfirmDialog {
        id: confirmRemove
        z: 10
        onAccepted: {
            if (pane.service.removeProfile(pane.profileId))
                pane.selectProfile({})
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        Flow {
            id: controls
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            spacing: 5
            TouchButton { text: "打开音频…"; height: 30; autoWidth: true; enabled: !pane.service.busy; onClicked: audioFile.open() }
            TouchButton {
                text: "使用最近录音"; height: 30; autoWidth: true
                enabled: !!pane.latestRecording && !pane.service.busy
                onClicked: pane.useRecording(pane.latestRecording, "")
            }
            RowLayout {
                height: 30
                Label { text: "起点 (s)" }
                SpinBox { id: startTime; from: 0; to: 86400; value: 0; editable: true; Layout.preferredWidth: 84 }
                Label { text: "时长 (s)" }
                SpinBox { id: duration; from: 1; to: 600; value: 30; editable: true; Layout.preferredWidth: 78 }
                Label { text: "阈值" }
                SpinBox {
                    id: threshold
                    from: 45; to: 99; value: 77; editable: true
                    Layout.preferredWidth: 83
                    textFromValue: function(value, locale) { return Number(value/100).toLocaleString(locale, "f", 2) }
                    valueFromText: function(text, locale) { return Math.round(Number.fromLocaleString(locale, text)*100) }
                }
            }
            CheckBox { id: transcribe; height: 30; text: "按档案语种转写"; enabled: pane.service.whisperReady && !pane.service.busy }
            TouchButton {
                objectName: "startSpeakerRecognition"
                text: "识别"; height: 30; autoWidth: true; variant: "primary"
                enabled: !!pane.sourceLabel && pane.service.ready && !pane.service.busy
                onClicked: {
                    pane.feedback = ""
                    pane.selectedSegment = -1
                    pane.service.analyze(pane.source, pane.rangeStart(), pane.rangeLength(),
                                         threshold.value/100, transcribe.enabled && transcribe.checked)
                }
            }
            TouchButton { text: "加载模型"; height: 30; autoWidth: true; visible: !pane.service.ready && !pane.service.busy; onClicked: pane.service.warmup() }
            TouchButton { text: "停止"; height: 30; autoWidth: true; visible: pane.service.busy; onClicked: pane.service.cancel() }
            TouchButton {
                text: "导出…"; height: 30; autoWidth: true
                enabled: pane.service.segments.length > 0 && !pane.service.busy
                onClicked: resultFile.open()
            }
        }
        Label {
            Layout.fillWidth: true
            text: [pane.sourceLabel || pane.service.metrics.source, pane.feedback || pane.service.status].filter(Boolean).join("　·　")
            color: Colors.textSecondary; elide: Text.ElideMiddle
            ToolTip.text: text
            ToolTip.visible: statusHover.hovered
            HoverHandler { id: statusHover }
        }
        GridLayout {
            id: panels
            columns: width >= 760 ? 2 : 1
            Layout.fillWidth: true; Layout.fillHeight: true
            columnSpacing: 7; rowSpacing: 6
            GroupBox {
                title: "声纹档案"
                Layout.fillWidth: panels.columns === 1
                Layout.fillHeight: true
                Layout.preferredWidth: 314
                Layout.preferredHeight: panels.columns === 1 ? 220 : 0
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 5
                    DataTable {
                        objectName: "speakerProfiles"
                        spreadsheet: true
                        Layout.fillWidth: true; Layout.fillHeight: true
                        items: pane.service.profiles
                        emptyText: "暂无档案"
                        columns: [{key:"name",title:"姓名",width:0},{key:"language",title:"语种",width:52},
                                  {key:"samples",title:"样本",width:50}]
                        selectedIndex: items.findIndex(row => row.id === pane.profileId)
                        onRowClicked: function(index, entry) { pane.selectProfile(entry) }
                    }
                    RowLayout {
                        visible: pane.manager
                        Layout.fillWidth: true
                        TextField {
                            id: nameField
                            placeholderText: "发言人姓名"; maximumLength: 80
                            Layout.fillWidth: true; Layout.minimumWidth: 90
                        }
                        AppComboBox {
                            id: languageBox
                            Layout.preferredWidth: 100
                            model: pane.languages; textRole: "name"; valueRole: "id"
                        }
                    }
                    Flow {
                        visible: pane.manager
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 4
                        TouchButton {
                            text: "新建并录入"; height: 28; autoWidth: true
                            enabled: !!pane.sourceLabel && !!nameField.text.trim() && pane.service.ready && !pane.service.busy
                            onClicked: pane.service.enroll(pane.source, pane.rangeStart(), Math.min(30,pane.rangeLength()),
                                                           "", nameField.text, languageBox.currentValue)
                        }
                        TouchButton {
                            text: "追加样本"; height: 28; autoWidth: true
                            enabled: !!pane.sourceLabel && !!pane.profile.id && pane.profile.samples < 3
                                && pane.service.ready && !pane.service.busy
                            onClicked: pane.service.enroll(pane.source, pane.rangeStart(), Math.min(30,pane.rangeLength()),
                                                           pane.profileId, "", "")
                        }
                        TouchButton {
                            text: "保存档案"; height: 28; autoWidth: true
                            enabled: !!pane.profile.id && !!nameField.text.trim() && !pane.service.busy
                            onClicked: pane.service.updateProfile(pane.profileId, nameField.text, languageBox.currentValue)
                        }
                        TouchButton {
                            text: "删除"; height: 28; autoWidth: true
                            enabled: !!pane.profile.id && !pane.service.busy
                            onClicked: confirmRemove.openDialog("删除声纹档案", "删除「" + pane.profile.name + "」的全部声纹样本。", "删除", "取消")
                        }
                        TouchButton {
                            text: "清除选择"; height: 28; autoWidth: true
                            enabled: !!pane.profile.id && !pane.service.busy
                            onClicked: pane.selectProfile({})
                        }
                    }
                }
            }

            GroupBox {
                title: "发言时段"
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.minimumHeight: 135
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 5
                    Label {
                        Layout.fillWidth: true; elide: Text.ElideRight
                        text: pane.service.metrics.embeddings
                            ? "%1 个有效窗口　提取 %2 ms　总用时 %3 ms"
                                .arg(pane.service.metrics.embeddings).arg(pane.service.metrics.embedding_ms).arg(pane.service.metrics.total_ms)
                            : "时间戳基于原始录音"
                        color: Colors.textSecondary
                    }
                    SpeakerTimeline {
                        objectName: "speakerTimeline"
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(implicitHeight, Math.max(150,pane.height*0.36))
                        visible: pane.service.waveform.length > 0
                        samples: pane.service.waveform
                        segments: pane.service.segments
                        profiles: pane.service.profiles
                        metrics: pane.service.metrics
                        selectedIndex: pane.selectedSegment
                        onSelected: function(index) { pane.selectedSegment = index }
                    }
                    DataTable {
                        objectName: "speakerSegments"
                        spreadsheet: true
                        Layout.fillWidth: true; Layout.fillHeight: true
                        items: pane.service.segments
                        selectedIndex: pane.selectedSegment
                        onRowClicked: function(index, entry) { pane.selectedSegment = index }
                        emptyText: "选择一段录音后运行识别。"
                        columns: [{key:"range",title:"起止时间",width:178},
                                  {key:"speaker",title:"发言人",width:92},{key:"status",title:"状态",width:80},
                                  {key:"score",title:"余弦",width:55},{key:"language",title:"语种",width:45},
                                  {key:"transcript",title:"转写",width:0}]
                    }
                    ScrollView {
                        id: transcriptView
                        readonly property var row: pane.service.segments[pane.selectedSegment] || ({})
                        visible: !!row.transcript || row.transcript_status === "empty"
                        Layout.fillWidth: true; Layout.preferredHeight: 75
                        contentWidth: availableWidth
                        TextArea {
                            readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                            text: transcriptView.row.transcript || "该时段未返回转写文本。"
                        }
                    }
                }
            }
        }
    }
}
