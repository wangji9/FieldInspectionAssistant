#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QUrl>
#include <QVariantMap>

class SessionManager;
class DatabaseManager;

class ProjectWorkspace final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap project READ project NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList evidence READ evidence NOTIFY changed)
    Q_PROPERTY(QVariantList events READ events NOTIFY changed)
    Q_PROPERTY(QVariantList reports READ reports NOTIFY changed)
    Q_PROPERTY(QVariantList logs READ logs NOTIFY changed)
    Q_PROPERTY(QVariantList recentProjects READ recentProjects NOTIFY changed)
    Q_PROPERTY(QUrl demoProject READ demoProject CONSTANT)

public:
    ProjectWorkspace(DatabaseManager *business, SessionManager *session, QObject *parent);
    ~ProjectWorkspace() override;

    QVariantMap project() const { return project_; }
    QVariantList devices() const;
    QVariantList evidence() const;
    QVariantList events() const;
    QVariantList reports() const;
    QVariantList logs() const;
    QVariantList recentProjects() const;
    QUrl demoProject() const;

    Q_INVOKABLE QVariantMap createProject(const QString &name, const QUrl &parentFolder = {});
    Q_INVOKABLE QVariantMap openProject(const QUrl &folder);
    Q_INVOKABLE QVariantMap importProject(const QUrl &folder);
    Q_INVOKABLE QVariantMap openDemoProject();
    Q_INVOKABLE QVariantMap saveProject(const QString &name, const QString &description);
    Q_INVOKABLE void closeProject();
    Q_INVOKABLE QVariantMap copyProject(const QUrl &folder, const QString &name,
                                         const QString &description, bool archive);
    Q_INVOKABLE QVariantMap addDevice(const QString &name);
    Q_INVOKABLE QVariantMap registerAsset(const QString &assetId);
    Q_INVOKABLE QVariantMap importEvidence(const QUrl &file, const QString &device,
                                           const QString &point, const QString &kind,
                                           const QString &captureTime, const QString &recordId = {});
    Q_INVOKABLE QVariantMap importCaptured(const QString &path, const QString &device,
                                          const QString &point, const QString &kind, const QString &recordId = {});
    Q_INVOKABLE QVariantMap addTextEvidence(const QString &device, const QString &point,
                                           const QString &title, const QString &text, const QString &recordId = {});
    Q_INVOKABLE QVariantMap evidenceGraph(const QString &device, const QString &eventId = {}) const;
    Q_INVOKABLE QVariantList eventTimeline(const QString &eventId) const;
    Q_INVOKABLE QVariantMap fuse(const QString &anchorId);
    Q_INVOKABLE QVariantMap evidenceFile(const QString &evidenceId) const;
    Q_INVOKABLE bool playEvidenceVideo(const QString &evidenceId) const;
    QVariantList eventMedia(const QString &eventId) const;
    bool saveObservations(const QString &projectId, const QString &eventId, const QVariantList &observations);
    Q_INVOKABLE QVariantMap analysisInput(const QString &eventId) const;
    Q_INVOKABLE QString findingForEvent(const QString &eventId) const;
    Q_INVOKABLE QVariantMap saveFinding(const QString &projectId, const QString &eventId,
                                         const QString &text, const QString &model);
    Q_INVOKABLE QVariantList inspectionChoices(const QString &eventId) const;
    Q_INVOKABLE QVariantMap linkInspection(const QString &eventId, const QString &recordId);
    Q_INVOKABLE QVariantMap linkDefect(const QString &eventId, const QString &defectId);
    Q_INVOKABLE QVariantMap reviewEvent(const QString &eventId, const QString &classification);
    Q_INVOKABLE QVariantMap addAnalysisEvent(const QString &anchorId, const QString &source, const QString &time,
                                           const QString &title, const QString &note, const QString &classification);
    Q_INVOKABLE QVariantMap reportAnalysisInput(const QString &reportId) const;
    Q_INVOKABLE QVariantMap exportReport(const QString &eventId);
    Q_INVOKABLE QUrl reportUrl(const QString &reportId) const;
    Q_INVOKABLE QString reportHtml(const QString &reportId, const QString &templateId = {}) const;
    Q_INVOKABLE QVariantMap exportReportPdf(const QString &reportId, const QUrl &file,
                                               const QString &templateId = {}) const;

signals:
    void changed();

private:
    QVariantMap writeProjectCopy(const QSqlDatabase &sourceDb, const QVariantMap &sourceProject,
                                 const QUrl &folder, const QString &name, const QString &description,
                                 bool archive) const;
    QString reportText(const QString &reportId) const;
    bool ready() const;
    bool canReadLinkedRecord(const QString &recordId) const;
    bool canReadEvent(const QString &eventId) const;
    QVariantMap transferStatus() const;
    QVariantList rows(const QString &sql, const QVariantList &args = {}) const;
    void log(const QString &action, const QString &detail);
    void rememberProject();
    void release();

    DatabaseManager *business_;
    SessionManager *session_;
    QSqlDatabase db_;
    QVariantMap project_;
};
