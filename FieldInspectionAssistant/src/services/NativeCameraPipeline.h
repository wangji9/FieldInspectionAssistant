#pragma once
#include <QtCore>
#include <QImage>
#include <memory>

struct CameraFrameStore {
    mutable QMutex mutex;
    QByteArray jpeg;
    QImage preview;
    QSize previewLimit{960,540};
    int width=1280,height=720,fps=30,rotation=0;
    bool mirrored=false,opened=false;
    bool notifyPreview=false,previewNotificationPending=false;
    quint64 captured=0,decoded=0,sequence=0,stale=0,invalid=0;
    qint64 previewCapturedMs=0;
    int previewRotation=0;
    bool previewMirrored=false;
    double decodeMs=0,actualFps=0;
    QString error;
};

class NativeCameraCapture final:public QThread {
    Q_OBJECT
public:
    NativeCameraCapture(const QString &device,int width,int height,int fps,QObject *parent=nullptr);
    ~NativeCameraCapture()override;
    std::shared_ptr<CameraFrameStore> store()const{return store_;}
signals:
    void previewAvailable();
protected:void run()override;
private:QString device_;std::shared_ptr<CameraFrameStore> store_;
};

class NativeCameraRecorder final:public QThread {
public:
    NativeCameraRecorder(std::shared_ptr<CameraFrameStore>,const QString &target,int fps,int seconds,int quality,int rotation,bool mirrored,QObject *parent=nullptr);
    ~NativeCameraRecorder()override;
    QVariantMap result()const;
protected:void run()override;
private:
    std::shared_ptr<CameraFrameStore> store_;QString target_;
    int fps_,seconds_,quality_,rotation_;bool mirrored_;
    mutable QMutex mutex_;QVariantMap result_;
};

QByteArray cameraJpeg(const std::shared_ptr<CameraFrameStore>&store);
bool saveCameraPhoto(const QByteArray &jpeg,const QString &path,const QByteArray &format,int quality,int rotation,bool mirrored);
