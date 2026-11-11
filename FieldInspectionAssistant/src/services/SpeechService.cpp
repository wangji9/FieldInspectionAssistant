#include "SpeechService.h"
#include <QTextDocument>

namespace {
const QString helper=QStringLiteral("/userdata/ai-workbench/speech_session.py");
QString audioPath(const QString &suffix) {
    const auto dir=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/audio";
    QDir().mkpath(dir);
    return dir+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+suffix;
}
}

SpeechService::SpeechService(QObject *parent) : QObject(parent) {
    clock_.setInterval(1000);
    connect(&clock_,&QTimer::timeout,this,[this]{ ++seconds_; emit changed(); if(seconds_>=60) stopRecording(); });
    captureWatchdog_.setSingleShot(true); captureWatchdog_.setInterval(4000);
    connect(&captureWatchdog_,&QTimer::timeout,this,[this]{ if(capture_.state()!=QProcess::NotRunning) { discard_=true; capture_.kill(); fail(QStringLiteral("录音设备未响应，请重新连接麦克风")); } });
    connect(&capture_,&QProcess::readyReadStandardOutput,this,&SpeechService::readCapture);
    connect(&capture_,&QProcess::started,this,[this]{ if(stopping_)capture_.terminate(); });
    connect(&capture_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){
        if(e==QProcess::FailedToStart) { recording_=false; fail(QStringLiteral("无法启动录音程序：")+capture_.errorString()); }
    });
    connect(&capture_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        readCapture(); clock_.stop(); captureWatchdog_.stop(); recording_=false; stopping_=false;
        if(discard_) { QFile::remove(recordingPath_); message_=error_.isEmpty()?QStringLiteral("已取消录音"):error_; emit changed(); return; }
        if(code!=0 || !QFileInfo::exists(recordingPath_) || QFileInfo(recordingPath_).size()<=44) {
            QFile::remove(recordingPath_);
            fail(error_.isEmpty()?QStringLiteral("录音失败，请检查六麦连接和系统输入设备"):error_); return;
        }
        message_=QStringLiteral("录音已保存，可播放或转文字"); emit changed(); emit recordingReady(recordingPath_);
    });
    connect(&synth_,&QProcess::readyReadStandardOutput,this,&SpeechService::readSynth);
    connect(&synth_,&QProcess::stateChanged,this,[this]{emit changed();});
    connect(&player_,&QProcess::stateChanged,this,[this]{emit changed();});
    playbackTick_.setInterval(100);
    connect(&playbackTick_,&QTimer::timeout,this,[this]{emit changed();});
    connect(&player_,&QProcess::started,this,[this]{playbackClock_.start();playbackTick_.start();message_=replay_?QStringLiteral("正在播放语音…"):QStringLiteral("正在流式朗读…");emit changed();});
    connect(&synth_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)failPlayback(QStringLiteral("无法启动本地语音程序"));});
    connect(&player_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)failPlayback(QStringLiteral("无法启动 USB 声卡播放程序"));});
    connect(&synth_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        readSynth(); if(!textPath_.isEmpty())QFile::remove(textPath_);
        synthDone_=true;
        if(speechCancelled_) { cleanupParts(); emit changed(); return; }
        if(code!=0 || !ready_) { failPlayback(error_.isEmpty()?QStringLiteral("语音处理失败，请检查文件或本地模型"):error_); return; }
        startNext(); finishPlayback();
    });
    connect(&player_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){
        playbackTick_.stop();
        if(speechCancelled_) { cleanupParts(); emit changed(); return; }
        if(code) { failPlayback(QStringLiteral("USB 声卡播放失败：")+QString::fromUtf8(player_.readAllStandardError()).trimmed()); return; }
        played_+=partDurations_.value(playingPart_); playingPart_.clear();
        startNext(); finishPlayback();
    });
}

