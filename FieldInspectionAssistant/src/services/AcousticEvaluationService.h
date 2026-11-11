#pragma once
#include <QtCore>
#include <QtSql>
class DatabaseManager;class SessionManager;class WorkspaceService;class SignalAnalysisService;class AiInferenceService;
// Immutable evidence snapshots; ownership is checked in the service, never only in QML.
class AcousticEvaluationService final:public QObject {
 Q_OBJECT
 Q_PROPERTY(bool busy READ busy NOTIFY changed)
 Q_PROPERTY(QString status READ status NOTIFY changed)
 Q_PROPERTY(QString error READ error NOTIFY changed)
 Q_PROPERTY(QVariantMap report READ report NOTIFY changed)
 Q_PROPERTY(QVariantList pages READ pages NOTIFY changed)
 Q_PROPERTY(QVariantList history READ history NOTIFY changed)
public:
 AcousticEvaluationService(DatabaseManager*,SessionManager*,WorkspaceService*,SignalAnalysisService*,QObject*parent=nullptr);
 ~AcousticEvaluationService()override;
 bool busy()const{return busy_;}QString status()const{return status_==QString::fromUtf8("读取图表证据…")?status_+QString(" %1/%2").arg(visionBatch_+1).arg(pending_.value("vision_batches").toList().size()):status_==QString::fromUtf8("整理图表证据…")?status_+QString(" %1/%2").arg(textBatch_+1).arg(pending_.value("vision_batches").toList().size()):status_;}QString error()const{return error_;}
 QVariantMap report()const{return report_;}QVariantList pages()const{return pages_;}QVariantList history()const{return history_;}
 Q_INVOKABLE void generateRecording(const QString &recordingId,const QVariantMap &form);
 Q_INVOKABLE void stop();
 Q_INVOKABLE void refresh();
 Q_INVOKABLE void open(const QString &id,int revision=0);
 Q_INVOKABLE void revise(const QString &conclusion,const QString &reason);
 Q_INVOKABLE void review(const QString &conclusion,const QString &note);
 Q_INVOKABLE void exportPdf(const QString &path);
signals:
 void changed();
private:
 bool ready();bool allowed(const QString &owner)const;void fail(const QString&);
 void infer(const QVariantMap&);void visual();void textBatch();void summarize();void persist(QVariantMap);void render();void loadPages();
 QString folder(const QString&,int)const;QVariantMap read(const QString&,int);
 DatabaseManager *business_;SessionManager *session_;WorkspaceService *workspace_;SignalAnalysisService *analysis_;
 AiInferenceService *engine_;QProcess renderer_,collector_;QString collectionDir_,renderPhase_;int visionBatch_=0,textBatch_=0;QTimer watchdog_;
 QSqlDatabase store_;QString connection_,root_,owner_,status_,error_;bool busy_=false;
 QVariantMap report_,pending_,form_;QVariantList pages_,history_;
};
