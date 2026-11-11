#!/usr/bin/env python3
"""Desktop entry point: restore an existing window, otherwise launch once."""
from pathlib import Path
import fcntl
import os
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parent.parent

def desktop_environment():
    env=os.environ.copy()
    for proc in Path('/proc').glob('[0-9]*'):
        try:
            if (proc/'comm').read_text().strip()!='xfdesktop' or proc.stat().st_uid!=os.getuid():continue
            values=dict(v.split('=',1) for v in (proc/'environ').read_bytes().decode().split('\0') if '=' in v)
            for key in ['DISPLAY','XAUTHORITY','DBUS_SESSION_BUS_ADDRESS','XDG_RUNTIME_DIR']:
                if values.get(key):env[key]=values[key]
            break
        except (FileNotFoundError,PermissionError,ProcessLookupError):continue
    env.setdefault('DISPLAY',':0')
    env.setdefault('XAUTHORITY',str(Path.home()/'.Xauthority'))
    env.setdefault('XDG_RUNTIME_DIR','/run/user/'+str(os.getuid()))
    env.setdefault('DBUS_SESSION_BUS_ADDRESS','unix:path='+env['XDG_RUNTIME_DIR']+'/bus')
    return env

def app_windows(env):
    result=subprocess.run(['wmctrl','-lx'],env=env,capture_output=True,text=True,timeout=3)
    windows=[]
    for line in result.stdout.splitlines():
        fields=line.split(None,4)
        if len(fields)>=3 and fields[2].lower()=='field_inspection_assistant.fieldinspectionassistant':windows.append(fields[0])
    return windows

def main():
    env=desktop_environment()
    if '--session-bus' in sys.argv:
        print(env['DBUS_SESSION_BUS_ADDRESS']);return
    lock_dir=Path(env['XDG_RUNTIME_DIR']);lock_dir.mkdir(exist_ok=True)
    with (lock_dir/'fieldops-desktop-launch.lock').open('w') as lock:
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:return
        windows=app_windows(env)
        if windows:
            subprocess.run(['wmctrl','-ia',windows[-1]],env=env,check=True,timeout=3)
            return
        logs=Path.home()/'.local/share/FieldOps/FieldInspectionAssistant/logs'
        logs.mkdir(parents=True,exist_ok=True)
        with (logs/'desktop-launch.log').open('ab') as log:
            child=subprocess.Popen([str(ROOT/'deploy/run_field_inspection.sh')],env=env,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        for _ in range(30):
            if child.poll() is not None:raise RuntimeError('Application exited; see desktop-launch.log')
            if app_windows(env):return
            time.sleep(.1)

if __name__=='__main__':main()
