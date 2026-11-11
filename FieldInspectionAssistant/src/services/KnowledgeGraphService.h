#pragma once
#include <QtCore>
class DatabaseManager;class SessionManager;class AiInferenceService;
class KnowledgeGraphService final:public QObject{
    Q_OBJECT
    Q_PROPERTY(QVariantMap graph READ graph NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QVariantList revisions READ revisions NOTIFY changed)
    Q_PROPERTY(QVariantMap pending READ pending NOTIFY changed)
    Q_PROPERTY(QVariantMap previewGraph READ previewGraph NOTIFY changed)
    Q_PROPERTY(QString ontologyJson READ ontologyJson NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QVariantList hits READ hits NOTIFY changed)
public:
    KnowledgeGraphService(DatabaseManager*,SessionManager*,QObject *parent=nullptr);
    QVariantMap graph()const;QVariantList sources()const;QVariantList revisions()const;
    QVariantMap pending()const{return pending_;}QString ontologyJson()const;QString status()const{return status_;}
    QVariantMap previewGraph()const;
    Q_INVOKABLE QVariantMap selectCandidate(int index,bool selected);
    bool busy()const{return busy_;}bool canEdit()const;int revision()const;QVariantList hits()const{return hits_;}
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap saveOntology(const QString &text);
    Q_INVOKABLE void buildText(const QString &title,const QString &text,bool evolve);
    Q_INVOKABLE void buildFile(const QString &path,bool evolve);
    Q_INVOKABLE void buildConversation(const QString &id,bool evolve);
    Q_INVOKABLE QVariantMap conversationChoices(const QString &search=QString(),int page=0,int size=30)const;
    Q_INVOKABLE void cancel();Q_INVOKABLE void discard();Q_INVOKABLE QVariantMap applyPending();
    Q_INVOKABLE QVariantMap search(const QString &query,int limit=10);
    Q_INVOKABLE QVariantMap details(const QString &id,int version=0)const;
    Q_INVOKABLE QVariantMap renameSource(const QString &id,const QString &title);
    Q_INVOKABLE QVariantMap setSourceDeleted(const QString &id,bool deleted);
    Q_INVOKABLE QVariantMap renameRevision(int id,const QString &title);
    Q_INVOKABLE QVariantMap setRevisionDeleted(int id,bool deleted);
    Q_INVOKABLE void updateSource(const QString &id,const QString &title,const QString &content,bool evolve);
    Q_INVOKABLE QVariantMap graphAtRevision(int version)const;
    Q_INVOKABLE QVariantMap exportGraph(const QString &path)const;
    Q_INVOKABLE QVariantMap sourceDetail(const QString &id)const;
    bool ready()const;
signals:void changed();
private:
    QVariantMap ontology()const;QVariantMap validateOntology(const QVariantMap &o)const;
    void extractNext();void complete(bool,const QString&);void fail(const QString&);
    QVariantMap changeMetadata(const QString &kind,const QString &id,const QString &title,int deleted);
    DatabaseManager *db_;SessionManager *session_;AiInferenceService *engine_;
    QVariantMap pending_,baseOntology_;QVariantList hits_;QString status_=QStringLiteral("就绪"),title_,text_,origin_;
    QElapsedTimer extractionClock_;
    QString draftOwner_;
    QStringList chunks_;int chunk_=0,baseRevision_=0;bool busy_=false,evolve_=false,cancelled_=false,schemaDone_=false,stopSent_=false;
    QString stage_,replaceSource_;QPointer<QProcess> importer_;
};
