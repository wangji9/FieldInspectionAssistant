"""Deterministic signal features. All inputs are measured PCM, never synthetic UI data."""
import importlib
import inspect
import numpy as np
from scipy import signal
from scipy.fft import dct
from scipy.linalg import solve_toeplitz

METHODS = ['time','fft','welch','hilbert','stft','mfb','mfcc','lfcc','lpc','lpcc',
           'waterfall','bfcc','gfcc','msrcc','pncc','psrcc','ngcc','pca']
NAMES = ['时域分析','FFT 频谱','Welch 功率谱密度','Hilbert 包络谱','STFT 时频图',
         'MFB 梅尔滤波器组','MFCC 梅尔倒谱','LFCC 线性倒谱','LPC 线性预测','LPCC 预测倒谱',
         '瀑布声谱','BFCC · Bark 倒谱','GFCC · 伽马倒谱','MSRCC · 谱根倒谱',
         'PNCC · 功率归一化倒谱','PSRCC · 相位倒谱','NGCC · 归一化伽马倒谱','PCA 降维对比']

def finite(a):
    return np.nan_to_num(np.asarray(a,dtype=float),nan=0.0,posinf=0.0,neginf=0.0)

def metrics(x,fs,band=None):
    x=finite(x); n=len(x)
    if n<2: raise ValueError('有效采样点不足')
    rms=float(np.sqrt(np.mean(x*x))); peak=float(np.max(np.abs(x)));mean_abs=float(np.mean(np.abs(x)));root_abs=float(np.mean(np.sqrt(np.abs(x))))
    centered=x-np.mean(x); std=float(np.std(centered))
    f,p=signal.welch(x,fs,nperseg=min(n,2048),window='hann',scaling='density')
    low,high=band or (0,fs/2)
    mask=(f>=low)&(f<=high); power=float(np.trapz(p[mask],f[mask])) if mask.sum()>1 else 0.0
    total=float(p.sum()); probs=p/(total+1e-30);full_power=float(np.trapz(p,f))
    return dict(rms=rms,peak=peak,crest=peak/rms if rms>1e-15 else None,
                kurtosis=float(np.mean((centered/std)**4)) if std>1e-15 else None,
                skewness=float(np.mean((centered/std)**3)) if std>1e-15 else None,
                stddev=std,shape_factor=rms/mean_abs if mean_abs>1e-15 else None,
                impulse_factor=peak/mean_abs if mean_abs>1e-15 else None,
                clearance_factor=peak/(root_abs*root_abs) if root_abs>1e-15 else None,
                band_energy=power,band_ratio=power/full_power if full_power>1e-30 else None,centroid=float(np.sum(f*p)/(total+1e-30)),
                flatness=float(np.exp(np.mean(np.log(p+1e-30)))/(np.mean(p)+1e-30)),
                entropy=float(-np.sum(probs*np.log2(probs+1e-30))),
                zcr=float(np.mean(np.diff(np.signbit(x)))*fs),
                clipped_pct=float(np.mean(np.abs(x)>=0.999)*100),samples=n,duration=n/fs)

def envelope_points(x,fs,max_points=800):
    # Min/max aggregation preserves narrow impulses that uniform decimation loses.
    stride=max(1,int(np.ceil(len(x)/(max_points/2))))
    points=[]
    for start in range(0,len(x),stride):
        block=x[start:start+stride]
        indices=sorted(set([int(np.argmin(block)),int(np.argmax(block))]))
        points.extend([[float((start+i)/fs),float(block[i])] for i in indices])
    return points

