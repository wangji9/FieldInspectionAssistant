"""Measure first visible streamed text and completion with identical hardware prompts."""
import argparse,json,time
from pathlib import Path
from urllib.request import Request,urlopen

BASE='http://127.0.0.1:1828'
def api(path,payload):
    with urlopen(Request(BASE+path,json.dumps(payload).encode(),{'Content-Type':'application/json'}),timeout=20) as f: return json.load(f)

def run(payload,cancel=False):
    started=time.monotonic(); job=api('/api/jobs',payload); first=None; samples=[]; stopped=False
    with urlopen(BASE+'/api/jobs/'+job['id']+'/events',timeout=90) as f:
        for line in f:
            if not line.startswith(b'data: '): continue
            job=json.loads(line[6:]); elapsed=(time.monotonic()-started)*1000
            if job['text']:
                if first is None: first=elapsed
                if not samples or len(job['text'])!=samples[-1][1]: samples.append((round(elapsed,1),len(job['text'])))
                if cancel and not stopped: api('/api/jobs/'+job['id']+'/cancel',{}); stopped=True
            if job['state'] in ('done','error','cancelled'): break
    if job['state']=='error': raise RuntimeError(job)
    result={'job':job,'client_first_ms':first,'client_total_ms':(time.monotonic()-started)*1000,'chunks':samples}
    print(json.dumps(result,ensure_ascii=False),flush=True)
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--case',default='text',choices=['text','vision','cancel','markdown']); args=p.parse_args()
    payload=dict(mode='text',question='请用三句话介绍设备巡检的要点。',max_tokens=96,benchmark_raw=True)
    if args.case=='vision': payload.update(mode='vision',path='/userdata/ai-workbench/vendor/rknn-llm/examples/multimodal_model_demo/data/demo.jpg',question='请用中文简短描述图片中的场景。')
    if args.case=='markdown': payload.update(question='列出循环水泵现场检查的三项要点，用加粗小标题和编号列表。',max_tokens=256,benchmark_raw=False)
    report=[run(payload,cancel=args.case=='cancel') for _ in range(1 if args.case in ('cancel','markdown') else 2)]
    Path('/userdata/ai-workbench/optimized-'+args.case+'.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
