import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../theme"
Rectangle {
    id:root;color:Colors.panel;border.color:Colors.border
    readonly property var status:App.hardware.snapshot
    function size(value){return value===undefined?"—":(Number(value)/1073741824).toFixed(1)+" GB"}
    function percent(value){return value===undefined?"—":Number(value).toFixed(1)+"%"}
    readonly property var accelerator: App.visionLive.hardware
    Timer {
        interval: 3000; repeat: true; running: root.visible && App.hardware.active
        triggeredOnStart: true
        onTriggered: App.visionLive.refreshHardware()
    }
    ListModel{id:rowModel}
    function syncRows(){for(var i=0;i<rows.length;i++){var next={name:String(rows[i].name),value:String(rows[i].value)};if(i>=rowModel.count)rowModel.append(next);else{var old=rowModel.get(i);if(old.name!==next.name)rowModel.setProperty(i,"name",next.name);if(old.value!==next.value)rowModel.setProperty(i,"value",next.value)}}if(rowModel.count>rows.length)rowModel.remove(rows.length,rowModel.count-rows.length)}
    onRowsChanged:syncRows()
    Component.onCompleted:syncRows()
    readonly property var rows:[
        {name:"板卡",value:status.board_model||"—"},
        {name:"RK3588 NPU 频率",value:status.rk3588_npu_hz ? (status.rk3588_npu_hz/1000000).toFixed(0)+" MHz" : "无读数"},
        {name:"RK1828 NPU",value:accelerator.npuOnline ? "在线 · "+percent(accelerator.npuLoad)+" · 显存 "+(accelerator.npuMemory||"无读数") : "无读数"},
        {name:"系统负载（1 / 5 / 15 分钟）",value:(status.load||[]).join(" / ")||"—"},
        {name:"存储剩余 / 总量",value:status.disk_total?size(status.disk_free)+" / "+size(status.disk_total):"—"},
        {name:"设备温度",value:(status.temperatures||[]).map(function(t){return t.name+" "+Number(t.value).toFixed(1)+" °C"}).join(" · ")||"未提供温度传感器数据"},
        {name:"系统运行时间",value:status.uptime?Math.floor(status.uptime/3600)+" 小时 "+Math.floor(status.uptime%3600/60)+" 分钟":"—"},
        {name:"应用内存 / 线程",value:status.process_rss?(Number(status.process_rss)/1048576).toFixed(0)+" MB / "+status.threads:"—"},
        {name:"模型服务",value:App.ai.available?(App.ai.busy||App.agent.busy?"在线 · 正在推理":"在线 · 空闲"):"未连接"},
        {name:"常驻模型",value:(App.ai.system.resident_models||[]).map(function(m){return m.model}).join("、")||"无常驻模型"},
        {name:"声音采集",value:App.analysis.running?"采集中 · "+(App.analysis.summary.rate||"—")+" Hz · "+(App.analysis.summary.channels||"—")+" 通道":"未采集"},
        {name:"摄像头",value:App.camera.recording?"正在录像 · "+App.camera.recordingSeconds+" s":App.camera.hasFrame?"实时画面 · "+App.camera.resolution:App.camera.status||"未打开"},
        {name:"摄像头帧率 / 已采帧",value:App.camera.hasFrame?Number(App.camera.metrics.preview_fps||0).toFixed(1)+" fps / "+(App.camera.metrics.capture_frames||0):"—"},
        {name:"音频服务",value:status.audio_service===undefined?"—":status.audio_service?"可连接":"查询失败"},
        {name:"待同步记录",value:App.sync.pending+" 条 · "+(App.sync.configured?"已配置服务器":"未配置服务器")},
        {name:"软件 / 数据库版本",value:App.version+" / schema "+(status.schema||"—")}
    ]
    ColumnLayout{anchors.fill:parent;anchors.margins:16;spacing:10
        RowLayout{Layout.fillWidth:true;Text{text:"运行状态";Layout.fillWidth:true;font.pixelSize:Typography.title;font.weight:Font.DemiBold;color:Colors.text}TouchButton{objectName:"refreshRuntimeStatus";text:App.hardware.busy?"更新中…":"刷新";autoWidth:true;height:32;enabled:!App.hardware.busy;variant:"ghost";onClicked:{App.hardware.refresh();App.ai.refreshStatus()}}}
        Text{objectName:"runtimeUpdatedAt";Layout.fillWidth:true;text:App.hardware.error|| (root.status.timestamp?"每 3 秒更新 · "+Qt.formatDateTime(new Date(root.status.timestamp),"HH:mm:ss"):"正在读取设备状态…");wrapMode:Text.Wrap;font.pixelSize:Typography.body;color:App.hardware.error?Colors.warning:Colors.textSecondary}
        Repeater {
            model: [
                {label:"CPU · "+(root.status.cpu_cores||"—")+" 核", value:root.status.cpu_percent, detail:root.percent(root.status.cpu_percent)},
                {label:"系统内存", value:root.status.memory_total ? 100*(1-root.status.memory_available/root.status.memory_total) : undefined,
                    detail:root.status.memory_total ? root.size(root.status.memory_total-root.status.memory_available)+" / "+root.size(root.status.memory_total) : "—"},
                {label:"RK1828 NPU 利用率", value:root.accelerator.npuLoad, detail:root.percent(root.accelerator.npuLoad)}
            ]
            ColumnLayout {
                required property var modelData
                Layout.fillWidth: true; spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Label { Layout.fillWidth: true; text: modelData.label }
                    Label { text: modelData.detail }
                }
                ProgressBar { Layout.fillWidth: true; from:0; to:100; value:modelData.value === undefined ? 0 : modelData.value; visible:modelData.value !== undefined }
            }
        }
        ListView{id:runtimeList;objectName:"liveRuntimeList";Layout.fillWidth:true;Layout.fillHeight:true;clip:true;spacing:10;model:rowModel;ScrollBar.vertical:ScrollBar{}
            delegate:Column{required property string name;required property string value;width:runtimeList.width;spacing:5
                Text{width:parent.width;text:parent.name;font.pixelSize:Typography.body;color:Colors.textSecondary;wrapMode:Text.Wrap}
                Text{width:parent.width;text:parent.value;font.pixelSize:Typography.body;color:Colors.text;wrapMode:Text.Wrap}
                Rectangle{width:parent.width;height:1;color:Colors.border}
            }
        }
    }
}
