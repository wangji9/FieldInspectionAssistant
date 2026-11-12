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
    Q_PROPERTY(QImage heatmap READ heatmap NOTIFY heatmapChanged)
    Q_PROPERTY(bool hasHeatmap READ hasHeatmap NOTIFY heatmapChanged)
    Q_PROPERTY(QVariantMap observation READ observation NOTIFY changed)
    Q_PROPERTY(QVariantMap parameters READ parameters NOTIFY parametersChanged)
    Q_PROPERTY(QStringList cameras READ cameras NOTIFY changed)
    Q_PROPERTY(bool ownsPreview READ ownsPreview NOTIFY changed)
    Q_PROPERTY(bool arrayAvailable READ arrayAvailable NOTIFY changed)
    Q_PROPERTY(QVariantMap plots READ plots NOTIFY plotsChanged)

public:
    AcousticImagingService(SignalAnalysisService *audio, CameraService *camera, EvidenceService *evidence,
                           SpeakerRecognitionService *speakers, SessionManager *session, QObject *parent = nullptr);
    ~AcousticImagingService() override;
    bool running() const { return running_; }
    QString status() const { return status_; }
    QImage heatmap() const { return heatmap_; }
    bool hasHeatmap() const { return !heatmap_.isNull(); }
    QVariantMap observation() const { return observation_; }
    QVariantMap parameters() const { return parameters_; }
    QStringList cameras() const { return cameras_; }
    bool ownsPreview() const { return ownsCamera_; }
    bool arrayAvailable() const;
    QVariantMap plots() const { return plots_; }
    QVariantMap plotForDisplay(const QString &kind, int channel) const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool configure(const QVariantMap &values);
    Q_INVOKABLE bool start(const QString &cameraPath);
    Q_INVOKABLE void stop();
    Q_INVOKABLE bool openPreview(const QString &cameraPath);
    Q_INVOKABLE void closePreview();
    Q_INVOKABLE void leave();

signals:
    void changed();
    void parametersChanged();
    void plotsChanged();
    void heatmapChanged();

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
    void clearObservation(bool clearPlots = true, bool clearHeatmap = true);
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
    QVariantMap parameters_, observation_, match_, plots_;
    QString captureId_, status_;
    QImage heatmap_;
    QStringList cameras_;
    QVector3D direction_, stableDirection_, voiceDirection_;
    quint64 revision_ = 1, engineRevision_ = 0, lastFrame_ = 0, stableSince_ = 0, lastVoice_ = 0, beamRequest_ = 0;
    qint64 lastReceived_ = 0, lastLocalized_ = 0, voiceCaptured_ = 0, voiceFrom_ = 0, voiceTo_ = 0;
    bool running_ = false, ownsAudio_ = false, ownsCamera_ = false, computing_ = false;
    bool keepPreview_ = false;
};
