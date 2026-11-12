# 声纹识别

在「声音与信号 → 声纹识别」选择「实时麦克风」。板端优先使用 ListenGo 六麦的语音输入，采集 16 kHz 单声道 PCM16。

1. 填写姓名并点击「录制新声纹」。在安静环境中连续说 5–10 秒，再点「停止并保存声纹」。选中档案可录制追加样本，每人最多三组。
2. 点击「开始实时识别」。输入电平和波形约每 80 ms 刷新一次，CAM++ 按 2.4 秒窗口比对。单次识别最长 120 秒，结束后保留图表和识别时段，原始麦克风音频仅用于内存中的本次分析。
3. 默认余弦阈值为 0.77，两个候选相差不足 0.05 时保留为未知。余弦分数用于相似度比对，同人短录音也可能低于默认阈值。
4. 波形、分人轨道和余弦分数共用时间轴。点击轨道或表格行可查看所选时段。JSON 导出包含识别结果、阈值与可用的转写。

选择「音频文件」可打开文件，也可从「录音管理」送入六麦录音。FFmpeg 在本机解码原文件选定时段，每次最多 600 秒。启用「按档案语种转写」时，已匹配且配置语种的时段交给 Whisper。

语音活动检测结合能量和 WebRTC VAD。文件分析按静音划分候选发言区域，连续发言按约六秒窗口比对。两种输入的轨道边界均以分析窗口为准，快速换人或重叠讲话可能留下未知窗口。

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
