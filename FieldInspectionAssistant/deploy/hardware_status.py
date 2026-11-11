"""Read-only, bounded telemetry. Does not record, play audio or start cameras."""
import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import socket
import sqlite3
import subprocess
import time
from capture_devices import discover


def read(path):
    try:return Path(path).read_text().strip()
    except OSError:return ''


def pulse(*args):
    try:return json.loads(subprocess.check_output(['pactl', '-f', 'json', *args],text=True,timeout=.7,stderr=subprocess.DEVNULL))
    except (OSError, ValueError, subprocess.SubprocessError):return None


def snapshot(folder, pid):
    sources, sinks, info = pulse('list','sources'), pulse('list','sinks'), pulse('info')
    def source_query(*args,**kwargs):
        if sources is None:raise OSError('音频服务暂不可用')
        return json.dumps(sources)
    devices, note = discover(run=source_query)
    microphones=[]
    for d in devices:
        d=dict(d)
        if d.get('transport')=='six_serial':
            d['accessible']=all(os.access(d[k],os.R_OK|os.W_OK) for k in ['audio','control'])
            d['state']='接口就绪 · 6 通道' if d['accessible'] else '已连接 · 权限不足'
            d['detail']=d['audio']+' / '+d['control']
        else:
            source=next((s for s in sources or [] if s.get('name')==d['id']),{})
            d.update(muted=bool(source.get('mute')),accessible=True)
            d['state']='已连接 · 已静音' if d['muted'] else '接口就绪 · 待录音实测'
            d['detail']=f"{d['rate']} Hz · {d['channels']} 通道 · "+{'RUNNING':'正在使用','SUSPENDED':'空闲','IDLE':'空闲'}.get(source.get('state'),'未知')
        microphones.append(d)
    speakers=[]
    for sink in sinks or []:
        muted=bool(sink.get('mute'));volumes=[v.get('value',0)/65536*100 for v in sink.get('volume',{}).values()]
        active=sink.get('active_port');active=active.get('name') if isinstance(active,dict) else active
        port=next((p for p in sink.get('ports',[]) if p.get('name')==active),{})
        absent=str(port.get('availability','')).lower() in ['no','not available']
        sink_state={'RUNNING':'正在使用','SUSPENDED':'空闲','IDLE':'空闲'}.get(sink.get('state'),'未知')
        speakers.append(dict(name=sink.get('description',sink['name']),id=sink['name'],muted=muted,
            state='输出端口未连接' if absent else '已连接 · 已静音' if muted else '接口就绪 · 待试听',
            detail=f"音量 {round(max(volumes,default=0))}% · {sink_state} · {active or '默认端口'}",
            default=sink['name']==(info or {}).get('default_sink_name')))
    memory={}
    for line in read('/proc/meminfo').splitlines():
        k,_,v=line.partition(':');memory[k]=int(v.strip().split()[0])*1024
    cpu_rows=read('/proc/stat').splitlines()
    cpu=[int(v) for v in cpu_rows[0].split()[1:9]]
    cpu_cores=sum(1 for row in cpu_rows if row.startswith('cpu') and row.split()[0][3:].isdigit())
    npu_freq=next((read(p/'cur_freq') for p in Path('/sys/class/devfreq').glob('*.npu')), '')
    board_model=read('/proc/device-tree/model').rstrip('\0')
    temps=[]
    for p in Path('/sys/class/thermal').glob('thermal_zone*'):
        try:
            val=float(read(p/'temp'))/1000
            if -20<val<150:temps.append(dict(name={'bigcore0-thermal':'CPU 大核0','bigcore1-thermal':'CPU 大核1','littlecore-thermal':'CPU 小核','soc-thermal':'SoC','gpu-thermal':'GPU','npu-thermal':'NPU','center-thermal':'中心区域'}.get(read(p/'type'),read(p/'type')),value=val))
        except ValueError:pass
    disk=shutil.disk_usage(folder);process={}
    for line in read(f'/proc/{pid}/status').splitlines():
        if line.startswith(('VmRSS:','Threads:')):
            key,_,value=line.partition(':');process[key]=int(value.strip().split()[0])
    networks=[]
    import fcntl,struct
    for p in sorted(Path('/sys/class/net').iterdir()):
        if p.name=='lo':continue
        address=''
        try:
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
                address=socket.inet_ntoa(fcntl.ioctl(sock.fileno(),0x8915,struct.pack('256s',p.name.encode()[:15]))[20:24])
        except OSError:pass
        networks.append(dict(name=p.name,state=read(p/'operstate'),address=address))
    cameras=[]
    for p in sorted(Path('/sys/class/video4linux').glob('video*')):
        path='/dev/'+p.name
        try:
            fd=os.open(path,os.O_RDONLY|os.O_NONBLOCK)
            try:
                caps=bytearray(104);fcntl.ioctl(fd,0x80685600,caps,True)
                flags=int.from_bytes(caps[88:92],'little') or int.from_bytes(caps[84:88],'little')
                if not flags & (1|0x1000):continue
            finally:os.close(fd)
        except OSError:
            if 'usb' not in str(p.resolve()).lower():continue
        cameras.append(dict(path=path,name=read(p/'name'),accessible=os.access(path,os.R_OK|os.W_OK),usb='usb' in str(p.resolve()).lower()))
    cameras.sort(key=lambda c:(not c['usb'],c['path']))
    schema=None
    try:
        with sqlite3.connect((Path(folder)/'field_inspection.sqlite').as_uri()+'?mode=ro',uri=True,timeout=.2) as db:schema=db.execute('SELECT version FROM schema_meta').fetchone()[0]
    except (OSError,sqlite3.Error,TypeError):pass
    return dict(schema=schema,timestamp=datetime.datetime.now(datetime.timezone.utc).isoformat(),monotonic=time.monotonic(),
        cpu_total=sum(cpu),cpu_idle=sum(cpu[3:5]),cpu_cores=cpu_cores,board_model=board_model,
        rk3588_npu_hz=int(npu_freq) if npu_freq.isdigit() else None,
        memory_total=memory.get('MemTotal'),memory_available=memory.get('MemAvailable'),
        uptime=float(read('/proc/uptime').split()[0]),load=read('/proc/loadavg').split()[:3],temperatures=temps,
        disk_total=disk.total,disk_free=disk.free,process_rss=process.get('VmRSS',0)*1024,threads=process.get('Threads'),
        microphones=microphones,speakers=speakers,cameras=cameras,networks=networks,audio_service=sources is not None and sinks is not None,
        audio_note=note,default_source=(info or {}).get('default_source_name',''),default_sink=(info or {}).get('default_sink_name',''))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--data',required=True);p.add_argument('--pid',type=int,required=True);a=p.parse_args()
    print(json.dumps(snapshot(a.data,a.pid),ensure_ascii=False,allow_nan=False),flush=True)
