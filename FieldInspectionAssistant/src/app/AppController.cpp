#include "AppController.h"
#include "../services/AccessPolicy.h"
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
    exports_ = new ConversationExportService(ai_,this);
    speech_ = new SpeechService(this);
    dictation_ = new DictationService(speech_,ai_,this);
    input_ = new InputController(dictation_,this);
    session_ = new SessionManager(db_, this);
    speakerRecognition_ = new SpeakerRecognitionService(session_, this);
    visionAlgorithms_ = new VisionAlgorithmService(session_, this);
    visionLive_ = new VisionLiveService(camera_, session_, this);
    videoUnderstanding_ = new VideoUnderstandingService(camera_, session_, this);
    faceAccess_ = new FaceAccessService(db_, session_, visionLive_, this);
    projects_ = new ProjectWorkspace(db_, session_, this);
    ai_->setSession(session_);dictation_->setSession(session_);camera_->setSession(session_);
    workspace_ = new WorkspaceService(db_,session_,this);
    connect(workspace_, &WorkspaceService::dataChanged, projects_, &ProjectWorkspace::changed);
    hardware_=new HardwareStatusService(session_,QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),this);
    workflow_ = new InspectionWorkflowService(db_,session_,workspace_,this);
    projectInference_ = new ProjectInference(projects_, session_, workflow_, workspace_, this);
    chartTransfer_ = new ChartTransferService(session_,workspace_,this);
    inspection_->setSession(session_);assets_->setSession(session_);defects_->setSession(session_);evidence_->setSession(session_);sync_->setSession(session_);
    connect(session_,&SessionManager::sessionChanged,this,[this]{if(!session_->loggedIn()){ai_->clearSession();dictation_->clearSession();speech_->stopSpeech();speech_->cancelRecording();}refreshModels();});
    connect(workspace_,&WorkspaceService::dataChanged,this,&AppController::refreshModels);
    analysis_ = new SignalAnalysisService(db_,session_,QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/measurements",this);
    evaluation_=new AcousticEvaluationService(db_,session_,workspace_,analysis_,this);
    acousticImaging_=new AcousticImagingService(analysis_,camera_,evidence_,speakerRecognition_,session_,this);
    analysis_->setReportGuard([this]{if((evaluation_&&evaluation_->busy())||speakerRecognition_->microphoneInUse())return true;if(voiceAssistant_&&voiceAssistant_->engaged())return !voiceAssistant_->suspendForActivity();return false;});
    systemContext_=new SystemContextService(db_,session_,workspace_,analysis_,this);
    knowledge_ = new KnowledgeGraphService(db_,session_,this);
    agent_ = new AgentService(db_,session_,analysis_,knowledge_,workspace_,this);
    voiceAssistant_=new VoiceAssistantService(session_,workspace_,this);
    voiceAssistant_->setSystemContext(systemContext_);agent_->setSystemContext(systemContext_);
    connect(ai_,&AiInferenceService::changed,this,[this]{if(voiceAssistant_&&ai_->busy()&&voiceAssistant_->engaged())voiceAssistant_->suspendForActivity();});
    connect(agent_,&AgentService::changed,this,[this]{if(voiceAssistant_&&agent_->busy()&&voiceAssistant_->engaged())voiceAssistant_->suspendForActivity();});
    voiceAssistant_->setActivityGuard([this]{return ai_->busy()||agent_->busy()||knowledge_->busy()||evaluation_->busy()||analysis_->running()||workspace_->busy()||speech_->recording()||speech_->speechBusy()||dictation_->active()||speakerRecognition_->microphoneInUse();});
    speech_->setExternalGuard([this]{if(speakerRecognition_->microphoneInUse())return true;if(voiceAssistant_&&voiceAssistant_->engaged())return !voiceAssistant_->suspendForActivity();return false;});
    speakerRecognition_->setCaptureGuard([this] {
        if (analysis_->running() || analysis_->recordingBusy() || evaluation_->busy() ||
            speech_->recording() || speech_->speechBusy() || dictation_->active())
            return true;
        return voiceAssistant_->engaged() && !voiceAssistant_->suspendForActivity();
    });
    connect(session_,&SessionManager::sessionChanged,this,[this]{defects_->setCurrentActor(session_->userId());db_->setActor(session_->userId(),session_->userName());});
    workspace_->setActivityGuard([this]{return (voiceAssistant_&&(voiceAssistant_->engaged()||voiceAssistant_->busy()))||evaluation_->busy()||chartTransfer_->busy()||analysis_->running()||analysis_->exportBusy()||analysis_->recordingBusy()||camera_->recording()||camera_->finalizing()||camera_->photoSaving()||ai_->busy()||agent_->busy()||knowledge_->busy()||speech_->recording()||speakerRecognition_->busy()||dictation_->active();});
    connect(workspace_,&WorkspaceService::maintenanceReady,this,[this](const QString&,const QVariantMap&r){if(r.value("restart_required").toBool())QTimer::singleShot(500,qApp,&QCoreApplication::quit);});
    nav_ = new NavigationController(this);
    taskModel_ = new TaskListModel(this);
    deviceModel_ = new DeviceListModel(this);
    defectModel_ = new DefectListModel(this);

    connect(inspection_, &InspectionService::tasksChanged, this, &AppController::refreshModels);
    connect(defects_, &DefectService::defectsChanged, this, &AppController::refreshModels);
    connect(assets_, &AssetService::assetsChanged, this, &AppController::refreshModels);
}

