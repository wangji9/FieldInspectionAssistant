"""Read-only discovery. Never open capture ports or change USB drivers."""
import json
from pathlib import Path
import re
import subprocess


def usb_ports(sys_tty=Path('/sys/class/tty'), dev=Path('/dev')):
    ports = []
    for tty in sorted(sys_tty.glob('tty*')):
        if not tty.name.startswith(('ttyACM', 'ttyUSB')):
            continue
        for parent in (tty.resolve(), *tty.resolve().parents):
            try:
                vendor = (parent / 'idVendor').read_text().strip().lower()
                product = (parent / 'idProduct').read_text().strip().lower()
            except OSError:
                continue
            path = dev / tty.name
            if path.exists():
                ports.append((str(path), vendor, product))
            break
    return ports


def discover(dev=Path('/dev'), sys_tty=Path('/sys/class/tty'), run=subprocess.check_output,
             sys_usb=Path('/sys/bus/usb/devices')):
    devices, notes = [], []
    try:
        sources = json.loads(run(['pactl', '-f', 'json', 'list', 'sources'], text=True, timeout=5))
        for source in sources:
            name = source.get('name', '')
            if not name.startswith('alsa_input.') or name.endswith('.monitor'):
                continue
            spec = str(source.get('sample_specification', ''))
            rate, channels = re.search(r'(\d+)Hz', spec), re.search(r'(\d+)ch', spec)
            if rate and channels:
                devices.append(dict(id=name, name=source.get('description', name),
                                    rate=int(rate[1]), channels=int(channels[1])))
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        notes.append('系统音频设备查询失败，请重试刷新：' + str(error)[:160])
    ports = usb_ports(sys_tty, dev)
    audio = [path for path, vendor, product in ports if (vendor, product) == ('2208', '0001')]
    control = [path for path, vendor, product in ports if (vendor, product) == ('1a86', '7523')]
    def choose(alias, candidates):
        preferred = dev / alias
        if preferred.exists() and str(preferred.resolve()) in candidates:
            return str(preferred)
        return candidates[0] if len(candidates) == 1 else ''
    audio_path, control_path = choose('lg_speech_uac', audio), choose('lg_speech_serial', control)
    if audio_path and control_path:
        devices.insert(0, dict(id='ListenGo_raw_six', name='六麦原始采集 · 6 通道', rate=16000,
                              channels=6, transport='six_serial', control=control_path, audio=audio_path))
    elif audio:
        adapters = []
        for entry in sys_usb.iterdir() if sys_usb.is_dir() else ():
            try:
                if ((entry/'idVendor').read_text().strip(), (entry/'idProduct').read_text().strip()) == ('1a86', '7523'):
                    adapters.append(entry.name)
            except OSError:
                continue
        if not control and adapters:
            notes.append('已识别 CH340（' + '、'.join(sorted(adapters)) + '），控制串口尚未生成 ttyUSB。')
        elif not control:
            notes.append('六麦原始数据接口已连接，请检查 UART 控制线。')
        else:
            notes.append('检测到多个串口或六麦音频接口，请确认设备绑定。')
    return devices, '；'.join(notes)
