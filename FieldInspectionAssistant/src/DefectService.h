#pragma once

#include <QtCore>
#include "../models/InspectionModels.h"

class DatabaseManager;

// 缺陷中心服务：缺陷状态机（未分派→待处理→处理中→待复核→已关闭；复核失败重新打开）。
class DefectService final : public QObject {
    Q_OBJECT
public:
    explicit DefectService(DatabaseManager *db, QObject *parent = nullptr);

    Q_INVOKABLE QVariantList defects(const QString &statusFilter = QString()) const;
    Q_INVOKABLE QVariantMap defect(const QString &id) const;
    Q_INVOKABLE QVariantList events(const QString &defectId) const;
    Q_INVOKABLE QVariantList users() const;

    // 由异常检查项创建缺陷单（title 可留空自动生成）
    Q_INVOKABLE QVariantMap createDefect(const QString &recordId, const QString &assetId,
                                         const QString &title, const QString &description,
                                         const QString &priority);
    Q_INVOKABLE QVariantMap assign(const QString &defectId, const QString &assigneeId,
                                   const QString &dueAt, const QString &note);
    Q_INVOKABLE QVariantMap start(const QString &defectId, const QString &note);
    // 整改结果必填；提交后进入待复核
    Q_INVOKABLE QVariantMap submitFeedback(const QString &defectId, const QString &feedback);
    // passed=true 复核通过并关闭；passed=false 复核失败重新打开
    Q_INVOKABLE QVariantMap review(const QString &defectId, bool passed, const QString &comment);
    Q_INVOKABLE bool canAddEvidence(const QString &defectId) const;
    static QString statusText(const QString &status);
    static QString priorityText(const QString &priority);

    void setCurrentActor(const QString &name) { currentActor_ = name; }
    QString currentActor() const { return currentActor_; }

signals:
    void defectsChanged();
    void defectUpdated(const QString &defectId);

private:
    QVariantMap defectToMap(const InspectionDefect &d) const;
    QVariantMap transition(const QString &defectId, const QString &newStatus,
                           const QString &eventType, const QString &note);
    DatabaseManager *db_ = nullptr;
    QString currentActor_;
};
