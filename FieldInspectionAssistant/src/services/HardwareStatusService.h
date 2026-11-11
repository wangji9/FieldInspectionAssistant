#pragma once
#include <QtCore>
class SessionManager;
class HardwareStatusService final:public QObject{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    HardwareStatusService(SessionManager*,const QString&,QObject*parent=nullptr);
    ~HardwareStatusService()override;
    QVariantMap snapshot()const{return snapshot_;}bool active()const{return active_;}bool busy()const{return worker_.state()!=QProcess::NotRunning;}QString error()const{return error_;}
    void setActive(bool);
    Q_INVOKABLE void refresh();
signals:void changed();
private:
    SessionManager*session_;QString folder_,error_;QVariantMap snapshot_;QProcess worker_;QTimer timer_,timeout_;bool active_=false,cancelled_=false;QByteArray output_;quint64 total_=0,idle_=0;
};
