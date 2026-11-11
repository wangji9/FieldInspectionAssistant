#pragma once
#include <QtCore>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// Search stays outside the inference process; only public result excerpts are sent to the local LLM.
class WebSearchService final : public QObject {
    Q_OBJECT
public:
    explicit WebSearchService(QObject *parent=nullptr);
    void search(const QString &query);
    void cancel();
    static QVariantList parseResults(const QByteArray &html);
    static QString prompt(const QVariantMap &evidence);
    static QString sources(const QVariantMap &evidence);
    static bool validateAnswer(const QString &answer,const QVariantMap &evidence,QString *error=nullptr);
signals:
    void completed(const QVariantMap &evidence);
private:
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    QByteArray buffer_;
    QString query_;
    int provider_=0;
    quint64 generation_=0;
    void request();
    void finish(const QString &error=QString(),bool allowFallback=true);
    void finishParsed(const QVariantList &rows,const QString &error,bool allowFallback);
};
