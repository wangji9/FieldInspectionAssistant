# 现场巡检助手

面向 RK3588 和 RK1828 的现场巡检软件。以工程组织设备和现场资料，将诊断结果关联到巡检记录、整改和报告。

## 主要功能

| 功能 | 支持的操作 |
| --- | --- |
| 工程与设备管理 | 新建、打开、导入、保存和归档 `.miproj` 工程。在工程总览查看设备、证据和报告，维护设备档案、部件、测点及采集通道。 |
| 现场采集 | 摄像头实时预览、拍照和录像，热像预览与温度曲线，六麦原始六通道录音，以及文字和已有文件导入。 |
| 声音与信号分析 | 查看多通道时域、FFT、谱图和趋势，回听历史录音，导出原始采样 CSV 和分析图。支持本地声纹档案、分人轨道、指定语种转写，以及六麦声场叠加摄像头画面。 |
| 视觉分析 | 通过 RK1828 对摄像头画面或图片执行目标检测、实例分割和关键点识别，显示面部关键点、视线与头部姿态，管理人脸档案和识别记录。 |
| 大模型与多模态分析 | 支持文字问答、图片问答、语音转写和带时间戳的多帧视频理解。按设备、测点和 UTC 时间对齐视频、图片、热像、声音、信号和文字，保存诊断依据并构建工程图谱。 |
| 事件、缺陷与巡检 | 创建和派发巡检任务，填写检查项，手动登记事件或保存诊断建议。将事件关联到对应巡检，跟进缺陷分派、整改与复核。 |
| 报告与运维 | 生成并预览工程证据报告与正式巡检报告，按证据、摘要和整改模板导出 PDF，管理报告版本和业务备份。查看板端资源、外设连接与模型服务状态。 |

应用、推理服务、模型部署工具和串口配置统一维护在 [FieldInspectionAssistant](FieldInspectionAssistant/)。回归测试与硬件验收脚本集中在 [tests](FieldInspectionAssistant/tests/)。

## 部署和使用

- [板端环境和依赖安装命令](FieldInspectionAssistant/README.md#系统与开发环境)
- [源码安装、服务启动和桌面启动](FieldInspectionAssistant/README.md#板端安装与启动)
- [模型、数据库、工程、录音和日志存放位置](FieldInspectionAssistant/README.md#文件存放位置)
- [板端自检、推理和音频验证命令](FieldInspectionAssistant/README.md#验证与运维)
- [设备、诊断、事件与巡检的关联规则](FieldInspectionAssistant/docs/inspection-relationships.md)
- [现场业务流程与运维](FieldInspectionAssistant/PRODUCT_OPERATIONS.md)
- [多模态泵组演示与复现命令](FieldInspectionAssistant/README.md#多模态泵组演示)
- [变更记录](FieldInspectionAssistant/docs/CHANGELOG.md)
- [声纹识别与安装验证](FieldInspectionAssistant/docs/speaker-recognition.md)
- [六麦声学成像与标定](FieldInspectionAssistant/docs/acoustic-imaging.md)
