"""Run hardware inference checks and keep their exact outputs and metrics."""
import argparse, json, time
from pathlib import Path
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError

URL='http://127.0.0.1:1828'
ROOT=Path('/userdata/ai-workbench')
def api(path,payload=None):
    data=None if payload is None else json.dumps(payload,ensure_ascii=False).encode()
    with urlopen(Request(URL+path,data=data,headers={'Content-Type':'application/json'}),timeout=15) as response:
        return json.load(response)

def infer(payload):
    deadline=time.monotonic()+900
    while True:
        try: job=api('/api/jobs',payload); break
        except HTTPError as e:
            if e.code!=409 or time.monotonic()>deadline: raise
            time.sleep(2)
    print('START',payload,flush=True)
    last=0
    while job['state'] in ('queued','running'):
        if time.monotonic()-last>15:
            print(job['id'],job['state'],job['message'],flush=True); last=time.monotonic()
        if time.monotonic()>deadline: raise TimeoutError(job['id'])
        time.sleep(.5); job=api('/api/jobs/'+job['id'])
    print(json.dumps(job,ensure_ascii=False),flush=True)
    if job['state']!='done': raise RuntimeError(job.get('error'))
    return job

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--mode',choices=['text','encoder','vision','asr','all'],default='all')
    parser.add_argument('--warm',action='store_true'); parser.add_argument('--online',action='store_true')
    args=parser.parse_args(); report=[]
    for attempt in range(30):
        try: api('/api/status'); break
        except URLError:
            if attempt==29: raise
            time.sleep(1)
    for mode in ['encoder','asr','text','vision'] if args.mode=='all' else [args.mode]:
        deadline=time.monotonic()+3600; last=0
        while True:
            status=api('/api/status'); rows={m['mode']:m for m in status['models']}
            required=['vision','encoder'] if mode=='vision' else [mode]
            if all(rows[k]['ready'] for k in required): break
            if time.monotonic()>deadline: raise TimeoutError('Waiting for models')
            if time.monotonic()-last>30:
                print('WAIT',mode,{k:round(rows[k]['received']/rows[k]['bytes']*100,1) for k in required},flush=True); last=time.monotonic()
            time.sleep(5)
        path=status['sample_audio'] if mode=='asr' else status['sample_image'] if mode in ('vision','encoder') else ''
        payload=dict(mode=mode,path=path,question='请用三句话介绍设备巡检的要点。' if mode=='text' else '请用中文简短描述图片中的场景。',max_tokens=96,thinking=False,online=args.online)
        for _ in range(2 if args.warm else 1): report.append(infer(payload))
    target=ROOT/('smoke-'+args.mode+('-online' if args.online else '')+'.json')
    target.write_text(json.dumps(dict(system=api('/api/status'),results=report),ensure_ascii=False,indent=2),encoding='utf-8')
    print('REPORT',target,flush=True)
