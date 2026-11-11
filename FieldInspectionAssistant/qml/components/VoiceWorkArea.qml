import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Item {
    id: voice
    objectName: "voiceWorkArea"
    property var panel
    readonly property bool narrow:width<1100
    property string draftText: ""
    property string transcript: ""
    property bool autoSpeak: true
    property bool autoTranscribe: true
    property int selectedIndex: -1
    property int pendingAsrIndex: -1
    property int sourceVoiceIndex: -1
    property int pendingSpeechIndex: -1
    property int streamedUpdates: 0
    property int speechChunks: 0
    property string lastHistoryText: ""
    property string lastSpeechError: ""
    property alias conversationModel: messages
    readonly property bool locked: App.ai.busy || App.dictation.active || App.speech.recording || App.speech.speechBusy
    readonly property bool selectedAudio: selectedIndex >= 0 && selectedIndex < messages.count && messages.get(selectedIndex).kind === "audio"
    clip: true
    property string activeOwner:""
    property string persistenceError:""
    property bool restoring:false
    function persist(){if(restoring||!App.session.loggedIn||!messages.count)return;var rows=[];for(var i=0;i<messages.count;i++)rows.push(JSON.parse(JSON.stringify(messages.get(i))));var r=App.workspace.saveConversation("asr",rows,"qwen3_asr");persistenceError=r.ok?"":r.error}
    function loadConversation(){restoring=true;messages.clear();var r=App.workspace.conversation("asr");if(r.ok)for(var i=0;i<r.rows.length;i++){var row=Object.assign({outgoing:false,kind:"text",body:"",caption:"巡检助手",status:"",audioPath:"",duration:0,streaming:false,ready:true,transcribable:false},r.rows[i]);row.streaming=false;row.ready=true;messages.append(row)}selectedIndex=messages.count-1;restoring=false}
    function restoreHistory(result){var text=result.text||result.error||"";if(!text.length||text===lastHistoryText)return;lastHistoryText=text;transcript=text;appendMessage({body:text,kind:result.state==="failed"?"error":"text",caption:"巡检助手 · 转写记录"})}
    Timer{id:saveVoice;interval:600;onTriggered:voice.persist()}
    Connections{target:App.session;function onSessionEnding(){saveVoice.stop();voice.persist()}function onSessionChanged(){if(voice.activeOwner===App.session.userId)return;voice.activeOwner=App.session.userId;saveVoice.stop();voice.pendingAsrIndex=-1;voice.pendingSpeechIndex=-1;voice.sourceVoiceIndex=-1;if(App.session.loggedIn)voice.loadConversation();else{messages.clear();voice.draftText="";composer.text=Qt.binding(function(){return voice.draftText});voice.transcript="";voice.lastHistoryText="";voice.lastSpeechError="";voice.persistenceError=""}}}
    Component.onCompleted:if(App.session.loggedIn)loadConversation()
    ListModel { id: messages }
    function appendMessage(values) {
        var row = {outgoing: false, kind: "text", body: "", caption: "巡检助手", status: "", audioPath: "", duration: 0, streaming: false, ready: true, transcribable: false}
        for (var key in values) row[key] = values[key]
        messages.append(row);saveVoice.restart()
        chat.followOutput = true
        follow.restart()
        return messages.count - 1
    }
    function addAudio(path, seconds, caption) {
        if (!path || voice.locked) return
        selectedIndex = appendMessage({outgoing: true, kind: "audio", audioPath: path, duration: seconds || 0, caption: caption || "我 · 语音", transcribable: true})
        panel.inputPath = path
    }
    function transcribeIndex(index) {
        if (voice.locked || index < 0 || index >= messages.count || !App.ai.available || !panel.selectedReady) return
        var row = messages.get(index)
        if (row.kind !== "audio" || !row.ready || !row.audioPath) return
        var path = row.audioPath
        sourceVoiceIndex = index
        selectedIndex = index
        streamedUpdates = 0
        pendingAsrIndex = appendMessage({caption: "巡检助手 · 语音转文字", streaming: true, ready: false, status: "正在准备识别…"})
        App.ai.submit("asr", "", path, App.ai.answerLengthLimit, false, panel.online, "qwen3_asr")
    }
    function startRead(text) {
        if (voice.locked || !text.trim().length) return
        if (text.length > 3000) { appendMessage({kind: "error", body: "单次朗读最多 3000 字，请分段发送。"}); return }
        speechChunks = 0
        pendingSpeechIndex = appendMessage({kind: "audio", body: text, caption: "巡检助手 · 语音朗读", streaming: true, ready: false, status: "正在合成第一句…"})
        selectedIndex = pendingSpeechIndex
        App.speech.speak(text)
        if (pendingSpeechIndex >= 0) messages.setProperty(pendingSpeechIndex, "audioPath", App.speech.speechPath)
    }
    function playIndex(index) {
        if (index < 0 || index >= messages.count) return
        var row = messages.get(index)
        selectedIndex = index
        if (App.speech.currentPlaybackPath === row.audioPath && App.speech.speechBusy) { App.speech.stopSpeech(); return }
        if (!voice.locked && row.ready && row.audioPath) App.speech.playAudio(row.audioPath)
    }
    function sendText() {
        var text = draftText.trim()
        if (!text.length || voice.locked) return
        selectedIndex = appendMessage({outgoing: true, body: text, caption: "我 · 文字"})
        draftText = ""
        if (autoSpeak) startRead(text)
    }
    onVisibleChanged: if (!visible) { App.speech.cancelRecording(); App.speech.stopSpeech() }
    Component.onDestruction: App.speech.cancelRecording()
    Menu {
        id:voiceMenu
        MenuItem {text:"导入音频";enabled:!voice.locked;onTriggered:panel.chooseFile()}
        MenuItem {text:"语音输入文字";enabled:!voice.locked;onTriggered:App.input.open(composer.editor,true)}
        MenuItem {text:"取消录音";enabled:App.speech.recording;onTriggered:App.speech.cancelRecording()}
        MenuItem {text:"关联本次巡检";enabled:messages.count>0&&!voice.locked;onTriggered:{var r=messages.get(voice.selectedIndex>=0?voice.selectedIndex:messages.count-1);App.nav.push("product",{section:"evidence",path:r.audioPath||"",content:r.body||"",kind:r.audioPath?"audio":"ai"})}}
        MenuItem {text:"导出对话";enabled:!voice.locked;onTriggered:panel.exportConversation()}
        MenuItem {text:"清空对话";enabled:!voice.locked&&messages.count>0;onTriggered:{App.workspace.newConversation("asr");messages.clear();voice.selectedIndex=-1;voice.pendingAsrIndex=-1;voice.pendingSpeechIndex=-1}}
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 8
        Text{Layout.fillWidth:true;visible:voice.persistenceError.length>0;text:"会话保存失败："+voice.persistenceError;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.danger}
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 80
            color: "transparent"; radius: 6; clip: true
            ListView {
                visible:panel.view===0
                id: chat
                objectName: "voiceConversation"
                anchors.fill: parent; anchors.margins: 12
                property bool followOutput: true
                model: messages; clip: true; spacing: 4; cacheBuffer: 320;reuseItems:true
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                onContentHeightChanged: if (followOutput && !follow.running) follow.start()
                onWidthChanged: if (followOutput) follow.restart()
                onHeightChanged: if (followOutput) follow.restart()
                onMovementStarted: followOutput = false
                onMovementEnded: followOutput = atYEnd
                delegate: ChatBubble {
                    width: chat.width
                    outgoing: model.outgoing; kind: model.kind; body: model.body; caption: model.caption; status: model.status
                    audioPath: model.audioPath; duration: model.duration; streaming: model.streaming; ready: model.ready; transcribable: model.transcribable
                    actionsEnabled: !voice.locked
                    onSaveAudioRequested:panel.saveAudio(model.audioPath)
                    onPlayRequested: voice.playIndex(index)
                    onTranscribeRequested: voice.transcribeIndex(index)
                    onReadRequested: voice.startRead(model.body)
                    onEditRequested: { voice.draftText = model.body; composer.editor.forceActiveFocus() }
                    onTextRequested: voice.appendMessage({body: model.body, caption: "巡检助手 · 语音文字"})
                    onHeightChanged: if (chat.followOutput && !follow.running) follow.start()
                }
            }
        }
        AssistantComposer {
            id:composer;objectName:"voiceComposer";inputName:"voiceTextEditor"
            Layout.fillWidth:true;Layout.preferredHeight:implicitHeight;Layout.maximumWidth:1000;Layout.alignment:Qt.AlignHCenter
            text:voice.draftText;onTextChanged:voice.draftText=text
            placeholder:"发消息或录音…";busy:App.ai.busy||App.speech.speechBusy;recording:App.speech.recording||App.dictation.active
            canSend:!voice.locked;canVoice:!voice.locked
            onAttachmentsRequested:voiceMenu.popup()
            onSendRequested:voice.sendText()
            onVoiceRequested:App.speech.startRecording()
            onStopRequested:{if(App.speech.recording)App.speech.stopRecording();else if(App.dictation.active)App.dictation.cancel();else if(App.ai.busy)App.ai.stop();else App.speech.stopSpeech()}
        }
    }
    Timer { id: follow; interval: 90; onTriggered: if (chat.followOutput) chat.positionViewAtEnd() }
    Connections { target: panel; function onAudioFileSelected(path) { if (panel.mode === "asr") voice.addAudio(path, 0, "我 · 音频文件") } }
    Connections {
        target: App.speech
        function onChanged() {
            if (App.input.opened) return
            if (App.speech.error.length && App.speech.error !== voice.lastSpeechError) {
                voice.lastSpeechError = App.speech.error
                voice.appendMessage({kind: "error", body: App.speech.error, caption: "巡检助手 · 语音提示"})
            } else if (!App.speech.error.length) voice.lastSpeechError = ""
        }
        function onRecordingReady(path) {
            if (voice.visible && panel.visible && !App.dictation.active && !App.input.opened) {
                voice.addAudio(path, App.speech.seconds, "我 · 录音")
                if (voice.autoTranscribe) Qt.callLater(function() { voice.transcribeIndex(voice.selectedIndex) })
            }
        }
        function onAudioPrepared(path, seconds) {
            for (var i = 0; i < messages.count; ++i) if (messages.get(i).audioPath === path) messages.setProperty(i, "duration", seconds)
        }
        function onSpeechChunkReady(path, seconds) {
            if (voice.pendingSpeechIndex < 0) return
            voice.speechChunks++
            messages.setProperty(voice.pendingSpeechIndex, "audioPath", path)
            messages.setProperty(voice.pendingSpeechIndex, "duration", seconds)
            messages.setProperty(voice.pendingSpeechIndex, "status", "分句生成，依次播放…")
        }
        function onSpeechReady(path, seconds) {
            if (voice.pendingSpeechIndex < 0) return
            messages.setProperty(voice.pendingSpeechIndex, "audioPath", path)
            messages.setProperty(voice.pendingSpeechIndex, "duration", seconds)
            messages.setProperty(voice.pendingSpeechIndex, "ready", true)
            messages.setProperty(voice.pendingSpeechIndex, "status", "正在播放…")
        }
        function onSpeechFinished(ok) {
            if (voice.pendingSpeechIndex < 0) return
            var index = voice.pendingSpeechIndex
            messages.setProperty(index, "streaming", false)
            messages.setProperty(index, "status", ok ? "播放完成" : App.speech.error || "已停止")
            voice.pendingSpeechIndex = -1;saveVoice.restart()
        }
    }
    Connections {
        target: App.ai
        function onChanged() {
            if (voice.pendingAsrIndex >= 0) {
                var before = messages.get(voice.pendingAsrIndex).body
                if (before !== App.ai.answer) {
                    messages.setProperty(voice.pendingAsrIndex, "body", App.ai.answer)
                    voice.transcript = App.ai.answer
                    if (App.ai.busy && App.ai.answer.length) voice.streamedUpdates++
                }
                var status=App.ai.busy ? App.ai.message : "识别完成"
                if(messages.get(voice.pendingAsrIndex).status!==status)messages.setProperty(voice.pendingAsrIndex, "status", status)
            }
        }
        function onCompleted(ok, mode) {
            if (mode !== "asr" || voice.pendingAsrIndex < 0) return
            var index = voice.pendingAsrIndex
            messages.setProperty(index, "streaming", false)
            messages.setProperty(index, "ready", true)
            messages.setProperty(index, "status", ok ? "识别完成" : App.ai.message)
            if (!ok && !App.ai.answer.length) { messages.setProperty(index, "kind", App.ai.message === "已停止" ? "text" : "error"); messages.setProperty(index, "body", App.ai.message === "已停止" ? "已停止转写" : App.ai.message); messages.setProperty(index, "status", "") }
            if (ok && voice.sourceVoiceIndex >= 0) {
                messages.setProperty(voice.sourceVoiceIndex, "body", App.ai.answer)
                if (App.ai.metrics.audio_duration_s) messages.setProperty(voice.sourceVoiceIndex, "duration", App.ai.metrics.audio_duration_s)
            }
            voice.lastHistoryText = App.ai.answer
            voice.pendingAsrIndex = -1;saveVoice.restart()
            if (ok && voice.autoSpeak && voice.visible && panel.visible && panel.view===0 && App.ai.answer.trim().length) voice.startRead(App.ai.answer)
        }
    }
}
