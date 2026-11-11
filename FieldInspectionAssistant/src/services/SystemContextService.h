#pragma once
#include <QtCore>
#include <QtSql>
class DatabaseManager;class SessionManager;class WorkspaceService;class SignalAnalysisService;
// Read-only, permission-scoped retrieval shared by all user-facing assistants.
// The LLM never supplies SQL, filesystem paths or authorization predicates.
class SystemContextService final:public QObject {
 Q_OBJECT
public:
 SystemContextService(DatabaseManager*,SessionManager*,WorkspaceService*,SignalAnalysisService*,QObject *parent=nullptr);
 Q_INVOKABLE void selectAsset(const QString &id);
 Q_INVOKABLE QVariantMap lookup(const QString &question,const QString &previousAssetId=QString())const;
 Q_INVOKABLE QString prompt(const QVariantMap &evidence)const;
 Q_INVOKABLE QVariantMap validate(const QString &answer,const QVariantMap &evidence)const;
 Q_INVOKABLE QVariantMap destination(const QString &entity,const QString &id)const;
private:
 QVariantList read(QSqlDatabase,const QString&,const QVariantList &args={})const;
 QVariantList acoustic(const QVariantMap &asset)const;
 QString selectedAsset_,selectedOwner_;
 mutable QString readError_;
 DatabaseManager *db_;SessionManager *session_;WorkspaceService *workspace_;SignalAnalysisService *analysis_;
};
