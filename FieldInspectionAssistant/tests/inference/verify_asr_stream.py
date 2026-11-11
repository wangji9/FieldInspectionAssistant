"""End-to-end SSE check using an existing recording; never opens a microphone."""
import argparse
import json
import time
import urllib.request
from pathlib import Path

BASE = 'http://127.0.0.1:1828'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('recording', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
recording = args.recording.resolve()
if not recording.is_file():
    parser.error(f'recording does not exist: {recording}')

reports = []
for online in (False, True):
    started = time.monotonic()
    payload = dict(mode='asr', model='qwen3_asr', online=online, path=str(recording))
    request = urllib.request.Request(BASE+'/api/jobs', data=json.dumps(payload).encode(),
                                     headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, timeout=10) as response:
        job = json.load(response)
    frames = []
    with urllib.request.urlopen(BASE+'/api/jobs/'+job['id']+'/events', timeout=180) as response:
        for line in response:
            if line.startswith(b'data: '):
                value = json.loads(line[6:])
                frames.append(dict(t=round(time.monotonic()-started, 4), **value))
    distinct = []
    for frame in frames:
        if frame['state'] == 'running' and frame['text'] and (not distinct or distinct[-1]['text'] != frame['text']):
            distinct.append(frame)
    assert frames[-1]['state'] == 'done', frames[-1]
    assert len(distinct) >= 2, ('No incremental transcripts', frames)
    assert distinct[0]['t'] < frames[-1]['t']
    assert all('\ufffd' not in f['text'] for f in frames)
    reports.append(dict(online=online, job_id=job['id'], text=frames[-1]['text'],
                        partial_versions=len(distinct), first_partial_s=distinct[0]['t'],
                        completion_s=frames[-1]['t'], metrics=frames[-1]['metrics'], frames=frames))
    print(json.dumps({k:v for k,v in reports[-1].items() if k!='frames'}, ensure_ascii=False), flush=True)
args.output.write_text(
    json.dumps(reports, ensure_ascii=False, indent=2), encoding='utf-8')
