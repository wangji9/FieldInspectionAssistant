#pragma once
#include <QtCore>
#include <QtSql>
#include <functional>
class DatabaseManager;class SessionManager;class AssistantStorage;
class WorkspaceService final:public QObject{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap operationResult READ operationResult NOTIFY changed)
    Q_PROPERTY(int reminders READ reminders NOTIFY changed)
    Q_PROPERTY(QVariantMap memoryPreferences READ memoryPreferences NOTIFY memoryPreferencesChanged)
    Q_PROPERTY(QVariantMap searchPreferences READ searchPreferences NOTIFY memoryPreferencesChanged)
public:
    WorkspaceService(DatabaseManager*,SessionManager*,QObject*parent=nullptr);
    ~WorkspaceService()override;
    Q_PROPERTY(QVariantMap assistantPaths READ assistantPaths NOTIFY memoryPreferencesChanged)
    QVariantMap assistantPaths()const;
    Q_INVOKABLE QVariantMap inputFiles(const QString &directory,const QString &kind)const;
    QVariantMap prepareAgentAttachments(const QString &id,const QString &image,const QString &audio)const;
    QString resolveAgentAttachment(const QString &id,const QString &relative)const;
    void recordAgentState(const QString &id,const QVariantList &messages,const QStringList &tools,const QVariantList &history,bool memory)const;
    void recordAssistantPreferences(const QStringList &tools)const;
    void setActivityGuard(std::function<bool()> fn){activityGuard_=fn;}
    bool busy()const{return worker_.state()!=QProcess::NotRunning;}
    QString status()const{return status_;}QVariantMap operationResult()const{return result_;}
    int reminders()const;
    QVariantMap memoryPreferences()const;
    Q_INVOKABLE bool memoryEnabled(const QString &mode)const;
    Q_INVOKABLE QVariantMap setMemoryEnabled(const QString &mode,bool enabled);
    QVariantMap searchPreferences()const;
    Q_INVOKABLE bool searchEnabled(const QString &mode)const;
    Q_INVOKABLE QVariantMap setSearchEnabled(const QString &mode,bool enabled);
    Q_INVOKABLE QVariantMap query(const QString &entity,const QVariantMap &filter={},int page=0,int size=30)const;
    Q_INVOKABLE QVariantList deviceTimeline(const QString &assetId)const;
    Q_INVOKABLE QVariantList options(const QString &entity,const QVariantMap &filter={})const;
    Q_INVOKABLE QVariantMap save(const QString &entity,const QVariantMap &value);
    Q_INVOKABLE QVariantMap remove(const QString &entity,const QString &id,const QString &reason);
    Q_INVOKABLE QVariantMap restore(const QString &id);
    Q_INVOKABLE void scheduleDue();
    Q_INVOKABLE QVariantMap linkEvidence(const QVariantMap &value);
    Q_INVOKABLE QVariantMap report(const QString &recordId)const;
    Q_INVOKABLE QVariantMap reportVersion(const QString &id)const;
    Q_INVOKABLE QVariantMap confirmReport(const QString &recordId,const QString &conclusion,const QString &reason,int expectedRevision);
    Q_INVOKABLE QVariantMap packageReport(const QString &id,const QString &path);
    Q_INVOKABLE QVariantMap exportReport(const QString &reportId,const QString &path,const QString &format);
    Q_INVOKABLE QVariantMap channelProfile(const QString &bindingId)const;
    Q_INVOKABLE QVariantMap evaluate(const QString &pointId,const QString &metric,double value,const QString &unit,const QString &condition)const;
    Q_INVOKABLE QVariantMap conversation(const QString &mode)const;
    Q_INVOKABLE QVariantMap saveConversation(const QString &mode,const QVariantList &rows,const QString &model);
    Q_INVOKABLE QVariantMap openConversation(const QString &id);
    Q_INVOKABLE void newConversation(const QString &mode);
    Q_INVOKABLE QString contextualQuestion(const QString &mode,const QString &question)const;
    Q_INVOKABLE QVariantMap validateCitations(const QString &text,const QVariantList &allowed)const;
    Q_INVOKABLE QVariantMap diagnostics()const;
    Q_INVOKABLE void operation(const QString &action,const QVariantMap &arguments={});
    Q_INVOKABLE QString temporaryPath(const QString &suffix)const;
signals:
    void memoryPreferencesChanged();
    void changed();void conversationsChanged();void dataChanged();void maintenanceReady(const QString &action,const QVariantMap &result);
private:
    bool allowed(const QString &permission)const;QVariantMap fail(const QString&)const;
    QVariantMap audit(const QString&,const QString&,const QString&,const QString&);
    QVariantMap taskSave(const QVariantMap&,bool scheduled=false);
    QVariantMap insertEvidence(QVariantMap);
    QJsonObject reportSnapshot(const QString&)const;QString reportHtml(const QJsonObject&)const;
    DatabaseManager *db_;SessionManager *session_;QProcess worker_;QByteArray buffer_;QTimer scheduler_;
    std::function<bool()> activityGuard_;
    AssistantStorage *assistantStorage_=nullptr;
    QVariantMap verifiedFiles_;
    QString status_,operation_,operationUser_;QVariantMap result_,pendingEvidence_;mutable QHash<QString,QString> activeChats_;
};
