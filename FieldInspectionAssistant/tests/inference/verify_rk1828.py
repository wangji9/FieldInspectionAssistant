"""Hardware verification for the two RK1828 models through the production API."""
import json
from pathlib import Path
import subprocess
import time
from urllib.request import Request, urlopen
from urllib.error import HTTPError

BASE='http://127.0.0.1:1828'
OUT=Path('/userdata/ai-workbench/rk1828-verification.json')


def api(path,payload=None):
    data=None if payload is None else json.dumps(payload,ensure_ascii=False).encode()
    with urlopen(Request(BASE+path,data=data,headers={'Content-Type':'application/json'}),timeout=15) as r:
        return json.load(r)


def run(payload):
    job=api('/api/jobs',payload);start=time.monotonic();samples=[];last=0
    print('START',job['id'],payload,flush=True)
    while job['state'] in ('queued','running'):
        if time.monotonic()-start>620:raise TimeoutError(job['id'])
        if time.monotonic()-last>3:
            samples.append(subprocess.run(['rknn-smi','info'],capture_output=True,text=True,timeout=5).stdout)
            last=time.monotonic()
        time.sleep(.25);job=api('/api/jobs/'+job['id'])
    print(json.dumps(job,ensure_ascii=False),flush=True)
    if job['state']!='done' or not job['text']:raise RuntimeError(job)
    for key in ['model_load_ms','encoder_ms','inference_ms','ttft_ms']:
        assert 0 <= job['metrics'].get(key,0) <= job['metrics']['request_ms']+100, (key,job)
    return dict(job=job,device_samples=samples)


if __name__=='__main__':
    report=dict(results=[])
    try:
        # Reject invalid routing before acquiring the inference lock.
        try:api('/api/jobs',dict(mode='text',model='qwen25vl',question='test'))
        except HTTPError as e:assert e.code==400
        else:raise AssertionError('Mismatched model accepted')
        deadline=time.monotonic()+3600;last=0
        while True:
            status=api('/api/status')
            ready={r['model']:r['ready'] for r in status['models']}
            if ready.get('qwen35') and ready.get('qwen25vl'):break
            if time.monotonic()>deadline:raise TimeoutError('Model transfer incomplete')
            if time.monotonic()-last>45:
                print('WAIT',[(r['model'],round(r['received']/r['bytes']*100,1)) for r in status['models'] if r['model'] in ['qwen35','qwen25vl']],flush=True);last=time.monotonic()
            time.sleep(5)
        report['results'].append(run(dict(mode='text',model='qwen35',question='只回答数字：17加25等于多少？',max_tokens=32)))
        assert '42' in report['results'][-1]['job']['text'], 'Arithmetic answer was not 42'
        report['results'].append(run(dict(mode='text',model='qwen35',question='请用两句话说明设备巡检应该注意什么。',max_tokens=96)))
        report['results'].append(run(dict(mode='vision',model='qwen25vl',path=status['sample_image'],question='请用一句中文描述图片，说明人物和背景。',max_tokens=96)))
        report['status']=api('/api/status')
        report['success']=True
    finally:
        OUT.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
        print('REPORT',OUT,flush=True)
