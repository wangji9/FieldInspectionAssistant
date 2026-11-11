from answer_policy import constrain, visible_length
"""Local-only inference API for the Qt inspection assistant."""
import base64
import json
import logging
import os
import queue
import signal
import re
import subprocess
import threading
import time
import tempfile
import uuid
import wave
from asr_stream import AsrTranscriptStream
from resident_models import ResidentPool
import codecs
import atexit
from contextlib import contextmanager
from pathlib import Path
from flask import Flask, jsonify, request, Response, stream_with_context

ROOT=Path('/userdata/ai-workbench')
MODELS=Path('/userdata/models')
ASR=ROOT/'vendor/rknn3-model-zoo/install/rk3588_linux_aarch64/rknn_Qwen3_ASR_demo'
VIDEO_RUNTIME=ROOT/'video_worker/build'
VIDEO_SUFFIXES=('vision.rknn','vision.weight','llm.rknn','llm.weight','llm.tokenizer.gguf','llm.embed.bin')
VIDEO_MODELS={
    'qwen3_video':('2B','66560', (5408088,240046080,65267648,1079662592,5934249,622329856)),
    'qwen3_video_4b':('4B','34816', (5411672,244912128,57719160,2432394240,5934249,777912320)),
}
VIDEO_IDLE_SECONDS=10
DATA=ROOT/'data'; DATA.mkdir(exist_ok=True)
app=Flask(__name__); app.config['MAX_CONTENT_LENGTH']=32*1024*1024
lock=threading.Lock(); jobs={}; worker=None; rk_worker=None; asr_worker=None; events=queue.Queue()
submissions=queue.Queue(maxsize=8)
submission_lock=threading.Lock()
QUEUE_TIMEOUT_SECONDS=120
vision_reservation={'token':'','until':0.0}
updates=threading.Condition()
profile_lock=threading.RLock()
LOG=logging.getLogger('inference')

def publish(job,**values):
    with updates:
        job.update(values)
        job['revision']=job.get('revision',0)+1
        updates.notify_all()

def snapshot(job):
    with updates: return json.loads(json.dumps(job,ensure_ascii=False))

def video_model_files(key):
    size, context, lengths=VIDEO_MODELS[key]
    name='Qwen3-VL-'+size
    directory=MODELS/(name+'-Instruct-384x384-w4a16-8-'+context+'-kv-int4')
    return context, [(directory/(name+'-'+suffix), length) for suffix,length in zip(VIDEO_SUFFIXES,lengths)]

def vision_reserved():
    return time.monotonic()<vision_reservation['until']
FILES=[('text','Qwen3-4B-rk3588-w8a8.rkllm',4838759058),
       ('encoder','qwen3-vl-4b_vision_rk3588.rknn',867273821),
       ('vision','qwen3-vl-4b-instruct_w8a8_rk3588.rkllm',4846784612)]
ASR_FILES={'encoder.rknn':3450168,'encoder.weight':375020032,'encoder_online.rknn':3125048,
    'encoder_online.weight':374837248,'llm.embed.bin':311164928,'llm.rknn':47478048,
    'llm.tokenizer.gguf':5933241,'llm.weight':387201536}
RK1828_FILES={
    'qwen35': ('Qwen3.5-4B', {'Qwen3.5-4B.rknn':35634424,'Qwen3.5-4B.weight':2647438336,
        'Qwen3.5-4B.tokenizer.gguf':10942308,'Qwen3.5-4B.embed.bin':1271398400}),
    'qwen25vl': ('Qwen2.5-VL-7B_prune', {'llm_Qwen2.5-VL-7B.rknn':22078344,
        'llm_Qwen2.5-VL-7B.weight':4296033280,'vision_Qwen2.5-VL-7B.rknn':8865584,
        'vision_Qwen2.5-VL-7B.weight':425498112,'Qwen2.5-VL-7B.tokenizer.gguf':5932574,
        'Qwen2.5-VL-7B.embed.bin':1089994752})}
