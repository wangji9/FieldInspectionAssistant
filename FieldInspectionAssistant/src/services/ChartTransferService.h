#pragma once
#include <QtCore>
class SessionManager;class WorkspaceService;
class ChartTransferService final:public QObject{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QVariantMap bundle READ bundle NOTIFY changed)
public:
    ChartTransferService(SessionManager*,WorkspaceService*,QObject *parent=nullptr);
    ~ChartTransferService()override;
    bool busy()const{return worker_!=nullptr;}QString error()const{return error_;}QVariantMap bundle()const{return bundle_;}
    Q_INVOKABLE void prepare(const QVariantList &charts,const QString &target);
signals:void changed();void ready(const QVariantMap &bundle);
private:
    SessionManager *session_;WorkspaceService *workspace_;QThread *worker_=nullptr;quint64 generation_=0;QString error_;QVariantMap bundle_;
};
