#pragma once

#include "AcousticImagingEngine.h"
#include <QFutureWatcher>
#include <QThreadPool>
#include <QTimer>
#include <memory>

class SignalAnalysisService;
class CameraService;
class EvidenceService;
class SpeakerRecognitionService;
class SessionManager;

class AcousticImagingService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString heatmap READ heatmap NOTIFY changed)
    Q_PROPERTY(QVariantMap observation READ observation NOTIFY changed)
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY changed)
    Q_PROPERTY(QStringList cameras READ cameras NOTIFY changed)

public:
    AcousticImagingService(SignalAnalysisService *audio, CameraService *camera, EvidenceService *evidence,
                           SpeakerRecognitionService *speakers, SessionManager *session, QObject *parent = nullptr);
    ~AcousticImagingService() override;
    bool running() const { return running_; }
    QString status() const { return status_; }
    QString heatmap() const { return heatmap_; }
    QVariantMap observation() const { return observation_; }
    QVariantMap parameters() const { return parameters_; }
    QStringList cameras() const { return cameras_; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool configure(const QVariantMap &values);
    Q_INVOKABLE bool start(const QString &cameraPath);
    Q_INVOKABLE void stop();

signals:
    void changed();

private:
    struct Analysis {
        AcousticImageFrame frame;
        QByteArray beam;
        quint64 revision = 0;
        quint64 endFrame = 0;
        qint64 receivedMs = 0;
        qint64 voiceStartMs = 0, voiceEndMs = 0;
        int rate = 0;
    };

    void sample();
    void receive();
    void clearObservation();
    void updateSpeaker();
    bool adopt(const QVariantMap &values);
    bool reject(const QString &text);

    SignalAnalysisService *audio_;
    CameraService *camera_;
    EvidenceService *evidence_;
    SpeakerRecognitionService *speakers_;
    SessionManager *session_;
    std::shared_ptr<AcousticImagingEngine> engine_;
    QThreadPool pool_;
    QFutureWatcher<Analysis> watcher_;
    QTimer timer_;
    AcousticArrayConfig config_;
    QVariantMap parameters_, observation_, match_;
    QString captureId_, status_, heatmap_;
    QStringList cameras_;
    QVector3D direction_, stableDirection_, voiceDirection_;
    quint64 revision_ = 1, engineRevision_ = 0, lastFrame_ = 0, stableSince_ = 0, lastVoice_ = 0, beamRequest_ = 0;
    qint64 lastReceived_ = 0, lastLocalized_ = 0, voiceCaptured_ = 0, voiceFrom_ = 0, voiceTo_ = 0;
    bool running_ = false, ownsAudio_ = false, ownsCamera_ = false, computing_ = false;
};
