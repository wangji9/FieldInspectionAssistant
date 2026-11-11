#pragma once
#include <QtCore>
#include <QtSql>
class DatabaseManager;
class SessionManager;

// Metadata lives beside the raw WAV catalog. Removing an item never removes its samples.
class RecordingCatalog {
public:
    RecordingCatalog(DatabaseManager *,SessionManager *,const QString &folder);
    ~RecordingCatalog();
    bool ready() const {return db_.isOpen()&&error_.isEmpty();}
    QString error() const {return error_;}
    QVariantMap query(const QVariantMap &filter,int page,int size) const;
    QVariantMap detail(const QString &id) const;
    QVariantMap update(const QString &id,const QString &name,const QString &notes,int revision);
    QVariantMap setDeleted(const QString &id,bool deleted,int revision);
    QVariantMap beginCapture(const QVariantMap &record);
    bool completeCapture(const QVariantMap &record,const QVariantMap &calibration);
private:
    void recoverInterrupted(const QString &folder);
    bool manager() const;
    bool writable(const QVariantMap &row) const;
    QVariantMap row(const QString &id) const;
    QVariantMap decorate(QVariantMap) const;
    bool linked(const QString &id,const QString &path,QString *error) const;
    bool event(const QString &id,const QString &action,const QVariantMap &before,const QVariantMap &after);
    DatabaseManager *business_;SessionManager *session_;QSqlDatabase db_;QString error_,connection_;
};
