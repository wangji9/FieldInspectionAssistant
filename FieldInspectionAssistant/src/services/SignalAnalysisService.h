#pragma once
#include <QtCore>
#include <functional>
class SessionManager;class DatabaseManager;class RecordingCatalog;
class NativeAudioCapture;class NativeSignalAnalyzer;
struct SixChannelAudioWindow {
    QByteArray pcm;
    QString captureId;
    quint64 endFrame = 0;
    qint64 receivedMs = 0;
    int sampleRate = 0;
};

class SignalAnalysisService final: public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList methods READ methods NOTIFY changed)
    Q_PROPERTY(QVariantList recordings READ recordings NOTIFY changed)
    Q_PROPERTY(QVariantMap recordingPage READ recordingPage NOTIFY changed)
    Q_PROPERTY(bool recordingBusy READ recordingBusy NOTIFY changed)
    Q_PROPERTY(QVariantList trends READ trends NOTIFY trendsChanged)
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotChanged)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY snapshotChanged)
    Q_PROPERTY(double processingMs READ processingMs NOTIFY snapshotChanged)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY changed)
    Q_PROPERTY(QVariantMap exportResult READ exportResult NOTIFY changed)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    Q_PROPERTY(QString deviceStatus READ deviceStatus NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
public:
    SixChannelAudioWindow sixChannelWindow(double seconds) const;
    SignalAnalysisService(DatabaseManager *db,SessionManager *session,const QString &folder,QObject *parent=nullptr);
    ~SignalAnalysisService() override;
    void setReportGuard(std::function<bool()> guard){reportGuard_=guard;}
    QVariantList devices()const{return devices_;} QVariantList methods()const{return methods_;}
    QVariantList recordings()const{return recordings_;} QVariantList trends()const{return trends_;}
    QVariantMap recordingPage()const{return recordingPage_;}
    bool recordingBusy()const{return recordingBusy_;}
    Q_INVOKABLE void queryRecordings(const QVariantMap &filter=QVariantMap(),int page=0);
    Q_INVOKABLE QVariantMap recordingDetail(const QString &id)const;
    Q_INVOKABLE QVariantMap updateRecording(const QString &id,const QString &name,const QString &notes,int revision);
    Q_INVOKABLE QVariantMap deleteRecording(const QString &id,int revision);
    Q_INVOKABLE QVariantMap restoreRecording(const QString &id,int revision);
    QVariantMap snapshot()const{return snapshot_;} QString message()const{return message_;}
    double processingMs()const{return snapshot_.value("processing_ms").toDouble();}
    Q_INVOKABLE double deliveryMs()const{return deliveryMs_;}
    QVariantMap summary()const;
    bool exportBusy()const{return exportThread_!=nullptr;}
    QVariantMap exportResult()const{return exportResult_;}
    Q_INVOKABLE void cancelExport();
    bool running()const{return running_;}
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshDevices(bool withData=false);
    bool discovering()const{return discovering_;}
    QString deviceStatus()const{return deviceStatus_;}
    Q_INVOKABLE void start(const QString &source);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void configure(const QString &method,int channel,double window,const QVariantMap &options=QVariantMap());
    Q_INVOKABLE void importFile(const QString &path);
    Q_INVOKABLE void selectRecording(const QString &id);
    Q_INVOKABLE void exportFile(const QString &path,const QString &format,const QString &scope=QStringLiteral("full"));
signals:
    void changed();void snapshotChanged();void trendsChanged();void saved(const QString &path);
    void recordingOperationFinished(bool ok,const QString &message);
private:
    std::function<bool()> reportGuard_;
    void send(const QJsonObject &object);void read();
    void publishNative();void finishNative();
    void reloadRecordings();QVariantMap changeDeleted(const QString &id,bool deleted,int revision);
    RecordingCatalog *catalog_=nullptr;QVariantMap recordingPage_,recordingFilter_;int recordingPageIndex_=0;
    bool discovering_=false;QString deviceStatus_;QTimer discoveryTimer_;
    bool recordingBusy_=false;QString recordRequest_;
    SessionManager *session_;QString folder_,message_=QStringLiteral("选择真实采集设备或导入 WAV");
    QProcess worker_;QByteArray buffer_;QVariantList devices_,methods_,recordings_,trends_;
    QVariantMap snapshot_;bool running_=false;
    double deliveryMs_=0;
    NativeAudioCapture *capture_=nullptr;NativeSignalAnalyzer *analyzer_=nullptr;QTimer frameTimer_;
    QString method_="time";int channel_=0;double window_=.5;QVariantMap options_;
    bool featurePending_=false;QElapsedTimer measurementClock_;quint64 captureGeneration_=0;
    QVariantMap exportResult_,retainedInfo_;QPointer<QThread> exportThread_;
    QByteArray retainedPcm_;int retainedRate_=16000,retainedChannels_=1;
};
