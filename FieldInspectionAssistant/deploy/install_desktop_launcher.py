#!/usr/bin/env python3
"""Install and trust only the FieldOps desktop entry in the actual Xfce session."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import time
from open_field_inspection import ROOT,desktop_environment

env=desktop_environment()
home=Path.home()
source=ROOT/'FieldInspectionAssistant.desktop'
for folder in [home/'Desktop',home/'.local/share/applications',home/'.config/autostart']:
    folder.mkdir(parents=True,exist_ok=True)
    target=folder/source.name
    if target.exists():
        backup=home/'.local/share/FieldOps/FieldInspectionAssistant/backups/launchers'
        backup.mkdir(parents=True,exist_ok=True)
        shutil.copy2(target,backup/(folder.name+'-'+time.strftime('%Y%m%d-%H%M%S')+'.desktop'))
    shutil.copyfile(source,target);target.chmod(0o755)
    checksum=hashlib.sha256(target.read_bytes()).hexdigest()
    subprocess.run(['gio','set','-t','string',str(target),'metadata::xfce-exe-checksum',checksum],env=env,check=True,timeout=8)
    subprocess.run(['desktop-file-validate',str(target)],check=True,timeout=5)
    print('Installed trusted launcher:',target)
subprocess.run(['update-desktop-database',str(home/'.local/share/applications')],env=env,check=False,timeout=5)
