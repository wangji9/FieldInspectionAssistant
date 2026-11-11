# 六麦与 USB 扬声器准备记录

检测时间：2026-10-26，Asia/Shanghai。开发板：`ztl@10.206.38.255`，Ubuntu 24.04，RK3588，内核 6.1.141。

## 结果

**麦克风可用；扬声器/USB 声卡的实际出声仍未通过验证。**

- ListenGo 六麦：USB `2208:0001`，ALSA `CARD=L6Microphone`，当前 card 5/device 0。UAC 支持 16 kHz、16 位、单声道。
- 实际录音 45 秒，峰值 19245/32768，RMS 357.31（-39.25 dBFS）。板端 Qwen3-ASR 成功识别：`小飞，小飞。你好，小样！你好，小飞。` 这证明麦克风已录到有效声音，不代表识别文本逐字准确。
- C-Media USB 声卡：USB `0d8c:0012`，ALSA `CARD=Device`，当前 card 0/device 0。原生支持 44.1/48 kHz、16 位、双声道。已选择模拟立体声输出，解除静音，最终音量 80%。
- 桌面右上角音量图标斜线：音量插件曾与 PulseAudio 失去连接，重启 Xfce 面板后重新连接；用户确认斜线消失。
- 多次播放已知语音、蜂鸣和实际录音，包括绕过 PulseAudio 的原生 48 kHz 双声道播放，命令均成功返回。用户确认扬声器和替换耳机均无声。
- 已单独重置 C-Media USB 设备并恢复模拟输出，用户确认仍无声。

尚不能断定声卡损坏，也不能把音量面板电平跳动当作实际出声。下一步需要将这张声卡和耳机放到另一台电脑测试，或在本板换一张已知正常的声卡。用户当前暂时无法做这项交叉测试。

## 已部署配置

| 内容 | 板端位置 |
| --- | --- |
| 教程设备别名规则 | `/etc/udev/rules.d/xf-speech.rules` |
| UAC 控制口别名 | `/dev/lg_speech_uac` → `/dev/ttyACM0` |
| CH340 串口别名 | `/dev/lg_speech_serial` → `/dev/ttyUSB0` |
| 串口兼容初始化 | `/etc/systemd/system/yahboom-ch340@.service` |
| 默认输入、输出初始化 | `/home/ztl/.config/systemd/user/yahboom-audio-defaults.service` |
| root 所有的辅助脚本 | `/usr/local/libexec/yahboom-audio/` |
| 录音和测试语音 | `/userdata/ai-workbench/audio/` |

`ztl` 原本已属于 `audio` 和 `dialout` 组。规则使用 0660/dialout。已安装系统包 `python3-serial` 3.5。

默认输入为 ListenGo UAC；默认输出为 C-Media USB 模拟立体声。用户服务已启用；重启 PulseAudio 后再次执行初始化成功。**没有重启整块开发板，因此完整冷启动尚未实测。**

系统配置 `CONFIG_USB_SERIAL_CH341=m`，却没有安装 `ch341.ko`。当前采用内核自带 generic USB 串口传输，加 CH341 控制请求初始化，固定为 115200、8N1，已实测收到串口数据。这是固定波特率兼容措施，不能代替完整 CH341 驱动；后续若需要其他波特率，应安装与此内核匹配的厂商驱动。脚本发现原生 ch341 驱动可用时会优先使用它。

CH340 热插拔初始化及别名约束在目前连接的 USB 位置 `8-1.3`。换 USB 位置时需要更新对应 udev 规则。没有修改模块固件或唤醒词。

## 在开发板上使用

重新选择输入输出：

```bash
systemctl --user restart yahboom-audio-defaults.service
```

录制 10 秒，自动保存标准 WAV：

```bash
python3 /usr/local/libexec/yahboom-audio/audio_check.py record /userdata/ai-workbench/audio/test.wav --seconds 10
```

明确通过 USB 声卡回放：

```bash
python3 /usr/local/libexec/yahboom-audio/audio_check.py play /userdata/ai-workbench/audio/test.wav
```

按教程直接访问 ALSA；使用声卡名称避免编号变化：

```bash
pasuspender -- arecord -D hw:CARD=L6Microphone,DEV=0 -r 16000 -f S16_LE -c 1 -t wav -d 10 /userdata/ai-workbench/audio/test.wav
pasuspender -- aplay -D plughw:CARD=Device,DEV=0 /userdata/ai-workbench/audio/test.wav
```

已准备好的短语音：

```bash
python3 /usr/local/libexec/yahboom-audio/audio_check.py play /userdata/ai-workbench/audio/usb-voice-test.wav
```

恢复为系统自行选择设备时，禁用用户默认设备服务即可：

```bash
systemctl --user disable yahboom-audio-defaults.service
```

`mic-asr-result.json` 为识别原始结果，`user-voice-test.wav` 为实录备份，`verification.txt` 为配置验证记录。语音服务仍使用原有板端本地模型；没有创建讯飞账号或启用云端 API。

## 教程依据

- [使用前准备：硬件接线](https://www.yahboom.com/build?id=16445&cid=743)
- [使用前准备：设备规则绑定](https://www.yahboom.com/build?id=16451&cid=743)
- [音频采集与录制：UAC 与指定声卡回放](https://www.yahboom.com/build?id=16446&cid=743)
- [CH341 控制请求的 Linux 源码](https://github.com/torvalds/linux/blob/v6.1/drivers/usb/serial/ch341.c)
