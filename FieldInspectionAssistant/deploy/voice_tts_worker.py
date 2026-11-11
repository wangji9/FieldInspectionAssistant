#!/usr/bin/env python3
"""Persistent local speech synthesis. JSON lines; no microphone or playback access."""
import json,sys,time,wave,math,re,os
from pathlib import Path
import numpy as np
import sherpa_onnx
sys.path.insert(0,'/userdata/ai-workbench')
from speech_session import spoken_text,device

def emit(**v):print(json.dumps(v,ensure_ascii=False),flush=True)

def main():
 root=Path(sys.argv[1]).resolve();root.mkdir(parents=True,exist_ok=True);os.chmod(root,0o700)
 model=Path('/userdata/ai-workbench/tts/vits-icefall-zh-aishell3');start=time.monotonic()
 config=sherpa_onnx.OfflineTtsConfig(model=sherpa_onnx.OfflineTtsModelConfig(vits=sherpa_onnx.OfflineTtsVitsModelConfig(model=str(model/'model.onnx'),lexicon=str(model/'lexicon.txt'),tokens=str(model/'tokens.txt')),num_threads=2,provider='cpu',debug=False),rule_fsts=','.join(str(model/n) for n in ('phone.fst','date.fst','number.fst')),max_num_sentences=1)
 if not config.validate():raise ValueError('中文语音模型配置不可用')
 tts=sherpa_onnx.OfflineTts(config);emit(event='ready',load_ms=(time.monotonic()-start)*1000)
 merged=None;merge_path=None
 try:
  for line in sys.stdin:
   rid=''
   try:
    request=json.loads(line);rid=str(request.get('id',''));text=str(request.get('text','')).strip()
    if request.get('op')=='finish':
     if merged:merged.close();merged=None;merge_path=None
     emit(event='finished',id=rid);continue
    name=str(request.get('file',''));merge=str(request.get('merge',''))
    if not re.fullmatch(r'[a-zA-Z0-9_-]{1,100}\.wav',name) or not re.fullmatch(r'[a-zA-Z0-9_-]{1,100}\.wav',merge):raise ValueError('语音路径无效')
    if not text or len(text)>300:raise ValueError('每段语音必须为 1–300 字')
    sink=device('sinks','usb-C-Media_Electronics_Inc._USB_Audio_Device-')
    start=time.monotonic();audio=tts.generate(spoken_text(text),sid=10,speed=max(.85,min(1.2,float(request.get('speed',1.05)))))
    x=np.asarray(audio.samples,dtype=np.float32)
    if not len(x):raise ValueError('未生成有效语音')
    rms=float(np.sqrt(np.mean(x*x)));peak=float(np.max(np.abs(x)))
    if rms<.0003 or peak<.002:raise ValueError('语音结果接近静音')
    x*=min(3.,.07/rms,.5/peak)
    x=np.interp(np.arange(round(len(x)*48000/audio.sample_rate))*audio.sample_rate/48000,np.arange(len(x)),x)
    pcm=np.repeat((np.clip(x,-.9,.9)*32767).astype('<i2'),2).tobytes();path=root/name
    with wave.open(str(path),'wb') as w:w.setparams((2,2,48000,0,'NONE','not compressed'));w.writeframes(pcm)
    os.chmod(path,0o600)
    if merge_path!=str(root/merge):
     if merged:merged.close()
     merge_path=str(root/merge);merged=wave.open(merge_path,'wb');merged.setparams((2,2,48000,0,'NONE','not compressed'));os.chmod(merge_path,0o600)
    merged.writeframes(pcm)
    emit(event='audio',id=rid,path=str(path),merge=merge_path,sink=sink,duration_s=len(x)/48000,synthesis_ms=(time.monotonic()-start)*1000)
   except Exception as e:emit(event='error',id=rid,error=str(e)[:400])
 finally:
  if merged:merged.close()
if __name__=='__main__':
 try:main()
 except Exception as e:emit(event='error',error=str(e)[:400]);sys.exit(1)
