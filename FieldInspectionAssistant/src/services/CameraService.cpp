#include "CameraService.h"
#include "../app/SessionManager.h"
#include <QImageReader>
#include <QDesktopServices>
#include <QStorageInfo>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

CameraService::CameraService(QObject *parent):QObject(parent){
    QSettings settings;saveDirectory_=settings.value("capture/saveDirectory",QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)+QStringLiteral("/现场巡检")).toString();
    auto path=settings.value("capture/lastPhoto").toString();if(QFileInfo(path).isFile()&&QFileInfo(path).isReadable())lastPhoto_=path;
    previewTimer_.setInterval(33);previewTimer_.setTimerType(Qt::PreciseTimer);connect(&previewTimer_,&QTimer::timeout,this,&CameraService::refreshPreview);
}
CameraService::~CameraService(){
    previewTimer_.stop();if(capture_)capture_->requestInterruption();if(recorder_)recorder_->requestInterruption();
    if(recorder_){recorder_->wait();delete recorder_;recorder_=nullptr;}
    if(capture_){capture_->wait();delete capture_;capture_=nullptr;}
    for(auto *task:findChildren<QThread*>(QString(),Qt::FindDirectChildrenOnly))task->wait();
}
QStringList CameraService::availableDevices(){
    QStringList usb,other;QDir dir("/dev");for(auto info:dir.entryInfoList({"video*"},QDir::System|QDir::Files)){
        auto path=info.absoluteFilePath();int fd=::open(path.toLocal8Bit().constData(),O_RDWR|O_NONBLOCK);if(fd<0)continue;
        v4l2_capability cap{};if(ioctl(fd,VIDIOC_QUERYCAP,&cap)==0){auto flags=cap.device_caps?cap.device_caps:cap.capabilities;if((flags&V4L2_CAP_VIDEO_CAPTURE)&&(flags&V4L2_CAP_STREAMING)){
            auto name=QString::fromLocal8Bit(reinterpret_cast<char*>(cap.card)).trimmed();auto label=path+(name.isEmpty()?QString():" ("+name+")");
            if(QByteArray(reinterpret_cast<char*>(cap.bus_info)).startsWith("usb"))usb<<label;else other<<label;
        }}::close(fd);
    }return usb+other;
}
QStringList CameraService::resolutions(const QString&device)const{
    QStringList rows;int fd=::open(device.section('(',0,0).trimmed().toLocal8Bit().constData(),O_RDWR|O_NONBLOCK);if(fd<0)return rows;
    v4l2_frmsizeenum size{};size.pixel_format=V4L2_PIX_FMT_MJPEG;for(size.index=0;ioctl(fd,VIDIOC_ENUM_FRAMESIZES,&size)==0;++size.index)if(size.type==V4L2_FRMSIZE_TYPE_DISCRETE)rows<<QString::number(size.discrete.width)+"×"+QString::number(size.discrete.height);
    ::close(fd);return rows;
}
bool CameraService::authorized(){if(session_&&session_->can("inspect"))return true;setStatus("请使用有效账号登录后操作摄像头");return false;}
void CameraService::setStatus(const QString&message){status_=message;emit statusChanged();}
bool CameraService::configure(int width,int height,int fps){
    if(!authorized())return false;
    if(capture_||recording_||finalizing_||photoSaving_){setStatus("请先关闭摄像头，再修改采集设置");return false;}
    if(width<160||width>4096||height<120||height>4096||fps<1||fps>60)return false;
    width_=width;height_=height;fps_=fps;emit settingsChanged();return true;
}
void CameraService::setPreviewSize(int width,int height){
    previewSize_=QSize(qBound(160,width,1280),qBound(120,height,720));if(store_&&!nativePreview_){QMutexLocker lock(&store_->mutex);store_->previewLimit=previewSize_;}
}
bool CameraService::openDevice(const QString&path){return openCapture(path,false);}
bool CameraService::openNativePreview(const QString&path){return openCapture(path,true);}
bool CameraService::openCapture(const QString&path,bool nativePreview){
    if(!authorized())return false;
    if(capture_||recording_||finalizing_||photoSaving_){setStatus("摄像头正在工作或关闭中，请稍后重试");return false;}
    nativePreview_=nativePreview;
    capture_=new NativeCameraCapture(path,width_,height_,fps_,this);store_=capture_->store();
    {QMutexLocker lock(&store_->mutex);store_->rotation=rotation_;store_->mirrored=mirrored_;store_->previewLimit=nativePreview_?QSize():previewSize_;store_->notifyPreview=nativePreview_;}
    opening_=true;closing_=false;connected_=false;sequence_=0;metrics_.clear();lastFrame_=QImage();
    if (nativePreview_)
        connect(capture_, &NativeCameraCapture::previewAvailable, this, &CameraService::refreshPreview, Qt::QueuedConnection);
    previewTimer_.setInterval(nativePreview_ ? 250 : 33);
    connect(capture_,&QThread::finished,this,&CameraService::captureFinished);statsClock_.start();previewTimer_.start();capture_->start();
    setStatus("正在打开摄像头…");emit stateChanged();return true;
}
void CameraService::closeDevice(){
    if(recording_)stopVideo();if(!capture_)return;closing_=true;opening_=false;connected_=false;capture_->requestInterruption();
    lastFrame_=QImage();emit frameReady(lastFrame_);emit stateChanged();emit closed();setStatus(finalizing_?"正在关闭摄像头并保存录像…":"正在关闭摄像头…");
}
void CameraService::captureFinished(){
    QString failure;if(store_){QMutexLocker lock(&store_->mutex);failure=store_->error;}
    if(recording_)stopVideo();previewTimer_.stop();connected_=false;opening_=false;closing_=false;lastFrame_=QImage();
    capture_->deleteLater();capture_=nullptr;store_.reset();emit frameReady(lastFrame_);emit closed();emit stateChanged();
    if(!failure.isEmpty()){setStatus(failure);emit error(failure);}else if(!finalizing_)setStatus("摄像头已关闭");
}
void CameraService::refreshPreview(){
    if(!store_||closing_)return;QImage frame;bool active=false;quint64 sequence;qint64 capturedMs=0;QVariantMap values;
    {QMutexLocker lock(&store_->mutex);active=store_->opened;sequence=store_->sequence;if(sequence!=sequence_){frame=store_->preview;capturedMs=store_->previewCapturedMs;}
        if (sender() == capture_) store_->previewNotificationPending = false;
        if(opening_&&active){width_=store_->width;height_=store_->height;fps_=store_->fps;}
        if(statsClock_.elapsed()>=1000)values={{"capture_frames",QVariant::fromValue(store_->captured)},{"decoded_frames",QVariant::fromValue(store_->decoded)},{"stale_frames",QVariant::fromValue(store_->stale)},{"invalid_frames",QVariant::fromValue(store_->invalid)},{"decode_ms",store_->decodeMs},{"preview_fps",store_->actualFps},{"preview_width",store_->preview.width()},{"preview_height",store_->preview.height()},{"native",true}};
    }
    if(opening_&&active){opening_=false;connected_=true;emit settingsChanged();emit stateChanged();emit opened();setStatus("摄像头已打开 · 实时画面");}
    if(!frame.isNull()){bool first=lastFrame_.isNull();sequence_=sequence;lastFrame_=frame;frameCapturedMs_=capturedMs;emit frameReady(lastFrame_);if(first)emit stateChanged();}
    if(!values.isEmpty()){statsClock_.restart();metrics_=values;emit metricsChanged();if(recording_)emit recordingChanged();}
    if(recording_&&recordClock_.elapsed()>=maxVideoSeconds_*1000)stopVideo();
}
void CameraService::setRotation(int value){if(!authorized())return;if(recording_||finalizing_)return;rotation_=((value%360+360)%360)/90*90;if(store_){QMutexLocker lock(&store_->mutex);store_->rotation=rotation_;}emit settingsChanged();}
void CameraService::setMirrored(bool value){if(!authorized())return;if(recording_||finalizing_)return;mirrored_=value;if(store_){QMutexLocker lock(&store_->mutex);store_->mirrored=mirrored_;}emit settingsChanged();}
void CameraService::setSaveDirectory(const QString&path){
    if(!authorized())return;
    if(recording_||finalizing_||photoSaving_){setStatus("请等待文件保存完成后修改目录");return;}auto value=path.startsWith("file:")?QUrl(path).toLocalFile():path.trimmed();
    if(!QDir::isAbsolutePath(value)){setStatus("保存路径必须为绝对路径");emit error(status_);return;}
    if(!QDir().mkpath(value)||!QFileInfo(value).isDir()||!QFileInfo(value).isWritable()){setStatus("保存目录不可用，请选择可写目录");emit error(status_);return;}
    saveDirectory_=QDir::cleanPath(value);QSettings().setValue("capture/saveDirectory",saveDirectory_);emit settingsChanged();
}
QString CameraService::newMediaPath(const QString&extension)const{return QDir(saveDirectory_).filePath(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz")+"_"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(6)+"."+extension);}
void CameraService::photoSaved(const QString&path,bool ok){
    photoSaving_=false;emit stateChanged();if(!ok){setStatus("照片保存失败，请检查目录权限和空间");emit error(status_);return;}
    lastPhoto_=path;QSettings().setValue("capture/lastPhoto",path);setStatus("照片已保存："+path);emit mediaSaved();
}
bool CameraService::savePhotoAsync(const QString&format,int quality){
    if(!authorized())return false;
    if(recording_||finalizing_){setStatus("请先停止录像并等待保存完成再拍照");return false;}
    if(photoSaving_||opening_||closing_||!isOpen()||!hasFrame()){setStatus("请先打开摄像头并等待实时画面");return false;}auto jpeg=cameraJpeg(store_);if(jpeg.isEmpty())return false;
    auto type=format.toUpper()=="PNG"?QByteArray("PNG"):QByteArray("JPG");auto path=newMediaPath(type.toLower());int rotation=rotation_;bool mirrored=mirrored_;
    photoSaving_=true;setStatus("正在后台保存照片…");emit stateChanged();
    auto *task=QThread::create([this,jpeg,path,type,quality,rotation,mirrored]{bool ok=saveCameraPhoto(jpeg,path,type,quality,rotation,mirrored);QMetaObject::invokeMethod(this,[this,path,ok]{photoSaved(path,ok);},Qt::QueuedConnection);});
    task->setParent(this);connect(task,&QThread::finished,task,&QObject::deleteLater);task->start();return true;
}
QString CameraService::savePhoto(const QString&format,int quality){
    if(!authorized()||recording_||finalizing_||photoSaving_)return {};
    if(!isOpen()||!hasFrame())return {};auto type=format.toUpper()=="PNG"?QByteArray("PNG"):QByteArray("JPG");auto path=newMediaPath(type.toLower());bool ok=saveCameraPhoto(cameraJpeg(store_),path,type,quality,rotation_,mirrored_);photoSaved(path,ok);return ok?path:QString();
}
bool CameraService::captureJpeg(const QString&path){if(!authorized())return false;if(!isOpen()||!hasFrame())return false;return saveCameraPhoto(cameraJpeg(store_),path,"JPG",92,rotation_,mirrored_);}
QString CameraService::photoForQuestion(){
    if(recording_||finalizing_||photoSaving_){setStatus("请先等待照片或录像保存完成");return {};}
    if(lastPhoto_.isEmpty()){setStatus("请先拍照保存，再用于图片问答");return {};}
    if(!QFileInfo(lastPhoto_).isReadable()||!QImageReader(lastPhoto_).canRead()){lastPhoto_.clear();QSettings().remove("capture/lastPhoto");emit mediaSaved();setStatus("最近照片已移走或无法读取，请重新拍照");return {};}
    return lastPhoto_;
}
bool CameraService::startVideo(int fps,int seconds,int quality){
    if(!authorized())return false;
    if(recorder_||photoSaving_||opening_||closing_||!isOpen()||!hasFrame()){setStatus("请先打开摄像头并等待画面");return false;}
    if(!QDir().mkpath(saveDirectory_)||QStorageInfo(saveDirectory_).bytesAvailable()<100*1024*1024){setStatus("保存目录不可用或剩余空间不足 100 MB");return false;}
    maxVideoSeconds_=qBound(5,seconds,3600);recordedSeconds_=0;recording_=true;finalizing_=false;recordClock_.start();
    recorder_=new NativeCameraRecorder(store_,newMediaPath("mp4"),qBound(1,qMin(fps,fps_),30),maxVideoSeconds_,qBound(2,quality,10),rotation_,mirrored_,this);
    connect(recorder_,&QThread::finished,this,&CameraService::recorderFinished);recorder_->start();setStatus("正在录像 · 实时画面持续更新");emit recordingChanged();return true;
}
void CameraService::stopVideo(){if(!recorder_||!recording_)return;recordedSeconds_=recordClock_.elapsed()/1000;recording_=false;finalizing_=true;recorder_->requestInterruption();setStatus("正在后台保存录像…");emit recordingChanged();}
void CameraService::recorderFinished(){
    auto result=recorder_->result();recording_=false;finalizing_=false;recordedSeconds_=qRound(result["duration"].toDouble());recorder_->deleteLater();recorder_=nullptr;
    if(result["ok"].toBool()){lastVideo_=result["path"].toString();setStatus("录像已保存："+lastVideo_);emit mediaSaved();}else{setStatus("录像保存失败："+result["error"].toString());emit error(status_);}emit recordingChanged();
}
void CameraService::openSaveDirectory(){QDir().mkpath(saveDirectory_);QDesktopServices::openUrl(QUrl::fromLocalFile(saveDirectory_));}
