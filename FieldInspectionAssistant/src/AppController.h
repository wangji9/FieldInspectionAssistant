#pragma once

#include <QtCore>
#include <QObject>

#include "../database/DatabaseManager.h"
#include "../services/CameraService.h"
#include "../services/InspectionService.h"
#include "../services/AssetService.h"
#include "../services/DefectService.h"
#include "../services/EvidenceService.h"
#include "../services/SyncService.h"
#include "../services/AiInferenceService.h"
#include "SessionManager.h"
#include "NavigationController.h"
#include "../models/TaskListModel.h"
#include "../models/DeviceListModel.h"
#include "../models/DefectListModel.h"

// 应用装配根对象：持有数据库、摄像头、各业务服务与导航，暴露给 QML。
class AppController final : public QObject {
    Q_OBJECT
public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    bool start();   // 打开数据库 + 迁移 + 种子数据 + 默认登录
    bool started() const { return started_; }

    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QString evidenceDir READ evidenceDir CONSTANT)
    Q_PROPERTY(DatabaseManager *db READ db CONSTANT)
    Q_PROPERTY(CameraService *camera READ camera CONSTANT)
    Q_PROPERTY(InspectionService *inspection READ inspection CONSTANT)
    Q_PROPERTY(AssetService *assets READ assets CONSTANT)
    Q_PROPERTY(DefectService *defects READ defects CONSTANT)
    Q_PROPERTY(EvidenceService *evidence READ evidence CONSTANT)
    Q_PROPERTY(SyncService *sync READ sync CONSTANT)
    Q_PROPERTY(AiInferenceService *ai READ ai CONSTANT)
    Q_PROPERTY(SessionManager *session READ session CONSTANT)
    Q_PROPERTY(NavigationController *nav READ nav CONSTANT)
    Q_PROPERTY(TaskListModel *taskModel READ taskModel CONSTANT)
    Q_PROPERTY(DeviceListModel *deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(DefectListModel *defectModel READ defectModel CONSTANT)

    QString version() const { return QStringLiteral("1.0.0"); }
    QString dataDir() const;
    QString evidenceDir() const;
    DatabaseManager *db() const { return db_; }
    CameraService *camera() const { return camera_; }
    InspectionService *inspection() const { return inspection_; }
    AssetService *assets() const { return assets_; }
    DefectService *defects() const { return defects_; }
    EvidenceService *evidence() const { return evidence_; }
    SyncService *sync() const { return sync_; }
    AiInferenceService *ai() const { return ai_; }
    SessionManager *session() const { return session_; }
    NavigationController *nav() const { return nav_; }
    TaskListModel *taskModel() const { return taskModel_; }
    DeviceListModel *deviceModel() const { return deviceModel_; }
    DefectListModel *defectModel() const { return defectModel_; }

    Q_INVOKABLE QVariantMap dashboardStats();
    Q_INVOKABLE void refreshModels();
    Q_INVOKABLE QVariantList recentActivity(int limit = 20);
    Q_INVOKABLE QVariantList archiveRecords(const QString &filter = QString()) const;
    Q_INVOKABLE QString appState(const QString &key) const;

signals:
    void startedChanged();
    void modelsRefreshed();

private:
    DatabaseManager *db_ = nullptr;
    CameraService *camera_ = nullptr;
    InspectionService *inspection_ = nullptr;
    AssetService *assets_ = nullptr;
    DefectService *defects_ = nullptr;
    EvidenceService *evidence_ = nullptr;
    SyncService *sync_ = nullptr;
    AiInferenceService *ai_ = nullptr;
    SessionManager *session_ = nullptr;
    NavigationController *nav_ = nullptr;
    TaskListModel *taskModel_ = nullptr;
    DeviceListModel *deviceModel_ = nullptr;
    DefectListModel *defectModel_ = nullptr;
    bool started_ = false;
};
