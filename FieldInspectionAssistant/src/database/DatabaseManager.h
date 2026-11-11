#pragma once

#include <QtCore>
#include <QtSql>
#include "../models/InspectionModels.h"

class DatabaseManager final {
public:
    DatabaseManager();
    ~DatabaseManager();

    bool open(QString *error = nullptr);
    bool isOpen() const { return db_.isOpen(); }
    QString databasePath() const { return databasePath_; }
    QString evidenceDir() const { return evidenceDir_; }
    QSqlDatabase connection() const { return db_; }

    // 巡检记录
    QVector<InspectionRecord> loadInspectionRecords(const QString &filter = QString(), QString *error = nullptr) const;
    bool insertInspection(const InspectionRecord &record, QString *error = nullptr);
    bool updateRecordStatus(const QString &recordId, const QString &status, const QString &result, const QString &answer, QString *error = nullptr);
    int countRecords(const QString &datePrefix = QString()) const;

    // 检查项结果
    bool upsertCheckItem(const CheckItemResult &item, const QString &recordId, QString *error = nullptr);
    QVector<CheckItemResult> loadCheckItems(const QString &recordId, QString *error = nullptr) const;

    // 设备
    QVector<InspectionAsset> loadAssets(QString *error = nullptr) const;
    InspectionAsset findAssetByCode(const QString &code, QString *error = nullptr) const;
    InspectionAsset findAssetById(const QString &id, QString *error = nullptr) const;
    bool createAsset(const InspectionAsset &asset, QString *error = nullptr);
    bool updateAsset(const QString &id, const QString &name, const QString &location, const QString &model, QString *error = nullptr);
    bool setAssetActive(const QString &id, bool active, QString *error = nullptr);

    // 任务
    QVector<InspectionTask> loadTasks(const QString &statusFilter = QString(), QString *error = nullptr) const;
    InspectionTask findTask(const QString &id, QString *error = nullptr) const;
    bool createTask(const InspectionTask &task, QString *error = nullptr);
    bool updateTaskStatus(const QString &taskId, const QString &status, QString *error = nullptr);
    int countTasks(const QString &status, const QString &datePrefix = QString()) const;

    // 检查模板
    TemplateInfo defaultTemplate(QString *error = nullptr) const;
    QVector<TemplateInfo> loadTemplates(QString *error = nullptr) const;
    bool updateTemplateThreshold(const QString &templateId, const QString &itemKey,
                                 const QString &minText, const QString &maxText, const QString &unit,
                                 QString *error = nullptr);

    // 缺陷
    QVector<InspectionDefect> loadDefects(const QString &statusFilter = QString(), QString *error = nullptr) const;
    InspectionDefect findDefect(const QString &id, QString *error = nullptr) const;
    bool createDefect(const InspectionDefect &defect, QString *error = nullptr);
    bool updateDefect(const QString &id, const QString &priority, const QString &assigneeId,
                      const QString &dueAt, const QString &description, QString *error = nullptr);
    bool updateDefectStatus(const QString &defectId, const QString &status, QString *error = nullptr);
    bool transitionDefect(const QString &defectId, const QString &newStatus, const QString &eventType,
                          const QString &note, const QString &actorId, QString *error = nullptr,const QVariantMap &fields={});
    bool createDefectEvent(const QString &defectId, const QString &eventType, const QString &status,
                           const QString &note, const QString &actorId, QString *error = nullptr);
    QVector<DefectEvent> loadDefectEvents(const QString &defectId, QString *error = nullptr) const;
    int countDefects(const QString &status = QString()) const;

    // 附件（照片证据）
    bool createAttachment(const QString &recordId, const QString &defectId, const QString &path,
                          const QString &sha256, const QString &kind, QString *error = nullptr);
    QVector<AttachmentItem> loadAttachments(const QString &recordId = QString(), const QString &defectId = QString(),
                                            QString *error = nullptr) const;
    qint64 evidenceBytes() const;

    // 用户
    QVector<UserInfo> loadUsers(QString *error = nullptr) const;
    UserInfo findUserByName(const QString &name, QString *error = nullptr) const;
    bool createUser(const UserInfo &user, QString *error = nullptr);

    // 同步队列
    bool enqueueSync(const QString &entity, const QString &entityId, const QString &operation,
                     const QString &payloadJson, QString *error = nullptr);
    QVector<SyncItem> pendingSyncItems(QString *error = nullptr) const;
    bool markSyncSent(const QString &id, QString *error = nullptr);
    int pendingSyncCount() const;

    // 状态与审计
    QString state(const QString &key, const QString &fallback = QString()) const;
    bool setState(const QString &key, const QString &value, QString *error = nullptr);
    void setActor(const QString&id,const QString&name){actorId_=id;actorName_=name;}
    bool appendAudit(const QString &action, const QString &entity, const QString &entityId,
                     const QString &detail, QString *error = nullptr);
    QVector<QVariantMap> recentAudit(int limit = 20, QString *error = nullptr) const;

    // 编号与种子数据
    QString nextCode(const QString &prefix, const QString &dateStamp) const;
    bool seedDemoDataIfEmpty(QString *error = nullptr);
    bool saveTask(const QString &id,const QString &name,const QString &assetId,const QString &plannedAt,QString *error);
    bool saveDefect(const QString &id,const QString &title,const QString &description,const QString &priority,QString *error);
    bool removeEntity(const QString &entity,const QString &id,QString *error);
    bool transaction() { return db_.transaction(); }
    bool commit() { return db_.commit(); }
    void rollback() { db_.rollback(); }

private:
    bool migrate(QString *error);
    bool exec(const QString &sql, QString *error = nullptr) const;
    QString errorText(const QSqlQuery &query) const;

    QSqlDatabase db_;
    QString connectionName_;
    QString actorId_,actorName_;
    QString databasePath_;
    QString evidenceDir_;
};
