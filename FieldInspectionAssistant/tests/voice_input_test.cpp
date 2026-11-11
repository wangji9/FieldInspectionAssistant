#include "../src/services/NativeVoiceInput.h"
#include <QtEndian>
#include <stdexcept>
static void check(bool ok,const char *what){if(!ok)throw std::runtime_error(what);}
int main(int argc,char **argv){QCoreApplication app(argc,argv);try{
 QByteArray payload="{\"type\":\"wakeup\"}",buffer="noise";auto packet=voicePacket(4,payload,197);QList<VoiceFrame> frames;
 for(char c:packet){buffer+=c;frames+=takeVoiceFrames(buffer);}check(frames.size()==1&&frames[0].type==4&&frames[0].id==197&&frames[0].payload==payload,"fragmented wake frame");
 auto broken=packet;broken[10]=char(broken[10]^16);buffer=broken+packet+voicePacket(1,QByteArray(),65);frames=takeVoiceFrames(buffer);check(frames.size()==2&&frames[1].id==65,"CRC resynchronization / handshake");
 buffer=QByteArray::fromHex("a50104ffff0000")+packet;frames=takeVoiceFrames(buffer);check(frames.size()==1,"oversized frame guard");
 VoiceEndpoint endpoint;QByteArray silence(640,0);for(int i=0;i<500;++i)check(!endpoint.feed(silence)&&!endpoint.spoken(),"silence became speech");
 endpoint.reset();QByteArray impulse(640,0);qToLittleEndian<qint16>(15000,reinterpret_cast<uchar*>(impulse.data()+8));endpoint.feed(impulse);for(int i=0;i<80;++i)endpoint.feed(silence);check(!endpoint.spoken(),"single impulse became sustained speech");
 if(argc>1){QFile f(argv[1]);check(f.open(QIODevice::ReadOnly),"fixture open");auto wav=f.readAll();check(wav.startsWith("RIFF")&&wav.mid(8,4)=="WAVE","fixture WAV");int pos=12;QByteArray pcm;while(pos+8<=wav.size()){int size=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(wav.constData()+pos+4));if(wav.mid(pos,4)=="data"){pcm=wav.mid(pos+8,size);break;}pos+=8+size+(size&1);}check(!pcm.isEmpty(),"fixture PCM");endpoint.reset();bool ended=false;for(int i=0;i<pcm.size();i+=640){ended=endpoint.feed(pcm.mid(i,640));if(ended)break;}for(int i=0;!ended&&i<100;++i)ended=endpoint.feed(silence);check(endpoint.spoken()&&ended&&endpoint.audio().size()>6400,"spoken fixture endpoint");qInfo()<<"SPEECH_ENDPOINT"<<endpoint.durationMs()<<endpoint.audio().size();}
 qInfo()<<"VOICE_INPUT_CHECKS_OK";return 0;
 }catch(const std::exception&e){qCritical()<<e.what();return 1;}}
