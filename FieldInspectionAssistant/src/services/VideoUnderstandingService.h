#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QVariantList>

class CameraService;
class SessionManager;

class VideoUnderstandingService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QImage preview READ preview NOTIFY previewChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY eventAdded)
    Q_PROPERTY(double delayMs READ delayMs NOTIFY eventAdded)

public:
    VideoUnderstandingService(CameraService *camera, SessionManager *session, QObject *parent = nullptr);
    bool running() const { return active_; }
    QString status() const { return status_; }
    QImage preview() const { return preview_; }
    QVariantList events() const { return events_; }
    double delayMs() const { return delayMs_; }

    Q_INVOKABLE bool start(const QString &device, const QString &model = QStringLiteral("qwen3_video"));
    Q_INVOKABLE void stop();

signals:
    void changed();
    void previewChanged();
    void eventAdded();

private:
    struct Sample { QImage image; qint64 wallMs; qint64 elapsedMs; };
    void onFrame(const QImage &image);
    void submitLatest();
    void receive(QNetworkReply *reply, const QList<Sample> &clip);
    void finish(const QString &status);
    void setStatus(const QString &status);

    CameraService *camera_;
    SessionManager *session_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QFutureWatcher<QByteArray> encoding_;
    QList<Sample> samples_, pending_;
    QVariantList events_;
    QElapsedTimer clock_;
    QImage preview_;
    QString status_, model_;
    quint64 generation_ = 0, encodingGeneration_ = 0;
    qint64 submittedMs_ = -1;
    double delayMs_ = 0;
    bool active_ = false, inferenceEnabled_ = false;
};