DEFAULT_MODELS={'text':'qwen3','encoder':'qwen3_encoder','vision':'qwen3vl','asr':'qwen3_asr'}
MODEL_MODES={**{v:k for k,v in DEFAULT_MODELS.items()},'qwen35':'text','qwen25vl':'vision'}

def file_info(path,size):
    try: actual=path.stat().st_size if path.is_file() else 0
    except FileNotFoundError: actual=0
    ready=actual==size
    partial=Path(str(path)+'.partial')
    try: received=actual or partial.stat().st_size
    except FileNotFoundError: received=0
    if not ready:
        for p in path.parent.glob(path.name+'.partial.part-*'):
            try: received+=p.stat().st_size
            except FileNotFoundError: pass
    return ready,min(size,received)

def system_status():
    rows=[]
    for mode,name,size in FILES:
        ready,received=file_info(MODELS/name,size)
        rows.append(dict(mode=mode,model=DEFAULT_MODELS[mode],name=name,path=str(MODELS/name),ready=ready,bytes=size,received=received))
    checks=[file_info(MODELS/'Qwen3-ASR-0.6B'/name,size) for name,size in ASR_FILES.items()]
    rows.append(dict(mode='asr',model='qwen3_asr',name='Qwen3-ASR-0.6B',path=str(MODELS/'Qwen3-ASR-0.6B'),
        ready=all(x[0] for x in checks),bytes=sum(ASR_FILES.values()),received=sum(x[1] for x in checks)))
    for model,(folder,files) in RK1828_FILES.items():
        checks=[file_info(MODELS/folder/name,size) for name,size in files.items()]
        # During transfer the final symlink may not exist yet.
        if not (MODELS/folder).exists():
            checks=[file_info(Path('/opt/rk1828-models')/folder/name,size) for name,size in files.items()]
        rows.append(dict(mode=MODEL_MODES[model],model=model,name=folder,path=str(MODELS/folder),
            hardware='RK1828',ready=all(x[0] for x in checks),bytes=sum(files.values()),received=sum(x[1] for x in checks)))
    mem={}
    for line in Path('/proc/meminfo').read_text().splitlines():
        k,v=line.split(':',1); mem[k]=int(v.strip().split()[0])
    temps=[]
    for p in Path('/sys/class/thermal').glob('thermal_zone*/temp'):
        try: temps.append(int(p.read_text())/1000)
        except (OSError,ValueError): pass
    disk=os.statvfs(MODELS)
    npu=''
    try: npu=Path('/sys/kernel/debug/rknpu/load').read_text().strip()
    except OSError: pass
    return dict(**resident_pool.status(),models=rows,service='online',busy=lock.locked(),queued=submissions.qsize(),temperature_c=max(temps) if temps else None,
        memory_available_mb=round(mem['MemAvailable']/1024),memory_total_mb=round(mem['MemTotal']/1024),
        disk_free_gb=round(disk.f_bavail*disk.f_frsize/1024**3,2),npu_load=npu,hardware='RK3588 + RK1828',
        sample_image=str(ROOT/'vendor/rknn-llm/examples/multimodal_model_demo/data/demo.jpg'),
        sample_audio=str(ASR/'asr_en.wav'))


