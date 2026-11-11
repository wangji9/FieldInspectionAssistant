#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <memory>

class SessionManager;

class VisionAlgorithmService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList models READ models NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QUrl inputImage READ inputImage NOTIFY changed)
    Q_PROPERTY(QUrl resultImage READ resultImage NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY changed)

public:
    explicit VisionAlgorithmService(SessionManager *session, QObject *parent = nullptr);
    ~VisionAlgorithmService() override;
    QVariantList models() const;
    bool busy() const { return busy_; }
    QString status() const { return status_; }
    QUrl inputImage() const { return input_; }
    QUrl resultImage() const { return result_; }
    QVariantList results() const { return rows_; }
    qint64 elapsedMs() const { return elapsedMs_; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool run(const QString &model, const QUrl &file);
    Q_INVOKABLE void cancel();

signals:
    void changed();

private:
    void finish(int exitCode, QProcess::ExitStatus exitStatus);
    void fail(const QString &message);

    SessionManager *session_;
    QProcess process_;
    QTimer timeout_;
    QElapsedTimer elapsed_;
    std::unique_ptr<QTemporaryDir> runDirectory_;
    QUrl input_, result_;
    QVariantList rows_;
    QString status_, model_, interruption_;
    bool busy_ = false;
    qint64 elapsedMs_ = 0;
};
