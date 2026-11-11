#!/usr/bin/env python3
"""Local microphone capture / offline Chinese TTS for the Qt desktop user.

Uses named PulseAudio USB input and an available desktop output. Each invocation
has one owner (the Qt process); text and process arguments are never shell code.
"""
import array
import json
import os
import re
from pathlib import Path
import signal
import subprocess
import sys
import time
import wave
import math
import struct
from speech_endpoint import SpeechEndpoint
from six_mic_control import wake_microphone

MODEL = Path('/userdata/ai-workbench/tts/vits-icefall-zh-aishell3')


def emit(**values):
    print(json.dumps(values, ensure_ascii=False), flush=True)


def device(kind, fragment):
    rows = json.loads(subprocess.check_output(
        ['pactl', '-f', 'json', 'list', kind], text=True, timeout=5))
    found = next((x['name'] for x in rows if fragment in x['name']
                  and not x['name'].endswith('.monitor')), None)
    if not found:
        raise RuntimeError('未找到六麦输入' if kind == 'sources' else '未找到 USB 声卡输出')
    return found


def output_sink():
    try:
        return device('sinks', 'usb-C-Media_Electronics_Inc._USB_Audio_Device-')
    except RuntimeError:
        sink = subprocess.check_output(['pactl', 'get-default-sink'], text=True, timeout=5).strip()
        if not sink:
            raise RuntimeError('未找到可用的音频输出')
        return sink


