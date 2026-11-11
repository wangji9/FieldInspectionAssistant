"""Check cancellation and compatibility with the existing local model routes."""
import json
from pathlib import Path
import subprocess
import time
from verify_rk1828 import api, run

if __name__=='__main__':
    report={}; deadline=time.monotonic()+700
    path=Path('/userdata/ai-workbench/rk1828-verification.json')
    while not path.exists():
        if time.monotonic()>deadline:raise TimeoutError('Waiting for model verification')
        time.sleep(1)
    assert json.loads(path.read_text())['success']
    try:
        job=api('/api/jobs',dict(mode='text',model='qwen35',question='请详细列出五十条设备巡检注意事项，每条单独一行。',max_tokens=256))
        while not job['text'] and job['state'] in ('queued','running'):
            time.sleep(.1); job=api('/api/jobs/'+job['id'])
            if time.monotonic()>deadline:raise TimeoutError(job['id'])
        assert job['state']=='running',job
        api('/api/jobs/'+job['id']+'/cancel',{})
        while job['state'] in ('queued','running'):
            time.sleep(.1);job=api('/api/jobs/'+job['id'])
        assert job['state']=='cancelled',job
        report['cancelled_job']=job
        report['device_after_cancel']=subprocess.run(['rknn-smi','info'],capture_output=True,text=True).stdout
        report['legacy_text']=run(dict(mode='text',model='qwen3',question='请用一句话说明设备巡检的目的。',max_tokens=32))
        status=api('/api/status')
        report['asr']=run(dict(mode='asr',path=status['sample_audio'],max_tokens=96,online=False))
        report['success']=True
    finally:
        Path('/userdata/ai-workbench/rk1828-controls-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2))
        print(json.dumps(report,ensure_ascii=False),flush=True)
