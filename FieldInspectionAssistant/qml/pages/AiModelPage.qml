import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"
import "../components"

Rectangle {
    id: root
    objectName: "aiModelPage"
    color: Colors.background
    property var analysisBundle:({})
    function importAnalysis(bundle){if(bundle.user_id!==App.session.userId||App.ai.busy||App.agent.busy)return;root.mode=bundle.target;root.analysisBundle=bundle;root.inputPath=bundle.imagePath;var q="请分析这些图表的特征、含义与需要进一步验证的事项。";if(root.mode==="agent")assistantWork.importAnalysis(bundle,q);else if(root.mode==="vision")root.visionQuestion=q;else root.textQuestion=q}
    Connections {
        target: App.nav
        function onPageChanged() {
            if (App.nav.page !== "ai") return
            const bundle = App.nav.param("analysis_bundle")
            if (bundle) root.importAnalysis(bundle)
            else {
                const mode = App.nav.param("mode")
                if (["text", "vision", "algorithms", "knowledge", "agent", "voice"].includes(mode)) root.mode = mode
                if (App.nav.param("open_models")) Qt.callLater(root.openSettings)
            }
        }
    }
    Connections{target:App.session;function onSessionChanged(){root.analysisBundle={}}}
    function persistAll(){assistantWork.persistAll()}
    function saveAudio(path){conversationExporter.audio(path)}
    ConversationExporter {id:conversationExporter;mode:root.mode}
    function exportConversation() {
        var rows=assistantWork.exportRows()
        if(rows.length)conversationExporter.open(rows)
        else conversationExporter.complete({ok:false,error:"当前没有可导出的对话"})
    }
    function selectView(index){root.view=index;if(index===2)detailsContent.reload();detailsDialog.open()}
    function openSettings(){assistantSettings.open()}
    function closeSettings(){assistantSettings.close();detailsDialog.close()}
    function importPhoto(path) {
        if (!path.length || App.ai.busy || App.dictation.active || App.speech.recording) return false
        App.speech.stopSpeech()
        root.mode = "vision"
        root.view = 0
        root.imageExpanded = false
        root.inputPath = path
        App.ai.resetDisplay()
        Qt.callLater(function() { assistantWork.showImageInput() })
        return true
    }
    function restoreConversation(mode){if(mode==="asr"){root.mode="voice";detailsDialog.close();return;}root.mode=mode;if(mode==="asr")assistantWork.voiceSettings.loadConversation();else assistantWork.loadMode(mode);detailsDialog.close();root.view=0}
    function openHistory(result) {
        if(result.mode==="asr"){root.mode="voice";detailsDialog.close();return;}
        root.mode=result.mode==="encoder"?"vision":result.mode
        if(root.mode==="text")root.textModel=result.model||"qwen3"
        if(root.mode==="vision")root.visionModel=result.model==="qwen3_encoder"?"qwen3vl":result.model||"qwen3vl"
        root.inputPath=result.input_path||root.sample()
        if(result.question&&root.mode==="text")root.textQuestion=result.question
        if(result.question&&root.mode==="vision")root.visionQuestion=result.question
        App.ai.showResult(result);if(root.mode==="asr")assistantWork.voiceSettings.restoreHistory(result);else assistantWork.restoreHistory(result);root.online=!!result.metrics.streaming_rounds;root.view=0;detailsDialog.close()
    }
    onViewChanged:if(root.mode==="asr"&&root.view>0){App.speech.cancelRecording();App.speech.stopSpeech()}
    property string mode: "text"
    signal audioFileSelected(string path)
    property string inputPath: ""
    property int view: 0
    property bool imageExpanded: false
    property string previewImagePath: ""
    property bool online: false
    property string memoryError:""
    property var labels: ({text: "文字问答", encoder: "图片问答", vision: "图片问答", algorithms:"视觉算法", asr: "语音转写",knowledge:"知识图谱",agent:"智能体分析",voice:"语音助手"})
    property string textModel: "qwen35"
    property string visionModel: "qwen25vl"
    property string selectedModel: mode === "voice" || mode === "agent" || mode === "knowledge" ? "qwen35" : mode === "text" ? textModel : mode === "vision" ? visionModel : mode === "asr" ? "qwen3_asr" : "qwen3_encoder"
    onSelectedModelChanged: preload.restart()
    onOnlineChanged: preload.restart()
    readonly property string preparationText: {
        var key = root.selectedModel + (root.mode === "asr" && root.online ? "_online" : "")
        if (App.ai.system.loading_model === key) return "首次准备模型…"
        var loaded = App.ai.system.resident_models || []
        for (var i = 0; i < loaded.length; ++i) if (loaded[i].model === key) return "模型已就绪"
        return ""
    }
    property var modelNames: ({qwen35: "Qwen3.5-4B", qwen25vl: "Qwen2.5-VL-7B prune", qwen3: "Qwen3-4B", qwen3vl: "Qwen3-VL-4B", qwen3_encoder: "Qwen3-VL Vision", qwen3_asr: "Qwen3-ASR-0.6B"})
    property var names: ({text: modelNames[textModel], encoder: "Qwen3-VL Vision", vision: modelNames[visionModel], asr: "Qwen3-ASR-0.6B"})
    property string textQuestion: ""
    property string visionQuestion: ""
    property bool selectedReady: {
        var rows = App.ai.system.models || [], ready = {}
        for (var i = 0; i < rows.length; i++) ready[rows[i].model] = rows[i].ready
        return !!ready[root.selectedModel] && (root.selectedModel !== "qwen3vl" || !!ready.qwen3_encoder)
    }
    function value(key, digits) {
        var v = App.ai.metrics[key]
        if (key === "first_output_ms" && v === undefined) v = App.ai.metrics.ttft_ms
        if (key === "llm_ttft_ms" && root.mode === "asr" && v === undefined) v = App.ai.metrics.ttft_ms
        if (key === "encoder_fps" && App.ai.metrics.encoder_cache_hit) return "已复用"
        return v === undefined || v === null ? "—" : Number(v).toFixed(digits)
    }
    function sample() {
        return root.mode === "asr" ? (App.ai.system.sample_audio || "") : (App.ai.system.sample_image || "")
    }
    function chooseSample() {
        if (root.mode === "text") root.textQuestion = "请用三句话介绍设备巡检的要点。"
        else root.inputPath = root.sample()
    }
    function chooseFile() { picker.open() }
    function metric(label, key, digits, unit) {
        return {label: label, value: root.value(key, digits), unit: unit || ""}
    }
    property var readings: {
        if (root.mode === "encoder") return [
            metric("图片处理用时", "encoder_ms", 1, "ms"), metric("图片处理速度", "encoder_fps", 2, "FPS"),
            metric("图片数据量", "image_tokens", 0, "tokens"), metric("模型准备", "model_load_ms", 1, "ms"),
            metric("本次总用时", "request_ms", 1, "ms")]
        var list = [metric("回答速度", "decode_tps", 2, "tokens/s"), metric("输入处理速度", "prefill_tps", 2, "tokens/s"),
            metric("首字用时", "first_output_ms", 1, "ms"), metric("本次总用时", "request_ms", 1, "ms"),
            metric("输入量", "prefill_tokens", 0, "tokens"), metric("输出量", "decode_tokens", 0, "tokens"),
            metric("模型准备", "model_load_ms", 1, "ms"), metric("生成用时", "decode_ms", 1, "ms"),
            metric("输入处理用时", "prefill_ms", 1, "ms"), metric("模型首字用时", "llm_ttft_ms", 1, "ms")]
        if (root.mode === "asr") return list.concat([
            metric("录音处理用时", "encoder_ms", 1, "ms"), metric("录音时长", "audio_duration_s", 2, "s"),
            metric("处理用时比", "rtf", 4), metric("转写倍速", "realtime_factor", 2, "×"),
            metric("总用时比", "e2e_rtf", 4), metric("分段次数", "streaming_rounds", 0)])
        return list.concat([metric("图片处理用时", "encoder_ms", 1, "ms"), metric("最高内存占用", "memory_mb", 1, "MB")])
    }
    onModeChanged: {
        if(root.mode==="asr"){root.mode="voice";return;}
        analysisBundle={}
        assistantWork.closeSettings()
        App.input.cancel()
        root.inputPath = ""
        App.ai.resetDisplay()
        if (root.mode === "asr") root.online = false
    }
    onInputPathChanged: {if(analysisBundle.imagePath&&inputPath!==analysisBundle.imagePath)analysisBundle={};App.ai.resetDisplay()}
    readonly property bool wideVisionPreview: root.visible && (videoPane.visible || facePane.visible ||
        (liveVisionPane.visible && (liveVisionPane.imageMode || liveVisionPane.expandedPreview)))
    onTextModelChanged: App.ai.resetDisplay()
    onVisionModelChanged: App.ai.resetDisplay()
    onVisibleChanged: if (visible) { App.ai.refreshStatus(); App.ai.refreshResults(); preload.restart() }
    Timer {
        id: preload; interval: 300
        onTriggered: if (root.visible && root.mode!=="voice" && root.mode!=="algorithms" && !App.visionAlgorithms.busy && !App.voiceAssistant.active && !App.voiceAssistant.busy && !App.ai.busy && !App.agent.busy && !App.knowledge.busy && !App.dictation.active) App.ai.prepareModel(root.selectedModel,root.online)
    }
    Connections { target: App.ai; function onStatusChanged() { preload.restart() } }
    Connections { target: App.dictation; function onFinished(ok) { preload.restart() } }
    FileDialog {
        id: picker
        acceptLabel: "选择"
        rejectLabel: "取消"
        title: root.mode === "asr" ? "选择音频" : "选择图片"
        nameFilters: root.mode === "asr" ? ["音频 (*.wav *.mp3 *.flac *.m4a *.ogg *.webm)"] : ["图片 (*.jpg *.jpeg *.png *.bmp *.webp)"]
        onAccepted: {
            root.inputPath = decodeURIComponent(selectedFile.toString().replace(/^file:\/\//, ""))
            if (root.mode === "asr") root.audioFileSelected(root.inputPath)
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6
        Rectangle {
            Layout.fillWidth:true; Layout.preferredHeight:36; color:"transparent"
            visible: root.mode !== "algorithms"
            Rectangle { anchors.bottom:parent.bottom; width:parent.width; height:1; color:Colors.border }
            Row {
                anchors.left:parent.left; height:parent.height; spacing:2
                Repeater {
                    model:["text","vision","knowledge","agent","voice"]
                    Item {
                        objectName:"assistantTab_"+modelData
                        property string modelMode:modelData
                        property bool selected:root.mode===modelMode
                        width:Math.min((root.width-76)/5,142); height:36
                        Rectangle { anchors.fill:parent; color:parent.selected ? Colors.infoDark : Colors.panelAlt; border.color:Colors.border }
                        Text { anchors.centerIn:parent; text:root.labels[modelMode]; font.pixelSize:Typography.small; color:Colors.text }
                        MouseArea { anchors.fill:parent; cursorShape:Qt.PointingHandCursor; enabled:!App.ai.busy&&!App.dictation.active&&!App.visionAlgorithms.busy; onClicked:root.mode=modelMode }
                    }
                }
            }
            TouchButton {
                objectName:"assistantSettingsButton";anchors.right:parent.right;anchors.verticalCenter:parent.verticalCenter;width:60
                visible: root.mode !== "algorithms"
                text:"设置";onClicked:root.mode==="voice"?voicePane.openSettings():assistantWork.openSettings()
            }
        }
        VoiceAssistantPane{id:voicePane;visible:root.mode==="voice";Layout.fillWidth:true;Layout.fillHeight:true}
        ColumnLayout {
            visible: root.mode === "algorithms"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            TabBar {
                id: visionTabs
                Layout.fillWidth: true
                DesktopTab { objectName: "liveVisionTab"; text: "摄像头实时" }
                DesktopTab { objectName: "imageVisionTab"; text: "图片推理" }
                DesktopTab { objectName: "videoUnderstandingTab"; text: "实时视频理解" }
                DesktopTab { objectName: "faceAccessTab"; text: "Face ID 人脸识别" }
            }
            VisionLivePane {
                id: liveVisionPane
                imageMode: visionTabs.currentIndex === 1
                visible: parent.visible && visionTabs.currentIndex < 2
                Layout.fillWidth: true; Layout.fillHeight: true
            }
            VideoUnderstandingPane {
                id: videoPane
                visible: parent.visible && visionTabs.currentIndex === 2
                Layout.fillWidth: true; Layout.fillHeight: true
            }
            FaceAccessPane {
                id: facePane
                visible: parent.visible && visionTabs.currentIndex === 3
                Layout.fillWidth: true; Layout.fillHeight: true
            }
        }
        AiWorkArea {id:assistantWork;visible:root.mode!=="voice"&&root.mode!=="algorithms";panel:root;Layout.fillWidth:true;Layout.fillHeight:true}
    }
    AssistantDialog {
        id:assistantSettings;objectName:"assistantSettingsDialog";title:"助手设置";modal:true;anchors.centerIn:parent
        width:Math.min(root.width-24,480);height:Math.min(root.height-24,560)
        contentItem:ScrollView {
            id:settingsScroll;clip:true;contentWidth:availableWidth
            ColumnLayout {
                width:settingsScroll.availableWidth;spacing:12
                Text {visible:root.mode!=="asr";text:"模型";font.pixelSize:Typography.body;color:Colors.text}
                ModelPicker {visible:root.mode!=="asr";panel:root;taskMode:root.mode==="vision"?"vision":"text";Layout.fillWidth:true}
                AppCheckBox {objectName:"assistantMemoryToggle";visible:root.mode==="text"||root.mode==="vision";text:"记忆（当前会话）";checked:App.workspace.memoryPreferences[root.mode]===true;enabled:App.session.loggedIn&&!App.ai.busy&&App.session.permissions.inspect;onToggled:{var r=App.workspace.setMemoryEnabled(root.mode,checked);root.memoryError=r.ok?"":r.error;checked=Qt.binding(function(){return App.workspace.memoryPreferences[root.mode]===true})}ToolTip.visible:hovered;ToolTip.text:"开启时结合当前会话上下文；关闭时只处理本次输入。"}
                Text {visible:root.memoryError.length>0;Layout.fillWidth:true;text:root.memoryError;color:Colors.danger;font.pixelSize:Typography.body;wrapMode:Text.Wrap}
                AppCheckBox {objectName:"assistantWebSearchToggle";visible:root.mode==="text"||root.mode==="vision";text:"联网搜索";checked:App.workspace.searchPreferences[root.mode]===true;enabled:App.session.loggedIn&&!App.ai.busy;onToggled:{var r=App.workspace.setSearchEnabled(root.mode,checked);root.memoryError=r.ok?"":r.error;checked=Qt.binding(function(){return App.workspace.searchPreferences[root.mode]===true})}}
                AppCheckBox {objectName:"assistantAutoRead";text:"自动朗读";checked:root.mode==="asr"?assistantWork.voiceSettings.autoSpeak:assistantWork.autoSpeak;onToggled:if(root.mode==="asr")assistantWork.voiceSettings.autoSpeak=checked;else assistantWork.autoSpeak=checked}
                AppCheckBox {visible:root.mode==="asr";text:"录音后自动转文字";checked:assistantWork.voiceSettings.autoTranscribe;onToggled:assistantWork.voiceSettings.autoTranscribe=checked}
                AppCheckBox {visible:root.mode==="asr";text:"流式语音识别";checked:root.online;enabled:!App.ai.busy;onToggled:root.online=checked}
                TouchButton {text:"停止播放";Layout.fillWidth:true;enabled:App.speech.speechBusy;onClicked:App.speech.stopSpeech()}
                TouchButton {text:"导出对话";Layout.fillWidth:true;enabled:!App.ai.busy;onClicked:{assistantSettings.close();root.exportConversation()}}
                TouchButton {text:"使用记录";Layout.fillWidth:true;onClicked:{assistantSettings.close();root.selectView(2)}}
                TouchButton {text:"性能";Layout.fillWidth:true;onClicked:{assistantSettings.close();root.selectView(1)}}
                TouchButton {text:"更多设置";Layout.fillWidth:true;onClicked:{assistantSettings.close();App.nav.push("settings",{section:"assistant"})}}
            }
        }
    }
    AssistantDialog {
        id:detailsDialog;objectName:"assistantRecordsDialog";title:root.view===1?"性能":"使用记录";modal:true;anchors.centerIn:parent
        width:Math.max(300,root.width-24);height:Math.max(240,root.height-24)
        onClosed:root.view=0
        contentItem:AssistantDetails {id:detailsContent;panel:root}
    }

    Rectangle {
        anchors.fill: parent; visible: root.imageExpanded; z: 100
        color: "#142735"
        Image { anchors.fill: parent; anchors.margins: 16; anchors.topMargin: 62; source: root.imageExpanded && root.previewImagePath ? "file://" + root.previewImagePath : ""; fillMode: Image.PreserveAspectFit }
        TouchButton { anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 14; text: "关闭大图"; autoWidth: true; height: 38; onClicked: root.imageExpanded = false }
    }
}
