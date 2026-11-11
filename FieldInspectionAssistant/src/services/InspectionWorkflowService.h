#pragma once
#include <QtCore>
class DatabaseManager;class SessionManager;class WorkspaceService;
class InspectionWorkflowService final:public QObject {
    Q_OBJECT
public:
    InspectionWorkflowService(DatabaseManager*,SessionManager*,WorkspaceService*,QObject *parent=nullptr);
    Q_INVOKABLE QVariantMap overview(const QString &assetId)const;
    Q_INVOKABLE QVariantMap detail(const QString &kind,const QString &id)const;
    Q_INVOKABLE QVariantMap saveEntry(const QString &recordId,const QVariantMap &value);
    Q_INVOKABLE QVariantMap setEntryDeleted(const QString &id,bool deleted,int revision);
signals:void changed();
private:
    QVariantList entries(const QString &recordId,bool deleted=false)const;
    QVariantList flow(const QVariantMap &task,const QVariantMap &record,const QVariantList &defects,const QVariantList &entries)const;
    bool canWrite(const QString &recordId)const;
    DatabaseManager *db_;SessionManager *session_;WorkspaceService *workspace_;
};
