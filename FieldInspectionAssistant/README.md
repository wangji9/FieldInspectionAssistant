# 现场巡检助手

基于 C++17、Qt 6、QML 和 SQLite 的 RK3588 桌面应用。现场采集形成工程证据，分析结果可关联巡检任务与事件，缺陷经过派发、整改和复核后进入报告。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| `src/` | Qt 主程序、业务与设备服务、数据库和原生界面组件 |
| `qml/`、`resources.qrc` | 界面资源 |
| `migrations/` | SQLite 数据库迁移脚本 |
| `services/inference/` | 本地推理 API、模型注册与调度、常驻进程、视频理解工作进程和工程推理套接字服务 |
| `services/audio/` | 六麦唤醒协议、CH340 串口初始化和默认音频设备配置 |
| `deploy/inference/` | RKNN SDK 构建补丁、模型导入和语音运行环境安装 |
| `deploy/vision/` | RK1828 视觉模型转换及 C++ 工作进程构建工具 |
| `deploy/systemd/`、`deploy/udev/` | 服务定义与串口设备规则 |
| `tests/` | C++ 回归测试、Python 单元测试、演示工程生成与板端验收脚本 |
| `docs/` | 操作与设计文档 |
| `third_party/` | 随主程序构建的 libfvad 语音活动检测库、Qt 拼音引擎、词典及许可证 |

## 系统与开发环境

| 项目 | 部署基线 |
| --- | --- |
| 主板与系统 | RK3588，ARM64，Ubuntu 24.04 LTS，板端已核对 Ubuntu 24.04.4 和厂商内核 6.1.141 |
| 桌面 | Xfce/X11，应用部署账户 `ztl`，该账户需要 `audio`、`dialout`、`video` 组权限 |
| 编译工具 | GCC/G++ 支持 C++17，CMake 3.16 起，板端已核对 CMake 3.28.3 |
| 界面与本地库 | Qt 6.4.2、Qt Quick/QML、Qt Virtual Keyboard、SQLite 插件、OpenCV 4.6、ALSA、FFTW3、XCB |
| Python | 3.12，系统环境安装 Flask、NumPy、Pillow 和 pyserial，声学分析和本地朗读各用独立虚拟环境 |
| 硬件与媒体 | RK1828 使用板卡固件配套的 RKNN3 驱动与运行库，摄像头通过 V4L2，音频通过 ALSA/PulseAudio，编解码使用 FFmpeg |

板端须先装好与 RK1828 配套的系统镜像和运行库。以 `ztl` 执行以下命令确认架构、设备和权限。

```bash
uname -m
uname -r
cat /etc/os-release
id -nG
systemctl status rknn3.service --no-pager
rknn-smi info
```

检查 `id -nG` 的输出。缺少 `audio`、`dialout` 或 `video` 时，使用 `sudo usermod -aG audio,dialout,video ztl` 加入设备组，然后重新登录。中文输入法资源随主程序编译，来源见 [Qt 拼音引擎说明](third_party/pinyin/README.md)。

### 手动安装依赖与构建

完整板端安装会由 [安装脚本](deploy/install_board.sh) 执行 APT 安装。只构建桌面程序、准备开发机或排查依赖时，可以逐条执行以下命令。APT 仓库需要提供 Ubuntu 24.04 的 `universe` 软件包。