def resident_specs(key):
    base=ROOT/'vendor/rknn3-model-zoo/install/rk3588_linux_aarch64'
    if key in VIDEO_MODELS:
        context, files=video_model_files(key)
        return [str(VIDEO_RUNTIME/'video-worker'), *(str(path) for path,_ in files), context], VIDEO_RUNTIME
    if key.startswith('qwen3_asr'):
        online=key.endswith('_online');p=MODELS/'Qwen3-ASR-0.6B'
        encoder='encoder_online' if online else 'encoder'
        executable='rknn_qwen3_asr_demo'+('_online' if online else '')+'_resident'
        args=[str(ASR/executable),str(p/(encoder+'.rknn')),str(p/(encoder+'.weight')),
                str(p/'llm.rknn'),str(p/'llm.weight'),str(p/'llm.tokenizer.gguf'),
                str(p/'llm.embed.bin'),'0xff','0xff','__resident__']
        return args,ASR
    folder=MODELS/RK1828_FILES[key][0]
    if key=='qwen35':
        runtime=base/'rknn_Qwen3_5_demo'
        args=[str(runtime/'rknn_qwen3_5_demo_resident'),str(folder/'Qwen3.5-4B.rknn'),
              str(folder/'Qwen3.5-4B.weight'),str(folder/'Qwen3.5-4B.tokenizer.gguf'),
              str(folder/'Qwen3.5-4B.embed.bin'),'0xff','__resident__']
    else:
        runtime=base/'rknn_Qwen2_5_VL_demo'
        args=[str(runtime/'rknn_qwen2_5_vl_demo_resident'),str(folder/'vision_Qwen2.5-VL-7B.rknn'),
              str(folder/'vision_Qwen2.5-VL-7B.weight'),str(folder/'llm_Qwen2.5-VL-7B.rknn'),
              str(folder/'llm_Qwen2.5-VL-7B.weight'),str(folder/'Qwen2.5-VL-7B.tokenizer.gguf'),
              str(folder/'Qwen2.5-VL-7B.embed.bin'),'0xff','0xff','__resident__','__resident__',
              '392','392','0.0']
    return args,runtime

@contextmanager
def performance_profile():
    # Serialize model loading with inference and exclusive face access.
    with profile_lock:
        yield


resident_pool=ResidentPool(ROOT,resident_specs,profile=performance_profile)
atexit.register(resident_pool.close)


def rk1828(payload,job):
    global rk_worker
    decoder=codecs.getincrementaldecoder('utf-8')(errors='replace')
    pending='';had_error=False
    with (DATA/(job['id']+'.log')).open('w',encoding='utf-8') as log:
        def consume(chunk,final=False):
            nonlocal pending,had_error
            text=decoder.decode(chunk,final=final);log.write(text);log.flush();pending+=text
            while '\n' in pending or (final and pending):
                if '\n' in pending:line,pending=pending.split('\n',1)
                else:line,pending=pending,''
                if '@@JSON@@' in line:
                    event=json.loads(line.split('@@JSON@@',1)[1])
                    if event.get('event')=='delta' and event.get('text'):
                        metrics=dict(job['metrics'])
                        if not job['text']:metrics['first_output_ms']=(time.time()-job['created_at'])*1000
                        publish(job,text=job['text']+event['text'],metrics=metrics,message='正在 RK1828 上回答')
                elif '@@METRICS@@' in line:
                    metrics=dict(job['metrics']);metrics.update(json.loads(line.split('@@METRICS@@',1)[1]))
                    publish(job,metrics=metrics)
                elif 'Error occurred during inference' in line:had_error=True
        def process(value):
            global rk_worker
            rk_worker=value
        result=resident_pool.run(payload['model'],payload,consume,
            lambda message:publish(job,message=message),lambda:job.get('cancel_requested',False),process)
        # The resident framing consumes the newline preceding its completion
        # marker.  The final JSON delta can therefore remain in pending.
        consume(b'',final=True)
    result['preload_wait_ms']=result.get('preload_wait_ms',0)+job['metrics'].get('preload_wait_ms',0)
    values=dict(job['metrics']);values.update({k:v for k,v in result.items() if k!='cancelled'})
    publish(job,metrics=values,cancelled=result.get('cancelled',False))
    if result.get('cancelled'):return
    if had_error or not job['text']:raise RuntimeError('RK1828 未返回有效回答，请检查任务日志')


