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
        MenuItem { text: "恢复默认布局"; onTriggered: bar.workbench.restoreLayout() }
    }
    Menu {
        title: "工程(&P)"
        MenuItem { text: "工程属性"; onTriggered: bar.workbench.editProject() }
        MenuItem { text: "工程归档…"; enabled: bar.hasProject; onTriggered: bar.workbench.copyProject(true) }
    }
    Menu {
        title: "设备(&D)"
        MenuItem { text: "添加设备…"; enabled: !!App.session.permissions.manage_assets; onTriggered: bar.workbench.addDevice() }
        MenuItem { text: "设备台账"; onTriggered: App.nav.push("devices") }
        MenuItem { text: "热像仪连接与测试"; onTriggered: bar.workbench.thermalSetup("device") }
        MenuItem { text: "声音采集设置"; onTriggered: App.nav.push("analysis", {analysis_tab:0, open_capture_settings:true}) }
    }
    Menu {
        title: "采集(&C)"
        MenuItem { text: "视频与图像"; onTriggered: App.nav.push("camera") }
        MenuItem { text: "声音与信号"; onTriggered: bar.workbench.navigateSubsection("analysis") }
        MenuItem { text: "热像采集与分析"; onTriggered: App.nav.push("thermal") }
        MenuSeparator {}
        MenuItem { text: "停止当前采集"; enabled: bar.workbench.canStopCapture; onTriggered: bar.workbench.stopCapture() }
    }
    Menu {
        title: "巡检(&I)"
        MenuItem { text: "巡检任务"; onTriggered: App.nav.push("tasks") }
        MenuItem { text: "巡检记录"; onTriggered: App.nav.push("history") }
        MenuItem { text: "事件台账"; onTriggered: App.nav.push("events") }
        MenuItem { text: "缺陷整改"; onTriggered: App.nav.push("defects") }
    }
    Menu {
        title: "分析(&A)"
        MenuItem { text: "融合诊断"; onTriggered: App.nav.push("projectDiagnosis") }
        MenuItem { text: "视觉分析"; onTriggered: bar.workbench.navigateSubsection("visionAlgorithms") }
        MenuItem { text: "AI 助手"; onTriggered: bar.workbench.navigateSubsection("ai") }
        MenuItem { text: "多模态图谱"; onTriggered: App.nav.push("projectGraph") }
    }
    Menu {
        title: "报告(&R)"
        MenuItem { text: "正式巡检报告"; onTriggered: bar.workbench.navigateSubsection("formalReports") }
        MenuItem { text: "事件分析报告"; onTriggered: App.nav.push("reports") }
    }
    Menu {
        title: "工具(&T)"
        MenuItem { text: "模型服务诊断"; onTriggered: bar.workbench.rk1828() }
        MenuItem { text: "模型管理"; onTriggered: App.nav.push("ai", {mode:"text",open_models:true}) }
        MenuItem { text: "操作日志"; onTriggered: bar.workbench.showOutput() }
        MenuItem { text: "设置"; onTriggered: App.nav.push("settings") }
        MenuSeparator {}
        MenuItem { text: "打开兼容热像工具"; onTriggered: App.nav.push("legacyThermal") }
    }
    Menu {
        title: "设备信息(&N)"
        MenuItem { text: "设备连接与性能"; onTriggered: App.nav.push("settings", {section:"hardware"}) }
    }
    Menu {
        title: "帮助(&H)"
        MenuItem { text: "用户手册"; onTriggered: bar.workbench.showHelp() }
        MenuItem { text: "版本信息"; onTriggered: bar.workbench.showVersion() }
    }
}