SpeechService::~SpeechService() {
    discard_=true; speechCancelled_=true;
    for(auto *p:{&capture_,&synth_,&player_}) if(p->state()!=QProcess::NotRunning) { p->terminate(); if(!p->waitForFinished(1500)) { p->kill(); p->waitForFinished(500); } }
    if(!textPath_.isEmpty())QFile::remove(textPath_);
    cleanupParts();
}
void SpeechService::fail(const QString &message) { error_=message; message_=message; emit changed(); }
void SpeechService::startRecording(bool autoStop) {
    if(externalGuard_&&externalGuard_()){fail(QStringLiteral("请先关闭语音对话，再使用此录音或播放功能"));emit speechFinished(false);return;}
    if(recording_) return;
    stopSpeech(); error_.clear(); recordingPath_=audioPath(".wav"); captureBuffer_.clear();
    recording_=true; discard_=false; stopping_=false; seconds_=0; autoStop_=autoStop; message_=QStringLiteral("正在打开六麦…");
    capture_.start(QStringLiteral("/usr/bin/python3"),{helper,"record",recordingPath_,autoStop?"auto":"manual"}); emit changed();
}
void SpeechService::stopRecording() {
    if(!recording_ || stopping_)return;
    stopping_=true; clock_.stop(); message_=QStringLiteral("正在保存录音…");
    capture_.terminate(); captureWatchdog_.start(); emit changed();
}
void SpeechService::cancelRecording() { if(recording_) { discard_=true; error_.clear(); stopRecording(); } }
void SpeechService::readCapture() {
    captureBuffer_+=capture_.readAllStandardOutput();
    while(captureBuffer_.contains('\n')) {
        const auto pos=captureBuffer_.indexOf('\n'); const auto obj=QJsonDocument::fromJson(captureBuffer_.left(pos)).object(); captureBuffer_.remove(0,pos+1);
        if(obj["event"]=="recording") { message_=autoStop_?QStringLiteral("请说话，停顿后自动转文字（最长 30 秒）"):QStringLiteral("正在录音，请说话（最长 60 秒）"); clock_.start(); emit changed(); }
        if(obj.contains("error")&&!discard_) error_=obj["error"].toString();
    }
}
void SpeechService::speak(const QString &text) {
    if(externalGuard_&&externalGuard_()){fail(QStringLiteral("请先关闭语音对话，再使用此录音或播放功能"));emit speechFinished(false);return;}
    if(recording_ || speechBusy())return;
    QTextDocument document; document.setMarkdown(text);
    const auto plain=document.toPlainText().trimmed();
    if(plain.isEmpty()) { fail(QStringLiteral("请先输入文字或完成语音识别"));emit speechFinished(false); return; }
    if(plain.size()>3000) { fail(QStringLiteral("单次朗读最多 3000 字，请分段朗读"));emit speechFinished(false); return; }
    cleanupParts(); error_.clear(); speechCancelled_=false; ready_=false; replay_=false; synthDone_=false; synthBuffer_.clear();
    duration_=0;played_=0;queue_.clear();partDurations_.clear();
    if(!textPath_.isEmpty())QFile::remove(textPath_);
    textPath_=audioPath(".txt"); speechPath_=audioPath("-speech.wav");
    playbackPath_=speechPath_;
    QFile file(textPath_); if(!file.open(QIODevice::WriteOnly)) {fail(QStringLiteral("无法保存朗读文本"));emit speechFinished(false);return;}
    file.write(plain.toUtf8()); file.close();
    message_=QStringLiteral("正在合成语音…");
    synth_.start(QStringLiteral("/userdata/ai-workbench/tts-venv/bin/python"),{helper,"synthesize",textPath_,speechPath_}); emit changed();
}
void SpeechService::readSynth() {
    synthBuffer_+=synth_.readAllStandardOutput();
    while(synthBuffer_.contains('\n')) {
        const auto pos=synthBuffer_.indexOf('\n'); const auto obj=QJsonDocument::fromJson(synthBuffer_.left(pos)).object(); synthBuffer_.remove(0,pos+1);
        if(obj.contains("error"))error_=obj["error"].toString();
        if(speechCancelled_)continue;
        const auto event=obj["event"].toString();
        if(event=="audio_chunk" || event=="audio_ready") {
            const auto path=obj["path"].toString(); const auto seconds=obj["duration_s"].toDouble();
            sink_=obj["sink"].toString(); partDurations_[path]=seconds; queue_.enqueue(path);
            if(event=="audio_chunk") { temporaryFiles_<<path;duration_=obj["total_s"].toDouble();emit speechChunkReady(speechPath_,duration_); }
            else { ready_=true;duration_=seconds;if(path!=playbackPath_)temporaryFiles_<<path;emit audioPrepared(playbackPath_,duration_); }
            startNext();emit changed();
        }
        if(event=="synthesized") { ready_=true;duration_=obj["duration_s"].toDouble();emit speechReady(speechPath_,duration_);emit changed(); }
    }
}
void SpeechService::stopSpeech() {
    if(!speechBusy()) return;
    speechCancelled_=true;
    queue_.clear();playbackTick_.stop();
    if(synth_.state()!=QProcess::NotRunning)synth_.kill();
    if(player_.state()!=QProcess::NotRunning)player_.kill();
    cleanupParts();
    if(!replay_&&!ready_&&!speechPath_.isEmpty())QFile::remove(speechPath_);
    message_=QStringLiteral("已停止播放"); emit changed();emit speechFinished(false);
}

