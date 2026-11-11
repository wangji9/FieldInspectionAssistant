#include "NativeSignalPipeline.h"
#include <alsa/asoundlib.h>
#include <fftw3.h>
#include <QtEndian>
#include <complex>
#include <map>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstring>
#include <limits>
#include <sys/file.h>
#include <poll.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <QStorageInfo>

namespace {
constexpr double pi=3.14159265358979323846;
QByteArray wavHeader(quint32 size,int fs,int channels){
    QByteArray h;QDataStream out(&h,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF",4);out<<quint32(size+36);out.writeRawData("WAVEfmt ",8);out<<quint32(16)<<quint16(1)<<quint16(channels)<<quint32(fs)<<quint32(fs*channels*2)<<quint16(channels*2)<<quint16(16);out.writeRawData("data",4);out<<size;return h;
}
constexpr qint64 MaxWavData=std::numeric_limits<quint32>::max()-36;

qint64 checkpointWav(QFile &file,int rate,int channels,bool final) {
    const qint64 frameBytes=channels*2;
    const qint64 payload=file.size()-44;
    if (payload<0 || payload>MaxWavData)
        return -1;
    const qint64 complete=payload-payload%frameBytes;
    if (final && payload!=complete && !file.resize(44+complete))
        return -1;
    const auto header=wavHeader(quint32(complete),rate,channels);
    if (!file.seek(0) || file.write(header)!=header.size() || !file.flush()
        || (final && ::fdatasync(file.handle())!=0) || !file.seek(file.size()))
        return -1;
    return complete/frameBytes;
}
struct Transform {
    int n;double *input;fftw_complex *output;fftw_plan plan;
    explicit Transform(int size):n(size){input=fftw_alloc_real(n);output=fftw_alloc_complex(n/2+1);plan=fftw_plan_dft_r2c_1d(n,input,output,FFTW_ESTIMATE);}
    ~Transform(){fftw_destroy_plan(plan);fftw_free(input);fftw_free(output);}
};
// Plans and work buffers belong to this DSP thread, never to the GUI/capture thread.
thread_local std::map<int,std::unique_ptr<Transform>> transforms;
Transform &fft(const QVector<double> &x,int n){
    if(!transforms.count(n))transforms[n]=std::make_unique<Transform>(n);auto &t=*transforms[n];
    std::fill(t.input,t.input+n,0);std::copy_n(x.begin(),qMin(n,int(x.size())),t.input);fftw_execute(t.plan);return t;
}
int nextPower(int n){int p=1;while(p<n)p*=2;return p;}
struct HannWindow{QVector<double> values;double sum=0,squareSum=0;};
thread_local std::map<int,HannWindow> hannWindows;
const HannWindow &hann(int n){auto found=hannWindows.find(n);if(found!=hannWindows.end())return found->second;if(hannWindows.size()>=4)hannWindows.erase(hannWindows.begin());HannWindow w;w.values.resize(n);for(int i=0;i<n;i++){double value=.5-.5*std::cos(2*pi*i/n);w.values[i]=value;w.sum+=value;w.squareSum+=value*value;}return hannWindows.emplace(n,std::move(w)).first->second;}
Transform &windowedFft(const QVector<double>&x,int n,double &sum){
    if(!transforms.count(n))transforms[n]=std::make_unique<Transform>(n);auto &t=*transforms[n];
    const auto &window=hann(x.size());sum=window.sum;double mean=std::accumulate(x.begin(),x.end(),0.)/x.size();
    for(int i=0;i<x.size();++i)t.input[i]=(x[i]-mean)*window.values[i];
    std::fill(t.input+x.size(),t.input+n,0);fftw_execute(t.plan);return t;
}
QVector<double> powers(const QVector<double>&x,int n){auto&t=fft(x,n);QVector<double> out(n/2+1);for(int i=0;i<out.size();i++)out[i]=t.output[i][0]*t.output[i][0]+t.output[i][1]*t.output[i][1];return out;}
QPair<QVector<double>,QVector<double>> welch(const QVector<double>&x,int fs){
    int n=qMin(2048,int(x.size()));if(n<2)return {};QVector<double> p(n/2+1,0);const auto &window=hann(n);const auto &w=window.values;double norm=window.squareSum;
    int count=0;for(int start=0;start+n<=x.size();start+=qMax(1,n/2)){
        double mean=std::accumulate(x.begin()+start,x.begin()+start+n,0.)/n;QVector<double> frame(n);
        for(int i=0;i<n;i++)frame[i]=(x[start+i]-mean)*w[i];auto v=powers(frame,n);
        for(int i=0;i<v.size();i++)p[i]+=v[i]/(fs*norm)*(i==0||(n%2==0&&i==n/2)?1:2);count++;
    }
    QVector<double> f(p.size());for(int i=0;i<p.size();i++){p[i]/=qMax(1,count);f[i]=double(i)*fs/n;}return {f,p};
}
QVariantList points(const QVector<double>&x,const QVector<double>&y,int maximum=800){
    QVariantList result;int stride=qMax(1,int(std::ceil(double(y.size())/maximum)));
    for(int i=0;i<y.size();i+=stride){int peak=i;for(int j=i+1;j<qMin(i+stride,int(y.size()));j++)if(y[j]>y[peak])peak=j;result.append(QVariant(QVariantList{x[peak],y[peak]}));}return result;
}
QVariantList waveform(const QVector<double>&x,int fs){
    QVariantList result;int stride=qMax(1,int(std::ceil(double(x.size())/320)));
    for(int start=0;start<x.size();start+=stride){int minimum=start,maximum=start;for(int i=start+1;i<qMin(start+stride,int(x.size()));i++){if(x[i]<x[minimum])minimum=i;if(x[i]>x[maximum])maximum=i;}
        int a=qMin(minimum,maximum),b=qMax(minimum,maximum);result.append(QVariant(QVariantList{double(a)/fs,x[a]}));if(a!=b)result.append(QVariant(QVariantList{double(b)/fs,x[b]}));}return result;
}
QVariantMap statistics(const QVector<double>&x,int fs,const QPair<QVector<double>,QVector<double>>&spectrum){
    double sum=0,square=0,peak=0,clipped=0,zcr=0,absolute=0,rootAbsolute=0;for(int i=0;i<x.size();i++){auto v=x[i];sum+=v;square+=v*v;absolute+=std::abs(v);rootAbsolute+=std::sqrt(std::abs(v));peak=qMax(peak,std::abs(v));if(std::abs(v)>=.999)clipped++;if(i&&std::signbit(v)!=std::signbit(x[i-1]))zcr++;}absolute/=x.size();rootAbsolute/=x.size();
    double mean=sum/x.size(),variance=0,m3=0,m4=0;for(auto v:x){auto d=v-mean;variance+=d*d;m3+=d*d*d;m4+=d*d*d*d;}variance/=x.size();double rms=std::sqrt(square/x.size());
    double total=0,centroid=0,entropy=0,logMean=0,area=0;const auto& f=spectrum.first;const auto& p=spectrum.second;
    for(int i=0;i<p.size();i++){total+=p[i];centroid+=p[i]*f[i];logMean+=std::log(p[i]+1e-30);if(i)area+=(p[i]+p[i-1])*.5*(f[i]-f[i-1]);}
    for(auto value:p){double probability=value/(total+1e-30);entropy-=probability*std::log2(probability+1e-30);}
    return {{"rms",rms},{"peak",peak},{"crest",rms>1e-15?QVariant(peak/rms):QVariant()},
        {"kurtosis",variance>1e-30?QVariant(m4/x.size()/(variance*variance)):QVariant()},
        {"skewness",variance>1e-30?QVariant(m3/x.size()/std::pow(variance,1.5)):QVariant()},
        {"stddev",std::sqrt(variance)},{"shape_factor",absolute>1e-15?QVariant(rms/absolute):QVariant()},
        {"impulse_factor",absolute>1e-15?QVariant(peak/absolute):QVariant()},{"clearance_factor",rootAbsolute>1e-15?QVariant(peak/(rootAbsolute*rootAbsolute)):QVariant()},
        {"band_energy",area},{"band_ratio",area>1e-30?QVariant(1.):QVariant()},{"centroid",centroid/(total+1e-30)},{"flatness",p.isEmpty()?0:std::exp(logMean/p.size())/(total/qMax(1,int(p.size()))+1e-30)},
        {"entropy",entropy},{"zcr",zcr/qMax(1,int(x.size())-1)*fs},{"clipped_pct",clipped/x.size()*100},{"samples",x.size()},{"duration",double(x.size())/fs}};
}
QString title(const QString &method){static const QMap<QString,QString> names{{"time","时域分析"},{"fft","FFT 频谱"},{"welch","Welch 功率谱密度"},{"hilbert","Hilbert 包络谱"},{"stft","STFT 时频图"},{"waterfall","瀑布声谱"}};return names.value(method,QStringLiteral("正在异步计算特征…"));}
}

bool writePcmWav(const QString&path,const QByteArray&pcm,int fs,int channels){QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))return false;return f.write(wavHeader(pcm.size(),fs,channels))==44&&f.write(pcm)==pcm.size()&&f.commit();}
qint64 recoverCapturedWav(const QString &path,int rate,int channels) {
    if (rate<4000 || rate>192000 || channels<1 || channels>8)
        return -1;
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite|QIODevice::Unbuffered) || flock(file.handle(),LOCK_EX|LOCK_NB)!=0)
        return -1;
    auto header=file.read(44);
    const auto expected=wavHeader(0,rate,channels);
    if (header.size()!=44 || header.left(4)!=expected.left(4) ||
        header.mid(8,32)!=expected.mid(8,32) || file.size()<=44)
        return -1;
    return checkpointWav(file,rate,channels,true);
}
NativeAudioCapture::NativeAudioCapture(const QVariantMap&device,const QString&folder,QObject*parent):QThread(parent),device_(device),rate_(device["rate"].toInt()),channels_(device["channels"].toInt()){
    id_=QUuid::createUuid().toString(QUuid::WithoutBraces);QDir().mkpath(folder);path_=folder+"/"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")+"_"+id_.left(8)+".wav";ring_.resize(rate_*channels_*6);
}
NativeAudioCapture::~NativeAudioCapture(){requestInterruption();wait(3000);}
QVariantMap NativeAudioCapture::info()const{QMutexLocker lock(&mutex_);return {{"id",id_},{"path",path_},{"source",device_["name"]},{"fs",rate_},{"channels",channels_},{"started",started_},{"duration",double(frames_.load())/rate_},{"frames",QVariant::fromValue(frames_.load())},{"has_signal",hasSignal_.load()},{"finalized",finalized_.load()},{"xruns",QVariant::fromValue(xruns_.load())},{"packets",QVariant::fromValue(packets_.load())},{"checksum_errors",QVariant::fromValue(checksumErrors_.load())},{"transport",device_.value("transport","alsa")},{"error",error_},{"origin","sensor"}};}
QByteArray NativeAudioCapture::latestPcm(double seconds)const{
    QMutexLocker lock(&mutex_);quint64 size=qMin(available_,quint64(rate_*channels_*seconds));size-=size%channels_;QByteArray result(size*2,Qt::Uninitialized);
    quint64 start=(write_+ring_.size()-size)%ring_.size(),first=qMin(size,quint64(ring_.size())-start);std::memcpy(result.data(),ring_.constData()+start,first*2);if(first<size)std::memcpy(result.data()+first*2,ring_.constData(),(size-first)*2);return result;
}
QVector<double> NativeAudioCapture::latest(double seconds,int channel)const{auto pcm=latestPcm(seconds);QVector<double> out(pcm.size()/2/channels_);const auto *data=reinterpret_cast<const qint16*>(pcm.constData());channel=qBound(0,channel,channels_-1);for(int i=0;i<out.size();i++)out[i]=qFromLittleEndian<qint16>(data+i*channels_+channel)/32768.;return out;}
void NativeAudioCapture::run(){
    if(device_.value("transport").toString()=="six_serial"){runSerial();return;}
    auto fail=[this](const QString&e){QMutexLocker lock(&mutex_);error_=e;};
    if(rate_<4000||rate_>192000||channels_<1||channels_>8){fail("无效采集格式");return;}
    if(device_["id"].toString().contains("ListenGo")){
        QProcess wake;wake.start("/usr/bin/python3",{"-c","import sys;sys.path.insert(0,'/userdata/ai-workbench');from six_mic_control import wake_microphone;wake_microphone()"});if(!wake.waitForFinished(1500))wake.kill();
    }
    snd_config_t *config=nullptr;snd_pcm_t *pcm=nullptr;
    QString source=device_["id"].toString();if(source.contains('"')||source.contains('\\')||source.contains('\n')){fail("无效采集设备名称");return;}
    auto definition=QString("pcm.fieldops_capture { type pulse device \"%1\" }\n").arg(source).toUtf8();
    int code=snd_config_load_string(&config,definition.constData(),definition.size());
    if(code>=0)code=snd_pcm_open_lconf(&pcm,"fieldops_capture",SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK,config);
    if(config)snd_config_delete(config);
    if(code<0){fail(QString::fromLocal8Bit(snd_strerror(code)));return;}
    code=snd_pcm_set_params(pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,channels_,rate_,0,80000);
    if(code<0){fail(QString::fromLocal8Bit(snd_strerror(code)));snd_pcm_close(pcm);return;}
    QFile file(path_);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly|QIODevice::Unbuffered)
        || flock(file.handle(),LOCK_EX|LOCK_NB)!=0 || file.write(wavHeader(0,rate_,channels_))!=44){
        fail("无法创建原始录音文件");snd_pcm_close(pcm);return;
    }
    {QMutexLocker lock(&mutex_);started_=QDateTime::currentMSecsSinceEpoch()/1000.;}
    QVector<qint16> block(qMax(128,rate_/100)*channels_);QElapsedTimer timer,storage;timer.start();storage.start();
    while(!isInterruptionRequested()&&timer.elapsed()<300000){
        auto count=snd_pcm_readi(pcm,block.data(),block.size()/channels_);
        if(count==-EAGAIN){snd_pcm_wait(pcm,20);continue;}
        if(count<0){xruns_++;if(snd_pcm_recover(pcm,count,1)<0){fail(QString::fromLocal8Bit(snd_strerror(count)));break;}continue;}
        if(!count)continue;
        if(!appendPcm(file,reinterpret_cast<const char*>(block.constData()),count*channels_*2))break;
        if(storage.elapsed()>5000){storage.restart();if(QStorageInfo(path_).bytesAvailable()<100*1024*1024){fail("剩余空间不足 100 MB");break;}if(checkpointWav(file,rate_,channels_,false)<0){fail("无法更新录音文件头");break;}}
    }
    snd_pcm_drop(pcm);snd_pcm_close(pcm);
    const auto saved=checkpointWav(file,rate_,channels_,true);
    finalized_=saved>0;
    if(saved<0)fail("录音未能安全保存");
}

