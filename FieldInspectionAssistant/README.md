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
| `docs/`、`third_party/` | 操作与设计文档、第三方依赖及其许可证 |

## 系统与开发环境

| 项目 | 部署基线 |
| --- | --- |
| 主板与系统 | RK3588，ARM64，Ubuntu 24.04 LTS，板端已核对 Ubuntu 24.04.4 和厂商内核 6.1.141 |
| 桌面 | Xfce/X11，应用部署账户 `ztl`，该账户需要 `audio`、`dialout`、`video` 组权限 |
| 编译工具 | GCC/G++ 支持 C++17，CMake 3.16 起，板端已核对 CMake 3.28.3 |
| 界面与本地库 | Qt 6.4.2、Qt Quick/QML、Qt Virtual Keyboard、SQLite 插件、OpenCV 4.6、ALSA、FFTW3、XCB |
| Python | 3.12，系统环境安装 Flask、NumPy、Pillow 和 pyserial，声学分析和本地朗读各用独立虚拟环境 |
| 硬件与媒体 | RK1828 使用板卡固件配套的 RKNN3 驱动与运行库，摄像头通过 V4L2，音频通过 ALSA/PulseAudio，编解码使用 FFmpeg |

板端须先装好与 RK1828 配套的系统镜像和运行库。使用 `systemctl status rknn3` 和 `rknn-smi info` 检查设备状态。中文输入法资源来自与系统 Qt 匹配的 [Qt 拼音引擎](third_party/pinyin/README.md)。

### 构建桌面程序

在 ARM64 Ubuntu 上安装开发依赖，然后在应用目录执行 CMake。完整依赖清单也由 [板端安装脚本](deploy/install_board.sh) 维护。

```bash
sudo apt-get install build-essential cmake pkg-config \
  qt6-base-dev qt6-declarative-dev qt6-virtualkeyboard-dev libqt6sql6-sqlite \
  libopencv-dev libasound2-dev libfftw3-dev libxcb1-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

## 板端安装

通过获授权的 GitHub 账户获取源码。部署目录使用 `/home/ztl/FieldInspectionAssistant`。在目标板以 `ztl` 登录 Xfce 后打开终端执行以下命令。安装需要 sudo 权限、可访问的 APT 镜像及 SDK 和 Python 依赖下载源。

```bash
git clone git@github.com:wangji9/FieldInspectionAssistant.git "$HOME/field-inspection-source"
mkdir -p "$HOME/FieldInspectionAssistant"
cp -a "$HOME/field-inspection-source/FieldInspectionAssistant/." "$HOME/FieldInspectionAssistant/"
cd /home/ztl/FieldInspectionAssistant
bash deploy/install_board.sh
python3 deploy/install_desktop_launcher.py
./deploy/run_field_inspection.sh
```

更新时在已有的 `field-inspection-source` 执行 `git pull --ff-only`，再复制应用目录并运行安装脚本。安装会编译 Qt 应用和原生推理程序，安装声学与朗读依赖，部署并重启推理服务。请在维护时段更新已运行的设备。首次启动空数据库时，在登录页创建管理员。

| 运行位置 | 用途 |
| --- | --- |
| `/home/ztl/FieldInspectionAssistant/build/` | Qt 应用构建产物 |
| `/userdata/ai-workbench/` | 已安装推理程序、SDK、Python 虚拟环境、推理结果与日志 |
| `/userdata/models/` | 基础模型、视频模型及可选的模型完整性清单 |
| `/opt/rk1828-models/` | RK1828 文本与视觉语言模型的部署位置 |
| `/home/ztl/FieldInspectionAssistant/vision-models/` | 实时视觉模型包。可用 `FIELDOPS_VISION_MODELS` 指向已有目录 |
| `$XDG_RUNTIME_DIR/miiep/inference.sock` | 工程推理私有套接字 |
| `QStandardPaths::AppDataLocation` | 应用账户的数据库、工程与现场证据 |

模型权重、现场数据和编译产物分别存放在板端。安装时从授权来源准备权重，按模型清单核对文件名、大小和 SHA-256，再复制到运行目录。RK1828 的 Qwen3.5-4B 和 Qwen2.5-VL-7B prune 目录须能通过 `/userdata/models/` 访问，现有部署通过链接指向 `/opt/rk1828-models/`。Qwen3-ASR-0.6B 和 Qwen3-VL 视频模型使用各自完整目录。具体文件和大小以 [模型注册表](services/inference/server.py) 为准。

实时视觉功能按模型标识读取模型包。每包包括 `stream-runner`、`model/` 下的 RKNN 模型与标签，以及 `lib/librknn3_api.so`。人体姿态、实例分割和面部分析所需的文件由 [视觉模型注册表](src/services/VisionLiveService.cpp) 校验。替换模型时，核对 RK1828 运行库版本、标签顺序和模型输出格式。

板端安装脚本使用以下 SDK 版本。已部署的 SDK 版本不符时，先核对板卡固件和依赖，再更换 SDK。

- [RKNN-LLM](https://github.com/airockchip/rknn-llm) `f7390530443bf84f0394255a449d7cbe81e69d1c`
- [RKNN3 Model Zoo](https://github.com/airockchip/rknn3-model-zoo) `ca92e7fe1a418b220985455446a986b80d6a899f`

### 六麦与串口

六麦模块的 UAC 与 UART 两根 USB 线均接到主板。规则将 `2208:0001` 绑定为 `/dev/lg_speech_uac`，将接在指定 USB 位置的 CH340（`1a86:7523`）绑定为 `/dev/lg_speech_serial`。当前规则使用 USB `devpath=1.1`。换 USB 位置后，用 `udevadm info --attribute-walk` 核对拓扑并更新 [规则](deploy/udev/xf-speech.rules)。

原始信号分析经串口数据通道采集 16 kHz 六路 PCM。ALSA 的 `L6Microphone` 是处理后的单通道输入，可用于语音识别。串口优先使用系统 CH341 驱动，板卡镜像缺少对应模块时，可通过配套服务初始化为 115200、8N1。

```bash
bash deploy/install_audio.sh
# 重新插入 UAC 和 UART 两根 USB 线后检查别名，再初始化桌面音频。
ls -l /dev/lg_speech_uac /dev/lg_speech_serial
arecord -l
systemctl --user start yahboom-audio-defaults.service
```

## 验证与运维

以下命令从应用目录运行。单元测试和 C++ 回归测试可在板端无外设运行。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFIELDOPS_VOICE_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
PYTHONPATH="$PWD/services/inference:$PWD/services/audio" \
  python3 -m unittest discover -s tests/inference -v
```

硬件验收前检查推理服务和模型状态。推理冒烟测试使用服务报告的板端样本，样本和权重均须就绪。

```bash
systemctl status board-ai.service --no-pager
systemctl --user status miiep-inference.service --no-pager
curl -fsS http://127.0.0.1:1828/api/status
journalctl -u board-ai.service -n 60 --no-pager
python3 tests/miiep_socket_smoke.py "$XDG_RUNTIME_DIR/miiep/inference.sock"
python3 tests/inference/smoke_test.py --mode all
```

在应用内选择「文件 → 打开演示工程」，按 [泵组复检案例](docs/miiep-project.md) 检查工程导入、证据关联、模型分析与报告导出。设备与诊断、事件、缺陷的关系见 [巡检关联规则](docs/inspection-relationships.md)。维护、备份和报告操作见 [现场业务与运维说明](PRODUCT_OPERATIONS.md)。