AppController::~AppController() {
    delete acousticImaging_;
    delete speakerRecognition_;
    delete voiceAssistant_;voiceAssistant_=nullptr;delete evaluation_;evaluation_=nullptr;delete hardware_;delete chartTransfer_;delete workflow_;delete workspace_;delete agent_;delete knowledge_;delete systemContext_;delete analysis_;delete db_;
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
    session_->restoreLocalLogin();
    if (!session_->loggedIn())
        refreshModels();
    started_ = true;
    emit startedChanged();
    if (!qEnvironmentVariableIsSet("FIELDOPS_QA_SCRIPT"))
        QTimer::singleShot(0,ai_,&AiInferenceService::prepareStartup);
    return true;
}

QString AppController::dataDir() const {
    const QString dir = db_->databasePath();
    return dir.isEmpty() ? QString() : QFileInfo(dir).absolutePath();
}

QString AppController::evidenceDir() const { return db_->evidenceDir(); }

QVariantMap AppController::dashboardStats() {
    QVariantMap out;
    int todayTasks=0,pending=0,review=0,records=0;const auto today=QDate::currentDate();
    for(auto v:inspection_->tasks()){auto t=v.toMap();if(QDateTime::fromString(t["plannedAt"].toString(),Qt::ISODate).toLocalTime().date()==today)++todayTasks;}
    for(auto v:defects_->defects()){auto d=v.toMap();if(QStringList{"pending","unassigned","in_progress"}.contains(d["status"].toString()))++pending;if(d["status"]=="review")++review;}
    for(auto v:archiveRecords()){auto r=v.toMap();if(r["status"]=="submitted"&&QDateTime::fromString(r["time"].toString(),Qt::ISODate).toLocalTime().date()==today)++records;}
    out["todayTasks"]=todayTasks;out["pendingDefects"]=pending;out["reviewDefects"]=review;out["assetCount"]=AccessPolicy(db_,session_).authenticated()?db_->loadAssets().size():0;out["recordCount"]=records;
    out.insert(QStringLiteral("syncPending"), db_->pendingSyncCount());
    out.insert(QStringLiteral("evidenceBytes"), db_->evidenceBytes());
    return out;
}

void AppController::refreshModels() {
    QVector<InspectionTask> tasks;for(auto t:db_->loadTasks())if(AccessPolicy(db_,session_).task(t.id))tasks<<t;taskModel_->setTasks(tasks);
    deviceModel_->setAssets(AccessPolicy(db_,session_).authenticated()?db_->loadAssets():QVector<InspectionAsset>{});
    QVector<InspectionDefect> defects;for(auto d:db_->loadDefects())if(AccessPolicy(db_,session_).defect(d.id))defects<<d;defectModel_->setDefects(defects);
    emit modelsRefreshed();
}

QVariantList AppController::recentActivity(int limit) {
    QVariantList out;
    if(!AccessPolicy(db_,session_).manages())return out;
    const auto audit = db_->recentAudit(limit);
    for (const QVariantMap &m : audit) out.push_back(m);
    return out;
}

QVariantList AppController::archiveRecords(const QString &filter) const {
    QVariantList out;
    const auto recs = db_->loadInspectionRecords(filter);
    for (const InspectionRecord &r : recs) {
        if(!AccessPolicy(db_,session_).record(r.id))continue;
        const InspectionAsset a = db_->findAssetById(r.assetId);
        QVariantMap m;
        m.insert(QStringLiteral("id"), r.id);
        m["canEdit"]=AccessPolicy(db_,session_).recordEditable(r.id);
        m.insert(QStringLiteral("time"), r.time);
        m.insert(QStringLiteral("assetCode"), a.code);
        m.insert(QStringLiteral("assetId"), r.assetId);
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

