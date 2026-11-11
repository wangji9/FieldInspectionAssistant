# MIIEP V0.1 工程工作区

通过「文件」新建、打开或导入工程，通过「工程 → 工程归档」导出工程。`.miproj` 目录包含清单、SQLite 数据库、各类原始资料和报告，移动时保留整个目录。打开工程会使用所选目录，导入则在应用的工程目录里生成独立副本。保存提交工程属性，另存为生成可继续编辑的工程副本，导出生成可在其他板端打开的归档目录。

1. 在总览点击「登记设备」。在「证据管理」选择设备、填写测点和模态，再导入文件。采集时间使用 ISO 8601，留空时使用原文件修改时间。
2. 点击总览图表的分类可以筛选资料，点击缩略图或双击列表可看原始资料。WAV 可以回听或导入信号分析。板端用 `ffplay` 打开视频。
3. 选择一条证据生成事件。融合查找同设备、同测点、前后 60 秒内的记录，并要求至少两种模态。
4. 在「融合与 AI」选择事件运行 RK1828 分析。模型收到证据索引和文本摘录。热像、声学波形和实拍图由对应功能查看。
5. 生成报告后可在应用内预览。Markdown 随工程保存在 `reports/`，PDF 可通过「报告 → 导出 PDF」保存到任意目录。

## 泵组复检演示

在板端选择「文件 → 打开演示工程」。第一次打开会创建可编辑的演示副本。也可以解压演示包，通过「文件 → 导入工程」选择其中的 `设备巡检演示.miproj`。

案例记录 P-101 循环水泵驱动端轴承座的温升与振动异常，以及 P-102 备用泵的停机交接。照片类别中的设备位置图、热像、音频、信号、视频和读数均为培训样本。选中热像生成事件时，会匹配同一测点的六种资料。稍后的复检记录和备用泵资料分别因时间和设备不同而独立保留。

报告区包含板端 Qwen 模型生成的证据分析。证据管理里还可打开「演示_现场执行与复核.json」，查看从任务、超限记录、缺陷派发、整改到人工复核的正式记录。它由软件在隔离数据目录中按实际流程生成后导入工程，巡检任务本身仍由应用的巡检业务库管理。

## 板端服务

工作区通过 `QLocalSocket` 发送单行 JSON。用户级服务使用私有的 `$XDG_RUNTIME_DIR/miiep/inference.sock`，将请求交给板端已有的 `board-ai.service`，由其调度 RKNN3 模型。启动、状态和测试命令在板端执行：

```sh
cd ~/FieldInspectionAssistant
cmake --build build --target field_inspection_assistant --parallel 4
install -D -m 644 deploy/systemd/miiep-inference.service ~/.config/systemd/user/miiep-inference.service
systemctl --user daemon-reload
systemctl --user enable --now miiep-inference.service
python3 tests/miiep_socket_smoke.py "$XDG_RUNTIME_DIR/miiep/inference.sock"
```

可在独立临时目录以 `--backend mock` 启动服务，并对测试套接字运行上述脚本加 `--infer`。客户端识别模拟结果，工程报告只读取实际模型的分析记录。
