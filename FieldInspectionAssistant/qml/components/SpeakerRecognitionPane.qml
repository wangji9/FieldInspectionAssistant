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
            }
            CheckBox { id: transcribe; height: 30; text: "按档案语种转写"; visible: !pane.microphone; enabled: pane.service.whisperReady && !pane.service.busy }
            TouchButton {
                objectName: "startLiveSpeakerRecognition"
                visible: pane.microphone && pane.service.liveMode === ""
                text: "开始智能分析"; height: 30; autoWidth: true; variant: "primary"
                enabled: pane.service.ready && !pane.service.busy
                onClicked: {
                    pane.feedback = ""
                    pane.selectedSegment = -1
                    pane.service.startSmartAnalysis()
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
                    pane.service.analyzeSmart(pane.source, pane.rangeStart(), pane.rangeLength(),
                                              transcribe.enabled && transcribe.checked)
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
            text: pane.feedback || (!pane.microphone && pane.sourceLabel ? pane.sourceLabel : "")
            color: Colors.textSecondary; elide: Text.ElideMiddle
        }
        ScrollView {
            id: panelScroll
            readonly property bool narrow: width < 770
            readonly property real panelHeight: Math.max(height - topPadding - bottomPadding, narrow ? 580 : 380)
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
                    Layout.preferredWidth: 314
                    Layout.preferredHeight: panels.columns === 1 ? 195 : 0
                    Layout.maximumHeight: panels.columns === 1 ? 195 : Number.POSITIVE_INFINITY
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
                            onRowClicked: function(index, entry) { pane.profileId = entry.id }
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
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 56
                            visible: pane.microphone
                            color: "#f5f7fa"
                            border.color: Colors.border
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 6
                                spacing: 9
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Label {
                                        text: "最近发言人"
                                        color: Colors.textSecondary
                                    }
                                    Label {
                                        objectName: "liveSpeakerIdentity"
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.bold: true
                                        font.pixelSize: 17
                                        text: {
                                            const row = pane.service.liveMatch
                                            if (row.speaker)
                                                return row.speaker
                                            if (row.status)
                                                return row.status
                                            if (pane.service.busy)
                                                return pane.service.capturing ? "等待发言" : pane.service.status
                                            return "—"
                                        }
                                        color: pane.service.liveMatch.speaker ? "#005c8d" : Colors.text
                                    }
                                }
                                Label {
                                    text: "余弦  " + (pane.service.liveMatch.score || "—")
                                    visible: pane.microphone
                                }
                                ColumnLayout {
                                    visible: pane.microphone
                                    Layout.preferredWidth: Math.min(155, parent.width*0.34)
                                    Layout.maximumWidth: 155
                                    spacing: 2
                                    Label {
                                        text: pane.service.capturing
                                            ? "输入  %1 dBFS".arg(pane.service.inputLevel.toFixed(0)) : "输入  — dBFS"
                                    }
                                    ProgressBar {
                                        objectName: "speakerInputLevel"
                                        Layout.fillWidth: true
                                        from: -60; to: 0
                                        value: Math.max(-60, pane.service.inputLevel)
                                    }
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true; elide: Text.ElideRight
                            text: {
                                const info = pane.service.metrics
                                if (pane.service.operationError)
                                    return pane.service.operationError
                                if (!pane.service.ready || (!pane.service.capturing && pane.service.busy))
                                    return pane.service.status
                                if (!info.audio_ms)
                                    return pane.service.status === "分析完成" ? "" : pane.service.status
                                return "%1 s  ·  %2 个发言片段  ·  新建 %3 个档案"
                                    .arg((info.audio_ms/1000).toFixed(1)).arg(info.embeddings || 0)
                                    .arg(info.new_profiles || 0)
                            }
                            visible: !!text
                            color: Colors.textSecondary
                        }
                        SpeakerTimeline {
                            objectName: "speakerTimeline"
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(implicitHeight, Math.max(180,pane.height*0.44))
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
                            emptyText: pane.service.capturing ? "等待足够的有效语音"
                                : pane.service.metrics.audio_ms ? "本次未提取到可分析的发言片段"
                                : "发言片段会显示在这里"
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
}
