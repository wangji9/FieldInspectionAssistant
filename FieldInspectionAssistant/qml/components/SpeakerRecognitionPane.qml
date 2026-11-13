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
    readonly property bool microphone: sourceMode.currentIndex === 0
    readonly property var profile: service.profiles.find(row => row.id === profileId) || ({})
    readonly property bool manager: App.session.permissions.settings
    function useRecording(path, name) {
        sourceMode.currentIndex = 1
        source = "file://" + path
        sourceLabel = name || path.split("/").pop()
        feedback = ""
    }
    function rangeStart() { return startTime.value }
    function rangeLength() { return duration.value }
    function leaveMicrophone() {
        if (service.capturing)
            service.stopLive()
    }

    onVisibleChanged: if (!visible) leaveMicrophone()
    Component.onDestruction: if (service) leaveMicrophone()

    Connections {
        target: App.session
        function onSessionEnding() {
            pane.source = ""
            pane.sourceLabel = ""
            pane.feedback = ""
            pane.selectedSegment = -1
            pane.profileId = ""
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
                pane.profileId = ""
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
            AppComboBox {
                id: sourceMode
                objectName: "speakerSourceMode"
                width: 132; height: 30
                model: ["实时麦克风", "音频文件"]
                enabled: !pane.service.busy
                Accessible.name: "智能声纹分析输入"
            }
            TouchButton {
                text: "打开音频…"; height: 30; autoWidth: true; visible: !pane.microphone
                enabled: !pane.service.busy; onClicked: audioFile.open()
            }
            TouchButton {
                text: "使用最近录音"; height: 30; autoWidth: true
                visible: !pane.microphone
                enabled: !!pane.latestRecording && !pane.service.busy
                onClicked: pane.useRecording(pane.latestRecording, "")
            }
            RowLayout {
                height: 30
                visible: !pane.microphone
                Label { text: "起点 (s)" }
                SpinBox { id: startTime; from: 0; to: 86400; value: 0; editable: true; Layout.preferredWidth: 84 }
                Label { text: "时长 (s)" }
                SpinBox { id: duration; from: 1; to: 600; value: 30; editable: true; Layout.preferredWidth: 78 }
                Label { text: "发言人数" }
                SpinBox {
                    id: expectedSpeakers
                    from: 0; to: 20; value: 0; editable: true
                    enabled: !pane.service.busy
                    Layout.preferredWidth: 94
                    textFromValue: function(value, locale) { return value ? Number(value).toLocaleString(locale, "f", 0) : "自动" }
                    valueFromText: function(text, locale) { return text === "自动" ? 0 : Number.fromLocaleString(locale, text) }
                    validator: RegularExpressionValidator { regularExpression: /^(自动|[0-9]{1,2})$/ }
                }
            }
            CheckBox {
                id: transcribe
                height: 30; text: "发言转写"; checked: true
                enabled: pane.service.whisperReady && !pane.service.busy
            }
            AppComboBox {
                id: transcriptLanguage
                width: 98; height: 30
                model: [{name:"中文",code:"zh"},{name:"自动语种",code:""},{name:"英语",code:"en"}]
                textRole: "name"; valueRole: "code"
                enabled: transcribe.enabled && transcribe.checked
                Accessible.name: "转写语言"
            }
            TouchButton {
                objectName: "startLiveSpeakerRecognition"
                visible: pane.microphone && pane.service.liveMode === ""
                text: "开始智能分析"; height: 30; autoWidth: true; variant: "primary"
                enabled: pane.service.ready && !pane.service.busy
                onClicked: {
                    pane.feedback = ""
                    pane.selectedSegment = -1
                    pane.service.startSmartAnalysisWithTranscript(transcribe.checked, transcriptLanguage.currentValue)
                }
            }
            TouchButton {
                objectName: "stopLiveSpeakerCapture"
                visible: pane.service.liveMode !== ""
                enabled: pane.service.capturing
                text: "结束分析"
                height: 30; autoWidth: true; variant: "primary"
                onClicked: pane.service.stopLive()
            }
            TouchButton {
                objectName: "startSpeakerRecognition"
                text: "分析音频"; height: 30; autoWidth: true; variant: "primary"; visible: !pane.microphone
                enabled: !!pane.sourceLabel && pane.service.ready && !pane.service.busy
                onClicked: {
                    pane.feedback = ""
                    pane.selectedSegment = -1
                    pane.service.analyzeSmartMeeting(pane.source, pane.rangeStart(), pane.rangeLength(),
                        transcribe.enabled && transcribe.checked, transcriptLanguage.currentValue, expectedSpeakers.value)
                }
            }
            TouchButton { text: "加载模型"; height: 30; autoWidth: true; visible: !pane.service.ready && !pane.service.busy; onClicked: pane.service.warmup() }
            TouchButton {
                text: "取消分析"
                height: 30; autoWidth: true
                visible: pane.service.busy && pane.service.liveMode === ""
                onClicked: pane.service.cancel()
            }
            TouchButton {
                text: "导出…"; height: 30; autoWidth: true
                enabled: pane.service.segments.length > 0 && !pane.service.busy
                onClicked: resultFile.open()
            }
        }
        Label {
            visible: !!text
            Layout.fillWidth: true
            text: pane.service.operationError || pane.feedback || (!pane.microphone && pane.sourceLabel ? pane.sourceLabel : "")
            color: Colors.textSecondary; elide: Text.ElideMiddle
        }
        ScrollView {
            id: panelScroll
            readonly property bool narrow: width < 640
            readonly property real panelHeight: Math.max(height - topPadding - bottomPadding, narrow ? 750 : 580)
            Layout.fillWidth: true; Layout.fillHeight: true
            Layout.minimumHeight: 0
            contentWidth: availableWidth
            contentHeight: panelHeight
            clip: true
            GridLayout {
                id: panels
                width: panelScroll.availableWidth
                height: panelScroll.panelHeight
                columns: panelScroll.narrow ? 1 : 2
                columnSpacing: 7; rowSpacing: 6
                GroupBox {
                    title: "声纹档案"
                    Layout.fillWidth: panels.columns === 1
                    Layout.fillHeight: true
                    Layout.preferredWidth: panelScroll.width < 820 ? 155 : 210
                    Layout.preferredHeight: panels.columns === 1 ? 155 : 0
                    Layout.maximumHeight: panels.columns === 1 ? 155 : Number.POSITIVE_INFINITY
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 5
                        DataTable {
                            objectName: "speakerProfiles"
                            spreadsheet: true
                            Layout.fillWidth: true; Layout.fillHeight: true
                            items: pane.service.profiles
                            emptyText: "检测到新发言人后自动保存档案"
                            columns: [{key:"name",title:"发言人",width:0},{key:"samples",title:"样本",width:52}]
                            selectedIndex: items.findIndex(row => row.id === pane.profileId)
                            onRowClicked: function(index, entry) { pane.profileId = pane.profileId === entry.id ? "" : entry.id }
                        }
                        TouchButton {
                            visible: pane.manager
                            text: "删除所选档案"; height: 28; autoWidth: true
                            enabled: !!pane.profile.id && !pane.service.busy
                            onClicked: confirmRemove.openDialog("删除声纹档案", "删除「" + pane.profile.name + "」的全部声纹样本。", "删除", "取消")
                        }
                    }
                }

                GroupBox {
                    title: "智能声纹分析"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.minimumHeight: 135
                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 5
                        SpeakerTimeline {
                            id: speakerPlot
                            objectName: "speakerTimeline"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: Math.min(330, implicitHeight)
                            Layout.preferredHeight: implicitHeight
                            samples: pane.service.waveform
                            segments: pane.service.segments
                            profiles: pane.service.profiles
                            metrics: pane.service.metrics
                            spectrogram: pane.service.spectrogram
                            selectedIndex: pane.selectedSegment
                            profileId: pane.profileId
                            onSelected: function(index) {
                                pane.selectedSegment = index
                                if (index >= 0)
                                    speechLog.positionViewAtIndex(index, ListView.Contain)
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 147
                            Layout.minimumHeight: 122
                            color: "#ffffff"
                            border.color: Colors.border

                            ColumnLayout {
                                anchors { fill: parent; margins: 6 }
                                spacing: 4
                                Label {
                                    text: "发言记录"
                                    font.bold: true
                                    font.pixelSize: 13
                                    color: "#263e53"
                                    Layout.leftMargin: 4
                                }
                                ListView {
                                    id: speechLog
                                    objectName: "speakerSegments"
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    clip: true
                                    spacing: 4
                                    model: pane.service.segments
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                                    delegate: ItemDelegate {
                                        id: speechEntry
                                        objectName: index === 0 ? "speechLogFirstEntry" : ""
                                        required property int index
                                        required property var modelData
                                        readonly property bool selected: pane.selectedSegment === index
                                        readonly property color speakerColor: {
                                            speakerPlot.axesRevision
                                            return speakerPlot.segmentColor(index)
                                        }
                                        width: ListView.view.width - 10
                                        padding: 6
                                        leftPadding: 13
                                        hoverEnabled: true
                                        Accessible.name: [modelData.speaker || modelData.status, modelData.range,
                                                          modelData.transcript].filter(Boolean).join(" ")
                                        background: Rectangle {
                                            radius: 4
                                            color: speechEntry.selected ? "#edf5fc" : speechEntry.hovered ? "#f0f3f7" : "#f7f9fb"
                                            border.color: speechEntry.selected ? "#9ab9d5" : "#e4e9ef"
                                            Rectangle {
                                                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 2 }
                                                width: 3
                                                radius: 1.5
                                                color: speechEntry.speakerColor
                                            }
                                        }
                                        contentItem: ColumnLayout {
                                            spacing: 3
                                            RowLayout {
                                                Layout.fillWidth: true
                                                spacing: 8
                                                Label {
                                                    text: speechEntry.modelData.speaker || speechEntry.modelData.status || "未确认"
                                                    font.bold: true
                                                    color: speechEntry.speakerColor
                                                }
                                                Label {
                                                    Layout.fillWidth: true
                                                    text: speechEntry.modelData.range || ""
                                                    color: "#566778"
                                                    font.family: "Courier Prime"
                                                    elide: Text.ElideRight
                                                }
                                                Label {
                                                    visible: !!text
                                                    text: speechEntry.modelData.overlap ? "多人同讲" :
                                                        speechEntry.modelData.state === "new" ? "新档案" : ""
                                                    color: speechEntry.modelData.overlap ? "#7a4908" : "#285a7d"
                                                    font.bold: true
                                                }
                                                Label {
                                                    visible: !!text
                                                    text: speechEntry.modelData.score && speechEntry.modelData.score !== "—"
                                                        ? "余弦 " + speechEntry.modelData.score : ""
                                                    color: "#566778"
                                                }
                                            }
                                            Label {
                                                Layout.fillWidth: true
                                                wrapMode: speechEntry.selected ? Text.Wrap : Text.NoWrap
                                                elide: speechEntry.selected ? Text.ElideNone : Text.ElideRight
                                                text: {
                                                    const row = speechEntry.modelData
                                                    if (row.transcript_status === "partial")
                                                        return row.transcript + "  ·  同讲部分未转写"
                                                    if (row.transcript_status === "overlap")
                                                        return "同讲片段暂无可归属的转写"
                                                    return row.transcript || (row.status && row.state !== "matched"
                                                        ? row.status : "暂无转写")
                                                }
                                                color: speechEntry.modelData.transcript ? "#263e53" : "#566778"
                                            }
                                        }
                                        onClicked: {
                                            pane.selectedSegment = index
                                            speakerPlot.revealSegment(modelData)
                                        }
                                    }
                                }
                            }
                            Label {
                                visible: speechLog.count === 0
                                anchors.centerIn: parent
                                text: pane.service.capturing ? "等待有效语音"
                                    : pane.service.metrics.audio_ms ? "本次没有可分析的发言片段" : "发言片段会显示在这里"
                                color: "#566778"
                            }
                        }
                    }
                }
            }
        }
    }
}