```bash
sudo apt-get update
sudo apt-get install \
  build-essential cmake git curl pkg-config python3-venv \
  python3-flask python3-numpy python3-pil python3-serial \
  alsa-utils pulseaudio-utils ffmpeg hunspell-en-gb \
  qt6-base-dev qt6-declarative-dev qt6-virtualkeyboard-dev qt6-virtualkeyboard-plugin \
  libqt6sql6-sqlite qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-dialogs qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-window qml6-module-qtquick-virtualkeyboard \
  qml6-module-qtqml-workerscript qml6-module-qt-labs-folderlistmodel \
  libasound2-dev libfftw3-dev libxcb1-dev libopencv-dev libomp-dev libsndfile1-dev \
  desktop-file-utils wmctrl fonts-noto-cjk fonts-noto-core fonts-inter fonts-dejavu-core
cd /home/ztl/FieldInspectionAssistant
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

声学分析使用 `/userdata/ai-workbench/analytics-venv/`，依赖 NumPy、SciPy、spafe 和 pypdf。本地朗读使用 `/userdata/ai-workbench/tts-venv/`，依赖 sherpa-onnx 和 NumPy。需要单独修复时，从应用目录运行以下命令。

```bash
bash deploy/install_analytics.sh
bash deploy/inference/install_speech.sh
```

## 板端安装与启动

通过获授权的 GitHub 账户获取源码。部署目录使用 `/home/ztl/FieldInspectionAssistant`。在目标板以 `ztl` 登录 Xfce 后打开终端执行以下命令。安装需要 sudo 权限、可访问的 APT 镜像及 SDK 和 Python 依赖下载源。

```bash
sudo apt-get update
sudo apt-get install git
git clone git@github.com:wangji9/FieldInspectionAssistant.git "$HOME/field-inspection-source"
mkdir -p "$HOME/FieldInspectionAssistant"
cp -a "$HOME/field-inspection-source/FieldInspectionAssistant/." "$HOME/FieldInspectionAssistant/"
cd /home/ztl/FieldInspectionAssistant
bash deploy/install_board.sh
python3 deploy/install_desktop_launcher.py
```

更新时在已有的 `field-inspection-source` 执行 `git pull --ff-only`，再复制应用目录并运行安装脚本。安装会编译 Qt 应用和原生推理程序，安装声学与朗读依赖，部署并重启推理服务。请在维护时段更新已运行的设备。首次启动空数据库时，在登录页创建管理员。

登录时可选「在本机保持登录」。凭据仅保存在当前板端用户数据目录，退出登录或切换账号时清除。共用系统账户的场合可在登录时取消勾选。推理服务使用系统默认调频策略，运行推理无需 sudo。

安装后可双击桌面的「现场巡检助手」，也可在 `ztl` 的桌面终端执行以下命令。最后一条会打开程序或切换到已打开的窗口。

```bash
cd /home/ztl/FieldInspectionAssistant
sudo systemctl start board-ai.service
systemctl --user start miiep-inference.service
python3 deploy/open_field_inspection.py
```

需要在终端查看桌面程序的即时输出时，关闭当前窗口后运行 `./deploy/run_field_inspection.sh`。

## 文件存放位置

### 程序、推理和系统配置

| 板端路径 | 存放内容 |
| --- | --- |
| `/home/ztl/FieldInspectionAssistant/` | 应用源码、部署脚本和测试入口 |
| `/home/ztl/FieldInspectionAssistant/build/field_inspection_assistant` | Qt 主程序可执行文件 |
| `/userdata/ai-workbench/server.py`、`/userdata/ai-workbench/build/native_runner`、`/userdata/ai-workbench/video_worker/build/video-worker` | 已安装的推理 API、原生推理程序与视频理解进程 |
| `/userdata/ai-workbench/vendor/` | RKNN-LLM 和 RKNN3 Model Zoo SDK，以及 SDK 构建产物 |
| `/userdata/ai-workbench/analytics-venv/`、`/userdata/ai-workbench/tts-venv/` | 声学分析与朗读各自的 Python 环境 |
| `/userdata/ai-workbench/data/`、`/userdata/ai-workbench/native.log` | 单次推理结果和任务日志、原生进程日志 |
| `/etc/systemd/system/board-ai.service` | 系统级推理服务，API 监听 `127.0.0.1:1828` |
| `/home/ztl/.config/systemd/user/miiep-inference.service`、`$XDG_RUNTIME_DIR/miiep/inference.sock` | 工程推理用户服务及其私有套接字 |
| `/etc/udev/rules.d/xf-speech.rules`、`/etc/systemd/system/yahboom-ch340@.service` | 六麦串口别名规则与 CH340 初始化服务 |
| `/usr/local/libexec/yahboom-audio/`、`/home/ztl/.config/systemd/user/yahboom-audio-defaults.service` | 串口与默认声卡配置脚本及桌面音频服务 |

### 模型权重

| 板端路径 | 放入的文件 |
| --- | --- |
| `/userdata/models/` | RK3588 文本权重 `Qwen3-4B-rk3588-w8a8.rkllm`、图像编码器 `qwen3-vl-4b_vision_rk3588.rknn`、视觉语言权重 `qwen3-vl-4b-instruct_w8a8_rk3588.rkllm`，以及可选的 `models-manifest.json` |
| `/userdata/models/Qwen3-ASR-0.6B/` | 语音识别的编码器、语言模型、分词器及配套权重 |
| `/userdata/models/Qwen3-VL-*-Instruct-384x384-*` | 视频理解模型。每个版本使用独立目录，目录名和完整文件列表见下方模型注册表 |
| `/opt/rk1828-models/Qwen3.5-4B/`、`/opt/rk1828-models/Qwen2.5-VL-7B_prune/` | RK1828 文本模型与图文模型的完整模型包 |
| `/home/ztl/FieldInspectionAssistant/vision-models/<模型标识>/` | 实时检测、分割、姿态和面部分析模型包。可用 `FIELDOPS_VISION_MODELS` 指定其他父目录 |
| `/userdata/ai-workbench/tts/vits-icefall-zh-aishell3/` | 本地中文朗读模型，由语音安装脚本下载 |

从授权来源准备推理权重，按模型清单核对文件名、大小和 SHA-256，再复制到对应目录。RK1828 的两个模型目录须能通过 `/userdata/models/` 访问，现有部署使用同名符号链接指向 `/opt/rk1828-models/`。具体文件名和大小见 [模型注册表](services/inference/server.py)。

实时视觉功能按模型标识读取模型包。每包包括 `stream-runner`、`model/` 下的 RKNN 模型与标签，以及 `lib/librknn3_api.so`。人体姿态、实例分割和面部分析所需的文件由 [视觉模型注册表](src/services/VisionLiveService.cpp) 校验。替换模型时，核对 RK1828 运行库版本、标签顺序和模型输出格式。

实例分割模型包 `yolo26_segment` 的目录如下。其他模型的标签及配套权重按各自模型标识配置。

```text
/home/ztl/FieldInspectionAssistant/vision-models/yolo26_segment/
├── stream-runner
├── model/
│   ├── model.rknn
│   ├── model.weight
│   └── coco_80_labels_list.txt
└── lib/
    └── librknn3_api.so
