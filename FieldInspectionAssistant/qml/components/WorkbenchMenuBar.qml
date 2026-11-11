import QtQuick
import QtQuick.Controls
import QtQml.Models
import "../theme"

MenuBar {
    id: bar
    required property var workbench
    font.family: Typography.uiFamily
    readonly property bool hasProject: !!App.projects.project.id
    leftPadding: 18

    background: Rectangle {
        implicitHeight: 28
        border.color: "#c5ced8"
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#e2e9f1" }
            GradientStop { position: 0.38; color: "#f8fafc" }
            GradientStop { position: 1.0; color: "#edf2f7" }
        }
    }

    Menu {
        title: "文件(&F)"
        MenuItem { text: "新建工程…"; onTriggered: bar.workbench.newProject() }
        MenuItem { text: "打开工程…"; onTriggered: bar.workbench.openProject() }
        MenuItem { text: "导入工程…"; onTriggered: bar.workbench.importProject() }
        MenuItem {
            objectName: "openDemoProject"
            text: "打开演示工程"
            enabled: App.projects.demoProject.toString().length > 0
            onTriggered: bar.workbench.openDemoProject()
        }
        Menu {
            id: recent
            title: "最近工程"
            MenuItem { text: "暂无最近工程"; visible: App.projects.recentProjects.length === 0; enabled: false }
            Instantiator {
                model: App.projects.recentProjects
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.name
                    onTriggered: bar.workbench.openRecent(modelData.url)
                }
                onObjectAdded: function(index, item) { recent.insertItem(index + 1, item) }
                onObjectRemoved: function(index, item) { recent.removeItem(item) }
            }
        }
        MenuSeparator {}
        MenuItem { text: "保存"; enabled: bar.hasProject; onTriggered: bar.workbench.saveProject() }
        MenuItem { text: "另存为…"; enabled: bar.hasProject; onTriggered: bar.workbench.copyProject(false) }
        MenuItem { text: "关闭工程"; enabled: bar.hasProject; onTriggered: bar.workbench.closeProject() }
        MenuSeparator {}
        MenuItem { text: "退出登录 / 切换账户"; onTriggered: App.session.logout() }
        MenuItem { text: "退出"; onTriggered: bar.workbench.close() }
    }
    Menu {
        title: "编辑(&E)"
        MenuItem { text: "撤销"; enabled: bar.workbench.canEdit("undo"); onTriggered: bar.workbench.edit("undo") }
        MenuItem { text: "重做"; enabled: bar.workbench.canEdit("redo"); onTriggered: bar.workbench.edit("redo") }
        MenuSeparator {}
        MenuItem { text: "复制"; enabled: bar.workbench.canEdit("copy"); onTriggered: bar.workbench.edit("copy") }
        MenuItem { text: "粘贴"; enabled: bar.workbench.canEdit("paste"); onTriggered: bar.workbench.edit("paste") }
        MenuItem { text: "删除"; enabled: bar.workbench.canEdit("remove"); onTriggered: bar.workbench.edit("remove") }
        MenuSeparator {}
        MenuItem { text: "查找…"; onTriggered: bar.workbench.find() }
    }
    Menu {
        title: "视图(&V)"
        MenuItem {
            text: "工程资源管理器"; checkable: true; checked: bar.workbench.showResources
            onTriggered: bar.workbench.showResources = checked
        }
        MenuItem {
            text: "属性窗口"; checkable: true; checked: bar.workbench.showInspector
            onTriggered: bar.workbench.showInspector = checked
        }
        MenuItem { text: "输出窗口"; onTriggered: bar.workbench.showOutput() }
        MenuItem { text: "恢复默认布局"; onTriggered: bar.workbench.restoreLayout() }
    }
    Menu {
        title: "工程(&P)"
        MenuItem { text: "工程属性"; onTriggered: bar.workbench.editProject() }
        MenuItem { text: "工程配置"; onTriggered: bar.workbench.thermalSetup("software") }
        MenuItem { text: "设备管理"; onTriggered: App.nav.push("devices") }
        MenuItem { text: "检测方案"; onTriggered: App.nav.push("settings", {section:"standards"}) }
        MenuItem { text: "工程归档…"; enabled: bar.hasProject; onTriggered: bar.workbench.copyProject(true) }
    }
    Menu {
        title: "设备(&D)"
        MenuItem { text: "添加设备…"; enabled: !!App.session.permissions.manage_assets; onTriggered: bar.workbench.addDevice() }
        MenuItem { text: "设备连接"; onTriggered: bar.workbench.thermalSetup("device") }
        MenuItem { text: "设备测试"; onTriggered: bar.workbench.thermalSetup("test") }
        MenuItem { text: "通道配置"; onTriggered: App.nav.push("analysis", {open_capture_settings:true}) }
    }
    Menu {
        title: "采集(&C)"
        MenuItem { text: "开始采集"; enabled: !bar.workbench.thermal.connected; onTriggered: bar.workbench.startCapture() }
        MenuItem {
            text: bar.workbench.thermal.paused ? "继续采集" : "暂停"
            enabled: bar.workbench.thermal.connected && bar.workbench.thermal.hasFrame
            onTriggered: bar.workbench.thermal.togglePause()
        }
        MenuItem { text: "停止"; enabled: bar.workbench.canStopCapture; onTriggered: bar.workbench.stopCapture() }
        MenuSeparator {}
        MenuItem { text: "拍照"; onTriggered: bar.workbench.takePhoto() }
        MenuItem { text: App.speech.recording ? "结束录音" : "录音"; onTriggered: bar.workbench.toggleRecording() }
        MenuItem { text: "保存数据"; enabled: bar.workbench.thermal.hasFrame; onTriggered: bar.workbench.saveCapture() }
        MenuItem { text: "资料与文字"; onTriggered: App.nav.push("projectEvidence") }
        MenuItem { text: "多模态图谱"; onTriggered: App.nav.push("projectGraph") }
    }
    Menu {
        title: "巡检(&I)"
        MenuItem { text: "新建任务"; enabled: !!App.session.permissions.assign; onTriggered: bar.workbench.openTask(true) }
        MenuItem { text: "任务列表"; onTriggered: App.nav.push("tasks") }
        MenuItem { text: "执行巡检"; enabled: !!App.session.permissions.inspect; onTriggered: bar.workbench.openTask(false) }
        MenuItem { text: "历史记录"; onTriggered: App.nav.push("history") }
        MenuItem { text: "事件管理"; onTriggered: App.nav.push("events") }
    }
    Menu {
        title: "分析(&A)"
        MenuItem { text: "图像分析"; onTriggered: App.nav.push("ai", {mode:"vision"}) }
        MenuItem { text: "视觉算法"; onTriggered: App.nav.push("ai", {mode:"algorithms"}) }
        MenuItem { text: "声音分析"; onTriggered: App.nav.push("analysis", {analysis_tab:3}) }
        MenuItem { text: "热成像分析"; onTriggered: bar.workbench.thermalSetup("image", true) }
        MenuItem { text: "信号分析"; onTriggered: App.nav.push("analysis", {analysis_tab:1}) }
        MenuItem { text: "多模态融合"; onTriggered: App.nav.push("fusion") }
        MenuItem { text: "工程诊断"; onTriggered: bar.workbench.navigate("ai") }
        MenuItem {
            text: "手动登记分析事件…"
            enabled: bar.hasProject && !!App.session.permissions.inspect
            onTriggered: bar.workbench.registerAnalysisEvent()
        }
    }
    Menu {
        title: "报告(&R)"
        MenuItem { text: "新建报告"; enabled: bar.hasProject; onTriggered: bar.workbench.newReport() }
        MenuItem { text: "报告预览"; enabled: App.projects.reports.length > 0; onTriggered: bar.workbench.showReport() }
        MenuItem { text: "导出 PDF…"; enabled: App.projects.reports.length > 0; onTriggered: bar.workbench.exportPdf() }
        MenuItem { text: "报告归档"; onTriggered: App.nav.push("reports") }
        MenuItem { text: "正式巡检报告"; onTriggered: bar.workbench.navigateSubsection("formalReports") }
    }
    Menu {
        title: "工具(&T)"
        MenuItem { text: "RK1828管理"; onTriggered: bar.workbench.rk1828() }
        MenuItem { text: "模型管理"; onTriggered: App.nav.push("ai", {mode:"text",open_models:true}) }
        MenuItem { text: "日志管理"; onTriggered: bar.workbench.showOutput() }
        MenuItem { text: "系统设置"; onTriggered: App.nav.push("settings") }
        MenuSeparator {}
        MenuItem { text: "兼容热像软件"; onTriggered: App.nav.push("legacyThermal") }
    }
    Menu {
        title: "设备信息(&N)"
        MenuItem { text: "板端性能与资源"; onTriggered: App.nav.push("settings", {section:"deviceInfo"}) }
        MenuItem { text: "外设与连接"; onTriggered: App.nav.push("settings", {section:"hardware"}) }
    }
    Menu {
        title: "窗口(&W)"
        MenuItem { text: "关闭标签"; enabled: bar.workbench.section !== "projects"; onTriggered: bar.workbench.closeTab() }
        Menu {
            id: windows
            title: "切换窗口"
            Instantiator {
                model: bar.workbench.sections
                delegate: MenuItem {
                    required property var modelData
                    text: modelData.title
                    checkable: true
                    checked: bar.workbench.section === modelData.id
                    onTriggered: bar.workbench.navigate(modelData.id)
                }
                onObjectAdded: function(index, item) { windows.insertItem(index, item) }
                onObjectRemoved: function(index, item) { windows.removeItem(item) }
            }
        }
        MenuItem { text: "重置停靠布局"; onTriggered: bar.workbench.restoreLayout() }
    }
    Menu {
        title: "帮助(&H)"
        MenuItem { text: "用户手册"; onTriggered: bar.workbench.showHelp() }
        MenuItem { text: "版本信息"; onTriggered: bar.workbench.showVersion() }
        MenuItem { text: "系统诊断"; onTriggered: App.nav.push("settings", {section:"runtime"}) }
    }
}
