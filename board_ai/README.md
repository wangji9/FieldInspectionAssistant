# 板端 AI 模型中心

部署目标：`ztl@10.206.38.255`，Ubuntu 24.04 / RK3588 + RK1828。前端为已有 Qt「现场巡检助手」，通过板端回环地址访问推理服务。

界面使用系统标准可缩放窗口，显示最小化、最大化和关闭按钮，不更改桌面分辨率。窗口根据屏幕工作区调整初始大小并保存位置；窄窗口自动收起导航文字、调整分栏和滚动。正文/按钮 16 px、表格 14 px、设备信息 16–18 px、数值 28–32 px，代码使用 DejaVu Sans Mono 13 px；中文使用 Noto Sans CJK SC，拉丁文字使用 Inter/Noto Sans。

## 板端路径

| 内容 | 路径 |
| --- | --- |
| 模型及完整性清单 | `/userdata/models` |
| 文字模型 | `/userdata/models/Qwen3-4B-rk3588-w8a8.rkllm` |
| 视觉编码器 | `/userdata/models/qwen3-vl-4b_vision_rk3588.rknn` |
| 视觉语言模型 | `/userdata/models/qwen3-vl-4b-instruct_w8a8_rk3588.rkllm` |
| ASR 全部 8 个文件 | `/userdata/models/Qwen3-ASR-0.6B` |
| Qwen3.5-4B（RK1828） | `/opt/rk1828-models/Qwen3.5-4B`，链接到 `/userdata/models/Qwen3.5-4B` |
| Qwen2.5-VL-7B prune（RK1828） | `/opt/rk1828-models/Qwen2.5-VL-7B_prune`，链接到 `/userdata/models/Qwen2.5-VL-7B_prune` |
| 后端源码、构建产物、官方运行库 | `/userdata/ai-workbench` |
| Qt 源码及可执行程序 | `/home/ztl/FieldInspectionAssistant` |
| 实测 JSON 及 ASR 原始日志 | `/userdata/ai-workbench/data` |
| 服务 | `board-ai.service`，监听 `127.0.0.1:1828` |

## 使用

板端桌面打开「现场巡检助手」，侧栏选择 **智能助手**。程序也会在桌面会话登录时自动启动。

- **文字问答**：默认 Qwen3.5-4B（RK1828），可切换原 Qwen3-4B（RK3588）。可设置生成 Token 上限，当前关闭思考。
- **图片处理**：单独测量视觉 RKNN 的处理用时、FPS 和图像 Token 数。
- **图片检查**：默认 Qwen2.5-VL-7B prune（视觉和语言均在 RK1828），可切换原 Qwen3-VL-4B（RK3588）。左侧大图及检查内容，右侧检查结果。原有摄像头检查也接入同一服务。
- **语音转写**：选择板端音频文件，转换成 16 kHz 单声道后在 RK1828 上识别。整段转写最长 20 分钟，分段转写最长 60 秒。分段算法按 1 秒音频分块执行，包含回退修正，不是实时麦克风采集。
- **使用 / 性能 / 记录**：所有速度、首次响应和耗时指标均集中在性能页，使用页只显示输入和结果。点击记录可重新查看当时的输出及指标。

所有推理均发生在板端。RK3588 模型继续使用原生常驻运行器。RK1828 文字、图片及 ASR 现改为独立常驻进程，模型、分词器、嵌入映射和会话初始化后保留，每个请求清理 KV 并读取新输入；同模型后续请求直接推理。常规停止通过 SDK 停止会话，保留模型；异常或超时才重置进程。

服务启动时提前加载默认 Qwen3.5 和 ASR；Qt 选择模块或模型时触发后台预加载。任务可等待首次预加载完成，界面显示准备状态。RK1828 实测可分配模型内存约 5052 MiB：文字约 2889 MiB、ASR 约 2044 MiB，可以同时驻留；7B 图片模型需要更多内存，切换时按最近使用情况释放闲置进程，首次进入或返回已释放的模型仍需要加载一次。不会定时卸载闲置模型。

