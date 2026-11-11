# 现场巡检助手

面向 RK3588 和 RK1828 的 Qt 6 巡检应用。工程管理、现场采集、设备巡检、诊断、缺陷闭环与报告使用同一条业务链。

- [应用源码与板端构建](FieldInspectionAssistant/README.md)
- [现场操作与关联规则](FieldInspectionAssistant/docs/inspection-relationships.md)
- [部署与维护](FieldInspectionAssistant/PRODUCT_OPERATIONS.md)
- [板端推理服务](board_ai/README.md)
- [六麦设备配置](board_audio/README.md)

## 构建入口

在 RK3588 的 Linux 环境安装 Qt 6、Qt Virtual Keyboard 开发包、OpenCV、ALSA、FFTW 和 XCB 开发包。推理服务还需要匹配的 Rockchip SDK 和模型文件。

```sh
cmake -S FieldInspectionAssistant -B FieldInspectionAssistant/build -DCMAKE_BUILD_TYPE=Release
cmake --build FieldInspectionAssistant/build -j4
```

使用现有板端安装脚本时，将应用源码置于 `/home/ztl/FieldInspectionAssistant`，将 `board_ai` 中的运行文件置于 `/userdata/ai-workbench`。首次启动时在应用内创建管理员。