```

板端安装脚本使用以下 SDK 版本。已部署的 SDK 版本不符时，先核对板卡固件和依赖，再更换 SDK。

- [RKNN-LLM](https://github.com/airockchip/rknn-llm) `f7390530443bf84f0394255a449d7cbe81e69d1c`
- [RKNN3 Model Zoo](https://github.com/airockchip/rknn3-model-zoo) `ca92e7fe1a418b220985455446a986b80d6a899f`

### 业务数据和工程文件

`APP_DATA` 是当前桌面账户的应用数据目录。默认路径为 `/home/ztl/.local/share/FieldOps/FieldInspectionAssistant`。设置 `XDG_DATA_HOME` 时，使用其下的 `FieldOps/FieldInspectionAssistant`。

```bash
APP_DATA="${XDG_DATA_HOME:-$HOME/.local/share}/FieldOps/FieldInspectionAssistant"
printf '%s\n' "$APP_DATA"
ls -ld "$APP_DATA"
```

| 路径 | 存放内容 |
| --- | --- |
| `$APP_DATA/field_inspection.sqlite` | 业务主库，保存账户、设备台账、巡检任务、缺陷及业务记录 |
| `$APP_DATA/projects/<工程名>.miproj/` | 默认工程和导入的工程副本。用户选择其他路径时，以实际工程目录为准 |
| `<工程名>.miproj/project.json`、`<工程名>.miproj/database/project.db` | 工程清单和工程库 |
| `<工程名>.miproj/assets/` | `images/`、`audio/`、`thermal/`、`video/`、`text/` 分别存放工程图片、录音、热像、视频和文本 |
| `<工程名>.miproj/signals/raw/`、`<工程名>.miproj/reports/` | 原始信号和生成的工程报告 |
| `$APP_DATA/evidence/` | 巡检业务附件及托管证据 |
| `$APP_DATA/measurements/`、`$APP_DATA/measurements/measurements.sqlite` | 声音与信号采集的 WAV，以及录音索引和测量记录 |
| `$APP_DATA/audio/`、`$APP_DATA/assistant/` | 语音功能的工作文件和智能助手托管的附件 |
| `$APP_DATA/logs/desktop-launch.log`、`$APP_DATA/backups/` | 桌面启动日志和本地自动备份 |
| `$HOME/Pictures/现场巡检/` | 独立拍照和录像的默认目录，可在采集设置中修改 |

迁移工程时使用应用的工程归档功能，并保留完整的 `.miproj` 目录。备份业务主库和证据时使用应用的备份功能，生成一致的数据库快照。PDF、CSV 等手动导出的文件保存到导出时选择的位置。

只导入工程包时，软件会提示目标业务库中缺少的设备、巡检记录或缺陷。完整板端移交使用原板的业务备份 ZIP，外置工程目录另行归档。恢复后核对工程总览中的巡检和缺陷关联。当前支持离线交接，在线同步需部署对应服务。

## 六麦与串口

六麦模块的 UAC 与 UART 两根 USB 线均接到主板。规则将 `2208:0001` 绑定为 `/dev/lg_speech_uac`，将接在指定 USB 位置的 CH340（`1a86:7523`）绑定为 `/dev/lg_speech_serial`。当前规则使用 USB `devpath=1.1`。换 USB 位置后，用 `udevadm info --attribute-walk` 核对拓扑并更新 [规则](deploy/udev/xf-speech.rules)。

原始信号分析经串口数据通道采集 16 kHz 六路 PCM。ALSA 的 `L6Microphone` 是处理后的单通道输入，可用于语音识别。串口优先使用系统 CH341 驱动，板卡镜像缺少对应模块时，可通过配套服务初始化为 115200、8N1。

```bash
bash deploy/install_audio.sh
# 重新插入 UAC 和 UART 两根 USB 线后检查别名，再初始化桌面音频。
ls -l /dev/lg_speech_uac /dev/lg_speech_serial
udevadm info --query=property --name=/dev/lg_speech_uac
arecord -l
systemctl --user start yahboom-audio-defaults.service
```

## 验证与运维

### 构建与回归测试

单元测试和 C++ 回归测试可在板端无外设运行。独立构建目录 `build-test/` 用来存放测试产物。

```bash
cd /home/ztl/FieldInspectionAssistant
cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release -DFIELDOPS_VOICE_TESTS=ON
cmake --build build-test --target voice_input_test --parallel 4
ctest --test-dir build-test --output-on-failure
PYTHONPATH="$PWD/services/inference:$PWD/services/audio" \
  python3 -m unittest discover -s tests/inference -v
