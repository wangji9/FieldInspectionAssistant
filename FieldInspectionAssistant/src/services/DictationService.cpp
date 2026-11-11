#include "DictationService.h"

DictationService::DictationService(SpeechService *speech,AiInferenceService *model,QObject *parent)
    :QObject(parent),speech_(speech),model_(model),recognizer_(new AiInferenceService(this)) {
    connect(speech_,&SpeechService::recordingReady,this,[this](const QString &path){
        if(waiting_&&!cancelled_) { waiting_=false;recognize(path); }
    });
    connect(speech_,&SpeechService::changed,this,[this]{
        if(!waiting_)return;
        message_=speech_->message();
        if(!speech_->recording()&&!speech_->error().isEmpty()) {
            waiting_=false;error_=speech_->error();emit changed();emit finished(false);
        } else emit changed();
    });
    connect(recognizer_,&AiInferenceService::changed,this,[this]{
        if(!cancelled_) { partial_=recognizer_->answer();message_=recognizer_->message(); }
        if(cancelled_&&!cancelIssued_&&recognizer_->canStop()) {cancelIssued_=true;recognizer_->stop();}
        emit changed();
    });
    connect(recognizer_,&AiInferenceService::completed,this,[this](bool ok,const QString &){
        if(cancelled_) {message_=QStringLiteral("已取消语音输入");emit changed();emit finished(false);return;}
        partial_=recognizer_->answer();
        error_=ok?QString():recognizer_->message();
        message_=ok?QStringLiteral("识别完成，可继续编辑或完成输入"):error_;
        emit changed();emit finished(ok);
    });
}
bool DictationService::canStart() {
    if(active()||model_->busy()||speech_->recording()||speech_->speechBusy()) {
        error_=QStringLiteral("请先结束当前识别、录音或播放");message_=error_;emit changed();return false;
    }
    cancelled_=false;cancelIssued_=false;partial_.clear();error_.clear();return true;
}
void DictationService::start() {
    if(!canStart())return;
    waiting_=true;message_=QStringLiteral("正在打开麦克风…");emit changed();speech_->startRecording(true);
}
void DictationService::stopRecording() { if(waiting_)speech_->stopRecording(); }
void DictationService::transcribeFile(const QString &path) {
    if(!canStart())return;
    if(!QFileInfo(path).isFile()) {error_=QStringLiteral("录音文件不存在");message_=error_;emit changed();emit finished(false);return;}
    recognize(path);
}
void DictationService::recognize(const QString &path) {
    message_=QStringLiteral("正在语音转文字…");
    recognizer_->submit("asr","",path,256,false,false,"qwen3_asr");emit changed();
}
void DictationService::cancel() {
    cancelled_=true;
    if(waiting_) {waiting_=false;speech_->cancelRecording();}
    if(!cancelIssued_&&recognizer_->canStop()) {cancelIssued_=true;recognizer_->stop();}
    message_=QStringLiteral("已取消语音输入");emit changed();
}