bool NativeAudioCapture::appendPcm(QFile&file,const char *data,int bytes){
    const int samples=bytes/2;if(bytes%(channels_*2)||samples>ring_.size())return false;
    if(file.pos()-44+bytes>MaxWavData){QMutexLocker lock(&mutex_);error_="已达到单个 WAV 文件的容量上限";return false;}
    if(file.write(data,bytes)!=bytes){QMutexLocker lock(&mutex_);error_="录音写入失败";return false;}
    if(!hasSignal_ && std::any_of(data,data+bytes,[](char value){return value!=0;}))hasSignal_=true;
    {QMutexLocker lock(&mutex_);quint64 first=qMin(quint64(samples),quint64(ring_.size())-write_);std::memcpy(ring_.data()+write_,data,first*2);if(first<quint64(samples))std::memcpy(ring_.data(),data+first*2,(samples-first)*2);write_=(write_+samples)%ring_.size();available_=qMin(quint64(ring_.size()),available_+samples);}
    frames_+=bytes/(channels_*2);sequence_++;return true;
}
void NativeAudioCapture::runSerial(){
    auto fail=[this](const QString&e){QMutexLocker lock(&mutex_);error_=e;};
    struct Port{int fd=-1;~Port(){if(fd>=0)::close(fd);}}control,audio;
    auto openPort=[](const QString&path)->int{
        int fd=::open(path.toLocal8Bit().constData(),O_RDWR|O_NOCTTY|O_NONBLOCK);if(fd<0)return -1;
        if(flock(fd,LOCK_EX|LOCK_NB)<0){::close(fd);return -1;}termios settings{};
        if(tcgetattr(fd,&settings)<0){::close(fd);return -1;}cfmakeraw(&settings);cfsetispeed(&settings,B115200);cfsetospeed(&settings,B115200);settings.c_cflag|=CLOCAL|CREAD;settings.c_cflag&=~CRTSCTS;settings.c_cc[VMIN]=0;settings.c_cc[VTIME]=0;
        if(tcsetattr(fd,TCSANOW,&settings)<0){::close(fd);return -1;}tcflush(fd,TCIFLUSH);return fd;
    };
    control.fd=openPort(device_.value("control","/dev/lg_speech_serial").toString());audio.fd=openPort(device_.value("audio","/dev/lg_speech_uac").toString());
    if(control.fd<0||audio.fd<0){fail("六麦原始串口不可用或已被占用，请停止其他串口采集程序");return;}
    auto packet=[](int type,const QByteArray&payload,quint16 identity=1){QByteArray out;out.append(char(0xA5));out.append(char(1));out.append(char(type));quint16 size=qToLittleEndian<quint16>(payload.size()),id=qToLittleEndian(identity);out.append(reinterpret_cast<const char*>(&size),2);out.append(reinterpret_cast<const char*>(&id),2);out.append(payload);unsigned sum=0;for(auto c:out)sum+=uchar(c);out.append(char(-sum));return out;};
    auto send=[&](const QByteArray&data){int offset=0;QElapsedTimer timeout;timeout.start();while(offset<data.size()&&timeout.elapsed()<500){auto n=::write(control.fd,data.constData()+offset,data.size()-offset);if(n>0)offset+=n;else{pollfd p{control.fd,POLLOUT,0};poll(&p,1,10);}}return offset==data.size();};
    QFile file(path_);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly|QIODevice::Unbuffered)
        || flock(file.handle(),LOCK_EX|LOCK_NB)!=0 || file.write(wavHeader(0,rate_,channels_))!=44){
        fail("无法创建六麦原始录音文件");return;
    }
    {QMutexLocker lock(&mutex_);started_=QDateTime::currentMSecsSinceEpoch()/1000.;}
    auto enable=packet(5,"{\"type\":\"get_original_audio\",\"content\":{\"audio\":1}}");
    send(packet(5,"{\"type\":\"manual_wakeup\",\"content\":{\"beam\":0}}",0));
    if(!send(enable)){fail("六麦原始数据开启命令发送失败");return;}
    QByteArray controlBytes,audioBytes,payloadTail;QElapsedTimer storage,lastAudio;storage.start();lastAudio.start();char block[65536];
    auto consume=[&](QByteArray&bytes,bool raw){
        while(bytes.size()>=8){
            auto sync=bytes.indexOf(QByteArray::fromHex("a501"));if(sync<0){bytes=bytes.right(1);break;}if(sync)bytes.remove(0,sync);if(bytes.size()<8)break;
            const auto *header=reinterpret_cast<const uchar*>(bytes.constData());int size=qFromLittleEndian<quint16>(header+3);int total=size+8;if(bytes.size()<total)break;
            unsigned sum=0;for(int i=0;i<total;i++)sum+=uchar(bytes[i]);if(sum&255){checksumErrors_++;bytes.remove(0,1);continue;}
            auto type=uchar(bytes[2]);auto identity=qFromLittleEndian<quint16>(header+5);auto payload=bytes.mid(7,size);bytes.remove(0,total);
            if(!raw&&type==1){send(packet(255,QByteArray::fromHex("a5000000"),identity));send(enable);}
            if(raw&&type==6){
                packets_++;lastAudio.restart();payloadTail+=payload;if(payloadTail.size()<16)continue;int frames=payloadTail.size()/16;QByteArray pcm;pcm.reserve(frames*12);
                for(int i=0;i<frames;i++)pcm.append(payloadTail.constData()+i*16,12);payloadTail.remove(0,frames*16);
                if(!pcm.isEmpty()&&!appendPcm(file,pcm.constData(),pcm.size()))requestInterruption();
            }
        }
        if(bytes.size()>256*1024){checksumErrors_++;bytes=bytes.right(7);}
    };
    while(!isInterruptionRequested()){
        pollfd ports[2]={{control.fd,POLLIN,0},{audio.fd,POLLIN,0}};int ready=poll(ports,2,30);if(ready<0&&errno!=EINTR){fail("六麦串口读取失败");break;}
        for(int i=0;i<2;i++){
            if(ports[i].revents&(POLLERR|POLLHUP|POLLNVAL)){fail("六麦串口已断开");requestInterruption();break;}
            if(!(ports[i].revents&POLLIN))continue;auto &bytes=i?audioBytes:controlBytes;
            for(int reads=0;reads<32;reads++){auto n=::read(ports[i].fd,block,sizeof(block));if(n<=0)break;bytes.append(block,n);consume(bytes,i==1);}
        }
        if(lastAudio.elapsed()>4000){fail("未收到有效六通道原始数据，请检查串口连接和模块固件");break;}
        if(storage.elapsed()>5000){storage.restart();if(QStorageInfo(path_).bytesAvailable()<100*1024*1024){fail("剩余空间不足 100 MB");break;}if(checkpointWav(file,rate_,channels_,false)<0){fail("无法更新录音文件头");break;}}
    }
    send(packet(5,"{\"type\":\"get_original_audio\",\"content\":{\"audio\":0}}",2));
    const auto saved=checkpointWav(file,rate_,channels_,true);
    finalized_=saved>0;
    if(saved<0)fail("录音未能安全保存");
}

