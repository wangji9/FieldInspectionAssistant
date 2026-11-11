#include "HardwareStatusService.h"
#include "../app/SessionManager.h"
HardwareStatusService::HardwareStatusService(SessionManager*s,const QString&folder,QObject*p):QObject(p),session_(s),folder_(folder){
    timer_.setInterval(3000);connect(&timer_,&QTimer::timeout,this,&HardwareStatusService::refresh);
    timeout_.setSingleShot(true);timeout_.setInterval(2800);connect(&timeout_,&QTimer::timeout,this,[this]{error_="设备状态查询超时；显示的是上次结果";worker_.kill();emit changed();});
    connect(&worker_,&QProcess::readyReadStandardOutput,this,[this]{output_+=worker_.readAllStandardOutput();});
    connect(&worker_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){timeout_.stop();output_+=worker_.readAllStandardOutput();if(cancelled_){cancelled_=false;if(active_)QTimer::singleShot(0,this,&HardwareStatusService::refresh);emit changed();return;}if(!active_||!session_->loggedIn())return;auto value=QJsonDocument::fromJson(output_).object().toVariantMap();if(code||value.isEmpty()){if(error_.isEmpty())error_="设备状态查询失败；请重试";}else{auto total=value["cpu_total"].toULongLong(),idle=value["cpu_idle"].toULongLong();if(total_&&total>total_)value["cpu_percent"]=qBound(0.,100.*(1.-double(idle-idle_)/double(total-total_)),100.);total_=total;idle_=idle;snapshot_=value;error_.clear();}emit changed();});
    connect(&worker_,&QProcess::errorOccurred,this,[this]{if(active_&&!cancelled_){if(error_.isEmpty())error_="无法读取设备状态";emit changed();}});
    connect(s,&SessionManager::sessionChanged,this,[this]{if(!session_->loggedIn()){setActive(false);snapshot_.clear();error_.clear();total_=idle_=0;emit changed();}});
}
HardwareStatusService::~HardwareStatusService(){active_=false;cancelled_=true;timer_.stop();timeout_.stop();if(busy()){worker_.kill();worker_.waitForFinished(1000);}}
void HardwareStatusService::setActive(bool on){on=on&&session_->can("inspect");if(on==active_)return;active_=on;if(on){total_=idle_=0;timer_.start();refresh();}else{timer_.stop();timeout_.stop();if(busy()){cancelled_=true;worker_.kill();}}emit changed();}
void HardwareStatusService::refresh(){if(!active_||busy()||!session_->can("inspect"))return;cancelled_=false;error_.clear();output_.clear();worker_.start("/usr/bin/python3",{"/home/ztl/FieldInspectionAssistant/deploy/hardware_status.py","--data",folder_,"--pid",QString::number(QCoreApplication::applicationPid())});timeout_.start();emit changed();}