def feature(x,fs,method,options):
    from spafe.utils.preprocessing import SlidingWindow,framing,windowing
    frames,length=framing(x,fs=fs,win_len=.025,win_hop=.01)
    windows=windowing(frames,frame_len=length,win_type='hamming')
    valid=np.sum(windows*windows,axis=1)>1e-20
    nfft=2**int(np.ceil(np.log2(max(512,fs*.025))))
    if method in ('lpc','lpcc'):
        # Autocorrelation LPC. Relative diagonal loading stabilizes low-rank frames;
        # exact-silence frames stay invalid and are masked in the visualization.
        values=np.zeros((len(frames),13))
        for i,frame in enumerate(windows):
            if not valid[i]:continue
            r=signal.correlate(frame,frame,mode='full',method='fft')[len(frame)-1:len(frame)+12]
            original=r.copy();r[0]+=r[0]*1e-9
            a=np.r_[1.,solve_toeplitz((r[:-1],r[:-1]),-r[1:])]
            if method=='lpc':values[i]=a
            else:
                c=np.zeros(13);c[0]=np.log(max(1e-30,(original[0]+a[1:]@original[1:])/len(frame)))
                for n in range(1,13):c[n]=-a[n]-sum(k/n*c[k]*a[n-k] for k in range(1,n))
                values[i]=c
    elif method in ('msrcc','psrcc'):
        from spafe.fbanks.mel_fbanks import mel_filter_banks
        from spafe.features.mfcc import mel_spectrogram
        if method=='msrcc':
            energies,_=mel_spectrogram(x,fs=fs,pre_emph=False,window=SlidingWindow(.025,.01,'hamming'),nfilts=40,nfft=nfft)
            roots=np.maximum(energies,1e-20)**(-1/7)
        else:
            banks,_=mel_filter_banks(nfilts=40,nfft=nfft,fs=fs)
            phases=np.mod(np.angle(np.fft.rfft(windows,nfft),deg=True),360)
            phase_energy=phases@banks.T
            # Zero phase has no finite negative root; use the maximum finite root
            # as specified by spafe, excluding infinities from that maximum.
            roots=np.zeros_like(phase_energy);mask=phase_energy>1e-12
            roots[mask]=phase_energy[mask]**(-1/7)
            roots[~mask]=roots[mask].max() if mask.any() else 0
        values=dct(roots,type=2,axis=1,norm='ortho')[:,:13]
    else:
        values=_spafe_feature(x,fs,method,nfft)
    values=np.asarray(values,dtype=float)
    valid=valid[:len(values)]&np.isfinite(values).all(axis=1)
    if not valid.any():raise ValueError('当前片段为静音或无有效特征帧，请增加采样时长')
    if method!='lpc' and not options.get('c0',False):values=values[:,1:]
    if options.get('normalize',False):values=(values-values[valid].mean(axis=0))/(values[valid].std(axis=0)+1e-12)
    values[~valid]=0  # Masked cells are rendered grey, never presented as zero measurements.
    return values,valid

def _spafe_feature(x,fs,method,nfft):
    module=importlib.import_module('spafe.features.'+('lpc' if method in ('lpc','lpcc') else method))
    fn=getattr(module,method)
    kw=dict(fs=fs,num_ceps=13,order=13,pre_emph=False,nfilts=40,nfft=nfft,
            low_freq=0,high_freq=fs/2,normalize=None,lifter=0,use_energy=False)
    signature=inspect.signature(fn)
    if 'window' in signature.parameters:
        from spafe.utils.preprocessing import SlidingWindow
        kw['window']=SlidingWindow(win_len=.025,win_hop=.01,win_type='hamming')
    else: kw.update(win_len=.025,win_hop=.01,win_type='hamming')
    values=fn(x,**{k:v for k,v in kw.items() if k in signature.parameters})
    if values.ndim!=2 or not values.size:raise ValueError('特征矩阵为空，请增加采样时长')
    return values

