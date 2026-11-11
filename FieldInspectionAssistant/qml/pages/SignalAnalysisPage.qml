import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import "../theme"
import "../components"

Rectangle {
    id: root; objectName: "signalAnalysisPage";color: Colors.background
    property bool settingsOpen: false
    property bool chartSelection:false
    property var selectedCharts:[]
    property bool grabbingChart:false
    property string chartTarget:"vision"
    property string chartError:""
    property int chartGeneration:0
    function toggleChart(item,key){
        var rows=selectedCharts.slice(),at=rows.findIndex(function(r){return r.key===key})
        if(at>=0){rows.splice(at,1);selectedCharts=rows;return}
        if(grabbingChart||rows.length>=6){chartError="每次最多选择六张图";return}
        var meta=item.analysisContext()
        if(!(meta.points_sample||[]).length&&!(meta.series_sample||[]).length&&!meta.matrix_size){chartError="当前图表没有可分析的数据";return}
        var metadata={plot:meta,channel:overlayChannels&&tab===0?"1–6":channel+1,source:selectedSource,recording:snapshot.recording||{},sample_rate:snapshot.sample_rate||snapshot.rate||16000,unit:options.unit||"FS",processing_options:options,calibration:snapshot.calibration||{},analysis_scope:snapshot.analysis_scope||{},operating_condition:pointProfile.operating_condition||"未提供",captured_at:new Date().toISOString()}
        metadata.analysis_start=App.analysis.running?Math.max(0,(snapshot.capture_frames||0)/metadata.sample_rate-windowSeconds):Number(snapshot.analysis_start||0)
        metadata.analysis_end=App.analysis.running?(snapshot.capture_frames||0)/metadata.sample_rate:Number(snapshot.analysis_end||snapshot.full_duration||0)
        metadata.analysis_scope=tab===0?"window":(snapshot.analysis_scope||["full","range","window"][analysisScope])
        metadata.capture_frames=snapshot.capture_frames||0
        metadata.is_trend=tab===2
        metadata.trend_records=tab===2?App.analysis.trends:[]
        var owner=App.session.userId,generation=chartGeneration,path=App.workspace.temporaryPath("chart.png")
        grabbingChart=true;chartError="";item.grabToImage(function(result){grabbingChart=false;if(owner!==App.session.userId||generation!==chartGeneration)return;if(!result.saveToFile(path)){chartError="图表保存失败";return}var next=selectedCharts.slice();next.push({key:key,path:path,title:meta.title||key,metadata:metadata});selectedCharts=next})
    }
    function chartSelected(key){return selectedCharts.some(function(r){return r.key===key})}
    function askCharts(){if(selectedCharts.length&&!App.chartTransfer.busy)App.chartTransfer.prepare(selectedCharts,chartTarget)}
    Connections{target:App.chartTransfer;function onReady(bundle){if(bundle.user_id!==App.session.userId)return;chartPicker.close();App.nav.push("ai",{analysis_bundle:bundle})}}
    Connections{target:App.session;function onSessionChanged(){chartGeneration++;selectedCharts=[];chartSelection=false;chartPicker.close()}}
    AssistantDialog{id:chartPicker;objectName:"analysisChartPicker";title:"图表问答";anchors.centerIn:parent;width:Math.min(680,root.width-24);height:Math.min(520,root.height-24)
        contentItem:ColumnLayout{spacing:10
            RowLayout{Layout.fillWidth:true;AppComboBox{objectName:"chartQuestionTarget";Layout.fillWidth:true;model:[{id:"vision",name:"图片问答 · 视觉模型"},{id:"text",name:"文字问答 · 数据上下文"},{id:"agent",name:"智能体分析"}];textRole:"name";onActivated:root.chartTarget=model[currentIndex].id}TouchButton{objectName:"sendSelectedCharts";text:App.chartTransfer.busy?"准备中…":"转到问答";autoWidth:true;height:34;variant:"primary";enabled:root.selectedCharts.length>0&&!App.chartTransfer.busy&&!App.ai.busy&&!App.agent.busy;onClicked:root.askCharts()}}
            Text{visible:!!(root.chartError||App.chartTransfer.error);Layout.fillWidth:true;text:root.chartError||App.chartTransfer.error;wrapMode:Text.Wrap;color:Colors.danger;font.pixelSize:Typography.body}
            ListView{Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:8;model:root.selectedCharts;delegate:RowLayout{required property var modelData;required property int index;width:ListView.view.width;height:100;Image{Layout.preferredWidth:180;Layout.fillHeight:true;source:"file://"+modelData.path;fillMode:Image.PreserveAspectFit;asynchronous:true}Text{Layout.fillWidth:true;text:modelData.title;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.text}TouchButton{text:"移除";autoWidth:true;height:30;onClicked:{var rows=root.selectedCharts.slice();rows.splice(index,1);root.selectedCharts=rows}}}}
        }
    }
    property int expandedCapture:-1
    property int selectedCapture:0
    function selectedChart(){return selectedCapture===0?captureTime:captureFrequency}
    function toggleExpanded(index){selectedCapture=index;expandedCapture=expandedCapture===index?-1:index}
    property var pointProfile:({})
    Connections{target:App.nav;function onPageChanged(){if(App.nav.page!=="analysis")return;var p=App.nav.param("channel_profile");if(!p)return;root.pointProfile=p;root.selectedSource=p.source;root.channel=Number(p.channel)-1;var c=p.calibration;root.options={c0:false,normalize:false,gain:Number(c.gain)||0,offset:Number(c.offset)||0,unit:c.unit||"FS",calibrationId:c.id||"",channelBindingId:p.id,pointId:p.point_id,operatingCondition:p.operating_condition};App.analysis.refresh();root.configure()}}
    property string selectedSource: ""
    property string explicitSource: ""
    readonly property var captureSource: App.analysis.devices.find(device => device.id === selectedSource) || ({})
    function chooseSource(id) { explicitSource = id; selectedSource = id }
    function openSettings(){if(tab===3){evaluationPane.editForm();return}settingsView.section=tab===0?0:tab===1?1:tab===2?2:3;settingsOpen=true}
    function resetCharts(){captureTime.resetZoom();captureFrequency.resetZoom();primaryChart.resetZoom();envelopeChart.resetZoom()}
    function zoomCharts(){if(tab===0){captureTime.zoomY*=2;captureFrequency.zoomY*=2}else primaryChart.zoomY*=2}
    function importRecording(){importDialog.open()}
    function saveChart(){if(tab===4)tab=1;settingsOpen=false;Qt.callLater(function(){pngDialog.open()})}
    function linkChart(){var path=App.workspace.temporaryPath("chart.png"),target=tab===0?capturePlots:tab===2?trendCharts:primaryChart;target.grabToImage(function(image){if(!image.saveToFile(path)){root.feedback="图表保存失败";return}App.nav.push("product",{section:"evidence",path:path,kind:"chart",metadata:{recording:root.snapshot.recording,channel:root.channel+1,calibration:root.snapshot.calibration||{},analysis_scope:root.snapshot.analysis_scope,source:root.selectedSource}})})}
    function saveCurrentChart(path){var target=tab===0?capturePlots:tab===2?trendCharts:primaryChart;target.grabToImage(function(result){feedback=result.saveToFile(path)?"图表已保存":"图表保存失败"})}
    function syncSource(){
        if (App.analysis.running) return
        const rows = App.analysis.devices
        const requested = pointProfile.source || explicitSource
        const device = requested ? rows.find(row => row.id === requested) : rows.find(row => row.transport === "six_serial")
        selectedSource = device ? device.id : ""
    }
    function toggleCapture(){if(App.analysis.running)App.analysis.stop();else if(selectedSource.length){configure();App.analysis.start(selectedSource)}}
    property int tab: 0
    property int methodIndex: 0
    property int channel: 0
    property bool overlayChannels:false
    property real timeLimit:.1
    property real frequencyMinimum:-120
    property real frequencyMaximum:0
    property real windowSeconds: 1
    property int analysisScope: 0
    property int analysisStartSeconds: 0
    property int analysisEndSeconds: 0
    property var snapshot: visible?App.analysis.summary:({})
    property var methods: App.analysis.methods
    property var options: ({c0:false,normalize:false,gain:0,unit:"FS"})
    property real rangeStart: 0
    property real rangeEnd: 1
    property string exportScope:"full"
    property string exportKind: "json"
    property int trendMetric: 0
    property bool relativeTrend: true
    property int smoothing: 3
    property var metricKeys: ["rms","peak","crest","kurtosis","stddev","shape_factor","impulse_factor","clearance_factor","skewness","band_energy","band_ratio","centroid","flatness","entropy","zcr"]
    property var metricNames: ["有效值 RMS","峰值","峰值因子","峭度","标准差","波形因子","脉冲因子","裕度因子","偏度","频带能量","频带能量占比","频谱重心","频谱平坦度","频谱熵","过零率"]
    property var metricUnits: ["FS","FS","","","FS","","","","","FS²","","Hz","","bit","Hz"]
    function fmt(value) {if(value===undefined||value===null)return "—";return Math.abs(value)<.001&&value!==0?Number(value).toExponential(3):Number(value).toFixed(3)}
    function configure() {if(methods.length){var settings={c0:options.c0,normalize:options.normalize,gain:options.gain,offset:options.offset||0,unit:options.unit,calibrationId:options.calibrationId||"",channelBindingId:options.channelBindingId||"",pointId:options.pointId||"",operatingCondition:options.operatingCondition||"",previewChannels:false,pairedPlots:tab===0,overlayChannels:overlayChannels&&!(options.gain>0),analysisScope:tab===0?"window":["full","range","window"][analysisScope],analysisStart:analysisStartSeconds,analysisEnd:analysisEndSeconds||Math.ceil(snapshot.full_duration||windowSeconds)};App.analysis.configure(tab===1?methods[methodIndex].id:"time",channel,windowSeconds,settings)}}
    onTabChanged:{expandedCapture=-1;if(tab<3)configure()}
    function chooseExport(kind) {exportKind=kind;saveDialog.open()}
    function trendPlot(all) {
        var data=App.analysis.trends,indices=all?metricKeys.map(function(_,i){return i}):[trendMetric],series=[]
        for(var k of indices){var points=[],first=null
            for(var row of data){var val=row[metricKeys[k]];if(val===undefined||val===null)continue;if(first===null&&Math.abs(val)>1e-15)first=val;points.push([row.time,relativeTrend?(first===null?0:(val/first-1)*100):val])}
            series.push({name:metricNames[k],points:points})
        }
        if(!all&&series.length&&series[0].points.length){var raw=series[0].points,mean=[];for(var i=0;i<raw.length;i++){var sum=0,n=0;for(var j=Math.max(0,i-smoothing+1);j<=i;j++){sum+=raw[j][1];n++}mean.push([raw[i][0],sum/n])}series.push({name:"移动均值",color:"#D95319",points:mean})}
        return {kind:"line",title:all?"多指标趋势总览":metricNames[trendMetric]+"趋势",xLabel:"采集时间",xKind:"time",yLabel:relativeTrend?"相对首个非零基准 / %":all?"各指标原始值":metricUnits[trendMetric],series:series}
    }
    function showRequestedAnalysis() {
        const page = App.nav.param("analysis_tab")
        if (page !== undefined && page !== null) root.tab = Number(page)
        if (App.nav.param("open_capture_settings")) Qt.callLater(root.openSettings)
    }
    function selectWorkspaceTab(index) {
        const capturing = !!App.nav.param("capture")
        if ((index === 0) !== capturing)
            App.nav.push("analysis", {analysis_tab:index, capture:index === 0})
        else root.tab = index
    }
    onVisibleChanged:if(visible&&App.session.loggedIn){root.tab=0;root.settingsOpen=false;App.analysis.refresh();root.showRequestedAnalysis()}
    Connections { target: App.nav; function onPageChanged() { if (root.visible && App.nav.page === "analysis") root.showRequestedAnalysis() } }
    // Settings change the existing pipeline; opening settings never restarts acquisition.
    property string latestProjectRecording: ""
    signal projectCapture(string path, string kind)
    Connections {
        target: App.analysis
        function onChanged() { root.syncSource() }
        function onSaved(path) {
            root.feedback = "已保存：" + path
            if (/\.wav$/i.test(path)) root.latestProjectRecording = path
        }
    }
    Connections { target: App.speech; function onRecordingReady(path) { root.latestProjectRecording = path } }
    Connections {
        target: App.session
        function onSessionEnding() {
            root.latestProjectRecording = ""; root.explicitSource = ""; root.pointProfile = ({})
        }
    }
    Component.onCompleted:syncSource()
    SignalAnalysisSettings {id:settingsView;anchors.fill:parent;anchors.margins:12;panel:root;visible:root.settingsOpen}

    FileDialog {id:importDialog;title:"导入真实采样 WAV";nameFilters:["PCM 音频 (*.wav)"];onAccepted:App.analysis.importFile(selectedFile.toString())}
    FileDialog {id:saveDialog;title:root.exportKind==="csv"?(root.exportScope==="full"?"导出整段录音快照":"导出最近窗口") :"导出分析";fileMode:FileDialog.SaveFile;defaultSuffix:root.exportKind;nameFilters:root.exportKind==="csv"?["CSV 数据 (*.csv)"]:["JSON 分析 (*.json)"];onAccepted:App.analysis.exportFile(selectedFile.toString(),root.exportKind,root.exportScope)}
    FileDialog {id:pngDialog;title:"保存图表";fileMode:FileDialog.SaveFile;defaultSuffix:"png";nameFilters:["PNG 图片 (*.png)"];onAccepted:root.saveCurrentChart(decodeURIComponent(selectedFile.toString().replace(/^file:\/\//,"")))}
    AssistantDialog {
        id:trendLegend;title:"趋势图例";anchors.centerIn:parent;width:Math.min(root.width-24,460);height:Math.min(root.height-24,550)
        contentItem:ScrollView {id:legendScroll;clip:true;contentWidth:availableWidth
            Column {width:legendScroll.availableWidth;spacing:12
                Text {text:"● "+root.metricNames[root.trendMetric];font.pixelSize:Typography.body;color:"#0072BD"}
                Text {text:"● 移动均值";font.pixelSize:Typography.body;color:"#D95319"}
                Rectangle {width:parent.width;height:1;color:Colors.border}
                Repeater {model:root.metricNames;Text {width:parent.width;text:"● "+modelData;font.pixelSize:Typography.body;color:["#0072BD","#D95319","#EDB120","#7E2F8E","#77AC30","#4DBEEE","#A2142F"][index%7]}}
            }
        }
    }
    ColumnLayout {
        visible:!root.settingsOpen
        anchors.fill:parent;anchors.margins:8;spacing:6
        Item {
            objectName:"analysisMainNavigation";Layout.fillWidth:true;Layout.preferredHeight:36
        Flickable {anchors.left:parent.left;anchors.right:parent.right;anchors.rightMargin:68;height:36;contentWidth:navigationRow.width;clip:true;boundsBehavior:Flickable.StopAtBounds
            Row {id:navigationRow;height:36;spacing:4
                Repeater {model:["采集通道","数据分析","趋势分析","评价分析","数据管理"]
                    TouchButton{objectName:"analysisTab"+index;text:modelData;autoWidth:true;height:28;variant:root.tab===index?"tonal":"secondary";onClicked:root.selectWorkspaceTab(index)}
                }
                TouchButton{objectName:"startSensorCapture";text:App.analysis.running?"停止并保存":"开始采集";autoWidth:true;height:28;variant:App.analysis.running?"danger":"primary";enabled:App.analysis.running||(!App.evaluation.busy&&root.selectedSource.length>0);onClicked:root.toggleCapture()}
                TouchButton { text:"录音归入工程"; autoWidth:true; height:32
                    visible: !!App.nav.param("capture")
                    enabled: !!root.latestProjectRecording && !!App.projects.project.id && !App.analysis.running && !App.speech.recording
                    onClicked: root.projectCapture(root.latestProjectRecording, "audio") }
                TouchButton{objectName:"selectAnalysisCharts";text:root.chartSelection?"完成选图":"选择图表";autoWidth:true;height:32;visible:root.tab<3;variant:root.chartSelection?"tonal":"ghost";onClicked:{root.chartSelection=!root.chartSelection;if(!root.chartSelection&&root.selectedCharts.length)chartPicker.open()}}
                TouchButton{visible:root.selectedCharts.length>0&&root.tab<3;text:"已选 "+root.selectedCharts.length;autoWidth:true;height:32;onClicked:chartPicker.open()}
            }}
            TouchButton {
                objectName:"analysisSettingsButton";anchors.right:parent.right;width:62;text:"设置"
                onClicked:root.openSettings()
            }
        }
        RowLayout {
            visible: root.tab === 0
            Layout.fillWidth: true; spacing: 5
            Label { text: "采集设备" }
            AppComboBox {
                objectName: "captureDeviceInline"
                Layout.preferredWidth: 210; Layout.minimumWidth: 110
                height: 28
                model: App.analysis.devices; textRole: "name"; valueRole: "id"
                currentIndex: model.findIndex(row => row.id === root.selectedSource)
                displayText: currentIndex < 0 ? "请选择采集设备" : currentText
                enabled: !App.analysis.running && !App.analysis.discovering && !root.pointProfile.source
                onActivated: root.chooseSource(currentValue)
            }
            TouchButton { text:"刷新设备"; autoWidth:true; height:28; enabled:!App.analysis.running&&!App.analysis.discovering; onClicked:App.analysis.refreshDevices() }
            Label {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: root.captureSource.transport === "six_serial"
                    ? root.captureSource.control + " / " + root.captureSource.audio + " · 115200 控制"
                    : App.analysis.deviceStatus
            }
        }
        Label {
            visible: root.tab === 0 && !!App.analysis.message
            Layout.fillWidth: true; text: App.analysis.message; elide: Text.ElideRight
        }
        Item {
            visible:root.tab<3;Layout.fillWidth:true;Layout.preferredHeight:32
            AppComboBox {
                objectName:"analysisChannelChoice";width:116;height:32
                model:Math.max(1,root.snapshot.channels||1);currentIndex:Math.min(root.channel,model-1)
                displayText:"通道 "+(currentIndex+1);enabled:!root.overlayChannels||root.tab!==0
                onActivated:{root.channel=currentIndex;root.configure()}
            }
            Row {
                visible:root.tab===0&&root.overlayChannels;x:132;height:32;spacing:12
                Repeater {model:Math.min(6,root.snapshot.channels||0)
                    Text {height:32;verticalAlignment:Text.AlignVCenter;text:"● 通道 "+(index+1);color:["#0072BD","#D95319","#EDB120","#7E2F8E","#77AC30","#4DBEEE"][index];font.pixelSize:Typography.body}
                }
            }
            Row {
                visible:root.tab===0;anchors.right:parent.right;height:30;spacing:2
                ToolButton {objectName:"expandCapturePlot";width:30;height:30;text:root.expandedCapture>=0?"↙":"⛶";Accessible.name:root.expandedCapture>=0?"还原图表":"放大选中图表";onClicked:root.toggleExpanded(root.selectedCapture);ToolTip.visible:hovered;ToolTip.text:Accessible.name}
                ToolButton {objectName:"zoomCaptureIn";width:30;height:30;text:"+";font.pixelSize:21;Accessible.name:"放大范围";onClicked:root.selectedChart().zoomAt(1.5,.5,.5,true);ToolTip.visible:hovered;ToolTip.text:Accessible.name}
                ToolButton {objectName:"zoomCaptureOut";width:30;height:30;text:"−";font.pixelSize:21;Accessible.name:"缩小范围";onClicked:root.selectedChart().zoomAt(1/1.5,.5,.5,true);ToolTip.visible:hovered;ToolTip.text:Accessible.name}
                ToolButton {objectName:"resetCapturePlot";width:30;height:30;text:"↺";font.pixelSize:20;Accessible.name:"复位选中图表";onClicked:root.selectedChart().resetZoom();ToolTip.visible:hovered;ToolTip.text:Accessible.name}
            }
        }
        Text{visible:App.analysis.message.indexOf("分析失败")===0||App.analysis.message.indexOf("无法")===0;Layout.fillWidth:true;text:App.analysis.message;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.danger}
        StackLayout {
            Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:0;Layout.preferredHeight:0;currentIndex:root.tab
            ColumnLayout{spacing:8
                Item{id:capturePlots;Layout.fillWidth:true;Layout.fillHeight:true;readonly property bool sideBySide:false
                    SignalChart{id:captureTime;aiSelectable:root.chartSelection;aiSelected:root.chartSelected("time-"+root.channel+"-"+root.methodIndex);onAiSelectRequested:root.toggleChart(captureTime,"time-"+root.channel+"-"+root.methodIndex);objectName:"captureTimeChart";showTitle:false;matlabStyle:true;visible:root.expandedCapture!==1;selected:root.selectedCapture===0;onSelectedRequested:root.selectedCapture=0;onExpandRequested:root.toggleExpanded(0);x:0;y:0;width:root.expandedCapture>=0?capturePlots.width:capturePlots.sideBySide?(capturePlots.width-8)/2:capturePlots.width;height:root.expandedCapture>=0||capturePlots.sideBySide?capturePlots.height:(capturePlots.height-6)/2;analysisSource:root.tab===0?App.analysis:null;analysisField:"capture_time";manualY:true;yMinimum:-root.timeLimit;yMaximum:root.timeLimit}
                    SignalChart{id:captureFrequency;aiSelectable:root.chartSelection;aiSelected:root.chartSelected("fft-"+root.channel+"-"+root.methodIndex);onAiSelectRequested:root.toggleChart(captureFrequency,"fft-"+root.channel+"-"+root.methodIndex);objectName:"captureFrequencyChart";showTitle:false;matlabStyle:true;visible:root.expandedCapture!==0;selected:root.selectedCapture===1;onSelectedRequested:root.selectedCapture=1;onExpandRequested:root.toggleExpanded(1);x:root.expandedCapture<0&&capturePlots.sideBySide?captureTime.width+8:0;y:root.expandedCapture<0&&!capturePlots.sideBySide?captureTime.height+6:0;width:captureTime.width;height:captureTime.height;analysisSource:root.tab===0?App.analysis:null;analysisField:"capture_frequency";manualY:true;yMinimum:root.frequencyMinimum;yMaximum:root.frequencyMaximum}
                }
            }
            RowLayout {
                spacing:10
                Rectangle {
                    Layout.preferredWidth:root.width<900?140:166;Layout.fillHeight:true;color:Colors.panel;border.color:Colors.border
                    ScrollView {
                        id:methodScroll;anchors.fill:parent;anchors.margins:8;contentWidth:availableWidth;clip:true
                        Column {
                            width:methodScroll.availableWidth;spacing:4
                            Repeater{model:root.methods;Rectangle{objectName:"signalMethod"+index;width:parent.width;height:34;radius:7;color:root.methodIndex===index?Colors.infoDark:"transparent";signal clicked();onClicked:{root.methodIndex=index;root.configure()} Text{anchors.fill:parent;anchors.margins:8;verticalAlignment:Text.AlignVCenter;text:modelData.name;elide:Text.ElideRight;font.pixelSize:Typography.body;color:root.methodIndex===index?Colors.normal:Colors.textSecondary}MouseArea{anchors.fill:parent;onClicked:parent.clicked()}}}
                        }
                    }
                }
                Item {
                    id:detailArea;Layout.fillWidth:true;Layout.fillHeight:true;Layout.minimumHeight:0;clip:true
                    SignalChart{id:primaryChart;aiSelectable:root.chartSelection;aiSelected:root.chartSelected("analysis-"+root.channel+"-"+root.methodIndex);onAiSelectRequested:root.toggleChart(primaryChart,"analysis-"+root.channel+"-"+root.methodIndex);objectName:"primarySignalChart";x:0;y:0;width:parent.width;height:root.snapshot.has_envelope?(parent.height-8)/2:parent.height;analysisSource:root.tab===1?App.analysis:null;analysisField:"plot";startFraction:root.rangeStart;endFraction:root.rangeEnd;manualY:root.methodIndex<4;yMinimum:root.methodIndex===0?-root.timeLimit:root.frequencyMinimum-(root.methodIndex>=2?60:0);yMaximum:root.methodIndex===0?root.timeLimit:root.frequencyMaximum;onImageSaved:function(ok){root.feedback=ok?"图表已保存":"图表保存失败"}}
                    SignalChart{id:envelopeChart;aiSelectable:root.chartSelection;aiSelected:root.chartSelected("envelope-"+root.channel+"-"+root.methodIndex);onAiSelectRequested:root.toggleChart(envelopeChart,"envelope-"+root.channel+"-"+root.methodIndex);visible:!!root.snapshot.has_envelope;x:0;y:primaryChart.height+8;width:parent.width;height:(parent.height-8)/2;analysisSource:root.tab===1?App.analysis:null;analysisField:"plot.envelope"}
                }
            }
            Item {
                id:trendCharts;objectName:"trendCharts"
                property bool sideBySide:root.width>1100
                SignalChart {
                    id:singleTrend;aiSelectable:root.chartSelection;aiSelected:root.chartSelected("trend-"+root.channel+"-"+root.trendMetric);onAiSelectRequested:root.toggleChart(singleTrend,"trend-"+root.channel+"-"+root.trendMetric);objectName:"singleMetricTrend";x:0;y:0
                    width:trendCharts.sideBySide?(trendCharts.width-10)/2:trendCharts.width
                    height:trendCharts.sideBySide?trendCharts.height:(trendCharts.height-8)/2
                    plot:root.tab===2?root.trendPlot(false):({})
                }
                SignalChart {
                    id:overviewTrend;aiSelectable:root.chartSelection;aiSelected:root.chartSelected("trend-overview-"+root.channel+"-"+root.methodIndex);onAiSelectRequested:root.toggleChart(overviewTrend,"trend-overview-"+root.channel+"-"+root.methodIndex);objectName:"multiMetricTrend";x:trendCharts.sideBySide?(trendCharts.width+10)/2:0
                    y:trendCharts.sideBySide?0:(trendCharts.height+8)/2
                    width:trendCharts.sideBySide?(trendCharts.width-10)/2:trendCharts.width
                    height:trendCharts.sideBySide?trendCharts.height:(trendCharts.height-8)/2
                    plot:root.tab===2?root.trendPlot(true):({})
                }
                ToolButton {x:overviewTrend.x+overviewTrend.width-68;y:overviewTrend.y+8;width:56;height:28;text:"图例";onClicked:trendLegend.open();Accessible.name:"趋势图例";background:Rectangle{radius:7;color:parent.hovered?Colors.infoDark:"transparent"}}
            }
            EvaluationAnalysisPane {id:evaluationPane;panel:root}
            RecordingManager {
                onImportRequested:root.importRecording()
                onAnalyzeRequested:root.tab=1
            }
        }
    }
    property string feedback: ""
}


