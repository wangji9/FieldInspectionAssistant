#pragma once
#include <QtCore>
#include <functional>

class SpeechService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(bool speechBusy READ speechBusy NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(int seconds READ seconds NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString recordingPath READ recordingPath NOTIFY changed)
    Q_PROPERTY(QString speechPath READ speechPath NOTIFY changed)
    Q_PROPERTY(QString currentPlaybackPath READ currentPlaybackPath NOTIFY changed)
    Q_PROPERTY(qreal playbackDuration READ playbackDuration NOTIFY changed)
    Q_PROPERTY(qreal playbackPosition READ playbackPosition NOTIFY changed)
public:
    explicit SpeechService(QObject *parent = nullptr);
    ~SpeechService() override;
    void setExternalGuard(std::function<bool()> f){externalGuard_=f;}
    bool recording() const { return recording_; }
    bool speechBusy() const { return synth_.state()!=QProcess::NotRunning || player_.state()!=QProcess::NotRunning || !queue_.isEmpty(); }
    bool playing() const { return player_.state()!=QProcess::NotRunning; }
    int seconds() const { return seconds_; }
    QString message() const { return message_; }
    QString error() const { return error_; }
    QString recordingPath() const { return recordingPath_; }
    QString speechPath() const { return speechPath_; }
    QString currentPlaybackPath() const { return playbackPath_; }
    qreal playbackDuration() const { return duration_; }
    qreal playbackPosition() const;
    Q_INVOKABLE void startRecording(bool autoStop=false);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void cancelRecording();
    Q_INVOKABLE void speak(const QString &text);
    Q_INVOKABLE void playAudio(const QString &path);
    Q_INVOKABLE void stopSpeech();
signals:
    void changed();
    void recordingReady(const QString &path);
    void speechFinished(bool ok);
    void speechReady(const QString &path, qreal duration);
    void speechChunkReady(const QString &path, qreal duration);
    void audioPrepared(const QString &path, qreal duration);
private:
    std::function<bool()> externalGuard_;
    void readCapture();
    void readSynth();
    void fail(const QString &message);
    void failPlayback(const QString &message);
    void startNext();
    void finishPlayback();
    void cleanupParts();
    QProcess capture_, synth_, player_;
    QTimer clock_, captureWatchdog_, playbackTick_;
    QElapsedTimer playbackClock_;
    QByteArray captureBuffer_, synthBuffer_;
    QString recordingPath_, speechPath_, textPath_, message_=QStringLiteral("准备就绪"), error_, sink_;
    QString playbackPath_, playingPart_, convertedPath_;
    QQueue<QString> queue_;
    QStringList temporaryFiles_;
    QHash<QString,qreal> partDurations_;
    qreal duration_=0, played_=0;
    bool replay_=false, synthDone_=false, ready_=false;
    bool recording_=false, discard_=false, stopping_=false, speechCancelled_=false;
    int seconds_=0;
    bool autoStop_=false;
};
