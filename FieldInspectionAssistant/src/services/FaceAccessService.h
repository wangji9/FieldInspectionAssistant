#pragma once

#include <QNetworkAccessManager>
#include <QRect>
#include <QTimer>
#include <QVariantList>
#include <array>
#include <functional>

class DatabaseManager;
class SessionManager;
class VisionLiveService;

class FaceAccessService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool canManage READ canManage NOTIFY recordsChanged)
    Q_PROPERTY(bool canEnroll READ canEnroll NOTIFY observationsChanged)
    Q_PROPERTY(int enrollmentView READ enrollmentView NOTIFY observationsChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY recordsChanged)
    Q_PROPERTY(QVariantList faces READ faces NOTIFY observationsChanged)
    Q_PROPERTY(QVariantList people READ people NOTIFY recordsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY recordsChanged)

public:
    FaceAccessService(DatabaseManager *db, SessionManager *session, VisionLiveService *live, QObject *parent = nullptr);
    bool running() const { return acquiring_ || !token_.isEmpty(); }
    bool canManage() const;
    bool canEnroll() const;
    int enrollmentView() const;
    QString status() const { return status_; }
    QString notice() const { return notice_; }
    QVariantList faces() const { return faces_; }
    QVariantList people() const { return people_; }
    QVariantList history() const { return history_; }

    Q_INVOKABLE bool start(const QString &device);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool save(const QString &id, const QString &code, const QString &name, bool active, bool recapture);
    Q_INVOKABLE bool saveSide(const QString &id, int view);
    Q_INVOKABLE bool remove(const QString &id);

signals:
    void changed();
    void observationsChanged();
    void recordsChanged();

private:
    using Feature = std::array<float, 128>;
    struct Person {
        QString id, code, name;
        bool active;
        QList<Feature> features;
        int sideCount = 0;
        double similarity(const Feature &sample) const;
    };
    QByteArray enrollmentFeature(const QString &id);
    void observe(const QVariantList &rows, qint64 capturedMs);
    void record(const Person *person, double score, qint64 capturedMs);
    void release();
    void resource(const QString &action, const QString &token, std::function<void(QString, QString)> completed);
    bool report(const QString &text, bool success = false);

    DatabaseManager *db_;
    SessionManager *session_;
    VisionLiveService *live_;
    QNetworkAccessManager network_;
    QTimer heartbeat_;
    QString token_, status_, notice_, candidateKey_, lastKey_;
    QList<Person> templates_;
    QVariantList faces_, people_, history_;
    QList<Feature> enrollment_;
    int enrollmentView_ = -1;
    QString heldId_;
    QRect previousBox_;
    Feature heldFeature_{};
    qint64 heldMs_ = 0;
    double heldScore_ = 0;
    qint64 enrollmentMs_ = 0, lastSeenMs_ = 0, lastEventMs_ = 0;
    int consecutive_ = 0;
    bool acquiring_ = false, cancelled_ = false, ownsWorker_ = false, stopping_ = false;
};
