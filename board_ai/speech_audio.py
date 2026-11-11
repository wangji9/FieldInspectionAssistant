"""Non-destructive preparation of the ASR working copy (16 kHz PCM WAV)."""
import array
import wave


def normalize_asr(path):
    with wave.open(str(path), 'rb') as audio:
        params = audio.getparams()
        if params.nchannels != 1 or params.sampwidth != 2 or params.framerate != 16000:
            raise ValueError('识别音频必须为 16 kHz 单声道 16 位 PCM')
        samples = array.array('h', audio.readframes(audio.getnframes()))
    peak = max(map(abs, samples), default=0)
    if peak < 8:
        raise ValueError('录音中没有音频信号，请检查六麦输入或模块唤醒状态')
    # Amplify quiet speech for the recognizer only; never change replay volume.
    gain = min(6.0, 16384.0 / peak)
    if gain > 1.1:
        samples = array.array('h', (round(x*gain) for x in samples))
        with wave.open(str(path), 'wb') as audio:
            audio.setparams(params)
            audio.writeframes(samples.tobytes())
    else:
        gain = 1.0
    return {'input_peak': peak, 'asr_gain': round(gain, 3)}