def record(path, auto_stop=False):
    source = device('sources', 'alsa_input.usb-EII_ListenGo_Circular_6-Microphone_')
    subprocess.run(['pactl', 'set-source-mute', source, '0'], check=True, timeout=5)
    subprocess.run(['pactl', 'set-source-volume', source, '100%'], check=True, timeout=5)
    control_status = wake_microphone()
    child = None
    stopped = False

    def stop(signum, frame):
        nonlocal stopped
        stopped = True
        if child and child.poll() is None:
            child.send_signal(signal.SIGINT)  # arecord closes the WAV header.

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    env = dict(os.environ, PULSE_SOURCE=source)
    with subprocess.Popen(['arecord', '-q', '-D', 'pulse', '-f', 'S16_LE',
                           '-r', '16000', '-c', '1', '--period-size=1024',
                           '--buffer-size=4096', '-t', 'wav', '-d', '30' if auto_stop else '60', path],
                          env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE) as proc:
        child = proc
        if stopped:
            proc.send_signal(signal.SIGINT)
        emit(event='recording', source=source, control=control_status)
        try:
            if auto_stop:
                endpoint = SpeechEndpoint()
                offset = None
                while proc.poll() is None and not stopped:
                    time.sleep(0.1)
                    try:
                        with open(path, 'rb') as recording:
                            if offset is None:
                                offset = wav_data_offset(recording)
                                if offset is None:
                                    continue
                            recording.seek(offset)
                            data = recording.read(16000)  # At most half a second.
                        data = data[:len(data)//2*2]
                        offset += len(data)
                        samples = array.array('h', data)
                        if endpoint.update(samples, offset/32000, len(samples)/16000):
                            stop(0, None)
                    except FileNotFoundError:
                        pass
            _, error = proc.communicate(timeout=70)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.communicate()
            raise RuntimeError('录音设备超时，请重新连接麦克风')
    # SIGINT makes arecord return nonzero even after a successful user stop.
    if proc.returncode and not stopped:
        raise RuntimeError(error.decode(errors='replace').strip() or '无法打开麦克风')
    with wave.open(path) as audio:
        samples = array.array('h', audio.readframes(audio.getnframes()))
        duration = len(samples) / audio.getframerate()
    if duration < 0.3:
        raise RuntimeError('录音太短，请至少说一句话后再停止')
    peak = max(map(abs, samples), default=0)
    rms = math.sqrt(sum(x*x for x in samples)/max(1, len(samples)))
    # Do not reject a quiet voice using the endpoint threshold: send it to ASR.
    if peak < 8:
        raise RuntimeError('六麦输入没有音频信号，请检查 USB 连接或模块唤醒状态（输入未静音）')
    emit(event='recorded', path=path, duration_s=duration, peak=peak, rms=rms)


def wav_data_offset(stream):
    """Locate data after arbitrary RIFF chunks, including ALSA's JUNK/LIST."""
    if stream.read(12)[:4] != b'RIFF':
        return None
    while True:
        header = stream.read(8)
        if len(header) < 8:
            return None
        tag, size = struct.unpack('<4sI', header)
        if tag == b'data':
            return stream.tell()
        stream.seek(size + (size & 1), 1)


def spoken_text(text):
    # This model's lexicon is Chinese. Unknown Latin words otherwise produce
    # near-digital silence while reporting success. Read their letter names.
    names = ['诶','比','西','迪','伊','艾弗','吉','艾尺','爱','杰','开','艾勒',
             '艾姆','恩','欧','披','丘','阿尔','艾丝','踢','优','维','达布溜',
             '艾克斯','歪','贼德']
    return re.sub(r'[A-Za-z]+', lambda m: '、'.join(names[ord(c.lower())-97] for c in m[0]), text)


def synthesize(text_path, output):
    import sherpa_onnx
    text = Path(text_path).read_text(encoding='utf-8').strip()
    if not text or len(text) > 3000:
        raise RuntimeError('朗读文本需为 1–3000 字')
    sink = output_sink()
    config = sherpa_onnx.OfflineTtsConfig(
        model=sherpa_onnx.OfflineTtsModelConfig(
            vits=sherpa_onnx.OfflineTtsVitsModelConfig(
                model=str(MODEL/'model.onnx'), lexicon=str(MODEL/'lexicon.txt'),
                tokens=str(MODEL/'tokens.txt')),
            num_threads=3, provider='cpu', debug=False),
        rule_fsts=','.join(str(MODEL/n) for n in ('phone.fst', 'date.fst', 'number.fst')),
        max_num_sentences=1)
    if not config.validate():
        raise RuntimeError('本地中文朗读模型配置无效')
    tts = sherpa_onnx.OfflineTts(config)
    chunks = []
    for sentence in re.split(r'(?<=[。！？!?；;\n])', text):
        sentence = sentence.strip()
        chunks.extend(sentence[i:i+80] for i in range(0, len(sentence), 80))
    total = 0
    with wave.open(output, 'wb') as merged:
        merged.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
        for index, sentence in enumerate(chunks):
            audio = tts.generate(spoken_text(sentence), sid=10, speed=1.0)
            if not len(audio.samples):
                raise RuntimeError('文字未能生成语音，请检查内容')
            import numpy as np
            samples = np.asarray(audio.samples, dtype=np.float32)
            peak = float(np.max(np.abs(samples)))
            rms = float(np.sqrt(np.mean(samples*samples)))
            if peak < 0.002 or rms < 0.0003:
                raise RuntimeError('朗读模型生成了静音，未播放；请检查文本语言或模型词典')
            # Keep sentence loudness consistent without changing the USB volume.
            # Limit both gain and peaks so short/quiet sentences cannot jump loud.
            gain = min(3.0, 0.075/rms, 0.55/peak)
            samples *= gain
            part = str(Path(output).with_suffix('')) + f'.part-{index:03d}.wav'
            # Native USB card format; keep the speaker's existing low volume.
            samples = np.interp(np.arange(round(len(samples)*48000/audio.sample_rate))
                                * audio.sample_rate/48000, np.arange(len(samples)), samples)
            pcm = (np.clip(samples, -0.95, 0.95)*32767).astype('<i2')
            with wave.open(part, 'wb') as fragment:
                fragment.setparams((2, 2, 48000, 0, 'NONE', 'not compressed'))
                fragment.writeframes(np.repeat(pcm, 2).tobytes())
            with wave.open(part) as fragment:
                merged.writeframes(fragment.readframes(fragment.getnframes()))
            duration = len(pcm)/48000
            total += duration
            emit(event='audio_chunk', path=part, sink=sink, duration_s=duration, total_s=total,
                 peak=peak*gain, rms=rms*gain, gain=gain)
    emit(event='synthesized', path=output, sink=sink, duration_s=total)


def inspect_audio(source, converted):
    source = str(Path(source).resolve(strict=True))
    sink = output_sink()
    try:
        with wave.open(source) as audio:
            duration = audio.getnframes()/audio.getframerate()
            if audio.getsampwidth() == 2:
                samples = array.array('h', audio.readframes(audio.getnframes()))
                if max(map(abs, samples), default=0) < 8:
                    raise RuntimeError('这条语音文件接近静音，请用文字重新朗读生成语音')
        playable = source
    except (wave.Error, EOFError):
        subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-y', '-i', source,
                        '-ac', '1', '-ar', '16000', '-c:a', 'pcm_s16le', converted],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=120)
        with wave.open(converted) as audio:
            duration = audio.getnframes()/audio.getframerate()
        playable = converted
    if duration <= 0:
        raise RuntimeError('音频文件没有可播放内容')
    emit(event='audio_ready', path=playable, source=source, sink=sink, duration_s=duration)


if __name__ == '__main__':
    try:
        if len(sys.argv) in (3,4) and sys.argv[1] == 'record':
            record(sys.argv[2], len(sys.argv)==4 and sys.argv[3]=='auto')
        elif len(sys.argv) == 4 and sys.argv[1] == 'synthesize':
            synthesize(sys.argv[2], sys.argv[3])
        elif len(sys.argv) == 4 and sys.argv[1] == 'inspect':
            inspect_audio(sys.argv[2], sys.argv[3])
        else:
            raise RuntimeError('无效的语音操作')
    except Exception as exc:
        emit(error=str(exc))
        sys.exit(1)
