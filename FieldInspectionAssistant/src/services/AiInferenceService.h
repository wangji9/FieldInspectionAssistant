#pragma once

#include <QtCore>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QElapsedTimer>

class SessionManager;
class WebSearchService;
class AiInferenceService final : public QObject {
    Q_OBJECT
public:
    void setSession(SessionManager*s){session_=s;}
    void setRealtimeAudio(bool enabled){realtimeAudio_=enabled;}
    explicit AiInferenceService(QObject *parent = nullptr);

    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString answer READ answer NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY changed)
    Q_PROPERTY(QVariantMap system READ system NOTIFY statusChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int outputTokenLimit READ outputTokenLimit WRITE setOutputTokenLimit NOTIFY preferencesChanged)
    Q_PROPERTY(int answerLengthLimit READ outputTokenLimit WRITE setOutputTokenLimit NOTIFY preferencesChanged)
    Q_PROPERTY(int answerFontSize READ answerFontSize WRITE setAnswerFontSize NOTIFY preferencesChanged)
    Q_PROPERTY(bool canStop READ canStop NOTIFY changed)

    bool available() const { return available_; }
    QString statusText() const { return available_ ? QStringLiteral("AI 推理服务在线") : QStringLiteral("AI 服务未连接"); }
    bool busy() const { return busy_; }
    QString answer() const { return answer_; }
    QString message() const { return message_; }
    QVariantMap metrics() const { return metrics_; }
    QVariantMap system() const { return system_; }
    QVariantList results() const { return results_; }
    int outputTokenLimit() const { return outputTokenLimit_; }
    int answerFontSize() const { return answerFontSize_; }
    bool canStop() const { return busy_; }
    void setOutputTokenLimit(int value);
    void setAnswerFontSize(int value);
    Q_INVOKABLE QString renderMarkdown(const QString &markdown, qreal width = 600) const;
    Q_INVOKABLE void copyText(const QString &text) const;
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clearSession();
    Q_INVOKABLE void refreshStatus();
    Q_INVOKABLE void prepareModel(const QString &model, bool online = false);
    void prepareStartup();
    Q_INVOKABLE void refreshResults();
    Q_INVOKABLE void showResult(const QVariantMap &result);
    Q_INVOKABLE void selectMode(const QString &mode);
    Q_INVOKABLE void resetDisplay();
    Q_INVOKABLE void submit(const QString &mode, const QString &question, const QString &path = QString(), int maxTokens = 256, bool thinking = false, bool online = false, const QString &model = QString(), bool webSearch = false, const QString &searchQuery = QString(), int tokenBudget = 2048);
    Q_INVOKABLE QVariantMap askImage(const QString &path, const QString &question) const;
    Q_INVOKABLE QVariantMap analyzeImage(const QString &path) const;
signals:
    void changed();
    void statusChanged();
    void resultsChanged();
    void completed(bool ok, const QString &mode);
    void preferencesChanged();
private:
    void requestPrepare(const QJsonObject &options);
    void startStream();
    void consumeStream();
    void applyJob(const QJsonObject &obj);
    void pollJob();
    void finishError(const QString &error);
    void postJob(QJsonObject payload,int attempt=0);
    quint64 submissionSerial_=0;
    WebSearchService *web_;
    bool searching_=false;
    bool visionSearchStage_=false;
    QString visionQuestion_;int visionAnswerLimit_=0;QVariantMap visionMetrics_;
    QVariantMap searchEvidence_;
    QJsonObject pendingPayload_;
    SessionManager *session_=nullptr;
    QNetworkAccessManager manager_;
    QTimer poll_, statusTimer_;
    bool available_ = false, busy_ = false, polling_ = false;
    bool realtimeAudio_ = false;
    QString jobId_, mode_, answer_, message_ = QStringLiteral("等待推理");
    QVariantMap metrics_, system_;
    QVariantList results_;
    QPointer<QNetworkReply> stream_;
    QByteArray streamBuffer_;
    QElapsedTimer requestClock_;
    mutable QCache<QString,QString> markdownCache_{2*1024*1024};
    double visibleFirstMs_ = -1;
    int outputTokenLimit_ = 256, answerFontSize_ = 16;
};
