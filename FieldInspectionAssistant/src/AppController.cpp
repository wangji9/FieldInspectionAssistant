#include "AppController.h"
#include "../database/DatabaseManager.h"
#include "../services/CameraService.h"
#include "../services/InspectionService.h"
#include "../services/AssetService.h"
#include "../services/DefectService.h"
#include "../services/EvidenceService.h"
#include "../services/SyncService.h"
#include "../services/AiInferenceService.h"
#include "../models/TaskListModel.h"
#include "../models/DeviceListModel.h"
#include "../models/DefectListModel.h"
#include "SessionManager.h"
#include "NavigationController.h"

AppController::AppController(QObject *parent) : QObject(parent) {
    db_ = new DatabaseManager();
    camera_ = new CameraService(this);
    inspection_ = new InspectionService(db_, this);
    assets_ = new AssetService(db_, this);
    defects_ = new DefectService(db_, this);
    evidence_ = new EvidenceService(db_, camera_, this);
    sync_ = new SyncService(db_, this);
    ai_ = new AiInferenceService(this);
    session_ = new SessionManager(db_, this);
    nav_ = new NavigationController(this);
    taskModel_ = new TaskListModel(this);
    deviceModel_ = new DeviceListModel(this);
    defectModel_ = new DefectListModel(this);

    connect(inspection_, &InspectionService::tasksChanged, this, &AppController::refreshModels);
    connect(defects_, &DefectService::defectsChanged, this, &AppController::refreshModels);
    connect(assets_, &AssetService::assetsChanged, this, &AppController::refreshModels);
}

AppController::~AppController() {
    delete db_;
}

bool AppController::start() {
    QString err;
    if (!db_->open(&err)) {
        qWarning().noquote() << QStringLiteral("数据库打开失败:") << err;
        return false;
    }
    if (!db_->seedDemoDataIfEmpty(&err)) {
        qWarning().noquote() << QStringLiteral("种子数据初始化失败:") << err;
    }
    // 默认登录为巡检员（首个活跃用户）
    const auto users = db_->loadUsers();
    for (const UserInfo &u : users) {
        if (u.active) { session_->login(u.name); break; }
    }
    defects_->setCurrentActor(session_->userName());
    refreshModels();
    started_ = true;
    emit startedChanged();
    return true;
}

QString AppController::dataDir() const {
    const QString dir = db_->databasePath();
    return dir.isEmpty() ? QString() : QFileInfo(dir).absolutePath();
}

QString AppController::evidenceDir() const { return db_->evidenceDir(); }

QVariantMap AppController::dashboardStats() {
    QVariantMap out;
    out.insert(QStringLiteral("todayTasks"), db_->countTasks(QString(), QDate::currentDate().toString(Qt::ISODate)));
    out.insert(QStringLiteral("pendingDefects"), db_->countDefects(QStringLiteral("pending")) + db_->countDefects(QStringLiteral("unassigned")) + db_->countDefects(QStringLiteral("in_progress")));
    out.insert(QStringLiteral("reviewDefects"), db_->countDefects(QStringLiteral("review")));
    out.insert(QStringLiteral("assetCount"), db_->loadAssets().size());
    out.insert(QStringLiteral("recordCount"), db_->countRecords(QDate::currentDate().toString(Qt::ISODate)));
    out.insert(QStringLiteral("syncPending"), db_->pendingSyncCount());
    out.insert(QStringLiteral("evidenceBytes"), db_->evidenceBytes());
    return out;
}

void AppController::refreshModels() {
    taskModel_->setTasks(db_->loadTasks());
    deviceModel_->setAssets(db_->loadAssets());
    defectModel_->setDefects(db_->loadDefects());
    emit modelsRefreshed();
}

QVariantList AppController::recentActivity(int limit) {
    QVariantList out;
    const auto audit = db_->recentAudit(limit);
    for (const QVariantMap &m : audit) out.push_back(m);
    return out;
}

QVariantList AppController::archiveRecords(const QString &filter) const {
    QVariantList out;
    const auto recs = db_->loadInspectionRecords(filter);
    for (const InspectionRecord &r : recs) {
        const InspectionAsset a = db_->findAssetById(r.assetId);
        QVariantMap m;
        m.insert(QStringLiteral("id"), r.id);
        m.insert(QStringLiteral("time"), r.time);
        m.insert(QStringLiteral("assetCode"), a.code);
        m.insert(QStringLiteral("assetName"), a.name);
        m.insert(QStringLiteral("templateName"), r.templateName);
        m.insert(QStringLiteral("result"), r.result);
        m.insert(QStringLiteral("status"), r.status);
        m.insert(QStringLiteral("statusText"), r.status == QLatin1String("submitted") ? QStringLiteral("已提交")
                                            : r.status == QLatin1String("draft") ? QStringLiteral("草稿")
                                            : r.status);
        m.insert(QStringLiteral("taskCode"), r.taskCode);
        m.insert(QStringLiteral("answer"), r.answer);
        m.insert(QStringLiteral("imagePath"), r.imagePath);
        out.push_back(m);
    }
    return out;
}

QString AppController::appState(const QString &key) const {
    return db_->state(key);
}
