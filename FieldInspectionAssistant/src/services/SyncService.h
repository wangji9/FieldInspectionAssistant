#pragma once

#include <QtCore>

class DatabaseManager;
class SessionManager;

// 同步服务：离线优先，本地先写库；网络恢复后按 outbox 队列推送。
class SyncService final : public QObject {
    Q_OBJECT
public:
    void setSession(SessionManager*s){session_=s;}
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(QString status READ status CONSTANT)
    bool configured()const{return false;}
    QString status()const{return QStringLiteral("未配置同步服务器，数据保存在本机");}
    explicit SyncService(DatabaseManager *db, QObject *parent = nullptr);

    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY onlineChanged)
    Q_PROPERTY(int pending READ pendingCount NOTIFY pendingChanged)

    bool online() const { return online_; }
    void setOnline(bool online);
    Q_INVOKABLE int pendingCount() const;
    // Unconfigured servers fail closed; no queue row is acknowledged locally.
    Q_INVOKABLE QVariantMap flush();
    Q_INVOKABLE QString lastSync() const;
    Q_INVOKABLE QVariantList outbox() const;

signals:
    void onlineChanged();
    void pendingChanged();

private:
    DatabaseManager *db_ = nullptr;
    SessionManager *session_=nullptr;
    bool online_ = false;
};
