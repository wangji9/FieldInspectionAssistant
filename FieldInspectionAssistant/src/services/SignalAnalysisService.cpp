#include "SignalAnalysisService.h"
#include "NativeSignalPipeline.h"
#include "WaveCsvExport.h"
#include "RecordingCatalog.h"
#include "../app/SessionManager.h"
#include <QUrl>
#include <QSaveFile>
SignalAnalysisService::SignalAnalysisService(DatabaseManager *db,SessionManager *session,const QString &folder,QObject *parent)
 :QObject(parent),session_(session),folder_(folder) {
    discoveryTimer_.setSingleShot(true);discoveryTimer_.setInterval(8000);connect(&discoveryTimer_,&QTimer::timeout,this,[this]{discovering_=false;deviceStatus_="设备刷新超时，请稍后重试";emit changed();});
    catalog_=new RecordingCatalog(db,session,folder);
    connect(&worker_,&QProcess::readyReadStandardOutput,this,&SignalAnalysisService::read);
    connect(&worker_,&QProcess::errorOccurred,this,[this]{discovering_=false;discoveryTimer_.stop();deviceStatus_="设备查询服务启动失败，请刷新重试";recordingBusy_=false;message_=QStringLiteral("无法启动分析服务：")+worker_.errorString();emit changed();});
    connect(&worker_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){discovering_=false;discoveryTimer_.stop();recordingBusy_=false;if(!capture_)running_=false;if(code)message_=QStringLiteral("分析服务已退出，请刷新重试");emit changed();});
    connect(session_,&SessionManager::sessionChanged,this,[this]{if(!session_->loggedIn()){cancelExport();stop();snapshot_.clear();trends_.clear();emit snapshotChanged();emit trendsChanged();}recordingFilter_.clear();recordingPageIndex_=0;reloadRecordings();});
    frameTimer_.setInterval(16);frameTimer_.setTimerType(Qt::PreciseTimer);connect(&frameTimer_,&QTimer::timeout,this,&SignalAnalysisService::publishNative);
}
SignalAnalysisService::~SignalAnalysisService(){cancelExport();frameTimer_.stop();if(analyzer_){delete analyzer_;analyzer_=nullptr;}if(capture_){delete capture_;capture_=nullptr;}for(auto *thread:findChildren<QThread*>(QString(),Qt::FindDirectChildrenOnly))thread->wait();if(worker_.state()!=QProcess::NotRunning){worker_.closeWriteChannel();if(!worker_.waitForFinished(6000)){worker_.terminate();worker_.waitForFinished(2000);}}delete catalog_;}
void SignalAnalysisService::refresh(){
    reloadRecordings();refreshDevices(true);
}
void SignalAnalysisService::refreshDevices(bool withData){
    if(!session_->can("inspect")||discovering_)return;
    discovering_=true;deviceStatus_="正在重新扫描采集设备…";discoveryTimer_.start();emit changed();
    if(worker_.state()==QProcess::NotRunning){
        buffer_.clear();worker_.start(QStringLiteral("/userdata/ai-workbench/analytics-venv/bin/python"),
          {QStringLiteral("/home/ztl/FieldInspectionAssistant/deploy/sensor_analysis.py"),QStringLiteral("--data"),folder_});
    }else if(worker_.state()==QProcess::Running)send({{"op","devices"},{"with_data",withData}});
}
QVariantMap SignalAnalysisService::summary()const{
    QVariantMap result;for(const auto *key:{"rate","channels","channel","unit","time","recording","source","full_duration","analysis_scope","analysis_start","analysis_end","native","capture_frames","checksum_errors","transport","calibration"})if(snapshot_.contains(key))result[key]=snapshot_[key];
    auto plot=snapshot_.value("plot").toMap();result["has_plot"]=!plot.isEmpty();result["has_envelope"]=plot.contains("envelope");return result;
}
void SignalAnalysisService::send(const QJsonObject &o){
    if(worker_.state()!=QProcess::Running){message_=QStringLiteral("请先刷新，等待分析服务就绪");emit changed();return;}
    auto command=o;command["reply_user"]=session_->userId();worker_.write(QJsonDocument(command).toJson(QJsonDocument::Compact)+'\n');
}
void SignalAnalysisService::start(const QString &source){
    if(reportGuard_&&reportGuard_()){message_=QStringLiteral("请先结束语音对话或声学评价报告，再开始采集");emit changed();return;}
    if(!session_->can("inspect")||capture_||recordingBusy_||exportBusy())return;
    QVariantMap device;for(auto v:devices_)if(v.toMap()["id"].toString()==source)device=v.toMap();
    if(device.isEmpty()){message_=QStringLiteral("请刷新并选择真实采集设备");emit changed();return;}
    snapshot_.clear();emit snapshotChanged();
    captureGeneration_++;featurePending_=false;capture_=new NativeAudioCapture(device,folder_,this);
    auto registered=catalog_->beginCapture(capture_->info());if(!registered.value("ok").toBool()){delete capture_;capture_=nullptr;message_=registered.value("error").toString();emit changed();return;}reloadRecordings();
    analyzer_=new NativeSignalAnalyzer(capture_,folder_,this);analyzer_->configure(method_,channel_,window_,options_);
    connect(capture_,&QThread::finished,this,&SignalAnalysisService::finishNative);
    running_=true;message_=QStringLiteral("C++ 原生采集正在启动");measurementClock_.start();capture_->start();analyzer_->start();frameTimer_.start();emit changed();
}
void SignalAnalysisService::stop(){if(capture_){capture_->requestInterruption();message_=QStringLiteral("正在保存原始录音…");emit changed();}else if(worker_.state()==QProcess::Running)send({{"op","stop"}});}
void SignalAnalysisService::configure(const QString &method,int channel,double window,const QVariantMap &options){
    if(method_!=method||channel_!=channel||window_!=window||options_!=options){captureGeneration_++;featurePending_=false;}
    method_=method;channel_=channel;window_=qBound(.08,window,5.);options_=options;
    if(analyzer_)analyzer_->configure(method_,channel_,window_,options_);
    else send({{"op","configure"},{"method",method},{"channel",channel},{"window",window_},{"options",QJsonObject::fromVariantMap(options)}});
}
void SignalAnalysisService::publishNative(){
    QElapsedTimer delivery;delivery.start();
    if(!analyzer_||!capture_)return;auto value=analyzer_->takeSnapshot();if(value.isEmpty())return;
    const auto feature=value.value("feature_method").toString();
    if(!feature.isEmpty()){
        // Keep the last completed feature frame while the independent worker computes the next.
        auto previous=snapshot_.value("plot").toMap();if(previous.value("method_id").toString()==feature)value["plot"]=previous;
        else{QString name=feature;for(auto m:methods_)if(m.toMap()["id"]==feature)name=m.toMap()["name"].toString();value["plot"]=QVariantMap{{"kind","line"},{"title",name},{"method_id",feature},{"series",QVariantList{}},{"error",QStringLiteral("正在计算…")}};}
        if(!featurePending_&&!value.value("feature_path").toString().isEmpty()){
            featurePending_=true;send({{"op","native_features"},{"path",value.value("feature_path").toString()},{"method",feature},{"channel",channel_},{"options",QJsonObject::fromVariantMap(options_)},{"generation",double(captureGeneration_)}});
        }
    }
    snapshot_=value;
    if(measurementClock_.elapsed()>=1000){
        measurementClock_.restart();auto info=capture_->info();
        send({{"op","native_metrics"},{"record",QJsonObject::fromVariantMap(info)},{"channel",channel_},{"time",value.value("time").toDouble()},{"metrics",QJsonObject::fromVariantMap(value.value("plot").toMap().value("metrics").toMap())},{"channel_metrics",QJsonArray::fromVariantList(value.value("channel_metrics").toList())}});
        message_=value.value("transport").toString()=="six_serial"?QStringLiteral("C++ 六通道实采 · 16000 Hz · 校验错误 %1").arg(value.value("checksum_errors").toULongLong()):QStringLiteral("C++ 实采 · %1 Hz · 处理 %2 ms · XRUN %3").arg(value.value("rate").toInt()).arg(value.value("processing_ms").toDouble(),0,'f',1).arg(value.value("xruns").toULongLong());emit changed();
    }
    emit snapshotChanged();deliveryMs_=delivery.nsecsElapsed()/1e6;
}
void SignalAnalysisService::finishNative(){
    frameTimer_.stop();publishNative();auto info=capture_->info();
    retainedInfo_=info;retainedPcm_=capture_->latestPcm(6);retainedRate_=capture_->rate();retainedChannels_=capture_->channels();
    if(analyzer_){delete analyzer_;analyzer_=nullptr;}
    send({{"op","native_finished"},{"record",QJsonObject::fromVariantMap(info)},{"method",method_},{"channel",channel_},{"options",QJsonObject::fromVariantMap(options_)}});
    capture_->deleteLater();capture_=nullptr;running_=false;
    message_=info.value("error").toString().isEmpty()?QStringLiteral("C++ 采集已保存，原始数据完整保留"):info.value("error").toString();
    if (info.value("frames").toULongLong() > 0 && info.value("error").toString().isEmpty())
        emit saved(info.value("path").toString());
    emit changed();
}
void SignalAnalysisService::importFile(const QString &path){
    if(!session_->can("inspect")||running_||exportBusy()||recordingBusy_){message_="请先登录并等待采集、导入或导出结束";emit changed();return;}
    if(worker_.state()!=QProcess::Running){message_="分析服务尚未就绪，请稍后再试";refresh();emit changed();return;}
    recordingBusy_=true;recordRequest_=QUuid::createUuid().toString();message_="正在导入录音…";emit changed();
    send({{"op","import"},{"path",path.startsWith("file:")?QUrl(path).toLocalFile():path},{"owner_id",session_->userId()},{"request",recordRequest_}});
}
void SignalAnalysisService::selectRecording(const QString &id){
    if(!session_->can("inspect")||running_||recordingBusy_||exportBusy())return;auto r=catalog_->detail(id).value("record").toMap();
    if(r.isEmpty()||!r.value("deleted_at").toString().isEmpty()||!r.value("complete").toBool()||!r.value("file_exists").toBool()){message_="录音不存在、未完成或原始文件缺失";emit changed();return;}
    if(worker_.state()!=QProcess::Running){refresh();return;}recordingBusy_=true;recordRequest_=QUuid::createUuid().toString();emit changed();send({{"op","select"},{"id",id},{"request",recordRequest_}});
}
void SignalAnalysisService::reloadRecordings(){recordingPage_=catalog_->query(recordingFilter_,recordingPageIndex_,20);recordings_=recordingPage_.value("rows").toList();recordingPageIndex_=recordingPage_.value("page").toInt();emit changed();}
void SignalAnalysisService::queryRecordings(const QVariantMap &filter,int page){recordingFilter_=filter;recordingPageIndex_=qMax(0,page);reloadRecordings();}
QVariantMap SignalAnalysisService::recordingDetail(const QString &id)const{return catalog_->detail(id);}
QVariantMap SignalAnalysisService::updateRecording(const QString &id,const QString &name,const QString &notes,int revision){
    if(reportGuard_&&reportGuard_())return {{"ok",false},{"error",QStringLiteral("请先结束语音对话或声学评价报告，再维护录音")}};
    if(running_||recordingBusy_||exportBusy())return {{"ok",false},{"error","请等待采集、读取或导出结束再编辑"}};
    auto result=catalog_->update(id,name,notes,revision);if(result.value("ok").toBool())reloadRecordings();return result;
}
QVariantMap SignalAnalysisService::changeDeleted(const QString &id,bool deleted,int revision){
    if(reportGuard_&&reportGuard_())return {{"ok",false},{"error",QStringLiteral("请先结束语音对话或声学评价报告，再维护录音")}};
    if(running_||recordingBusy_||exportBusy())return {{"ok",false},{"error","请等待采集、读取或导出结束再操作"}};
    auto result=catalog_->setDeleted(id,deleted,revision);
    if(result.value("ok").toBool()){
        if(deleted&&snapshot_.value("recording").toString()==id){snapshot_.clear();retainedInfo_.clear();retainedPcm_.clear();trends_.clear();emit snapshotChanged();emit trendsChanged();send({{"op","clear_selection"}});}
        reloadRecordings();
    }return result;
}
QVariantMap SignalAnalysisService::deleteRecording(const QString &id,int revision){return changeDeleted(id,true,revision);}
QVariantMap SignalAnalysisService::restoreRecording(const QString &id,int revision){return changeDeleted(id,false,revision);}
void SignalAnalysisService::cancelExport(){if(exportThread_)exportThread_->requestInterruption();}
void SignalAnalysisService::exportFile(const QString &path,const QString &format,const QString &scope){
    if(!session_->can("inspect")){message_="请登录后导出";emit changed();return;}
    if(exportBusy()){message_="已有导出任务正在执行";emit changed();return;}
    const QString target=path.startsWith("file:")?QUrl(path).toLocalFile():path;
    if(!QFileInfo(target).isAbsolute()||(format!="csv"&&format!="json")||(scope!="full"&&scope!="window")){message_="导出格式、范围或保存路径无效";emit changed();return;}
    const auto snapshot=snapshot_;auto info=capture_?capture_->info():retainedInfo_;QString source;QString identity=snapshot.value("recording").toString();qint64 pinned=-1;
    if(snapshot.value("native").toBool()&&info.value("id").toString()==identity){source=info.value("path").toString();pinned=info.value("frames").toLongLong();}
    else{source=snapshot.value("recording_path").toString();for(auto row:recordings_)if(row.toMap()["id"].toString()==identity){source=row.toMap()["path"].toString();break;}}
    if(!source.isEmpty()&&QFileInfo(source).absoluteFilePath()==QFileInfo(target).absoluteFilePath()){message_="不能覆盖原始录音文件";emit changed();return;}
    if(format=="csv"&&source.isEmpty()){message_="完整原始录音尚未就绪，请稍后重试";emit changed();return;}
    const auto owner=session_->userId();const double seconds=window_;QVariantMap origin{{"recording_id",identity},{"snapshot_while_recording",capture_!=nullptr},{"display_calibration",snapshot.value("calibration")}};
    exportResult_.clear();message_="正在后台导出…";
    exportThread_=QThread::create([this,target,format,scope,snapshot,source,pinned,seconds,origin,owner]{
        auto cancelled=[](){return QThread::currentThread()->isInterruptionRequested();};QVariantMap result;
        if(format=="csv")result=exportWaveCsv(source,target,scope,seconds,pinned,origin,cancelled);
        else{QSaveFile file(target);auto data=QJsonDocument::fromVariant(snapshot).toJson();bool ok=file.open(QIODevice::WriteOnly)&&file.write(data)==data.size()&&!cancelled()&&file.commit();result={{"ok",ok},{"path",target},{"error",ok?"":"分析文件导出失败或已取消"}};}
        QMetaObject::invokeMethod(this,[this,result,owner,target,scope,format]{
            exportThread_=nullptr;
            if(session_->userId()!=owner){emit changed();return;}
            exportResult_=result;
            if(result.value("ok").toBool()){
                message_=format=="csv"?QStringLiteral("已导出%1：%2 行，%3–%4 s，原始 FS 数据").arg(scope=="full"?"整段录音快照":"当前窗口").arg(result.value("rows").toLongLong()).arg(result.value("start_seconds").toDouble(),0,'f',3).arg(result.value("end_seconds_exclusive").toDouble(),0,'f',3):"已保存："+target;
                if(result.contains("warning"))message_+="；"+result["warning"].toString();emit saved(target);
            }else message_=result.value("error").toString();emit changed();
        },Qt::QueuedConnection);
    });
    auto *thread=exportThread_.data();thread->setParent(this);connect(thread,&QThread::finished,thread,&QObject::deleteLater);thread->start();emit changed();
}
void SignalAnalysisService::read(){
    buffer_+=worker_.readAllStandardOutput();
    while(buffer_.contains('\n')){
        const auto n=buffer_.indexOf('\n');const auto obj=QJsonDocument::fromJson(buffer_.left(n)).object();buffer_.remove(0,n+1);
        const auto type=obj["event"].toString();
        if(type=="devices"){discovering_=false;discoveryTimer_.stop();devices_=obj["devices"].toArray().toVariantList();methods_=obj["methods"].toArray().toVariantList();deviceStatus_=obj["note"].toString();if(deviceStatus_.isEmpty())deviceStatus_=devices_.isEmpty()?QStringLiteral("未发现采集设备，请检查连接后刷新"):QStringLiteral("已找到 %1 个采集设备").arg(devices_.size());message_=deviceStatus_;emit changed();}
        else if(type=="records"){reloadRecordings();}
        else if(type=="trends"){if(!session_->can("inspect")||obj["reply_user"].toString()!=session_->userId())continue;trends_=obj["trends"].toArray().toVariantList();emit trendsChanged();}
        else if(type=="snapshot"){if(capture_||!session_->can("inspect")||obj["reply_user"].toString()!=session_->userId())continue;snapshot_=obj.toVariantMap();message_=QStringLiteral("真实采样 · ")+QString::number(obj["rate"].toInt())+QStringLiteral(" Hz · ")+obj["unit"].toString();emit snapshotChanged();emit changed();}
        else if(type=="record_operation"&&obj["request"].toString()==recordRequest_){recordingBusy_=false;if(session_->can("inspect")&&obj["reply_user"].toString()==session_->userId()){message_=obj["ok"].toBool()?QStringLiteral("录音已读取"):QStringLiteral("录音读取失败：")+obj["error"].toString();emit recordingOperationFinished(obj["ok"].toBool(),message_);}reloadRecordings();}
        else if(type=="feature_reply"){
            featurePending_=false;
            if(capture_&&quint64(obj["generation"].toDouble())==captureGeneration_&&obj["method"].toString()==method_){snapshot_["plot"]=obj["plot"].toObject().toVariantMap();emit snapshotChanged();}
        }
        else if(type=="capture"){running_=obj["running"].toBool();message_=running_?QStringLiteral("正在采集并保存原始 WAV"):QStringLiteral("采集已停止，录音已保存");emit changed();}
        else if(type=="error"){message_=obj["message"].toString();emit changed();}
        else if(type=="saved"){message_=QStringLiteral("已保存：")+obj["path"].toString();emit changed();emit saved(obj["path"].toString());}
    }
}
