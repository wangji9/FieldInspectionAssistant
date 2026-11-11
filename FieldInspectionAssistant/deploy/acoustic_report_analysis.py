"""Offline, full-recording analysis for a report. Never opens a capture device."""
import argparse,hashlib,json,os,sys,time,wave
from pathlib import Path
import numpy as np
from scipy.io import wavfile
import signal_features as sf

def write(path,value):
 tmp=path.with_suffix(path.suffix+'.tmp');tmp.write_text(json.dumps(value,ensure_ascii=False,allow_nan=False),encoding='utf-8');tmp.replace(path);os.chmod(path,0o600)

def run(request,output):
 spec=json.loads(Path(request).read_text());record=spec['record'];path=Path(record['path']);before=path.stat()
 if before.st_size>512*1024*1024:raise ValueError('录音超过 512 MB，本版无法生成整段报告；未截取部分数据替代整段')
 fs,data=wavfile.read(path,mmap=True)
 if data.ndim==1:data=data[:,None]
 if data.ndim!=2 or not 1<=data.shape[1]<=8 or not len(data):raise ValueError('仅支持 1–8 通道的有效 PCM 录音')
 if int(fs)!=int(record['fs']) or data.shape[1]!=int(record['channels']):raise ValueError('录音头与数据库采样信息不一致')
 if data.dtype.kind not in 'iu':raise ValueError('报告仅支持整数 PCM，浮点 PCM 需先确认数字满量程定义')
 divisor=float(2**(data.dtype.itemsize*8-1));offset=divisor if data.dtype.kind=='u' else 0
 channels=[(np.asarray(data[:,i],dtype=np.float64)-offset)/divisor for i in range(data.shape[1])]
 measured=[sf.metrics(x,fs) for x in channels]
 sha=hashlib.sha256()
 with path.open('rb') as f:
  for block in iter(lambda:f.read(1024*1024),b''):sha.update(block)
 source={**record,'sha256':sha.hexdigest(),'bytes':before.st_size,'duration':len(data)/fs}
 charts=[];errors=[];original_metrics=sf.metrics
 for method,title in zip(sf.METHODS,sf.NAMES):
  print(json.dumps({'method':method,'title':title}),flush=True);panels=[];summaries=[];envelopes=[]
  for ch,x in enumerate(channels):
   sf.metrics=lambda *_args,**_kw:measured[ch]
   try:
    plot=sf.analyze(x,fs,method,{'c0':False,'normalize':False},'FS')
    # All samples enter the transform. Only the rendered grid/curve is bounded by signal_features.
    if plot.get('envelope'):envelopes.append({'channel':ch+1,'plot':plot.pop('envelope')})
    matrix=np.asarray(plot.get('matrix',[]));summary={'channel':ch+1,'status':'ok'}
    if matrix.size:summary.update(display_min=float(plot.get("colorMin",matrix.min())),display_max=float(plot.get("colorMax",matrix.max())),feature_frames=plot.get('feature_frames'),invalid_frames=plot.get('invalid_frames',0))
    elif plot.get('series'):
     points=np.asarray(plot['series'][0]['points']);summary.update(display_peak_x=float(points[np.argmax(points[:,1]),0]),display_peak_y=float(points[:,1].max()))
    panels.append({'channel':ch+1,'plot':plot});summaries.append(summary)
   except Exception as e:
    error=str(e)[:300]
    if method=='hilbert':envelopes.append({'channel':ch+1,'plot':{'title':'Hilbert 时域包络','error':error}})
    errors.append({'method':method,'channel':ch+1,'error':error});panels.append({'channel':ch+1,'plot':{'title':title,'error':error}});summaries.append({'channel':ch+1,'status':'unavailable','error':error})
  sf.metrics=original_metrics
  charts.append({'method':method,'title':title,'panels':panels,'summaries':summaries})
  if envelopes:charts.append({'method':'hilbert_envelope','title':'Hilbert 时域包络','panels':envelopes,'summaries':[{'channel':p['channel'],'status':'ok'} for p in envelopes]})
 # Per-second statistics scan the entire recording, including its final incomplete second.
 series=[]
 for ch,x in enumerate(channels):
  points=[]
  for i in range(0,len(x),fs):points.append([i/fs,float(np.sqrt(np.mean(x[i:i+fs]**2)))])
  series.append({'name':'通道 '+str(ch+1),'points':points})
 charts.append({'method':'recording_rms_trend','title':'整段录音逐秒 RMS 趋势','panels':[{'channel':'1–'+str(len(channels)),'plot':{'kind':'line','title':'整段 RMS 趋势','xLabel':'时间 / s','yLabel':'RMS / FS','series':series}}],'summaries':[{'channel':i+1,'status':'ok','minimum_rms':min(p[1] for p in series[i]['points']),'maximum_rms':max(p[1] for p in series[i]['points'])} for i in range(len(channels))]})
 after=path.stat()
 if before.st_size!=after.st_size or before.st_mtime_ns!=after.st_mtime_ns:raise ValueError('分析时原始录音发生变化，已停止报告')
 write(Path(output)/'plots.json',{'record':source,'sources':[source],'charts':charts,'channel_metrics':[{'channel':i+1,**m} for i,m in enumerate(measured)],'analysis_errors':errors,'duration':len(data)/fs,'sample_rate':int(fs),'channels':len(channels),'computed_at':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime()),'processing':'整段原始整数 PCM，所有通道；Hann FFT，Welch PSD，25 ms/10 ms 特征帧；仅显示曲线/矩阵做聚合，不截取分析时间范围；无物理标定，使用 FS。'})

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('request');p.add_argument('output');args=p.parse_args()
 try:run(args.request,args.output)
 except Exception as e:print(json.dumps({'error':str(e)},ensure_ascii=False),file=sys.stderr);sys.exit(1)
