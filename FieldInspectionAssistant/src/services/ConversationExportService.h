#pragma once
#include <QtCore>
class AiInferenceService;

class ConversationExportService final : public QObject {
    Q_OBJECT
public:
    explicit ConversationExportService(AiInferenceService *ai, QObject *parent=nullptr) : QObject(parent),ai_(ai) {}
    Q_INVOKABLE QUrl defaultFile(const QString &mode,const QString &format) const;
    Q_INVOKABLE QVariantMap save(const QUrl &url,const QString &mode,const QVariantList &messages,const QString &format);
    Q_INVOKABLE QVariantMap saveAudio(const QUrl &url,const QString &source);
private:
    AiInferenceService *ai_;
};
