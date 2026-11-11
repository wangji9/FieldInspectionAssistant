#pragma once
#include "AiInferenceService.h"
#include "SpeechService.h"

// Owns a separate ASR client so dictation never replaces a model's answer.
class DictationService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(QString partial READ partial NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    void setSession(SessionManager*s){recognizer_->setSession(s);}
    DictationService(SpeechService *speech, AiInferenceService *model, QObject *parent=nullptr);
    bool active() const { return waiting_ || recognizer_->busy(); }
    bool recording() const { return waiting_ && speech_->recording(); }
    QString partial() const { return partial_; }
    QString message() const { return message_; }
    QString error() const { return error_; }
    Q_INVOKABLE void start();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void transcribeFile(const QString &path);
    Q_INVOKABLE void cancel();
    void clearSession(){cancel();recognizer_->clearSession();partial_.clear();error_.clear();waiting_=false;emit changed();}
signals:
    void changed();
    void finished(bool ok);
private:
    bool canStart();
    void recognize(const QString &path);
    SpeechService *speech_;
    AiInferenceService *model_, *recognizer_;
    bool waiting_=false, cancelled_=false,cancelIssued_=false;
    QString partial_,message_=QStringLiteral("可使用键盘或语音输入"),error_;
};
