#pragma once

#include <QFutureWatcher>
#include <QJsonArray>
#include <QThreadPool>
#include <QUrl>
#include <QVariant>
#include <atomic>
#include <memory>

class SessionManager;
class SpeakerRecognitionEngine;

class SpeakerRecognitionService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool whisperReady READ whisperReady NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QVariantList segments READ segments NOTIFY changed)
    Q_PROPERTY(QVariantList waveform READ waveform NOTIFY changed)
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY changed)

public:
    explicit SpeakerRecognitionService(SessionManager *session, QObject *parent = nullptr);
    ~SpeakerRecognitionService() override;
    bool ready() const { return ready_; }
    bool busy() const { return watcher_.isRunning() || bool(cancelled_); }
    bool whisperReady() const { return whisperReady_; }
    QString status() const { return status_; }
    QVariantList profiles() const;
    QVariantList segments() const { return segments_; }
    QVariantList waveform() const { return waveform_; }
    QVariantMap metrics() const { return metrics_; }

    bool canClassifyBeam() const;
    bool classifyBeam(const QByteArray &pcm, double threshold, quint64 request);
    Q_INVOKABLE void warmup();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool enroll(const QUrl &source, double start, double length, const QString &id,
                            const QString &name, const QString &language);
    Q_INVOKABLE bool analyze(const QUrl &source, double start, double length, double threshold, bool transcribe);
    Q_INVOKABLE bool updateProfile(const QString &id, const QString &name, const QString &language);
    Q_INVOKABLE bool removeProfile(const QString &id);
    Q_INVOKABLE bool exportResult(const QUrl &destination) const;

signals:
    void changed();
    void beamClassified(quint64 request, const QVariantMap &match);

private:
    bool report(const QString &status);
    void complete();
    bool saveProfiles(const QJsonArray &candidate);
    void readProfiles();
    int profileIndex(const QString &id) const;

    SessionManager *session_;
    std::shared_ptr<SpeakerRecognitionEngine> engine_;
    QThreadPool pool_;
    QFutureWatcher<QVariantMap> watcher_, beamWatcher_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    QString libraryPath_, status_, owner_, pendingProfile_, pendingName_, pendingLanguage_;
    QString beamOwner_;
    quint64 beamRequest_ = 0;
    QJsonArray profiles_;
    QVariantList segments_, waveform_;
    QVariantMap metrics_;
    bool ready_ = false, whisperReady_ = false, storageValid_ = true, beamActive_ = false;
};