实测同样的短文本/图片问题与同一段 45 秒录音，连续请求完整服务端耗时：文字约 19.60 s → 0.44 s，图片约 33.76 s → 1.26 s，ASR 约 7.55 s → 0.90 s。首次模型准备仍单独计时，不能把这些连续调用结果解释为冷启动耗时。

## 指标口径

- `decode_tps`：SDK 实际生成 Token 数 / 解码阶段秒数，不用中文字数估算。
- `prefill_tps`：SDK 实际预填充 Token 数 / 预填充阶段秒数。
- `llm_ttft_ms`：从调用语言模型推理到首 Token 回调。
- `ttft_ms`：图像/语音任务含编码阶段，不含模型初始化；图像预处理和文件 I/O 包含在端到端耗时中。
- `model_load_ms`：首次实际模型初始化耗时；文字、图片与 ASR 驻留命中时为 0。
- `cache_hit` / `worker_pid`：本次是否复用已加载权重及实际工作进程；回答仍重新推理。
- `preload_wait_ms`：本次等待后台预加载或常驻执行锁的时间，计入完整请求耗时。
- `encoder_ms`：视觉编码或语音编码推理耗时。
- `inference_ms`：文字/VL 为语言模型推理；ASR 为编码和语言推理的合计。
- `request_ms`：服务端完整任务耗时，含检查、模型加载、输入处理及推理，不含 Qt 轮询延迟。
- `rtf`：ASR 推理秒数 / 音频秒数，越小越快。`realtime_factor = 1 / rtf`。
- `e2e_rtf`：ASR 子进程完整耗时 / 音频时长，包含模型加载；与 `rtf` 分开显示。
- 流式 ASR 的阶段耗时和 Token 数是全部轮次之和，包含回退重算；首 Token 取第一轮。输出 Token 数并非最终转录文本的唯一 Token 数。
- `memory_mb`：RKLLM 进程峰值常驻内存，不代表 RK1828 板载显存。
- 不适用或未测量的指标显示 `—`。

## 重新部署源码

在 Windows 的 `E:\RK3588` 执行：

```powershell
python sshrun.py putdir board_ai /userdata/ai-workbench
python sshrun.py putdir FieldInspectionAssistant /home/ztl/FieldInspectionAssistant
python sshrun.py "bash /userdata/ai-workbench/install.sh"
```

安装脚本不会删除模型、巡检数据库或历史结果。模型迁移使用 `deploy_models.py` 和临时受限接收器，支持断点续传，完整迁移结束生成 `models-manifest.json`，逐文件对比两端 SHA-256。

## 运维及验证

```bash
systemctl status board-ai --no-pager
journalctl -u board-ai -n 60 --no-pager
curl -s http://127.0.0.1:1828/api/status
curl -s http://127.0.0.1:1828/api/results
python3 /userdata/ai-workbench/smoke_test.py --mode all
python3 /userdata/ai-workbench/smoke_test.py --mode asr --online
/home/ztl/FieldInspectionAssistant/deploy/run_field_inspection.sh
```

原生运行库日志：`/userdata/ai-workbench/native.log`。Qt 日志：`/userdata/ai-workbench/qt.log`。每次任务保留独立的 JSON 记录，ASR 同时保留原始日志。

## 官方源码版本

- [airockchip/rknn-llm](https://github.com/airockchip/rknn-llm)：`f7390530443bf84f0394255a449d7cbe81e69d1c`
- [airockchip/rknn3-model-zoo](https://github.com/airockchip/rknn3-model-zoo)：`ca92e7fe1a418b220985455446a986b80d6a899f`

`patch_resident_demos.py` 在仪表化的官方源码基础上生成 `_resident` 目标，原 CLI 目标保留，便于核对冷启动基线。常驻初始化日志为 `/userdata/ai-workbench/*-resident.log`。

ASR 原始主程序保留为 `main.cc.fieldops.orig`、`online_main.cc.fieldops.orig`。`patch_asr_metrics.py` 仅增加初始化/总推理计时，不改变模型或解码算法。

RKLLM 使用 `base_domain_id=1`，视觉 RKNN 保留默认域 0，避免联合加载时耗尽同一 IOVA 地址域。这与官方多模态示例的 RK3588 配置一致。
