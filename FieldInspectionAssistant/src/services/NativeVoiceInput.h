#pragma once
#include <QtCore>
#include <atomic>
#include <fvad.h>
// Pure protocol/VAD primitives are shared by capture and hardware-free tests.
struct VoiceFrame {int type=0;quint16 id=0;QByteArray payload;};
QList<VoiceFrame> takeVoiceFrames(QByteArray &buffer);
QByteArray voicePacket(int type,const QByteArray &payload,quint16 id=0);
class VoiceEndpoint {
public:
 VoiceEndpoint();~VoiceEndpoint();VoiceEndpoint(const VoiceEndpoint&)=delete;VoiceEndpoint&operator=(const VoiceEndpoint&)=delete;
 void reset(int silenceMs=700);bool feed(const QByteArray &pcm);bool spoken()const{return voiced_>=160;}double level()const{return level_;}int durationMs()const{return elapsed_;}
 QByteArray audio()const{return audio_;}
private:Fvad *vad_=nullptr;QByteArray pre_,audio_,vadFrame_;bool speechFrame_=false;int elapsed_=0,voiced_=0,quiet_=0,silence_=700;double noise_=12,level_=0;bool started_=false;
};
class NativeVoiceInput final:public QThread {
 Q_OBJECT
public:
 NativeVoiceInput(const QString &source,const QString &folder,QObject*parent=nullptr);
 ~NativeVoiceInput()override;
 void listen(bool manual=false);void block();void setSilence(int value){silence_=value;}
 static QVariantMap discover();
signals:
 void ready(bool wakeAvailable);void wakeDetected(const QVariantMap &event);void levelChanged(double level);void speechDetected();void utterance(const QString &path,int durationMs);void idleTimeout();void failed(const QString &error);
protected:void run()override;
private:QString source_,folder_;std::atomic<int> command_{0},silence_{700};
};
