#pragma once
#include <QtCore>
#include <atomic>
#include <memory>
#include <vector>

// The audio producer never waits for DSP, Python, SQL or the GUI event loop.
class NativeAudioCapture final:public QThread{
public:
    NativeAudioCapture(const QVariantMap &device,const QString &folder,QObject *parent=nullptr);
    ~NativeAudioCapture() override;
    QVector<double> latest(double seconds,int channel)const;
    QByteArray latestPcm(double seconds, quint64 *endFrame = nullptr, qint64 *receivedMs = nullptr)const;
    QVariantMap info()const;
    quint64 sequence()const{return sequence_.load();}
    int rate()const{return rate_;}int channels()const{return channels_;}
protected:void run()override;
private:
    void runSerial();
    bool appendPcm(QFile &file,const char *data,int bytes);
    QVariantMap device_;QString path_,id_,error_;int rate_,channels_;
    mutable QMutex mutex_;QVector<qint16> ring_;quint64 write_=0,available_=0;qint64 receivedMs_=0;
    std::atomic<quint64>sequence_{0},frames_{0},xruns_{0};double started_=0;
    std::atomic<quint64>packets_{0},checksumErrors_{0};
    std::atomic_bool hasSignal_{false};
    std::atomic_bool finalized_{false};
};

class NativeSignalAnalyzer final:public QThread{
public:
    NativeSignalAnalyzer(NativeAudioCapture *capture,const QString &folder,QObject *parent=nullptr);
    ~NativeSignalAnalyzer()override;
    void configure(const QString &method,int channel,double seconds,const QVariantMap &options);
    QVariantMap takeSnapshot();
    static bool nativeMethod(const QString &method);
protected:void run()override;
private:
    NativeAudioCapture *capture_;QString folder_;
    QMutex mutex_;QString method_="time";int channel_=0;double seconds_=.5;
    QVariantMap options_,latest_;quint64 revision_=0,taken_=0;
};

QVariantMap nativeSignalPlot(const QVector<double>&samples,int fs,const QString &method,const QString &unit,bool includeMetrics=true);
bool writePcmWav(const QString &path,const QByteArray &pcm,int fs,int channels);
qint64 recoverCapturedWav(const QString &path,int rate,int channels);
