#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

class ProjectWorkspace;
class SessionManager;
class InspectionWorkflowService;
class WorkspaceService;

class ProjectInference final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString answer READ answer NOTIFY changed)
    Q_PROPERTY(QString eventId READ eventId NOTIFY changed)
    Q_PROPERTY(QString subjectId READ subjectId NOTIFY changed)
    Q_PROPERTY(QString subjectKind READ subjectKind NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool queued READ queued NOTIFY changed)
public:
    ProjectInference(ProjectWorkspace *workspace, SessionManager *session, InspectionWorkflowService *workflow,
                     WorkspaceService *records, QObject *parent);
    QString status() const { return status_; }
    QString answer() const { return answer_; }
    QString eventId() const { return event_; }
    QString subjectId() const { return subject_; }
    QString subjectKind() const { return kind_; }
    bool ready() const { return ready_; }
    bool busy() const { return busy_; }
    bool queued() const { return queued_; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void cancelQueued();
    Q_INVOKABLE void analyze(const QString &eventId);
    Q_INVOKABLE void analyzeSubject(const QString &kind, const QString &id);

signals:
    void changed();

private:
    void request(const QJsonObject &payload);
    void consume();
    void fail(const QString &message);
    bool canAnalyze() const;
    void submit(const QString &kind, const QString &id, const QString &question);

    ProjectWorkspace *workspace_;
    InspectionWorkflowService *workflow_;
    WorkspaceService *records_;
    QLocalSocket socket_;
    QTimer timeout_, poll_;
    QElapsedTimer elapsed_;
    QByteArray request_, buffer_;
    QString operation_, job_, project_, event_, subject_, kind_, answer_;
    QString status_ = tr("尚未检查推理服务");
    bool ready_ = false, busy_ = false, queued_ = false;
};