python3 tests/site_acceptance.py build/field_inspection_assistant
```

### 服务与模型验证

```bash
cd /home/ztl/FieldInspectionAssistant
systemctl status board-ai.service --no-pager
systemctl --user status miiep-inference.service --no-pager
curl -fsS http://127.0.0.1:1828/api/status | python3 -m json.tool
journalctl -u board-ai.service -n 60 --no-pager
journalctl --user -u miiep-inference.service -n 60 --no-pager
python3 tests/miiep_socket_smoke.py "$XDG_RUNTIME_DIR/miiep/inference.sock"
```

确认所测模型的 `ready` 为 `true`，并核对状态返回中的 `sample_image` 和 `sample_audio` 文件是否存在。下面的命令会提交推理任务，适合在空闲时运行。`--mode all` 的验收结果保存在 `/userdata/ai-workbench/smoke-all.json`。

```bash
python3 tests/inference/smoke_test.py --mode all
python3 tests/miiep_socket_smoke.py "$XDG_RUNTIME_DIR/miiep/inference.sock" --infer
```

### 音频与业务数据验证

在六麦设备已连通并选为默认输入后，用以下命令检查处理后的单通道语音。录制期间对麦克风说话，检查输出的 `peak` 和 `rms` 是否大于零。测试 WAV 保存在终端打印的临时路径。

```bash
cd /home/ztl/FieldInspectionAssistant
pactl get-default-source
sample_dir=$(mktemp -d)
python3 tests/audio/audio_check.py record "$sample_dir/voice.wav" --seconds 5
```

验证原始六通道时，在软件的声音与信号页面选择六麦原始输入，录制并停止后，用录音管理中对应 WAV 的路径检查采样率和通道数。应看到 `sample_rate=16000` 和 `channels=6`。

```bash
read -r -p '输入原始录音 WAV 的完整路径: ' recording
ffprobe -v error -select_streams a:0 -show_entries stream=sample_rate,channels \
  -of default=noprint_wrappers=1 "$recording"
```

业务主库可执行只读完整性检查。正常结果为 `ok`。

```bash
APP_DATA="${XDG_DATA_HOME:-$HOME/.local/share}/FieldOps/FieldInspectionAssistant"
python3 - "$APP_DATA/field_inspection.sqlite" <<'PY'
from pathlib import Path
import sqlite3
import sys

uri = Path(sys.argv[1]).resolve().as_uri()
with sqlite3.connect(f"{uri}?mode=ro", uri=True) as db:
    for (result,) in db.execute("PRAGMA quick_check"):
        print(result)
PY
```

在应用内选择「文件 → 打开演示工程」，按 [泵组复检案例](docs/miiep-project.md) 检查工程导入、证据关联、模型分析与报告导出。设备与诊断、事件、缺陷的关系见 [巡检关联规则](docs/inspection-relationships.md)。维护、备份和报告操作见 [现场业务与运维说明](PRODUCT_OPERATIONS.md)。