bool NativeSignalAnalyzer::nativeMethod(const QString&m){return QStringList{"time","fft","welch","stft","waterfall","hilbert"}.contains(m);}
NativeSignalAnalyzer::NativeSignalAnalyzer(NativeAudioCapture*c,const QString&folder,QObject*p):QThread(p),capture_(c),folder_(folder){}
NativeSignalAnalyzer::~NativeSignalAnalyzer(){requestInterruption();wait(4000);}
void NativeSignalAnalyzer::configure(const QString&m,int channel,double seconds,const QVariantMap&o){QMutexLocker lock(&mutex_);method_=m;channel_=channel;seconds_=seconds;options_=o;}
QVariantMap NativeSignalAnalyzer::takeSnapshot(){QMutexLocker lock(&mutex_);if(taken_==revision_)return {};taken_=revision_;return latest_;}
void NativeSignalAnalyzer::run(){
    quint64 previous=0;QString previousMethod;QElapsedTimer advancedClock,metricClock;advancedClock.start();metricClock.start();QVariantList channelMetrics;QVariantMap calibratedMetrics;double lastGain=-1,lastOffset=0;int lastCalibrationChannel=-1;QElapsedTimer calibrationClock;calibrationClock.start();double metricSeconds=-1;int metricFrames=0;
    while(!isInterruptionRequested()){
        QElapsedTimer cost;cost.start();QString method;int channel;double seconds;QVariantMap options;
        {QMutexLocker lock(&mutex_);method=method_;channel=channel_;seconds=seconds_;options=options_;}
        channel=qBound(0,channel,capture_->channels()-1);
        auto sequence=capture_->sequence();if(!sequence){msleep(20);continue;}
        bool native=nativeMethod(method);if((sequence==previous&&method==previousMethod)||(!native&&advancedClock.elapsed()<500)){msleep(3);continue;}
        previous=sequence;previousMethod=method;if(!native)advancedClock.restart();
        auto raw=capture_->latest(seconds,channel);if(raw.size()<64){msleep(20);continue;}auto x=raw;
        double gain=options.value("gain").toDouble();QString unit=gain>0?options.value("unit","FS").toString():"FS";double offset=gain>0?options.value("offset").toDouble():0;if(gain>0)for(auto&v:x)v=v*gain+offset;
        const bool paired=options.value("pairedPlots").toBool();
        QVariantMap plot;if(!paired)plot=nativeSignalPlot(x,capture_->rate(),native?method:"time",unit,false);auto info=capture_->info();QVariantList channels;
        QVariantMap timePlot,frequencyPlot;
        if(options.value("pairedPlots").toBool()){
            QVariantList timeSeries,frequencySeries;const int count=options.value("overlayChannels").toBool()&&gain<=0?capture_->channels():1;
            const auto alignedPcm=capture_->latestPcm(seconds);const auto *samples=reinterpret_cast<const qint16*>(alignedPcm.constData());const int frames=alignedPcm.size()/2/capture_->channels();
            for(int i=0;i<count;i++){
                int selected=count==1?channel:i;QVector<double> values(frames);for(int frame=0;frame<frames;frame++)values[frame]=samples[frame*capture_->channels()+selected]/32768.*(gain>0?gain:1)+offset;
                auto t=nativeSignalPlot(values,capture_->rate(),"time",unit,false),f=nativeSignalPlot(values,capture_->rate(),"fft",unit,false);if(selected==channel)plot=method=="fft"?f:t;
                for(auto v:t["series"].toList()){auto s=v.toMap();s["name"]=QStringLiteral("通道 %1").arg(selected+1);timeSeries.append(s);}for(auto v:f["series"].toList()){auto s=v.toMap();s["name"]=QStringLiteral("通道 %1").arg(selected+1);frequencySeries.append(s);}
                if(i==0){timePlot=t;frequencyPlot=f;}
            }
            timePlot["series"]=timeSeries;frequencyPlot["series"]=frequencySeries;
        }
        for(int i=0;options.value("previewChannels").toBool()&&i<qMin(8,capture_->channels());i++){
            if(i==channel){auto rawPlot=nativeSignalPlot(raw,capture_->rate(),"time","FS",false);channels.append(QVariantMap{{"channel",i},{"metrics",rawPlot["metrics"]},{"plot",rawPlot}});}
            else{auto values=capture_->latest(seconds,i);auto p=nativeSignalPlot(values,capture_->rate(),"time","FS",false);channels.append(QVariantMap{{"channel",i},{"metrics",p["metrics"]},{"plot",p}});}
        }
        if(channelMetrics.isEmpty()||metricClock.elapsed()>=1000||seconds!=metricSeconds||(metricFrames<int(seconds*capture_->rate())&&raw.size()>=int(seconds*capture_->rate()))){
            metricClock.restart();metricSeconds=seconds;metricFrames=raw.size();channelMetrics.clear();
            for(int i=0;i<capture_->channels();i++){auto v=capture_->latest(seconds,i);if(v.size()>1)channelMetrics.append(QVariantMap{{"channel",i},{"metrics",statistics(v,capture_->rate(),welch(v,capture_->rate()))}});}
        }
        QVariantMap stat;for(auto v:channelMetrics){auto item=v.toMap();if(item["channel"].toInt()==channel)stat=item["metrics"].toMap();}
        if(gain>0){for(auto key:{"rms","peak","stddev"})stat[key]=stat[key].toDouble()*gain;stat["band_energy"]=stat["band_energy"].toDouble()*gain*gain;}if(gain>0&&offset!=0){if(calibratedMetrics.isEmpty()||calibrationClock.elapsed()>=1000||gain!=lastGain||offset!=lastOffset||channel!=lastCalibrationChannel){calibratedMetrics=statistics(x,capture_->rate(),welch(x,capture_->rate()));lastGain=gain;lastOffset=offset;lastCalibrationChannel=channel;calibrationClock.restart();}stat=calibratedMetrics;}plot["metrics"]=stat;
        for(int i=0;i<channels.size();i++){auto row=channels[i].toMap();for(auto value:channelMetrics)if(value.toMap()["channel"]==row["channel"])row["metrics"]=value.toMap()["metrics"];channels[i]=row;}
        QVariantMap snapshot{{"plot",plot},{"calibration",options},{"capture_time",timePlot},{"capture_frequency",frequencyPlot},{"rate",capture_->rate()},{"channels",capture_->channels()},{"channel",channel},{"unit",unit},{"time",QDateTime::currentMSecsSinceEpoch()/1000.},
            {"recording",info["id"]},{"source","sensor"},{"channels_data",channels},{"channel_metrics",channelMetrics},{"native",true},{"capture_frames",info["frames"]},{"xruns",info["xruns"]},{"transport",info["transport"]},{"packets",info["packets"]},{"checksum_errors",info["checksum_errors"]},{"sequence",QVariant::fromValue(sequence)}};
        if(!native){auto path=folder_+"/.feature-buffer.wav";if(writePcmWav(path,capture_->latestPcm(seconds),capture_->rate(),capture_->channels()))snapshot["feature_path"]=path;snapshot["feature_method"]=method;}
        snapshot["processing_ms"]=cost.nsecsElapsed()/1e6;
        {QMutexLocker lock(&mutex_);latest_=std::move(snapshot);revision_++;}
        const int interval=paired?16:options.value("previewChannels").toBool()||method=="stft"||method=="waterfall"?250:100;
        msleep(qMax(1,interval-int(cost.elapsed())));
    }
    transforms.clear();
}

