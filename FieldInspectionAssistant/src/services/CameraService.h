#pragma once
#include <QtCore>
#include <QImage>
#include <memory>
#include "NativeCameraPipeline.h"

class SessionManager;
class CameraService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString saveDirectory READ saveDirectory WRITE setSaveDirectory NOTIFY settingsChanged)
    Q_PROPERTY(QString resolution READ resolution NOTIFY settingsChanged)
    Q_PROPERTY(int fps READ fps NOTIFY settingsChanged)
    Q_PROPERTY(int rotation READ rotation WRITE setRotation NOTIFY settingsChanged)
    Q_PROPERTY(bool mirrored READ mirrored WRITE setMirrored NOTIFY settingsChanged)
    Q_PROPERTY(bool opening READ opening NOTIFY stateChanged)
    Q_PROPERTY(bool closing READ closing NOTIFY stateChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY stateChanged)
    Q_PROPERTY(qint64 frameCapturedMs READ frameCapturedMs NOTIFY frameMetadataChanged)
    Q_PROPERTY(int frameRotation READ frameRotation NOTIFY frameMetadataChanged)
    Q_PROPERTY(bool frameMirrored READ frameMirrored NOTIFY frameMetadataChanged)
    Q_PROPERTY(bool photoSaving READ photoSaving NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY metricsChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool finalizing READ finalizing NOTIFY recordingChanged)
    Q_PROPERTY(int recordingSeconds READ recordingSeconds NOTIFY recordingChanged)
    Q_PROPERTY(QString lastPhoto READ lastPhoto NOTIFY mediaSaved)
    Q_PROPERTY(QString lastVideo READ lastVideo NOTIFY mediaSaved)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    void setSession(SessionManager*s){session_=s;}
    explicit CameraService(QObject *parent=nullptr);
    ~CameraService()override;
    QImage currentFrame()const{return lastFrame_;}
    qint64 frameCapturedMs()const{return frameCapturedMs_;}
    int frameRotation()const{return frameRotation_;}
    bool frameMirrored()const{return frameMirrored_;}
    bool isOpen()const{return connected_;}
    bool opening()const{return opening_;}bool closing()const{return closing_;}
    bool hasFrame()const{return !lastFrame_.isNull();}bool photoSaving()const{return photoSaving_;}
    QVariantMap metrics()const{return metrics_;}
    static QStringList availableDevices();
    Q_INVOKABLE QStringList resolutions(const QString &device)const;
    QString saveDirectory()const{return saveDirectory_;}
    void setSaveDirectory(const QString &path);
    QString resolution()const{return QString::number(width_)+"×"+QString::number(height_);}
    int fps()const{return fps_;}int rotation()const{return rotation_;}void setRotation(int value);
    bool mirrored()const{return mirrored_;}void setMirrored(bool value);
    bool recording()const{return recording_;}bool finalizing()const{return finalizing_;}
    int recordingSeconds()const{return recording_?recordClock_.elapsed()/1000:recordedSeconds_;}
    QString lastPhoto()const{return lastPhoto_;}QString lastVideo()const{return lastVideo_;}QString status()const{return status_;}
    Q_INVOKABLE bool configure(int width,int height,int fps);
    Q_INVOKABLE void setPreviewSize(int width,int height);
    bool openNativePreview(const QString &path);
    Q_INVOKABLE bool savePhotoAsync(const QString &format="JPG",int quality=92);
    Q_INVOKABLE QString savePhoto(const QString &format="JPG",int quality=92);
    Q_INVOKABLE QString photoForQuestion();
    Q_INVOKABLE bool startVideo(int fps=15,int maxSeconds=300,int quality=4);
    Q_INVOKABLE void stopVideo();
    Q_INVOKABLE void openSaveDirectory();
public slots:
    bool openDevice(const QString &path);
    void closeDevice();
    Q_INVOKABLE bool captureJpeg(const QString &filePath);
signals:
    void frameReady(const QImage &frame);
    void frameMetadataChanged();
    void opened();void closed();void error(const QString &message);
    void settingsChanged();void stateChanged();void metricsChanged();void recordingChanged();void mediaSaved();void statusChanged();
private:
    bool authorized();
    bool openCapture(const QString &path, bool nativePreview);
    SessionManager *session_=nullptr;
    void setStatus(const QString &message);
    QString newMediaPath(const QString &extension)const;
    void refreshPreview();void captureFinished();void recorderFinished();
    void photoSaved(const QString &path,bool ok);
    NativeCameraCapture *capture_=nullptr;NativeCameraRecorder *recorder_=nullptr;
    std::shared_ptr<CameraFrameStore> store_;
    QTimer previewTimer_;QElapsedTimer statsClock_,recordClock_;
    QImage lastFrame_;QSize previewSize_{960,540};QVariantMap metrics_;
    bool nativePreview_=false;
    quint64 sequence_=0;
    qint64 frameCapturedMs_=0;
    int frameRotation_=0;
    bool frameMirrored_=false;
    QString saveDirectory_,lastPhoto_,lastVideo_,status_;
    int width_=1280,height_=720,fps_=30,rotation_=0,recordedSeconds_=0,maxVideoSeconds_=300;
    bool mirrored_=false,connected_=false,opening_=false,closing_=false,photoSaving_=false,recording_=false,finalizing_=false;
};