qreal SpeechService::playbackPosition() const {
    return qMin(duration_,played_+(playing()&&playbackClock_.isValid()?playbackClock_.elapsed()/1000.0:0));
}
void SpeechService::playAudio(const QString &path) {
    if(externalGuard_&&externalGuard_()){fail(QStringLiteral("请先关闭语音对话，再使用此录音或播放功能"));emit speechFinished(false);return;}
    if(recording_||speechBusy())return;
    if(!QFileInfo(path).isFile()) { fail(QStringLiteral("语音文件不存在"));emit speechFinished(false);return; }
    cleanupParts();queue_.clear();partDurations_.clear();synthBuffer_.clear();error_.clear();
    replay_=true;ready_=false;synthDone_=false;speechCancelled_=false;duration_=0;played_=0;
    playbackPath_=path;convertedPath_=audioPath("-play.wav");textPath_.clear();
    message_=QStringLiteral("正在准备语音播放…");
    synth_.start(QStringLiteral("/usr/bin/python3"),{helper,"inspect",path,convertedPath_});emit changed();
}
void SpeechService::startNext() {
    if(speechCancelled_||player_.state()!=QProcess::NotRunning||queue_.isEmpty())return;
    playingPart_=queue_.dequeue();playbackClock_.invalidate();
    // Give this stream its own identity and unity gain. Pulse can otherwise
    // restore an old muted application volume even though the sink is unmuted.
    player_.start(QStringLiteral("paplay"),{"--device="+sink_,"--volume=65536",
                   "--client-name=FieldInspectionSpeech","--stream-name=FieldInspectionSpeech",playingPart_});
}
void SpeechService::finishPlayback() {
    if(speechCancelled_||!synthDone_||speechBusy())return;
    cleanupParts();message_=QStringLiteral("播放完成");emit changed();emit speechFinished(true);
}
void SpeechService::cleanupParts() {
    for(const auto &file:temporaryFiles_)QFile::remove(file);
    temporaryFiles_.clear();
    if(!convertedPath_.isEmpty()) { QFile::remove(convertedPath_);convertedPath_.clear(); }
}
void SpeechService::failPlayback(const QString &message) {
    speechCancelled_=true;queue_.clear();playbackTick_.stop();
    if(synth_.state()!=QProcess::NotRunning)synth_.kill();
    if(player_.state()!=QProcess::NotRunning)player_.kill();
    cleanupParts();
    if(!replay_&&!ready_&&!speechPath_.isEmpty())QFile::remove(speechPath_);
    fail(message);emit speechFinished(false);
}
