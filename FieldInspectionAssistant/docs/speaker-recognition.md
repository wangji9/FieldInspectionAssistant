# 声纹识别

在「声音与信号 → 智能声纹分析」点击「开始智能分析」。默认读取 ListenGo 的六路 16 kHz 原始信号，按各路信噪比选取语音并平滑切换，再将单声道送入声纹和转写模型。拔除设备时会提示检查六麦连接。也可以选择音频文件或从录音管理送入已有录音。

- 麦克风采集用 WebRTC VAD 和自适应噪声门限触发分析。原生 sherpa-onnx 使用 Pyannote 3.0 FP32 划分发言时间和同讲区间，长录音使用中英双语 CAM++ 全局聚类。声纹档案和转写的输入经 100 Hz 高通，转写前再从低能量帧估计并抑制稳态噪声。
- 声纹建档和比对继续使用原版 CAM++ 从单人发言提取 192 维声纹。至少累计 1.2 秒单人语音才可自动建档。长录音首次建档至少累计 3 秒单人语音。可选填实际发言人数辅助文件聚类。已有档案通过余弦相似度匹配。同讲中缺乏足够单人样本的轨道显示为「未确认」。
- 波形、0–8 kHz 声谱图和发言轨道共用时间轴。黄色区间表示同时检测到两位或以上发言人，轨道留白表示间隙。点击彩色发言条可查看转写和归属。麦克风分析最多 15 分钟，音频文件每次最多 600 秒。可放大、平移时间轴，点击档案可高亮相应轨道。
- 智能分析调用板端常驻 Qwen3-ASR-0.6B 转写各轨道的单人片段，语种由模型自动识别。相邻单人片段之间最多 350 毫秒的同讲用静音占位后一起转写，模型能利用前后文，时间戳仍与原音频对齐。多人同讲保留重叠起止时间。JSON 导出包含转写模型和带时间的转写片段。

## 安装与验证

Pyannote 和 CAM++ 通过 sherpa-onnx C API 在 RK3588 CPU 上运行。智能分析通过本机 `board-ai.service` 的作业队列调用 Qwen3-ASR-0.6B。服务不可用时，发言记录标记转写未返回。原有文件识别接口仍可调用 Whisper。

```bash
sudo bash deploy/install_speaker_recognition.sh /path/to/speaker-assets
ldd /userdata/fieldops-speaker/lib/libsherpa-onnx-c-api.so
./deploy/run_field_inspection.sh
```

安装脚本固定 sherpa-onnx 2.0.1、CAM++、Pyannote 3.0 和 Whisper tiny 的 SHA-256。下载不便时可把脚本列出的模型文件放到同一目录，再传入该目录。

| 路径 | 内容 |
| --- | --- |
| `/userdata/fieldops-speaker/lib/` | ARM64 sherpa-onnx C API 和 ONNX Runtime 共享库 |
| `/userdata/fieldops-speaker/models/` | CAM++ 档案模型、中英双语 CAM++ 聚类模型、Pyannote 3.0 FP32 和兼容旧安装的 INT8 模型 |
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
