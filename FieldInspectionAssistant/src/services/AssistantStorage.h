#pragma once
#include <QtCore>
class SessionManager;
class AssistantStorage {
public:
    explicit AssistantStorage(SessionManager *);
    ~AssistantStorage();
    QVariantMap paths()const;
    QVariantMap files(const QString &directory,const QString &kind)const;
    QVariantMap stage(const QString &conversation,const QString &image,const QString &audio)const;
    QString resolve(const QString &conversation,const QString &relative)const;
    QVariantMap snapshot(const QString &mode,const QString &id,const QVariantList &messages,const QString &model,const QStringList &tools={})const;
    QVariantMap memory(const QString &id,const QString &mode,bool enabled,const QVariantList &history)const;
    QVariantMap preferences(const QVariantMap &memory,const QStringList &tools,const QVariantMap &search)const;
private:
    struct WriteTask {QString path;QString ownerRoot;QVariantMap value;};
    void runWriter();
    QString userRoot()const;QString sessionRoot(const QString &)const;
    bool permitted(const QString &path)const;
    QVariantMap write(const QString &,const QVariantMap &)const;
    SessionManager *session_;QString root_;
    QThread *writer_=nullptr;
    mutable QMutex writeMutex_;
    mutable QWaitCondition writeReady_;
    mutable QHash<QString,WriteTask> writes_;
    mutable QStringList writeOrder_;
    bool stopping_=false;
};
