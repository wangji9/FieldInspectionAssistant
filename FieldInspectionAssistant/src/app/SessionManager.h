#pragma once

#include <QtCore>

class DatabaseManager;

// 会话与权限：本地账户、角色权限由 C++ 业务层校验（不是仅隐藏按钮）。
class SessionManager final : public QObject {
    Q_OBJECT
public:
    explicit SessionManager(DatabaseManager *db, QObject *parent = nullptr);

    Q_PROPERTY(QString userId READ userId NOTIFY sessionChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY sessionChanged)
    Q_PROPERTY(QString role READ role NOTIFY sessionChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY sessionChanged)
    Q_PROPERTY(bool setupRequired READ setupRequired NOTIFY sessionChanged)
    Q_PROPERTY(QString message READ message NOTIFY sessionChanged)
    Q_PROPERTY(QVariantMap permissions READ permissions NOTIFY sessionChanged)

    QString userName() const { return userName_; }
    QString role() const { return role_; }
    QString userId() const { return userId_; }
    bool loggedIn() const { return !userId_.isEmpty(); }
    bool setupRequired() const;
    QString message() const { return message_; }

    Q_INVOKABLE QVariantList users() const;
    Q_INVOKABLE bool login(const QString &name);
    Q_INVOKABLE bool authenticate(const QString &name,const QString &password);
    Q_INVOKABLE bool setupAdministrator(const QString &name,const QString &password);
    Q_INVOKABLE QVariantMap saveUser(const QString &id,const QString &name,const QString &role,bool active,const QString &password);
    Q_INVOKABLE QVariantMap removeUser(const QString &id);
    Q_INVOKABLE void logout();
    Q_INVOKABLE bool can(const QString &permission) const;
    QVariantMap permissions()const;
    Q_INVOKABLE QString roleText() const;

signals:
    void sessionEnding();
    void sessionChanged();
    void usersChanged();

private:
    DatabaseManager *db_ = nullptr;
    QString userId_;
    QString userName_;
    QString role_;
    QString message_;
    int failures_=0;
    QDateTime retryAfter_;
};