def start_worker():
    global worker,events
    if worker and worker.poll() is None: return
    events=queue.Queue()
    env=os.environ.copy(); env['RKLLM_LOG_LEVEL']='1'
    worker=subprocess.Popen([str(ROOT/'build/native_runner')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace',bufsize=1,env=env)
    current_worker=worker; current_events=events
    def read():
        with (ROOT/'native.log').open('a',encoding='utf-8') as log:
            for line in current_worker.stdout:
                if line.startswith('@@JSON@@'):
                    try: current_events.put(json.loads(line[8:]))
                    except json.JSONDecodeError: log.write(line); log.flush()
                else: log.write(line); log.flush()
        current_events.put(dict(event='error',error='Native inference process exited; inspect native.log'))
    threading.Thread(target=read,daemon=True).start()

def native(payload,job):
    global worker
    start_worker()
    worker.stdin.write(json.dumps(payload,ensure_ascii=False)+'\n'); worker.stdin.flush()
    deadline=time.monotonic()+600
    while time.monotonic()<deadline:
        try: event=events.get(timeout=1)
        except queue.Empty: continue
        if event['event']=='delta':
            delta=event.get('text','')
            if delta:
                metrics=dict(job['metrics'])
                if not job['text']: metrics['first_output_ms']=(time.time()-job['created_at'])*1000
                publish(job,text=job['text']+delta,metrics=metrics,message='正在回答')
        elif event['event']=='status': publish(job,message=event['text'])
        elif event['event']=='error': raise RuntimeError(event['error'])
        elif event['event']=='done':
            values=dict(job['metrics']); values.update(event.get('metrics',{}))
            publish(job,text=event.get('text') or job['text'],metrics=values,cancelled=event.get('cancelled',False))
            return
    worker.kill(); worker.wait(); worker=None
    raise TimeoutError('Inference exceeded 600 seconds; worker restarted')

def checked_path(value,exts):
    path=Path(value.removeprefix('file://')).expanduser().resolve()
    if not path.is_file() or path.suffix.lower() not in exts: raise ValueError('请选择有效的图片或音频文件')
    return path

def asr(payload,job):
    global asr_worker
    source=checked_path(payload.get('path',''),{'.wav','.mp3','.m4a','.flac','.ogg','.webm','.aac'})
    audio=DATA/(job['id']+'.wav')
    subprocess.run(['ffmpeg','-nostdin','-y','-i',str(source),'-ar','16000','-ac','1','-c:a','pcm_s16le',str(audio)],
        check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,timeout=60)
    from speech_audio import normalize_asr
    levels=normalize_asr(audio)
    with wave.open(str(audio)) as f: duration=f.getnframes()/f.getframerate()
    p=MODELS/'Qwen3-ASR-0.6B'; online=bool(payload.get('online',False)); suffix='_online' if online else ''
    limit=60 if online else 1200
    if duration<=0 or duration>limit:
        audio.unlink(missing_ok=True)
        raise ValueError(f'当前模式音频长度必须在 0 到 {limit} 秒之间')
    publish(job,message='正在准备语音识别模型')
    started=time.monotonic()
    try:
        parser=AsrTranscriptStream(online)
        stream_updates=0
        def consume(chunk):
            nonlocal stream_updates
            partial=parser.feed(chunk)
            if partial is not None:
                stream_updates+=1
                publish(job,text=partial,message='正在流式转写…')
        def process(value):
            global asr_worker
            asr_worker=value
        key='qwen3_asr_online' if online else 'qwen3_asr'
        result=resident_pool.run(key,
            dict(payload,path=str(audio)),consume,lambda message:publish(job,message=message),
            lambda:job.get('cancel_requested',False),process)
        parser.feed(b'',final=True)
        out=parser.output
        (DATA/(job['id']+'.log')).write_text(out,encoding='utf-8')
        if job.get('cancel_requested') or result.get('cancelled'):
            job['cancelled']=True
            return
        if online:
            match=re.search(r'Final Commit Result[^\n]*\n(.*?)(?:\n\n|$)',out,re.S)
        else:
            match=re.search(r'text res:\s*\n(.*?)(?:\n\n|-{5,}|LLM part performance)',out,re.S)
        if not match or not match[1].strip(): raise RuntimeError('ASR did not return a transcript; see saved inference log')
        publish(job,text=match[1].strip())
        metrics=dict(audio_duration_s=duration,total_ms=(time.monotonic()-started)*1000,stream_updates=stream_updates,**levels)
        labels={'model_load_ms':r'Model load\s*=\s*([\d.]+)','encoder_ms':r'Audio latency\s*=\s*([\d.]+)', 'inference_ms':r'Total Inference\s*=\s*([\d.]+)',
            'rtf':r'RTF\s*=\s*([\d.]+)','ttft_ms':r'TTFT \(include encoder\)\s*=\s*([\d.]+)'}
        for key,pattern in labels.items():
            m=re.search(pattern,out)
            if m: metrics[key]=float(m[1])
        for stage,prefix in [('Prefill','prefill'),('Generate','decode')]:
            m=re.search(r'\b'+stage+r'\s+([\d.]+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)',out)
            if m: metrics.update({prefix+'_ms':float(m[1]),prefix+'_tokens':int(m[2]),prefix+'_tps':float(m[4])})
        if online:
            rounds=re.findall(r'perf: audio=([\d.]+) ms, prefill_tokens=(\d+), decode_tokens=(\d+), ttft=([\d.]+) ms, decode=([\d.]+) ms',out)
            if not rounds: raise RuntimeError('No streaming ASR performance records returned')
            values=[list(map(float,row)) for row in rounds]
            metrics.update(encoder_ms=sum(v[0] for v in values),prefill_tokens=int(sum(v[1] for v in values)),
                decode_tokens=int(sum(v[2] for v in values)),prefill_ms=sum(v[3] for v in values),
                decode_ms=sum(v[4] for v in values),ttft_ms=values[0][0]+values[0][3],streaming_rounds=len(rounds))
            metrics['prefill_tps']=metrics['prefill_tokens']*1000/metrics['prefill_ms'] if metrics['prefill_ms'] else 0
            metrics['decode_tps']=metrics['decode_tokens']*1000/metrics['decode_ms'] if metrics['decode_ms'] else 0
        if metrics.get('inference_ms',0)>0: metrics['rtf']=metrics['inference_ms']/1000/duration
        if metrics.get('rtf',0)>0: metrics['realtime_factor']=1/metrics['rtf']
        result['preload_wait_ms']=result.get('preload_wait_ms',0)+job['metrics'].get('preload_wait_ms',0)
        metrics.update({k:v for k,v in result.items() if k!='cancelled'})
        metrics['e2e_rtf']=metrics['total_ms']/1000/duration
        job['metrics']=metrics
    finally:
        audio.unlink(missing_ok=True)

def execute(job,payload):
    final=dict(state='done',message='推理完成')
    profile_acquired=False
    try:
        publish(job,state='running'); mode=payload['mode']
        prepare_wait=time.monotonic()
        announced=False
        while not profile_lock.acquire(timeout=.2):
            if not announced:
                publish(job,message='正在等待首次模型准备完成');announced=True
            if job.get('cancel_requested'):
                final.update(state='cancelled',message='已停止');return
        profile_acquired=True
        publish(job,metrics=dict(job['metrics'],preload_wait_ms=(time.monotonic()-prepare_wait)*1000))
        status=system_status(); selected=payload['model']; required=[selected]
        if selected=='qwen3vl': required.append('qwen3_encoder')
        if any(not row['ready'] for row in status['models'] if row['model'] in required):
            raise RuntimeError('模型文件仍在传输或不完整，请稍后重试')
        if mode=='asr': asr(payload,job)
        else:
            if mode in ('vision','encoder'): payload['path']=str(checked_path(payload.get('path',''),{'.jpg','.jpeg','.png','.bmp','.webp'}))
            if mode=='text' and not payload.get('question','').strip(): raise ValueError('请输入问题')
            request=constrain(payload,0)
            if selected in RK1828_FILES:rk1828(request,job)
            else:native(request,job)
            if not job.get('cancelled'):
                length=visible_length(job['text']);limit=payload.get('answer_limit',0)
                # Prompt-level length guidance never replaces an answer that
                # has already been streamed to the user.
                publish(job,metrics=dict(job['metrics'],answer_chars=length,answer_limit=limit,length_attempts=1,length_compliant=not limit or length<=limit))
        if job.get('cancelled'): final.update(state='cancelled',message='已停止')
    except Exception as e:
        LOG.exception('Inference failed'); final.update(state='error',error=str(e),message='推理失败')
    finally:
        if profile_acquired:
            profile_lock.release()
        metrics=dict(job['metrics']); metrics['request_ms']=(time.time()-job['created_at'])*1000
        final.update(finished_at=time.time(),metrics=metrics)
        saved=snapshot(job); saved.update(final)
        path=DATA/(job['id']+'.json')
        temporary=path.with_suffix('.tmp')
        try:
            temporary.write_text(json.dumps(saved,ensure_ascii=False,indent=2),encoding='utf-8')
            temporary.replace(path)
        finally:
            lock.release()
            publish(job,**final)

def submit(payload,temporary_input=None):
    if payload.get('mode') not in ('text','encoder','vision','asr'): return None,('Invalid mode',400)
    model=payload.get('model') or DEFAULT_MODELS[payload['mode']]
    if MODEL_MODES.get(model)!=payload['mode']: return None,('模型与任务类型不匹配',400)
    payload['model']=model
    try: payload['max_tokens']=max(8,min(4096,int(payload.get('max_tokens',256))));payload['answer_limit']=int(payload.get('answer_limit',0));assert 0<=payload['answer_limit']<=4096
    except (ValueError,TypeError,AssertionError): return None,('回答长度无效',400)
    if payload['mode']=='asr':payload['answer_limit']=0
    if payload['answer_limit']:payload['max_tokens']=4096
    payload['thinking']=False
    job=dict(id=uuid.uuid4().hex,state='queued',text='',metrics={},message='等待推理',mode=payload['mode'],model=model,
        online=bool(payload.get('online',False)),thinking=bool(payload.get('thinking',False)),
        question=payload.get('question',''),input_path=payload.get('path',''),answer_limit=payload.get('answer_limit',0),max_tokens=payload.get('max_tokens',256),created_at=time.time())
    with submission_lock:
        if submissions.full():
            return None,('推理队列已满，请稍后重试',409)
        finished=[key for key,item in jobs.items() if item['state'] in ('done','error','cancelled')]
        for key in finished[:max(0,len(jobs)-99)]:
            jobs.pop(key,None)
        jobs[job['id']]=job
        submissions.put_nowait((job,payload,temporary_input))
    return job,None


def run_submissions():
    while True:
        job,payload,temporary_input=submissions.get()
        try:
            while job['state']=='queued':
                if time.time()-job['created_at']>=QUEUE_TIMEOUT_SECONDS:
                    with updates:
                        if job['state']=='queued':
                            publish(job,state='error',error='等待推理资源超时',message='等待推理资源超时',
                                    finished_at=time.time())
                    break
                if not lock.acquire(timeout=.2):
                    continue
                if vision_reserved():
                    lock.release()
                    time.sleep(.2)
                    continue
                with updates:
                    if job['state']!='queued':
                        lock.release()
                        break
                    publish(job,state='running')
                execute(job,payload)
                break
        except Exception:
            LOG.exception('Could not finish inference job %s',job['id'])
        finally:
            if temporary_input:
                try:
                    temporary_input.unlink(missing_ok=True)
                except OSError:
                    LOG.warning('Could not remove the temporary image for %s',job['id'])
            submissions.task_done()

@app.get('/health')
@app.get('/api/status')
def status(): return jsonify(system_status())

@app.post('/api/models/prepare')
def prepare_model():
    payload=request.get_json(silent=True) or {}
    model=payload.get('model','')
    if model not in ('qwen35','qwen25vl','qwen3_asr'):
        return jsonify(resident_pool.status())
    key=model+('_online' if model=='qwen3_asr' and payload.get('online') else '')
    if lock.acquire(False):
        try:
            if not vision_reserved():
                resident_pool.prepare(key,also_asr=key=='qwen35' and payload.get('also_asr') is True)
        finally:lock.release()
    return jsonify(resident_pool.status()),202


@app.post('/api/vision/reservation')
def reserve_vision():
    payload=request.get_json(silent=True) or {}
    if not isinstance(payload,dict): return jsonify(error='请求无效'),400
    action=payload.get('action')
    if not lock.acquire(False): return jsonify(error='RK1828 正在处理其他任务'),409
    try:
        if action=='acquire':
            if vision_reserved() or not profile_lock.acquire(timeout=35):
                return jsonify(error='RK1828 正在处理其他任务'),409
            try:
                resident_pool.close(cancel_prepares=True)
                vision_reservation.update(token=uuid.uuid4().hex,until=time.monotonic()+30)
            finally:profile_lock.release()
        elif action in ('renew','release'):
            if not vision_reserved() or payload.get('token')!=vision_reservation['token']:
                return jsonify(error='人脸识别的计算资源已释放'),409
            vision_reservation['until']=time.monotonic()+30 if action=='renew' else 0.0
        else:
            return jsonify(error='未知资源操作'),400
        return jsonify(token=vision_reservation['token'] if action!='release' else '')
    finally:lock.release()


@app.post('/api/video/clip')
def video_clip():
    payload=request.get_json(silent=True)
    if not isinstance(payload,dict): return jsonify(error='视频片段格式无效'),400
    frames, offsets=payload.get('frames'),payload.get('offsets')
    if (not isinstance(frames,list) or len(frames)!=3 or
            not all(isinstance(frame,str) and 0<len(frame)<=350000 for frame in frames) or
            not isinstance(offsets,list) or len(offsets)!=3 or not all(type(t) is int for t in offsets) or
            not 0==offsets[0]<offsets[1]<offsets[2]<=10000):
        return jsonify(error='需要三张按时间排列的采样帧'),400
    try:
        images=[base64.b64decode(frame,validate=True) for frame in frames]
        if any(not frame.startswith(b'\xff\xd8\xff') for frame in images): raise ValueError('JPEG required')
    except ValueError:
        return jsonify(error='视频采样帧损坏'),400
    model=payload.get('model','qwen3_video')
    if not isinstance(model,str) or model not in VIDEO_MODELS: return jsonify(error='未知视频模型'),400
    _, files=video_model_files(model)
    if not (VIDEO_RUNTIME/'video-worker').is_file() or any(not file_info(path,size)[0] for path,size in files):
        return jsonify(error='RK1828 视频理解模型尚未安装完整'),503
    if not lock.acquire(False): return jsonify(error='RK1828 正在处理其他任务'),409
    try:
        if vision_reserved(): return jsonify(error='RK1828 正在进行人脸识别'),409
        with performance_profile(), tempfile.TemporaryDirectory(prefix='fieldops-video-') as temp:
            files=[]
            for i, image in enumerate(images):
                path=Path(temp)/f'{i}.jpg'; path.write_bytes(image); files.append(str(path))
            output=bytearray()
            stats=resident_pool.run(model,dict(frames=files,offsets=offsets),output.extend)
            result=json.loads(output.rsplit(b'@@VIDEO@@',1)[1].splitlines()[0])
        text=result.pop('text').strip()
        match=re.fullmatch(r'\s*(0|([1-3])\s*[-–—]\s*([1-3]))\s*[|｜]\s*(.{1,160})\s*',text)
        if not match or (match[2] and int(match[2])>int(match[3])):
            LOG.warning('Unexpected video response: %r',text)
            return jsonify(error='模型未返回可定位的事件格式'),422
        description=match[4].strip()
        static_description=re.search(r'(?:无|没有|未见|未发现|未观察到|未有|并无)(?:任何|明显的?|显著的?)?(?:动作变化|动作|活动|改变|变化|移动)[。！!\s]*$',description)
        event=match[2] is not None and not static_description
        return jsonify(event=event,text=description,first=int(match[2]) if event else 1,
                       last=int(match[3]) if event else 3,**result,**stats)
    except Exception:
        LOG.exception('Video clip inference failed')
        return jsonify(error='RK1828 视频推理失败，请检查模型服务日志'),500
    finally:
        lock.release()


@app.post('/api/jobs')
def create_job():
    payload=request.get_json(silent=True)
    if not isinstance(payload,dict): return jsonify(error='JSON object required'),400
    job,error=submit(payload)
    if error: return jsonify(error=error[0]),error[1]
    return jsonify(snapshot(job)),202

@app.get('/api/jobs/<job_id>')
def get_job(job_id):
    job=jobs.get(job_id)
    if job is None: return jsonify(error='Job not found'),404
    return jsonify(snapshot(job))

@app.get('/api/jobs/<job_id>/events')
def job_events(job_id):
    job=jobs.get(job_id)
    if job is None: return jsonify(error='Job not found'),404
    @stream_with_context
    def generate():
        revision=-1
        while True:
            with updates:
                if revision==job.get('revision',0): updates.wait(timeout=10)
                current=snapshot(job)
            if revision!=current.get('revision',0):
                revision=current.get('revision',0)
                yield 'data: '+json.dumps(current,ensure_ascii=False)+'\n\n'
            else: yield ': heartbeat\n\n'
            if current['state'] in ('done','error','cancelled'): break
    return Response(generate(),mimetype='text/event-stream',headers={'Cache-Control':'no-cache','X-Accel-Buffering':'no'})

@app.post('/api/jobs/<job_id>/cancel')
def cancel_job(job_id):
    job=jobs.get(job_id)
    if job is None: return jsonify(error='Job not found'),404
    if job['state'] in ('done','error','cancelled'): return jsonify(snapshot(job))
    with updates:
        if job['state']=='queued':
            publish(job,state='cancelled',message='已取消排队',finished_at=time.time())
            return jsonify(snapshot(job))
    if job['mode'] not in ('text','vision','asr'): return jsonify(error='当前处理不支持中断'),409
    if not job['text'] and job['mode']!='asr': return jsonify(error='正在准备模型，请在开始回答后停止'),409
    publish(job,message='正在停止')
    if job['mode']=='asr':
        job['cancel_requested']=True
        if asr_worker and asr_worker.poll() is None: asr_worker.send_signal(signal.SIGUSR1)
    elif job.get('model') in RK1828_FILES:
        job['cancel_requested']=True
        if rk_worker and rk_worker.poll() is None: rk_worker.send_signal(signal.SIGUSR1)
    elif worker and worker.poll() is None: worker.send_signal(signal.SIGUSR1)
    return jsonify(snapshot(job))

@app.get('/api/results')
def results():
    files=sorted(DATA.glob('*.json'),key=lambda p:p.stat().st_mtime,reverse=True)[:30]
    return jsonify([json.loads(p.read_text()) for p in files])

@app.post('/api/vision_chat')
def legacy_vision():
    payload=request.get_json(silent=True) or {}
    try:
        encoded=payload.get('image','').split(',',1)[-1]
        image=DATA/(uuid.uuid4().hex+'.jpg'); image.write_bytes(base64.b64decode(encoded,validate=True))
        job,error=submit(dict(mode='vision',path=str(image),question=payload.get('question','请描述图片'),max_tokens=payload.get('max_tokens',256),answer_limit=payload.get('answer_limit',0)),temporary_input=image)
        if error: return jsonify(message=error[0]),error[1]
        deadline=time.monotonic()+610
        while job['state'] in ('queued','running') and time.monotonic()<deadline: time.sleep(.1)
        if job['state']!='done': return jsonify(message=job.get('error','推理超时')),500
        return jsonify(answer=job['text'],elapsed_ms=job['metrics']['request_ms'],metrics=job['metrics'])
    except Exception as e: return jsonify(message=str(e)),400
    finally:
        if 'image' in locals() and not locals().get('job'): image.unlink(missing_ok=True)

def release_idle_video():
    while True:
        time.sleep(2)
        try:
            resident_pool.release_idle(VIDEO_MODELS, VIDEO_IDLE_SECONDS)
        except Exception:
            LOG.exception('Could not release the idle video model')


if __name__=='__main__':
    logging.basicConfig(level=logging.INFO)
    threading.Thread(target=run_submissions,daemon=True).start()
    threading.Thread(target=release_idle_video, daemon=True).start()
    app.run(host='127.0.0.1',port=1828,threaded=True)
