#include "NativeVoiceInput.h"
#include "NativeSignalPipeline.h"
#include <alsa/asoundlib.h>
#include <fcntl.h>
#include <sys/file.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#include <cmath>
#include <QtEndian>
QByteArray voicePacket(int type,const QByteArray &payload,quint16 id){QByteArray b;b.append(char(0xa5));b.append(char(1));b.append(char(type));quint16 size=qToLittleEndian<quint16>(payload.size());id=qToLittleEndian(id);b.append(reinterpret_cast<char*>(&size),2);b.append(reinterpret_cast<char*>(&id),2);b+=payload;unsigned sum=0;for(auto c:b)sum+=uchar(c);b.append(char(-sum));return b;}
QList<VoiceFrame> takeVoiceFrames(QByteArray &b){QList<VoiceFrame> frames;while(b.size()>=8){int i=b.indexOf(QByteArray::fromHex("a501"));if(i<0){b=b.right(1);break;}if(i)b.remove(0,i);if(b.size()<8)break;int n=qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(b.constData()+3));if(n>4096){b.remove(0,1);continue;}if(b.size()<n+8)break;unsigned sum=0;for(int j=0;j<n+8;++j)sum+=uchar(b[j]);if(sum&255){b.remove(0,1);continue;}frames<<VoiceFrame{uchar(b[2]),qFromLittleEndian<quint16>(reinterpret_cast<const uchar*>(b.constData()+5)),b.mid(7,n)};b.remove(0,n+8);}return frames;}
VoiceEndpoint::VoiceEndpoint():vad_(fvad_new()){reset();}
VoiceEndpoint::~VoiceEndpoint(){if(vad_)fvad_free(vad_);}
void VoiceEndpoint::reset(int silenceMs){pre_.clear();audio_.clear();vadFrame_.clear();speechFrame_=false;if(vad_){fvad_reset(vad_);fvad_set_sample_rate(vad_,16000);fvad_set_mode(vad_,2);}elapsed_=voiced_=quiet_=0;silence_=qBound(450,silenceMs,1500);noise_=12;level_=0;started_=false;}
bool VoiceEndpoint::feed(const QByteArray &pcm){int samples=pcm.size()/2;if(!samples)return false;double energy=0,peak=0;const auto *p=reinterpret_cast<const uchar*>(pcm.constData());for(int i=0;i<samples;++i){double v=qFromLittleEndian<qint16>(p+2*i);energy+=v*v;peak=qMax(peak,std::abs(v));}double rms=std::sqrt(energy/samples);int ms=qMax(1,samples*1000/16000);elapsed_+=ms;level_=qBound(0.,rms/2200.,1.);
 vadFrame_+=pcm;while(vadFrame_.size()>=640){qint16 frame[320];for(int i=0;i<320;++i)frame[i]=qFromLittleEndian<qint16>(reinterpret_cast<const uchar*>(vadFrame_.constData()+2*i));speechFrame_=vad_&&fvad_process(vad_,frame,320)==1;vadFrame_.remove(0,640);}
 bool voiced=speechFrame_&&rms>qBound(24.,noise_*3.0,160.)&&peak>70;
 if(!voiced&&(!started_||quiet_>300))noise_=noise_*.97+qMin(120.,rms)*.03;
 if(!started_){pre_+=pcm;if(pre_.size()>11200)pre_=pre_.right(11200);if(voiced){voiced_+=ms;if(voiced_>=100){started_=true;audio_=pre_;pre_.clear();quiet_=0;}}else voiced_=qMax(0,voiced_-ms/2);}
 else{audio_+=pcm;if(voiced){voiced_+=ms;quiet_=0;}else quiet_+=ms;}
 return started_&&voiced_>=160&&quiet_>=silence_;
}
NativeVoiceInput::NativeVoiceInput(const QString&s,const QString&f,QObject*p):QThread(p),source_(s),folder_(f){}
NativeVoiceInput::~NativeVoiceInput(){requestInterruption();wait(5000);}
void NativeVoiceInput::listen(bool manual){command_=manual?2:1;}
void NativeVoiceInput::block(){command_=3;}
QVariantMap NativeVoiceInput::discover(){QVariantMap out;QProcess p;p.start("pactl",{"-f","json","list","sources"});if(!p.waitForFinished(3500))return out;for(auto v:QJsonDocument::fromJson(p.readAllStandardOutput()).array()){auto o=v.toObject();auto name=o["name"].toString();if(name.startsWith("alsa_input.")&&name.contains("ListenGo_Circular_6-Microphone")&&!name.endsWith(".monitor")){out={{"source",name},{"name",o["description"].toString()},{"wake",QFileInfo::exists("/dev/lg_speech_serial")}};break;}}return out;}
void NativeVoiceInput::run(){
 // Device discovery and all port IO stay off the GUI thread. No stream is saved before wake/manual activation.
 if(source_.isEmpty())source_=discover().value("source").toString();if(isInterruptionRequested())return;if(source_.isEmpty()){emit failed("未找到六麦语音输入，请检查 USB 连接后重试");return;}
 if(source_.contains('"')||source_.contains('\\')||source_.contains('\n')){emit failed("麦克风设备标识无效");return;}
 const auto control=(qEnvironmentVariableIsSet("FIELDOPS_QA_SCRIPT")&&!qEnvironmentVariable("FIELDOPS_QA_VOICE_CONTROL").isEmpty())?qEnvironmentVariable("FIELDOPS_QA_VOICE_CONTROL"):QStringLiteral("/dev/lg_speech_serial");
 int fd=::open(control.toLocal8Bit().constData(),O_RDWR|O_NOCTTY|O_NONBLOCK);bool wake=fd>=0;
 if(wake){if(flock(fd,LOCK_EX|LOCK_NB)<0){::close(fd);emit failed("六麦控制口被占用，请先停止数据采集或其他语音功能");return;}termios config{};if(tcgetattr(fd,&config)<0){::close(fd);fd=-1;wake=false;}else{cfmakeraw(&config);cfsetispeed(&config,B115200);cfsetospeed(&config,B115200);config.c_cflag|=CLOCAL|CREAD;config.c_cflag&=~CRTSCTS;tcsetattr(fd,TCSANOW,&config);}}
 snd_config_t *cfg=nullptr;snd_pcm_t *pcm=nullptr;auto def=QString("pcm.fieldops_voice { type pulse device \"%1\" }\n").arg(source_).toUtf8();int ret=snd_config_load_string(&cfg,def.constData(),def.size());if(ret>=0)ret=snd_pcm_open_lconf(&pcm,"fieldops_voice",SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK,cfg);if(cfg)snd_config_delete(cfg);
 if(ret>=0)ret=snd_pcm_set_params(pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,1,16000,1,60000);
 if(ret<0){if(pcm)snd_pcm_close(pcm);if(fd>=0)::close(fd);emit failed("六麦录音无法打开："+QString::fromLocal8Bit(snd_strerror(ret)));return;}
 auto send=[&](int type,const QByteArray &body,quint16 id=0){if(fd>=0){auto bytes=voicePacket(type,body,id);auto written=::write(fd,bytes.constData(),bytes.size());if(written!=bytes.size())emit failed(QStringLiteral("六麦控制命令发送失败"));}};
 VoiceEndpoint endpoint;endpoint.reset(silence_);bool accepting=false,seenSpeech=false;QByteArray serial;QElapsedTimer levelTick;levelTick.start();QVector<qint16> samples(320);emit ready(wake);
 while(!isInterruptionRequested()){
  auto command=command_.exchange(0);if(command){endpoint.reset(silence_);seenSpeech=false;accepting=command!=3;if(command==2)send(5,"{\"type\":\"manual_wakeup\",\"content\":{\"beam\":0}}",1);}
  if(fd>=0){char buffer[4096];ssize_t n;while((n=::read(fd,buffer,sizeof(buffer)))>0)serial.append(buffer,n);pollfd port{fd,POLLIN,0};if(poll(&port,1,0)>0&&(port.revents&(POLLHUP|POLLERR|POLLNVAL))){emit failed("六麦控制接口已断开，请重新连接后开启助手");break;}
   for(auto f:takeVoiceFrames(serial)){if(f.type==1)send(255,QByteArray::fromHex("a5000000"),f.id);if(f.type==4){auto object=QJsonDocument::fromJson(f.payload).object();if(!object.isEmpty())emit wakeDetected(object.toVariantMap());}}
  }
  auto count=snd_pcm_readi(pcm,samples.data(),samples.size());if(count==-EAGAIN){snd_pcm_wait(pcm,20);continue;}if(count<0){if(snd_pcm_recover(pcm,count,1)<0){emit failed("六麦音频接口已断开，请重新连接后重试");break;}continue;}if(!count)continue;
  QByteArray block(reinterpret_cast<const char*>(samples.constData()),count*2);
  if(accepting){bool end=endpoint.feed(block);if(endpoint.spoken()&&!seenSpeech){seenSpeech=true;emit speechDetected();}if(end||(endpoint.spoken()&&endpoint.durationMs()>=20000)){
    accepting=false;auto data=endpoint.audio();QString path=folder_+"/input-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".wav";if(!writePcmWav(path,data,16000,1)){emit failed("语音录音保存失败，请检查存储空间");break;}QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner);emit utterance(path,data.size()/32);endpoint.reset(silence_);
   }else if(endpoint.durationMs()>=10000&&!endpoint.spoken()){endpoint.reset(silence_);emit idleTimeout();}
  }
  if(levelTick.elapsed()>65){emit levelChanged(accepting?endpoint.level():0);levelTick.restart();}
 }
 snd_pcm_drop(pcm);snd_pcm_close(pcm);if(fd>=0)::close(fd);emit levelChanged(0);
}
