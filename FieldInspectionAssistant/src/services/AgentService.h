#pragma once
#include <QtCore>
class DatabaseManager;class SessionManager;class SignalAnalysisService;class AiInferenceService;class KnowledgeGraphService;class WorkspaceService;class WebSearchService;class SystemContextService;
class AgentService final:public QObject{
    Q_OBJECT
    Q_PROPERTY(QVariantList messages READ messages NOTIFY changed)
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY changed)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString conversationId READ conversationId NOTIFY changed)
    Q_PROPERTY(bool graphEnabled READ graphEnabled WRITE setGraphEnabled NOTIFY changed)
    Q_PROPERTY(QStringList enabledTools READ enabledTools NOTIFY changed)
    Q_PROPERTY(QVariantMap graphEvidence READ graphEvidence NOTIFY changed)
    Q_PROPERTY(QVariantMap webEvidence READ webEvidence NOTIFY changed)
public:
    AgentService(DatabaseManager*,SessionManager*,SignalAnalysisService*,KnowledgeGraphService*,WorkspaceService*,QObject *parent=nullptr);
    void setSystemContext(SystemContextService*s){system_=s;}
    bool graphEnabled()const;void setGraphEnabled(bool v);
    QStringList enabledTools()const;
    Q_INVOKABLE QVariantMap configureTools(const QStringList &tools);
    QVariantMap graphEvidence()const{return graphEvidence_;}
    QVariantMap webEvidence()const{return webEvidence_;}
    Q_INVOKABLE QVariantList auditTools()const;
    QVariantList messages()const{return messages_;}QVariantList conversations()const;
    QVariantList steps()const{return steps_;}bool busy()const{return busy_;}
    QString status()const{return status_;}QString conversationId()const{return conversationId_;}
    Q_INVOKABLE void submit(const QString &task,const QString &image=QString(),const QString &audio=QString(),const QString &analysisContext=QString());
    Q_INVOKABLE void cancel();Q_INVOKABLE void newConversation();Q_INVOKABLE void openConversation(const QString &id);
signals:void changed();
private:
    void add(const QString &role,const QString &body,const QString &kind="text",const QVariantList &attachments={},const QVariantMap &webEvidence={});void persist(int index);void plan();
    void complete(bool ok,const QString &mode);void next();void scheduleNext();void schedulePlan();void finish();void fail(const QString &error,bool discardCurrent=false);
    void observation(const QString &tool,const QString &body);QString databaseSummary()const;
    DatabaseManager *db_;SessionManager *session_;SignalAnalysisService *analysis_;AiInferenceService *engine_;
    KnowledgeGraphService *knowledge_;WorkspaceService *workspace_;QVariantMap graphEvidence_;QStringList activeTools_;bool activeMemory_=true;
    QString historyContext_,chartContext_;
    void applyPlan(const QJsonArray &steps);
    SystemContextService *system_=nullptr;QVariantMap systemEvidence_;QString previousAsset_;
    WebSearchService *web_;QVariantMap webEvidence_;
    quint64 runSerial_=0;
    QVariantList messages_,steps_;QString conversationId_,status_=QStringLiteral("描述任务，智能体将规划并调用本地工具"),task_,image_,audio_,audioTranscript_,context_,phase_;
    bool busy_=false,cancelled_=false,cancelSent_=false,ownCapture_=false;double captureSince_=0;int step_=0,streamIndex_=-1;QTimer toolTimer_;
};