def analyze(samples,fs,method='time',options=None,unit='FS'):
    if method not in METHODS:raise ValueError('不支持的分析方法')
    options=options or {}; x=finite(samples)
    if len(x)<max(64,int(fs*.04)):raise ValueError('至少需要 40 ms 的真实采样')
    title=NAMES[METHODS.index(method)]
    result=dict(kind='line',title=title,method_id=method,xLabel='时间 / s',yLabel=unit,
                source_samples=len(x),sample_rate=fs,metrics=metrics(x,fs),series=[])
    if method=='time':result['series']=[dict(name='原始波形',points=envelope_points(x,fs))]
    elif method in ('fft','welch','hilbert'):
        result.update(xLabel='频率 / Hz',xRange=[0,fs/2])
        if method=='fft':
            n=len(x);w=signal.windows.hann(n,sym=False)
            amplitude=2*np.abs(np.fft.rfft((x-np.mean(x))*w))/w.sum()
            amplitude[0]/=2
            if n%2==0:amplitude[-1]/=2
            f=np.fft.rfftfreq(n,1/fs);y=20*np.log10(np.maximum(amplitude,1e-12))
            result['yLabel']='幅值 / dB re 1 '+unit
            result.update(xRange=[0,20000],xLabel='频率 / kHz',xDisplayScale=.001,nyquist_hz=fs/2)
        else:
            value=x
            if method=='hilbert':
                value=np.abs(signal.hilbert(x-np.mean(x)))
                value-=value.mean()
            f,p=signal.welch(value,fs,window='hann',nperseg=min(len(x),4096),scaling='density')
            y=10*np.log10(np.maximum(p,1e-24));result['yLabel']='PSD / dB re 1 '+unit+'²/Hz'
        # Peak-preserving frequency bin aggregation, including high frequency bins.
        stride=max(1,int(np.ceil(len(y)/1000)))
        points=[[float(f[i+np.argmax(y[i:i+stride])]),float(np.max(y[i:i+stride]))] for i in range(0,len(y),stride)]
        result['series']=[dict(name=title,points=points)]
        if method=='hilbert':
            result['envelope']=dict(kind='line',title='Hilbert 包络',xLabel='时间 / s',yLabel=unit,
                                    series=[dict(name='包络',points=envelope_points(np.abs(signal.hilbert(x)),fs))])
    elif method in ('stft','mfb','waterfall'):
        n=min(len(x),max(64,int(fs*.025)));hop=max(1,int(fs*.01));nfft=2**int(np.ceil(np.log2(n)))
        f,t,z=signal.stft(x,fs,window='hamming',nperseg=n,noverlap=max(0,n-hop),nfft=nfft,boundary=None,padded=False)
        power=np.abs(z)**2
        if method=='mfb':
            from spafe.fbanks.mel_fbanks import mel_filter_banks
            banks=mel_filter_banks(nfilts=40,nfft=nfft,fs=fs)
            if isinstance(banks,tuple):banks=banks[0]
            matrix=10*np.log10(np.maximum(banks@power,1e-24));ylabel='梅尔滤波器编号';yrange=[1,40]
        else:
            matrix=10*np.log10(np.maximum(power,1e-24));ylabel='频率 / Hz';yrange=[0,fs/2]
        if method=='waterfall':
            rows=np.linspace(0,matrix.shape[1]-1,min(24,matrix.shape[1])).astype(int)
            ix=np.linspace(0,len(f)-1,min(320,len(f))).astype(int)
            result.update(kind='waterfall',xLabel='频率 / Hz',yLabel='时间 / s',zLabel='功率 / dB re 1 '+unit+'²',
                          slices=[dict(time=float(t[j]),points=np.column_stack((f[ix],matrix[ix,j])).tolist()) for j in rows])
        else:
            # Cap display grid only. Full feature matrices remain available to export.
            yi=np.linspace(0,matrix.shape[0]-1,min(160,matrix.shape[0])).astype(int)
            ti=np.linspace(0,matrix.shape[1]-1,min(160,matrix.shape[1])).astype(int)
            result.update(kind='heatmap',matrix=matrix[np.ix_(yi,ti)].tolist(),xRange=[float(t[0]),float(t[-1])],
                          yRange=yrange,yLabel=ylabel,colorLabel='功率 / dB re 1 '+unit+'²')
    else:
        values,valid=feature(x,fs,'mfcc' if method=='pca' else method,options)
        if method=='pca':
            values=values[valid]
            if len(values)<3:raise ValueError('PCA 至少需要三个有效特征帧')
            centered=(values-values.mean(axis=0))/(values.std(axis=0)+1e-12)
            u,s,v=np.linalg.svd(centered,full_matrices=False);scores=centered@v[:2].T
            explained=(s*s)/(np.sum(s*s)+1e-30)
            result.update(kind='scatter',xLabel='PC1 ('+format(explained[0]*100,'.1f')+'%)',
                          yLabel='PC2 ('+format(explained[1]*100,'.1f')+'%)',
                          series=[dict(name='真实采样帧 / MFCC 特征',points=scores.tolist())],explained_variance=explained.tolist())
        else:
            # Long recordings can have tens of thousands of frames. Render a bounded
            # full-duration grid, retaining valid-frame means rather than stale images.
            groups=np.array_split(np.arange(len(values)),min(400,len(values)))
            shown=np.array([values[g][valid[g]].mean(axis=0) if valid[g].any() else np.zeros(values.shape[1]) for g in groups])
            shown_valid=np.array([valid[g].any() for g in groups])
            result.update(kind='heatmap',matrix=shown.T.tolist(),xRange=[.0125,.0125+(len(values)-1)*.01],
                          yRange=[0 if options.get('c0',False) or method=='lpc' else 1,values.shape[1]-1 if options.get('c0',False) or method=='lpc' else values.shape[1]],
                          yLabel='系数编号',colorLabel='标准化系数' if options.get('normalize',False) else '特征系数',
                          validFrames=shown_valid.tolist(),invalid_frames=int((~valid).sum()),feature_frames=len(values),display_aggregation='valid frame mean')
    if result['kind']=='heatmap':
        grid=np.asarray(result['matrix']);mask=result.get('validFrames')
        valid_grid=grid[:,np.asarray(mask,dtype=bool)] if mask is not None else grid
        result['colorMin']=float(valid_grid.min());result['colorMax']=float(valid_grid.max())
    # Refuse non-finite JSON at the boundary instead of making a misleading chart.
    return result
