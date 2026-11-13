# 声纹识别

在「声音与信号 → 智能声纹分析」点击「开始智能分析」。板端优先使用 ListenGo 六麦的 16 kHz 单声道语音输入，也可以选择音频文件或从录音管理送入已有录音。

- WebRTC VAD 连续检测语音，按停顿或最长窗口划分片段。有效人声至少 0.9 秒才提取声纹，自动建档至少需要 1.2 秒。短静音可以保留在同一片段中。
- CAM++ 输出 192 维声纹。足够清晰且与已有档案明显不同的片段自动保存为「发言人 001」等匿名档案。后续发言自动比对，相似度不足或候选太接近时显示「待确认」。同一个人的不同录音可能被分为不同档案。
- 麦克风波形、输入电平、发言时间轴和余弦分数实时显示。单次麦克风分析最长 120 秒，采集的原始 PCM 仅用于本次分析，声纹库只保存特征向量及档案信息。导出的 JSON 包含发言时段和匹配结果。
- 文件分析支持选取最多 600 秒的时段。启用「按档案语种转写」时，已匹配且原有档案配置语种的片段交给 Whisper。

## 安装与验证

模型运行在 RK3588 CPU。CAM++ 由 C++ 调用 sherpa-onnx C API，复用后台线程上的模型会话。Whisper 使用同一发行版的原生程序。两个模块运行时都使用 ARM64 共享库和 ONNX 权重。

```bash
sudo bash deploy/install_speaker_recognition.sh /path/to/speaker-assets
ldd /userdata/fieldops-speaker/lib/libsherpa-onnx-c-api.so
./deploy/run_field_inspection.sh
```

安装脚本固定 sherpa-onnx 2.0.1、CAM++ 和 Whisper tiny 的 SHA-256。下载不便时可把脚本列出的三个文件放到同一目录，再传入该目录。

| 路径 | 内容 |
| --- | --- |
| `/userdata/fieldops-speaker/lib/` | ARM64 sherpa-onnx C API 和 ONNX Runtime 共享库 |
| `/userdata/fieldops-speaker/models/` | 3D-Speaker CAM++ 的 192 维嵌入模型 |
| `/userdata/fieldops-speaker/whisper/` | 多语种 Whisper tiny INT8 与词表 |
| `$XDG_DATA_HOME/FieldOps/FieldInspectionAssistant/voiceprints/profiles.json` | 本机声纹档案。默认数据根目录为 `~/.local/share` |

使用 [上游公开声纹样本](https://github.com/csukuangfj/sr-data/tree/92b4978ba6b4358e95be385e51e86ece86524d33/test/3d-speaker) 做隔离板端验收。

```bash
qa=$(mktemp -d /tmp/fieldops-speaker-qa.XXXXXXXX)
python3 tests/speaker_recognition_demo.py "$qa" /path/to/sr-data/test/3d-speaker
export XDG_DATA_HOME="$qa/data" XDG_CONFIG_HOME="$qa/config"
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
FIELDOPS_QA_SCRIPT="$qa/steps.json" FIELDOPS_QA_DIR="$qa/audit" ./build/field_inspection_assistant
FIELDOPS_QA_SCRIPT="$qa/reopen.json" FIELDOPS_QA_DIR="$qa/reopen" ./build/field_inspection_assistant
```

来源与授权见 [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx/releases/tag/v2.0.1)、[CAM++ 模型卡](https://www.modelscope.cn/models/iic/speech_campplus_sv_zh-cn_16k-common) 和 [Whisper](https://github.com/openai/whisper)。
