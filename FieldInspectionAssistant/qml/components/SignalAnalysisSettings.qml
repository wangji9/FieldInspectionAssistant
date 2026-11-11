import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"

Item {
    id: settings; objectName: "signalAnalysisSettings"
    property var panel
    property int section: 0
    ColumnLayout {
        anchors.fill: parent; spacing: 12
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: 36
            TouchButton { objectName:"backFromAnalysisSettings"; text:"‹ 返回"; width:78; height:34; variant:"ghost"; onClicked:panel.settingsOpen=false }
            Text { text:"分析设置";font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text }
            Item { Layout.fillWidth:true }
        }
        Flow {
            Layout.fillWidth:true;spacing:6
            Repeater { model:["采集","图表与分析","趋势","数据与导出"]
                TouchButton {objectName:"analysisSettingsTab"+index;text:modelData;autoWidth:true;height:32;variant:settings.section===index?"tonal":"ghost";onClicked:settings.section=index}
            }
        }
        ScrollView {
            id:scroll;Layout.fillWidth:true;Layout.fillHeight:true;contentWidth:availableWidth;clip:true
            ColumnLayout {
                width:scroll.availableWidth;spacing:16
                Rectangle {
                    visible:settings.section===0;Layout.fillWidth:true;implicitHeight:captureForm.implicitHeight+32;radius:16;color:Colors.panel;border.color:Colors.border
                    ColumnLayout {
                        id:captureForm;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:16;spacing:12
                        Text {text:"采集设备";font.pixelSize:Typography.body;color:Colors.text}
                        RowLayout {
                            Layout.fillWidth:true;spacing:8
                        AppComboBox {
                            objectName:"sensorSourceChoice";Layout.fillWidth:true;Layout.minimumWidth:100;height:36
                            model:App.analysis.devices;textRole:"name";enabled:!App.analysis.running&&!panel.pointProfile.source
                            currentIndex:App.analysis.devices.findIndex(function(d){return d.id===panel.selectedSource})
                            displayText:currentIndex<0?"未连接采集设备":currentText
                            onActivated:panel.chooseSource(App.analysis.devices[currentIndex].id)
                        }
                            TouchButton {objectName:"refreshCaptureDevices";text:App.analysis.discovering?"刷新中…":"刷新设备";height:36;autoWidth:true;variant:"tonal";enabled:App.session.loggedIn&&!App.analysis.discovering;onClicked:App.analysis.refreshDevices()}
                        }
                        Text {objectName:"captureDeviceStatus";Layout.fillWidth:true;visible:App.analysis.deviceStatus.length>0;text:App.analysis.deviceStatus;font.pixelSize:Typography.body;color:Colors.textSecondary;wrapMode:Text.Wrap}
                        Text {text:"采集时间窗口";font.pixelSize:Typography.body;color:Colors.text}
                        AppComboBox {objectName:"captureTimeWindow";Layout.preferredWidth:200;height:36;model:["1 s","2 s","5 s"];currentIndex:[1,2,5].indexOf(panel.windowSeconds);onActivated:{panel.windowSeconds=[1,2,5][currentIndex];panel.configure()}}
                        AppCheckBox {objectName:"captureOverlayChannels";text:"叠加显示六个通道";checked:panel.overlayChannels;enabled:(panel.snapshot.channels||0)>1&&!(panel.options.gain>0);onToggled:{panel.overlayChannels=checked;panel.configure()}}
                        Text {text:"传感器标定";font.pixelSize:Typography.body;color:Colors.text}
                        Flow {
                            Layout.fillWidth:true;spacing:8
                            FieldInput {id:gainInput;objectName:"sensorCalibrationGain";width:Math.min(280,scroll.availableWidth-32);height:36;text:panel.options.gain>0?String(panel.options.gain):"";placeholder:"物理单位 / FS";inputMethodHints:Qt.ImhFormattedNumbersOnly}
                            AppComboBox {id:unitInput;width:110;height:36;model:["V","Pa"];currentIndex:panel.options.unit==="Pa"?1:0}
                            TouchButton {objectName:"applySensorCalibration";text:"应用标定";autoWidth:true;height:36;enabled:!App.analysis.running;onClicked:{var gain=Number(gainInput.text);if(!isFinite(gain)||gain<=0){panel.feedback="请输入大于零的标定系数";return}panel.options={c0:panel.options.c0,normalize:panel.options.normalize,gain:gain,unit:unitInput.currentText};panel.configure();panel.feedback="标定已应用"}}
                            TouchButton {text:"恢复 FS";autoWidth:true;height:36;variant:"ghost";enabled:!App.analysis.running;onClicked:{panel.options={c0:panel.options.c0,normalize:panel.options.normalize,gain:0,unit:"FS"};gainInput.text="";panel.configure();panel.feedback="已恢复 FS"}}
                        }
                    }
                }
                Rectangle {
                    visible:settings.section===1;Layout.fillWidth:true;implicitHeight:analysisForm.implicitHeight+32;radius:16;color:Colors.panel;border.color:Colors.border
                    ColumnLayout {
                        id:analysisForm;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:16;spacing:12
                        Text {text:"分析范围";font.pixelSize:Typography.body;color:Colors.text}
                        AppComboBox {objectName:"analysisTimeScope";Layout.preferredWidth:220;height:36;model:["整段录音","指定秒数范围","最近采集窗口"];currentIndex:panel.analysisScope;enabled:!App.analysis.running;onActivated:{panel.analysisScope=currentIndex;panel.rangeStart=0;panel.rangeEnd=1;if(currentIndex===1&&panel.analysisEndSeconds<=panel.analysisStartSeconds)panel.analysisEndSeconds=Math.ceil(panel.snapshot.full_duration||1);panel.configure()}}
                        Flow {
                            visible:panel.analysisScope===1;Layout.fillWidth:true;spacing:8
                            FieldInput {objectName:"analysisStartSecond";width:130;height:36;enabled:!App.analysis.running;text:String(panel.analysisStartSeconds);placeholder:"开始秒";inputMethodHints:Qt.ImhDigitsOnly;onEditingFinished:function(value){panel.analysisStartSeconds=Math.max(0,Math.floor(Number(value)||0));panel.configure()}}
                            Text {height:36;verticalAlignment:Text.AlignVCenter;text:"至";font.pixelSize:Typography.body;color:Colors.textSecondary}
                            FieldInput {objectName:"analysisEndSecond";width:130;height:36;enabled:!App.analysis.running;text:String(panel.analysisEndSeconds);placeholder:"结束秒";inputMethodHints:Qt.ImhDigitsOnly;onEditingFinished:function(value){panel.analysisEndSeconds=Math.max(0,Math.floor(Number(value)||0));panel.configure()}}
                            Text {height:36;verticalAlignment:Text.AlignVCenter;text:"秒";font.pixelSize:Typography.body;color:Colors.textSecondary}
                        }
                        Text {text:"纵坐标范围";font.pixelSize:Typography.body;color:Colors.text}
                        Flow {
                            Layout.fillWidth:true;spacing:8
                            AppComboBox {objectName:"captureTimeScale";width:200;height:36;model:["时域 ±1","时域 ±0.1","时域 ±0.01"];currentIndex:[1,.1,.01].indexOf(panel.timeLimit);onActivated:{panel.timeLimit=[1,.1,.01][currentIndex];panel.resetCharts()}}
                            AppComboBox {objectName:"captureFrequencyScale";width:220;height:36;model:["频域 −120～0 dB","频域 −100～−20 dB","频域 −80～−20 dB"];currentIndex:[-120,-100,-80].indexOf(panel.frequencyMinimum);onActivated:{panel.frequencyMinimum=[-120,-100,-80][currentIndex];panel.frequencyMaximum=[0,-20,-20][currentIndex];panel.resetCharts()}}
                        }
                        Flow {
                            Layout.fillWidth:true;spacing:12
                            AppCheckBox {objectName:"analysisC0";text:"显示 C0";checked:panel.options.c0;onToggled:{panel.options={c0:checked,normalize:panel.options.normalize,gain:panel.options.gain,unit:panel.options.unit};panel.configure()}}
                            AppCheckBox {objectName:"analysisNormalize";text:"按系数标准化";checked:panel.options.normalize;onToggled:{panel.options={c0:panel.options.c0,normalize:checked,gain:panel.options.gain,unit:panel.options.unit};panel.configure()}}
                        }
                        Text {text:"图表显示范围";font.pixelSize:Typography.body;color:Colors.text}
                        RangeSlider {objectName:"analysisDisplayRange";Layout.fillWidth:true;from:0;to:1;first.value:panel.rangeStart;second.value:panel.rangeEnd;first.onMoved:panel.rangeStart=first.value;second.onMoved:panel.rangeEnd=second.value}
                        RowLayout {
                            TouchButton {text:"放大纵轴";autoWidth:true;height:34;onClicked:panel.zoomCharts()}
                            TouchButton {text:"复位图表";autoWidth:true;height:34;variant:"ghost";onClicked:{panel.rangeStart=0;panel.rangeEnd=1;panel.resetCharts()}}
                        }
                    }
                }
                Rectangle {
                    visible:settings.section===2;Layout.fillWidth:true;implicitHeight:trendForm.implicitHeight+32;radius:16;color:Colors.panel;border.color:Colors.border
                    ColumnLayout {
                        id:trendForm;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:16;spacing:12
                        Text {text:"趋势指标";font.pixelSize:Typography.body;color:Colors.text}
                        AppComboBox {objectName:"trendMetricChoice";Layout.preferredWidth:240;height:36;model:panel.metricNames;currentIndex:panel.trendMetric;onActivated:panel.trendMetric=currentIndex}
                        Text {text:"移动均值";font.pixelSize:Typography.body;color:Colors.text}
                        AppComboBox {objectName:"trendSmoothingChoice";Layout.preferredWidth:240;height:36;model:["3 点","5 点","10 点"];currentIndex:[3,5,10].indexOf(panel.smoothing);onActivated:panel.smoothing=[3,5,10][currentIndex]}
                        AppCheckBox {objectName:"trendRelative";text:"相对首条变化 %";checked:panel.relativeTrend;onToggled:panel.relativeTrend=checked}
                    }
                }
                Rectangle {
                    visible:settings.section===3;Layout.fillWidth:true;implicitHeight:fileForm.implicitHeight+32;radius:16;color:Colors.panel;border.color:Colors.border
                    ColumnLayout {
                        id:fileForm;anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top;anchors.margins:16;spacing:12
                        Flow {Layout.fillWidth:true;spacing:8
                            TouchButton {text:"导入 WAV";autoWidth:true;height:36;enabled:!App.analysis.running;onClicked:panel.importRecording()}
                            TouchButton {text:"刷新数据";autoWidth:true;height:36;variant:"ghost";onClicked:App.analysis.refresh()}
                        }
                        RowLayout {Layout.fillWidth:true
                            Text{text:"CSV 范围";font.pixelSize:Typography.body;color:Colors.text}
                            AppComboBox{objectName:"csvExportScope";Layout.preferredWidth:240;height:36;model:["整段录音（从开始到当前）","当前窗口（最近 "+panel.windowSeconds+" s）"];currentIndex:panel.exportScope==="full"?0:1;onActivated:panel.exportScope=currentIndex===0?"full":"window";enabled:!App.analysis.exportBusy}
                        }
                        Flow {Layout.fillWidth:true;spacing:8
                            TouchButton {text:"导出分析 JSON";autoWidth:true;height:36;enabled:!!panel.snapshot.has_plot&&!App.analysis.exportBusy;onClicked:panel.chooseExport("json")}
                            TouchButton {text:panel.exportScope==="full"?"导出整段 CSV":"导出窗口 CSV";autoWidth:true;height:36;enabled:!!panel.snapshot.has_plot&&!App.analysis.exportBusy;onClicked:panel.chooseExport("csv")}
                            TouchButton {text:"取消导出";autoWidth:true;height:36;visible:App.analysis.exportBusy;onClicked:App.analysis.cancelExport()}
                            TouchButton {text:"关联本次巡检";autoWidth:true;height:36;enabled:!!panel.snapshot.has_plot;onClicked:panel.linkChart()}
                            TouchButton {text:"保存图表 PNG";autoWidth:true;height:36;enabled:!!panel.snapshot.has_plot;onClicked:panel.saveChart()}
                        }
                        Text{Layout.fillWidth:true;text:App.analysis.exportBusy?"正在后台导出原始采样…":App.analysis.exportResult.ok?(App.analysis.exportResult.scope?("已导出"+(App.analysis.exportResult.scope==="full"?"整段录音":"当前窗口")+" · "+App.analysis.exportResult.rows+" 行 · "+Number(App.analysis.exportResult.start_seconds).toFixed(3)+"–"+Number(App.analysis.exportResult.end_seconds_exclusive).toFixed(3)+" s") :"分析文件已保存"):(App.analysis.exportResult.error||"");visible:text.length>0;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
                    }
                }
                Text {Layout.fillWidth:true;visible:panel.feedback.length>0;text:panel.feedback;wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:Colors.textSecondary}
            }
        }
    }
}
