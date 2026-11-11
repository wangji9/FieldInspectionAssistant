#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include "SegmentationOverlay.h"

class CameraService;
class SessionManager;

class VisionLiveService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
    Q_PROPERTY(QVariantList trend READ trend NOTIFY changed)
    Q_PROPERTY(QVariantList plotHistory READ plotHistory NOTIFY changed)
    Q_PROPERTY(QVariantMap hardware READ hardware NOTIFY hardwareChanged)
    Q_PROPERTY(double threshold READ threshold WRITE setThreshold NOTIFY changed)
    Q_PROPERTY(QImage previewFrame READ previewFrame NOTIFY previewChanged)
    Q_PROPERTY(QImage maskFrame READ maskFrame NOTIFY overlayChanged)
    Q_PROPERTY(bool hasPreview READ hasPreview NOTIFY previewChanged)
    Q_PROPERTY(bool synchronizedPreview READ synchronizedPreview WRITE setSynchronizedPreview NOTIFY previewChanged)
    Q_PROPERTY(bool peopleOnly READ peopleOnly WRITE setPeopleOnly NOTIFY changed)

public:
    VisionLiveService(CameraService *camera, SessionManager *session, QObject *parent = nullptr);
    ~VisionLiveService() override;
    QVariantList models() const;
    bool running() const { return active_ || worker_.state() != QProcess::NotRunning; }
    QString status() const { return status_; }
    QVariantMap summary() const;
    QVariantList results() const { return rows_; }
    QVariantList categories() const { return categories_; }
    QVariantList trend() const { return trend_; }
    QVariantList plotHistory() const { return plotHistory_; }
    QVariantMap hardware() const { return hardware_; }
    double threshold() const { return threshold_; }
    void setThreshold(double threshold);
    QImage previewFrame() const;
    bool hasPreview() const { return !previewFrame().isNull(); }
    bool synchronizedPreview() const { return synchronizedPreview_; }
    void setSynchronizedPreview(bool enabled);
    bool peopleOnly() const { return peopleOnly_; }
    void setPeopleOnly(bool enabled);
    bool overlayVisible() const;
    bool smoothLiveAnnotations() const { return active_ && !oneShot_ && !synchronizedPreview_ && !faceAccess_; }
    QSize resultSize() const { return resultSize_; }
    QImage maskFrame() const { return overlayVisible() ? maskOverlay_.image() : QImage(); }
    Q_INVOKABLE QVariantList annotationRows() const;

    Q_INVOKABLE QStringList cameras() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void refreshHardware();
    Q_INVOKABLE bool startCamera(const QString &model, const QString &camera);
    Q_INVOKABLE bool runSample(const QString &model);
    Q_INVOKABLE bool runImage(const QString &model, const QUrl &file);
    Q_INVOKABLE void stop();
    bool faceAccessReady() const;
    bool startFaceAccess(const QString &device);
    void setFaceLabels(const QStringList &labels);

signals:
    void changed();
    void modelsChanged();
    void previewChanged();
    void overlayChanged();
    void hardwareChanged();
    void faceObservations(const QVariantList &rows, qint64 capturedMs);

private:
    struct PreparedFrame {
        QImage original, input;
        qint64 capturedAt = 0;
        qint64 captureWallMs = 0;
        quint64 generation = 0;
        double preparationMs = 0;
    };
    bool start(const QString &model, const QString &camera, const QUrl &file = {}, bool sample = false);
    void cameraFrame(const QImage &frame);
    void sendNext();
    void consume();
    bool receiveFrame(const QByteArray &line, qsizetype &end);
    bool receiveFacePacket(int count, qsizetype bytes, qint64 time, qsizetype &offset);
    void finish(const QString &status);
    void updateResults(bool resetMaskTargets = false);
    void updateAcceleratorStatus(const QByteArray &report);
    bool followsMasks() const { return segmentation_ && sourceKind_ == "camera" && !synchronizedPreview_; }

    CameraService *camera_;
    SessionManager *session_;
    QProcess worker_;
    QProcess acceleratorMonitor_;
    QFutureWatcher<PreparedFrame> preparedFrame_;
    PreparedFrame readyFrame_;
    QTimer watchdog_;
    QTimer acceleratorTimeout_;
    QElapsedTimer frameTime_, rateClock_, streamClock_;
    QByteArray buffer_, mask_, diagnostics_;
    QStringList labels_;
    QImage latest_, pending_, sent_, displayed_;
    SegmentationOverlay maskOverlay_;
    QSize inputSize_, resultSize_;
    qint64 latestTime_ = 0, sentTime_ = 0, resultTime_ = 0;
    qint64 latestWallMs_ = 0, sentWallMs_ = 0;
    QVariantList detected_, rows_, categories_, trend_;
    QVariantList plotHistory_;
    qint64 nextPlotSample_ = 0;
    QVariantMap hardware_;
    quint64 cpuTotal_ = 0, cpuIdle_ = 0;
    QString status_, model_, source_, backend_, sourceKind_, landmarkKind_;
    bool active_ = false, ready_ = false, oneShot_ = false, ownsCamera_ = false;
    bool preparingFrame_ = false;
    bool synchronizedPreview_ = false, peopleOnly_ = false;
    bool segmentation_ = false;
    bool industrial_ = false;
    bool faceAccess_ = false;
    quint64 generation_ = 0;
    quint32 sequence_ = 0, frames_ = 0, skipped_ = 0;
    int cameraCount_ = 0, inferenceCount_ = 0;
    double inferenceMs_ = 0, latencyMs_ = 0, cameraFps_ = 0, inferenceFps_ = 0;
    double sentPreparationMs_ = 0;
    double threshold_ = 0.50, coverage_ = 0;
};
