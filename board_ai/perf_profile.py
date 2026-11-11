"""Use supported performance governors while a job runs; keep kernel thermal limits."""
import json
import sys
from pathlib import Path

STATE=Path('/run/fieldops-perf.json')
PATHS=list(Path('/sys/devices/system/cpu/cpufreq').glob('policy*/scaling_governor'))
PATHS += [Path('/sys/class/devfreq')/name/'governor' for name in ('dmc','fdab0000.npu')]

if sys.argv[1]=='enter':
    if STATE.exists():
        old=json.loads(STATE.read_text())
        for name,value in old.items():
            try: Path(name).write_text(value)
            except OSError: pass
    saved={}
    try:
        for path in PATHS:
            if not path.exists(): continue
            saved[str(path)]=path.read_text().strip()
            STATE.write_text(json.dumps(saved))
            path.write_text('performance')
    except Exception:
        for name,value in saved.items(): Path(name).write_text(value)
        STATE.unlink(missing_ok=True)
        raise
else:
    if STATE.exists():
        for name,value in json.loads(STATE.read_text()).items():
            try: Path(name).write_text(value)
            except OSError: pass
        STATE.unlink()
