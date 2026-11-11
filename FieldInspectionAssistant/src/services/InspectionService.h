#pragma once

#include <QtCore>
#include "../models/InspectionModels.h"

class DatabaseManager;
class SessionManager;

// 巡检任务与检查项执行服务：任务进度、草稿恢复、结果保存、提交校验。
class InspectionService final : public QObject {
    Q_OBJECT
public:
    void setSession(SessionManager*s){session_=s;}
    explicit InspectionService(DatabaseManager *db, QObject *parent = nullptr);

    Q_INVOKABLE QVariantList tasks(const QString &statusFilter = QString()) const;
    Q_INVOKABLE QVariantMap saveTask(const QString &id,const QString &name,const QString &assetId,const QString &plannedAt);
    Q_INVOKABLE QVariantMap deleteTask(const QString &id);
    Q_INVOKABLE QVariantMap saveArchive(const QString &id,const QString &assetId,const QString &result,const QString &answer);
    Q_INVOKABLE QVariantMap deleteArchive(const QString &id);
    Q_INVOKABLE QVariantList taskAssets(const QString &taskId);
    Q_INVOKABLE QVariantList templateItems(const QString &taskId=QString()) const;
    // 返回某任务+设备对应的草稿记录（不存在则创建），map 含 recordId
    Q_INVOKABLE QVariantMap recordFor(const QString &taskId, const QString &assetId);
    // 已保存的检查项结果（含状态）
    Q_INVOKABLE QVariantList progress(const QString &recordId) const;
    Q_INVOKABLE QVariantMap startTask(const QString &taskId);
    Q_INVOKABLE QVariantList inspectionCandidates(const QString &assetId) const;
    Q_INVOKABLE QVariantMap beginInspection(const QString &assetId,const QString &taskId);
    // result: normal / abnormal / skip；value 为测量值文本；note 为跳过原因/说明
    Q_INVOKABLE QVariantMap saveItem(const QString &recordId, const QString &itemKey,
                                     const QString &result, const QString &value, const QString &note);
    // 提交整个任务：校验必填项、跳过原因、未处理异常
    Q_INVOKABLE QVariantMap completeTask(const QString &taskId);

    static QVariantMap taskToMap(const InspectionTask &task);
    static QVariantMap itemToMap(const CheckItemResult &item);
    static QString statusText(const QString &status);

signals:
    void tasksChanged();
    void recordChanged(const QString &recordId);
    void taskCompleted(const QString &taskCode);

private:
    QVariantList parseTemplateItems(const QString &taskId=QString()) const;
    bool ownsTask(const QString&)const;
    QString recordTask(const QString&)const;
    bool freezeTemplate(const QString&);
    QVector<InspectionTask> loadTasksInternal(const QString &filter) const;
    int taskAnsweredCount(const QString &taskId, int *total = nullptr,
                          int *abnormal = nullptr, QString *firstPendingAbnormalItem = nullptr) const;
    DatabaseManager *db_ = nullptr;
    SessionManager *session_=nullptr;
};
