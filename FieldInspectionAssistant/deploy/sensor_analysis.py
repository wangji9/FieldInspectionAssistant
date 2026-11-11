#!/usr/bin/env python3
"""Qt-owned, local JSON-lines worker: real PulseAudio PCM, SQLite and DSP."""
import argparse
import collections
import json
import os
from pathlib import Path
import re
import signal
import sqlite3
import subprocess
import sys
import threading
import time
import uuid
import wave
import numpy as np
from signal_features import analyze,metrics,METHODS,NAMES

class Worker:
    def __init__(self,folder):
        self.folder=Path(folder);self.folder.mkdir(parents=True,exist_ok=True)
        self.dbpath=self.folder/'measurements.sqlite';self.output_lock=threading.Lock()
        self.lock=threading.RLock();self.stop_event=threading.Event();self.thread=None;self.child=None
        self.buffer=collections.deque(maxlen=60);self.fs=16000;self.channels=1
        self.method='time';self.channel=0;self.window=.5;self.options={};self.current=None
        self.full_data=None;self.full_duration=0;self.full_scale=32768.;self.full_origin='imported'
        self.recording_path='';self.selected='';self.started=0;self.latest={};self.last_error='';self.devices=[]
        with self.db() as db:
            db.executescript('''PRAGMA journal_mode=WAL;
            CREATE TABLE IF NOT EXISTS recordings(id TEXT PRIMARY KEY,path TEXT NOT NULL,source TEXT NOT NULL,
              fs INTEGER NOT NULL,channels INTEGER NOT NULL,started REAL NOT NULL,duration REAL NOT NULL DEFAULT 0,
              origin TEXT NOT NULL,complete INTEGER NOT NULL DEFAULT 0,calibration TEXT NOT NULL DEFAULT '{}');
            CREATE TABLE IF NOT EXISTS measurements(id INTEGER PRIMARY KEY,recording_id TEXT NOT NULL,
              channel INTEGER NOT NULL,time REAL NOT NULL,metrics TEXT NOT NULL);
            CREATE INDEX IF NOT EXISTS measurements_time ON measurements(recording_id,channel,time);
            CREATE TABLE IF NOT EXISTS recording_events(id INTEGER PRIMARY KEY,recording_id TEXT NOT NULL,action TEXT NOT NULL,
              actor_id TEXT NOT NULL,at TEXT NOT NULL,before_json TEXT NOT NULL,after_json TEXT NOT NULL);
            ''')
            columns={r[1] for r in db.execute('PRAGMA table_info(recordings)')}
            for name,definition in [('name',"TEXT NOT NULL DEFAULT ''"),('notes',"TEXT NOT NULL DEFAULT ''"),('owner_id',"TEXT NOT NULL DEFAULT ''"),('deleted_at',"TEXT NOT NULL DEFAULT ''"),('updated_at',"TEXT NOT NULL DEFAULT ''"),('updated_by',"TEXT NOT NULL DEFAULT ''"),('revision','INTEGER NOT NULL DEFAULT 0')]:
                if name not in columns:db.execute('ALTER TABLE recordings ADD COLUMN '+name+' '+definition)
        # A previous crash must not present incomplete raw files as valid recordings.
        self.refresh()

    def db(self):return sqlite3.connect(self.dbpath,timeout=5)
    def emit(self,**obj):
        obj.setdefault('reply_user',getattr(self,'reply_user',''))
        with self.output_lock:print(json.dumps(obj,ensure_ascii=False,allow_nan=False),flush=True)
    def error(self,message):self.last_error=str(message);self.emit(event='error',message=self.last_error)
    def refresh(self,with_data=True):
        from capture_devices import discover
        self.devices,note=discover()
        self.emit(event='devices',devices=self.devices,note=note,methods=[dict(id=i,name=n) for i,n in zip(METHODS,NAMES)])
        if with_data:self.records();self.trends()
    def records(self):
        with self.db() as db:
            rows=db.execute("SELECT id,path,source,fs,channels,started,duration,origin,complete,calibration FROM recordings WHERE deleted_at='' ORDER BY started DESC LIMIT 200").fetchall()
        self.emit(event='records',records=[dict(zip(['id','path','source','fs','channels','started','duration','origin','complete','calibration'],r)) for r in rows])
    def trends(self):
        with self.db() as db:
            if self.selected:
                rows=db.execute("SELECT m.time,m.metrics FROM measurements m JOIN recordings r ON r.id=m.recording_id WHERE m.channel=? AND m.recording_id=? AND r.deleted_at='' ORDER BY m.time DESC LIMIT 300",(self.channel,self.selected)).fetchall()
            else:
                rows=db.execute("SELECT m.time,m.metrics FROM measurements m JOIN recordings r ON r.id=m.recording_id WHERE m.channel=? AND r.deleted_at='' ORDER BY m.time DESC LIMIT 300",(self.channel,)).fetchall()
        self.emit(event='trends',trends=[dict(time=t,**json.loads(m)) for t,m in reversed(rows)])
    def start(self,payload):
        if self.thread and self.thread.is_alive():raise ValueError('采集正在进行，请先停止')
        device=next((d for d in self.devices if d['id']==payload.get('source')),None)
        if not device:raise ValueError('请先选择已连接的真实采集设备')
        self.fs=device['rate'];self.channels=device['channels'];self.channel=min(self.channel,self.channels-1)
        self.buffer.clear();self.stop_event.clear();self.selected=''
        self.thread=threading.Thread(target=self.capture,args=(device,),daemon=True);self.thread.start()
    def stop(self):
        self.stop_event.set()
        if self.child and self.child.poll() is None:self.child.terminate()
        if self.thread and self.thread is not threading.current_thread():self.thread.join(timeout=5)
    def capture(self,device):
        identity=uuid.uuid4().hex;path=self.folder/(time.strftime('%Y%m%d_%H%M%S')+'_'+identity[:8]+'.wav')
        started=time.time();frames=0;pending=b'';self.started=started;self.current=identity
        calibration=json.dumps(self.options,ensure_ascii=False)
        with self.db() as db:
            db.execute('INSERT INTO recordings(id,path,source,fs,channels,started,origin,calibration) VALUES(?,?,?,?,?,?,?,?)',
                       (identity,str(path),device['name'],self.fs,self.channels,started,'sensor',calibration))
        try:
            # Use the source's actual negotiated rate/channels, never claim an upsampled 51.2kHz signal.
            if 'ListenGo' in device['id']:
                sys.path.insert(0,'/userdata/ai-workbench')
                from six_mic_control import wake_microphone
                wake_microphone()
            env=dict(os.environ,PULSE_SOURCE=device['id'])
            self.child=subprocess.Popen(['arecord','-q','-D','pulse','-f','S16_LE','-r',str(self.fs),'-c',str(self.channels),'-t','raw'],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            self.emit(event='capture',running=True,source=device,recording=identity)
            chunk_frames=max(256,int(self.fs*.1));last=time.monotonic()
            with wave.open(str(path),'wb') as wav:
                wav.setnchannels(self.channels);wav.setsampwidth(2);wav.setframerate(self.fs)
                while not self.stop_event.is_set() and time.time()-started<300:
                    if os.statvfs(self.folder).f_bavail*os.statvfs(self.folder).f_frsize<100*1024*1024:raise ValueError('剩余空间不足 100 MB，采集已停止')
                    raw=self.child.stdout.read(chunk_frames*self.channels*2)
                    if not raw:break
                    pending+=raw;size=len(pending)//(self.channels*2)*(self.channels*2)
                    raw,pending=pending[:size],pending[size:]
                    if not raw:continue
                    wav.writeframesraw(raw);frames+=len(raw)//(self.channels*2)
                    block=np.frombuffer(raw,dtype='<i2').reshape(-1,self.channels).astype(float)/32768
                    with self.lock:self.buffer.append(block)
                    if time.monotonic()-last>=1:
                        last=time.monotonic();self.compute()
                        window=self.data(seconds=1)
                        with self.db() as db:
                            for channel in range(self.channels):
                                db.execute('INSERT INTO measurements(recording_id,channel,time,metrics) VALUES(?,?,?,?)',
                                           (identity,channel,time.time(),json.dumps(metrics(window[:,channel],self.fs),allow_nan=False)))
                        self.trends()
                if frames==0:
                    detail=self.child.stderr.read(1000).decode(errors='replace') if self.child.poll() is not None else ''
                    raise ValueError('未收到采集数据，请检查设备、权限或占用情况。'+detail)
            with self.db() as db:db.execute('UPDATE recordings SET duration=?,complete=1 WHERE id=?',(frames/self.fs,identity))
        except Exception as e:self.error(e)
        finally:
            if self.child and self.child.poll() is None:self.child.terminate()
            if self.child:
                try:self.child.wait(timeout=3)
                except subprocess.TimeoutExpired:self.child.kill()
            self.emit(event='capture',running=False,duration=frames/self.fs,recording=identity)
            self.current=None;self.records()
    def data(self,seconds=None):
        with self.lock:
            if not self.buffer:raise ValueError('尚无真实采样，请开始采集或导入 WAV')
            data=np.concatenate(tuple(self.buffer),axis=0)
        return data[-max(64,int(self.fs*(seconds or self.window))):]
    def compute(self):
        try:
            data=self.data();channel=min(self.channel,self.channels-1);offset=0;end=len(data)/self.fs;scope='capture'
            if self.full_data is not None:
                scope=self.options.get('analysisScope','full');total=len(self.full_data)
                if scope=='range':
                    start=max(0,min(total-1,int(float(self.options.get('analysisStart',0))*self.fs)))
                    stop=min(total,int(float(self.options.get('analysisEnd',self.full_duration))*self.fs))
                    if stop<=start:raise ValueError('分析结束时间必须大于开始时间')
                elif scope=='window':start=max(0,total-int(self.window*self.fs));stop=total
                else:start=0;stop=total
                value=self.normalized(self.full_data[start:stop,channel]);offset=start/self.fs;end=stop/self.fs
            else:value=data[:,channel]
            gain=float(self.options.get('gain',0) or 0);unit=self.options.get('unit','FS') if gain>0 else 'FS'
            plot=analyze(value*(gain if gain>0 else 1)+(float(self.options.get("offset",0)) if gain>0 else 0),self.fs,self.method,self.options,unit)
            plot['metrics']['clipped_pct']=float(np.mean(np.abs(value)>=.999)*100)
            self.offset_time(plot,offset)
            time_plot={};frequency_plot={}
            if self.options.get('pairedPlots'):
                indices=range(self.channels) if self.options.get('overlayChannels') else [channel]
                ts=[];fs=[]
                for c in indices:
                    values=self.normalized(self.full_data[start:stop,c]) if self.full_data is not None else data[:,c]
                    values=values*(gain if gain>0 else 1)+(float(self.options.get("offset",0)) if gain>0 else 0)
                    t=analyze(values,self.fs,'time',unit=unit);f=analyze(values,self.fs,'fft',unit=unit);self.offset_time(t,offset)
                    for series in t['series']:series['name']='通道 '+str(c+1)
                    for series in f['series']:series['name']='通道 '+str(c+1)
                    if not time_plot:time_plot=t;frequency_plot=f
                    ts.extend(t['series']);fs.extend(f['series'])
                time_plot['series']=ts;frequency_plot['series']=fs
            self.latest=dict(plot=plot,source=self.full_origin if self.full_data is not None else 'sensor',recording=self.selected or self.current,
                             channel=channel,channels=self.channels,rate=self.fs,unit=unit,time=time.time(),recording_path=self.recording_path,
                             full_duration=self.full_duration if self.full_data is not None else len(data)/self.fs,
                             analysis_scope=scope,analysis_start=offset,analysis_end=end,
                             calibration=self.options,capture_time=time_plot,capture_frequency=frequency_plot,channels_data=[dict(channel=c,metrics=metrics(data[:,c],self.fs),
                             plot=analyze(data[:,c],self.fs,'time',unit='FS')) for c in range(min(data.shape[1],8))] if self.options.get('previewChannels') else [])
            self.emit(event='snapshot',**self.latest)
        except Exception as e:
            self.error('分析失败：'+str(e))
            self.emit(event='snapshot',plot=dict(title=NAMES[METHODS.index(self.method)],kind='line',series=[],error=str(e)),
                      rate=self.fs,channels=self.channels,channel=self.channel,unit='FS',time=time.time())
    def configure(self,payload):
        self.method=payload.get('method',self.method)
        if self.method not in METHODS:raise ValueError('未知分析方法')
        self.channel=max(0,min(int(payload.get('channel',self.channel)),self.channels-1))
        self.window=max(.08,min(5,float(payload.get('window',self.window))))
        self.options.update(payload.get('options',{}))
        gain=float(self.options.get('gain',0) or 0)
        if not np.isfinite(gain) or gain<0 or gain>1e9 or not np.isfinite(float(self.options.get('offset',0))):raise ValueError('标定系数无效')
        if self.buffer:self.compute()
        self.trends()
    def normalized(self,data):
        if data.dtype.kind=='i':return np.asarray(data,dtype=float)/float(2**(data.dtype.itemsize*8-1))
        if data.dtype.kind=='u':return (np.asarray(data,dtype=float)-128)/128
        return np.asarray(data,dtype=float)
    def offset_time(self,plot,offset):
        if not offset:return
        if plot.get('xLabel')=='时间 / s':
            for series in plot.get('series',[]):
                for point in series['points']:point[0]+=offset
            if 'xRange' in plot:plot['xRange']=[v+offset for v in plot['xRange']]
        for item in plot.get('slices',[]):item['time']+=offset
        if plot.get('envelope'):self.offset_time(plot['envelope'],offset)
    def load(self,path,identity=None,owner=''):
        if self.thread and self.thread.is_alive():raise ValueError('请先停止实时采集再读取历史数据')
        path=Path(path)
        if not path.is_file() or path.stat().st_size>512*1024*1024:raise ValueError('请选择不超过 512 MB 的 WAV 文件')
        from scipy.io import wavfile
        try:fs,raw=wavfile.read(str(path),mmap=True)
        except ValueError:fs,raw=wavfile.read(str(path))
        if not 4000<=fs<=192000:raise ValueError('采样率不在 4–192 kHz 范围内')
        if raw.dtype.kind not in 'iuf':raise ValueError('不支持的 WAV 编码')
        if raw.ndim==1:raw=raw[:,None]
        if raw.shape[1]>8 or len(raw)<fs*.04:raise ValueError('需 1–8 通道且至少 40 ms 的音频')
        if raw.dtype.kind=='f' and not np.isfinite(raw).all():raise ValueError('采样包含非有限数值')
        origin='imported';recording_path=str(path)
        if not identity:
            if not owner:raise ValueError('导入录音需要有效用户')
            identity=uuid.uuid4().hex
            import shutil
            target=self.folder/(identity+'.wav');recording_path=str(target)
            try:
                shutil.copy2(path,target)
                with self.db() as db:
                    db.execute('INSERT INTO recordings(id,path,source,fs,channels,started,duration,origin,complete,name,owner_id) VALUES(?,?,?,?,?,?,?,?,1,?,?)',
                               (identity,str(target),path.name,fs,raw.shape[1],path.stat().st_mtime,len(raw)/fs,'imported',path.stem,owner))
                    for c in range(raw.shape[1]):
                        for start in range(0,len(raw),fs):
                            block=self.normalized(raw[start:start+fs,c])
                            if len(block)>1:db.execute('INSERT INTO measurements(recording_id,channel,time,metrics) VALUES(?,?,?,?)',
                                (identity,c,path.stat().st_mtime+start/fs,json.dumps(metrics(block,fs),allow_nan=False)))
                    db.execute('INSERT INTO recording_events(recording_id,action,actor_id,at,before_json,after_json) VALUES(?,?,?,?,?,?)',
                        (identity,'import',owner,time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'{}',json.dumps(dict(path=str(target),source=path.name,owner_id=owner))))
            except Exception:
                target.unlink(missing_ok=True);raise
        else:
            with self.db() as db:
                row=db.execute("SELECT origin FROM recordings WHERE id=? AND deleted_at=''",(identity,)).fetchone()
                if not row:raise ValueError('录音已被删除，请刷新列表')
                origin=row[0]
        self.fs=fs;self.channels=raw.shape[1];self.channel=min(self.channel,self.channels-1)
        self.recording_path=recording_path;self.full_data=raw;self.full_duration=len(raw)/fs;self.full_origin=origin
        data=self.normalized(raw[-fs*6:]);self.buffer.clear()
        # Keep the latest 6 seconds for interaction, never overwrite the original WAV.
        for block in np.array_split(data,min(60,max(1,len(data)//max(1,fs//10)))):self.buffer.append(block)
        self.selected=identity;self.compute();self.records();self.trends()
    def export(self,payload):
        path=Path(payload['path']);kind=payload.get('format','json')
        if not path.is_absolute():raise ValueError('请选择绝对保存路径')
        if kind=='json':path.write_text(json.dumps(self.latest,ensure_ascii=False,indent=2,allow_nan=False),encoding='utf-8')
        elif kind=='csv':
            import csv
            scope=payload.get('scope','full')
            if scope not in ('full','window'):raise ValueError('请选择整段录音或当前窗口')
            if self.full_data is None:raise ValueError('完整原始录音尚未就绪；实时采样应通过 C++ 导出')
            total=len(self.full_data);start=0 if scope=='full' else max(0,total-int(self.window*self.fs));temporary=path.with_name(path.name+'.partial-'+uuid.uuid4().hex)
            try:
                with temporary.open('w',encoding='utf-8-sig',newline='') as out:
                    writer=csv.writer(out);writer.writerow(['time_s']+['channel_'+str(c+1)+'_FS' for c in range(self.channels)])
                    for chunk in range(start,total,self.fs):
                        for i,row in enumerate(self.normalized(self.full_data[chunk:min(total,chunk+self.fs)])):writer.writerow([(chunk+i)/self.fs]+row.tolist())
                os.replace(temporary,path)
            finally:
                if temporary.exists():temporary.unlink()
        else:raise ValueError('不支持的导出格式')
        self.emit(event='saved',path=str(path))
    def command(self,p):
        self.reply_user=p.get('reply_user','')
        op=p.get('op')
        if op in ('native_metrics','native_finished'):
            record=p['record']
            with self.db() as db:
                db.execute('INSERT OR IGNORE INTO recordings(id,path,source,fs,channels,started,origin) VALUES(?,?,?,?,?,?,?)',
                    (record['id'],record['path'],record['source'],record['fs'],record['channels'],record['started'],'sensor'))
                if op=='native_metrics':
                    rows=p.get('channel_metrics') or [dict(channel=p['channel'],metrics=p['metrics'])]
                    for row in rows:db.execute('INSERT INTO measurements(recording_id,channel,time,metrics) VALUES(?,?,?,?)',
                               (record['id'],row['channel'],p['time'],json.dumps(row['metrics'],allow_nan=False)))
                else:
                    db.execute('UPDATE recordings SET duration=?,complete=?,calibration=? WHERE id=?',
                               (record['duration'],int(record.get('finalized',record.get('frames',0)>0)),json.dumps(p.get('options',{})),record['id']))
            self.selected=''
            if op=='native_finished':
                self.method=p.get('method',self.method);self.channel=p.get('channel',0);self.options=p.get('options',{})
                if record.get('finalized',record.get('frames',0)>0):self.load(record['path'],record['id'])
                self.records()
            self.trends()
            return
        if op=='native_features':
            try:
                from scipy.io import wavfile
                fs,data=wavfile.read(p['path']);data=data.astype(float)/32768
                if data.ndim>1:data=data[:,min(p.get('channel',0),data.shape[1]-1)]
                options=p.get('options',{});gain=float(options.get('gain',0) or 0);unit=options.get('unit','FS') if gain>0 else 'FS'
                plot=analyze(data*(gain if gain>0 else 1)+(float(options.get("offset",0)) if gain>0 else 0),fs,p['method'],options,unit)
                plot['method_id']=p['method']
            except Exception as error:
                plot=dict(title=NAMES[METHODS.index(p['method'])],kind='line',series=[],error=str(error),method_id=p['method'])
            self.emit(event='feature_reply',generation=p['generation'],method=p['method'],plot=plot)
            return
        if op=='devices':self.refresh(p.get('with_data',False))
        elif op=='start':self.start(p)
        elif op=='stop':self.stop()
        elif op=='configure':self.configure(p)
        elif op=='import':self.load(p['path'],owner=p.get('owner_id',''))
        elif op=='select':
            with self.db() as db:row=db.execute("SELECT path,calibration FROM recordings WHERE id=? AND complete=1 AND deleted_at=''",(p['id'],)).fetchone()
            if not row:raise ValueError('记录不存在或尚未保存完成')
            saved=json.loads(row[1])
            # A recording carries calibration, not the previous page's time scope.
            # Preserve the current full/range/window choice and display options.
            for key in ('gain','offset','unit','calibrationId','channelBindingId','pointId','operatingCondition'):
                if key in saved:self.options[key]=saved[key]
            self.load(row[0],p['id'])
        elif op=='export':self.export(p)
        elif op=='snapshot':self.compute()
        elif op=='clear_selection':
            self.selected='';self.recording_path='';self.full_data=None;self.buffer.clear();self.latest={};self.full_duration=0;self.trends()
        else:raise ValueError('未知命令')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--data',required=True);args=parser.parse_args()
    worker=Worker(args.data)
    def stop(*_):worker.stop();raise SystemExit(0)
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    try:
        for line in sys.stdin:
            payload={};error=''
            try:payload=json.loads(line);worker.command(payload)
            except Exception as e:error=str(e);worker.error(e)
            finally:
                if payload.get('op') in ('import','select'):worker.emit(event='record_operation',request=payload.get('request',''),reply_user=payload.get('reply_user',''),ok=not error,error=error)
    finally:worker.stop()
if __name__=='__main__':main()
