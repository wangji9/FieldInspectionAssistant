#pragma once
#include <QtCore>
#include <QtNetwork>
#include <functional>
class SessionManager;class WorkspaceService;class AiInferenceService;class NativeVoiceInput;class SystemContextService;
class VoiceMessageModel final:public QAbstractListModel{
 Q_OBJECT
public:
 enum Role{Body=Qt::UserRole+1,Outgoing,Kind,Status,Streaming,AudioPath,SystemEvidence};
 explicit VoiceMessageModel(QObject*p=nullptr):QAbstractListModel(p){}
 int rowCount(const QModelIndex &p={})const override{return p.isValid()?0:rows_.size();}
 QVariant data(const QModelIndex&,int)const override;QHash<int,QByteArray> roleNames()const override;
 int append(QVariantMap row);void patch(int index,const QVariantMap&);void load(QVariantList rows);QVariantList snapshot()const{return rows_;}
private:QVariantList rows_;
};
class VoiceAssistantService final:public QObject{
 Q_OBJECT
 Q_PROPERTY(bool engaged READ engaged NOTIFY changed)
 Q_PROPERTY(bool active READ active NOTIFY changed)
 Q_PROPERTY(bool busy READ busy NOTIFY changed)
 Q_PROPERTY(QString state READ state NOTIFY changed)
 Q_PROPERTY(QString status READ status NOTIFY changed)
 Q_PROPERTY(QString error READ error NOTIFY changed)
 Q_PROPERTY(double level READ level NOTIFY levelChanged)
 Q_PROPERTY(bool wakeAvailable READ wakeAvailable NOTIFY changed)
 Q_PROPERTY(QString playbackPath READ playbackPath NOTIFY changed)
 Q_PROPERTY(QAbstractItemModel *messages READ messages CONSTANT)
 Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY changed)
 Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY changed)
 Q_PROPERTY(int silenceMs READ silenceMs WRITE setSilenceMs NOTIFY changed)
 Q_PROPERTY(bool continuous READ continuous WRITE setContinuous NOTIFY changed)
 Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed)
public:
 VoiceAssistantService(SessionManager*,WorkspaceService*,QObject*parent=nullptr);~VoiceAssistantService()override;
 bool engaged()const{return requested_;}
 bool active()const{return input_!=nullptr;}bool busy()const;QString state()const{return state_;}QString status()const;QString error()const{return error_;}double level()const{return level_;}bool wakeAvailable()const{return wakeAvailable_;}
 QString playbackPath()const;
 QAbstractItemModel *messages(){return &messages_;}QVariantMap metrics()const{return metrics_;}int volume()const{return volume_;}int silenceMs()const{return silenceMs_;}bool continuous()const{return continuous_;}bool muted()const{return muted_;}
 void setVolume(int);void setSilenceMs(int);void setContinuous(bool);void setMuted(bool);
 void setSystemContext(SystemContextService *s){system_=s;}
 Q_INVOKABLE bool suspendForActivity();
 void setActivityGuard(std::function<bool()> f){guard_=f;}
 Q_INVOKABLE void enable();Q_INVOKABLE void disable();Q_INVOKABLE void talk();Q_INVOKABLE void interrupt();Q_INVOKABLE void sendText(const QString&);Q_INVOKABLE void transcribeFile(const QString&);Q_INVOKABLE void replay(int index);Q_INVOKABLE void newConversation();Q_INVOKABLE void restore(const QString &id=QString());Q_INVOKABLE QVariantList exportRows()const;Q_INVOKABLE void prepare();
signals:void changed();void levelChanged();
private:
 void releaseInput();void awaitModel(std::function<void()> action,int attempt=0);void setState(const QString&);void fail(const QString&);void wake(bool manual);void recognize(const QString&,int);void respond(const QString&);void flushText();void splitSpeech(bool final);void pumpTts();void pumpPlayback();void finishTurn();void cancelTurn();void persist();void createTts();void readTts();void followUp();bool authorized()const;QString prompt(const QString&)const;
 SessionManager *session_;WorkspaceService *workspace_;AiInferenceService *asr_,*llm_;NativeVoiceInput *input_=nullptr;SystemContextService *system_=nullptr;QVariantMap systemEvidence_;VoiceMessageModel messages_;QNetworkAccessManager network_;QProcess tts_,player_;QTimer availabilityTimer_,streamTimer_,followTimer_,stageTimeout_,ttsTimeout_,playTimeout_;QElapsedTimer endClock_;
 QString owner_,state_="off",error_,folder_,source_,audioInput_,pendingText_,speechBuffer_,answer_,playing_,ttsRequest_,mergeName_,sink_;QByteArray ttsBuffer_;QQueue<QString> sentences_,audioQueue_;QVariantMap metrics_;std::function<bool()> guard_;
 bool requested_=false; qint64 retryAt_=0;
 quint64 generation_=0;int assistantRow_=-1,volume_=25,silenceMs_=700,ttsIndex_=0;double level_=0;bool wakeAvailable_=false,continuous_=true,muted_=false,modelDone_=true,ttsReady_=false,ttsBusy_=false,finishSent_=false,firstAudio_=false,firstText_=false;

};