QVariantMap nativeSignalPlot(const QVector<double>&x,int fs,const QString&method,const QString&unit,bool includeMetrics){
    if(x.size()<2)return {};QPair<QVector<double>,QVector<double>> spectrum;if(includeMetrics||method=="welch")spectrum=welch(x,fs);QVariantMap result{{"kind","line"},{"title",title(method)},{"xLabel","时间 / s"},{"yLabel",unit},{"sample_rate",fs},{"source_samples",x.size()}};if(includeMetrics)result["metrics"]=statistics(x,fs,spectrum);QVariantList series;
    if(method=="time")series.append(QVariantMap{{"name","原始波形"},{"points",waveform(x,fs)}});
    else if(method=="fft"||method=="welch"||method=="hilbert"){
        QVector<double> f,y;QVariantList frequencyPoints;
        if(method=="fft"){
            int n=nextPower(x.size());double sum=0;
            auto &transform=windowedFft(x,n,sum);int bins=n/2+1,stride=qMax(1,int(std::ceil(bins/800.)));
            // Peak selection is monotonic in squared amplitude. Convert only the
            // retained bins to dB, rather than taking log/sqrt of every FFT bin.
            for(int start=0;start<bins;start+=stride){double peakPower=-1;int peak=start;for(int i=start;i<qMin(start+stride,bins);i++){double power=(transform.output[i][0]*transform.output[i][0]+transform.output[i][1]*transform.output[i][1])*(i==0||i==n/2?1:4);if(power>peakPower){peakPower=power;peak=i;}}frequencyPoints.append(QVariant(QVariantList{double(peak)*fs/n,10*std::log10(qMax(peakPower/(sum*sum),1e-24))}));}
            result["yLabel"]="幅值 / dB re 1 "+unit;result["xRange"]=QVariantList{0,20000};result["xDisplayScale"]=.001;result["nyquist_hz"]=fs/2.;result["xLabel"]="频率 / kHz";
        }else{
            if(method=="hilbert"){
                int n=x.size();auto &t=fft(x,n);auto *a=fftw_alloc_complex(n),*b=fftw_alloc_complex(n);for(int i=0;i<n;i++)a[i][0]=a[i][1]=0;
                for(int i=0;i<=n/2;i++){double scale=i==0||(n%2==0&&i==n/2)?1:2;a[i][0]=t.output[i][0]*scale;a[i][1]=t.output[i][1]*scale;}
                auto plan=fftw_plan_dft_1d(n,a,b,FFTW_BACKWARD,FFTW_ESTIMATE);fftw_execute(plan);QVector<double> envelope(n);for(int i=0;i<n;i++)envelope[i]=std::hypot(b[i][0],b[i][1])/n;
                fftw_destroy_plan(plan);fftw_free(a);fftw_free(b);spectrum=welch(envelope,fs);result["envelope"]=nativeSignalPlot(envelope,fs,"time",unit);
            }
            f=spectrum.first;for(auto power:spectrum.second)y.append(10*std::log10(qMax(power,1e-24)));result["yLabel"]="PSD / dB re 1 "+unit+"²/Hz";
        }
        if(method!="fft"){result["xLabel"]="频率 / Hz";result["xRange"]=QVariantList{0,fs/2.};}series.append(QVariantMap{{"name",title(method)},{"points",method=="fft"?frequencyPoints:points(f,y)}});
    }else{
        int length=qMin(int(x.size()),qMax(64,int(fs*.025))),hop=qMax(1,int(fs*.01)),n=nextPower(length);QVector<double> window(length);double sum=0;for(int i=0;i<length;i++){window[i]=.54-.46*std::cos(2*pi*i/(length-1));sum+=window[i];}
        QVector<QVector<double>> columns;QVector<double> times;
        int count=(x.size()-length)/hop+1,stride=qMax(1,int(std::ceil(count/100.)));
        for(int k=0;k<count;k+=stride){QVector<double> frame(length);for(int i=0;i<length;i++)frame[i]=x[k*hop+i]*window[i];auto p=powers(frame,n);for(auto&v:p)v=10*std::log10(qMax(v/(sum*sum),1e-24));columns.append(p);times.append((k*hop+length*.5)/fs);}
        if(method=="waterfall"){
            QVariantList slices;int step=qMax(1,int(std::ceil(columns.size()/24.)));for(int k=0;k<columns.size();k+=step){QVector<double> f(columns[k].size());for(int i=0;i<f.size();i++)f[i]=double(i)*fs/n;slices.append(QVariantMap{{"time",times[k]},{"points",points(f,columns[k],320)}});}result["kind"]="waterfall";result["slices"]=slices;result["xLabel"]="频率 / Hz";result["zLabel"]="功率 / dB re 1 "+unit+"²";
        }else{
            QVariantList matrix;double minimum=INFINITY,maximum=-INFINITY;int rows=qMin(64,n/2+1);for(int r=0;r<rows;r++){int bin=qRound(double(r)*(n/2)/(rows-1));QVariantList row;for(auto&c:columns){row.append(c[bin]);minimum=qMin(minimum,c[bin]);maximum=qMax(maximum,c[bin]);}matrix.append(QVariant(row));}result["kind"]="heatmap";result["matrix"]=matrix;result["colorMin"]=minimum;result["colorMax"]=maximum;result["xRange"]=QVariantList{times.first(),times.last()};result["yRange"]=QVariantList{0,fs/2.};result["yLabel"]="频率 / Hz";result["colorLabel"]="功率 / dB re 1 "+unit+"²";
        }
    }
    result["series"]=series;result["method_id"]=method;return result;
}
