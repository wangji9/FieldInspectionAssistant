#pragma once
#include "../services/HardwareStatusService.h"
#include "../services/AcousticEvaluationService.h"
#include "../services/VoiceAssistantService.h"
#include "../services/SystemContextService.h"

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
#include "../services/VisionAlgorithmService.h"
#include "../services/VisionLiveService.h"
#include "../services/VideoUnderstandingService.h"
#include "../services/FaceAccessService.h"
#include "../services/ConversationExportService.h"
#include "../services/SpeechService.h"
#include "../services/DictationService.h"
#include "../services/SignalAnalysisService.h"
#include "../services/AgentService.h"
#include "../services/KnowledgeGraphService.h"
#include "../services/WorkspaceService.h"
#include "../services/ProjectWorkspace.h"
#include "../services/ProjectInference.h"
#include "../services/InspectionWorkflowService.h"
#include "../services/ChartTransferService.h"
#include "../ui/InputController.h"
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
    Q_PROPERTY(VisionAlgorithmService *visionAlgorithms READ visionAlgorithms CONSTANT)
    Q_PROPERTY(VisionLiveService *visionLive READ visionLive CONSTANT)
    Q_PROPERTY(VideoUnderstandingService *videoUnderstanding READ videoUnderstanding CONSTANT)
    Q_PROPERTY(FaceAccessService *faceAccess READ faceAccess CONSTANT)
    Q_PROPERTY(ConversationExportService *exports READ exports CONSTANT)
    Q_PROPERTY(SpeechService *speech READ speech CONSTANT)
    Q_PROPERTY(DictationService *dictation READ dictation CONSTANT)
    Q_PROPERTY(SignalAnalysisService *analysis READ analysis CONSTANT)
    Q_PROPERTY(SystemContextService *systemContext READ systemContext CONSTANT)
    Q_PROPERTY(VoiceAssistantService *voiceAssistant READ voiceAssistant CONSTANT)
    Q_PROPERTY(AgentService *agent READ agent CONSTANT)
    Q_PROPERTY(WorkspaceService *workspace READ workspace CONSTANT)
    Q_PROPERTY(ProjectWorkspace *projects READ projects CONSTANT)
    Q_PROPERTY(ProjectInference *projectInference READ projectInference CONSTANT)
    Q_PROPERTY(InspectionWorkflowService *workflow READ workflow CONSTANT)
    Q_PROPERTY(HardwareStatusService *hardware READ hardware CONSTANT)
    Q_PROPERTY(AcousticEvaluationService *evaluation READ evaluation CONSTANT)
    Q_PROPERTY(ChartTransferService *chartTransfer READ chartTransfer CONSTANT)
    Q_PROPERTY(KnowledgeGraphService *knowledge READ knowledge CONSTANT)
    Q_PROPERTY(InputController *input READ input CONSTANT)
    Q_PROPERTY(SessionManager *session READ session CONSTANT)
    Q_PROPERTY(NavigationController *nav READ nav CONSTANT)
    Q_PROPERTY(TaskListModel *taskModel READ taskModel CONSTANT)
    Q_PROPERTY(DeviceListModel *deviceModel READ deviceModel CONSTANT)
    Q_PROPERTY(DefectListModel *defectModel READ defectModel CONSTANT)

    QString version() const { return QCoreApplication::applicationVersion(); }
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
    VisionAlgorithmService *visionAlgorithms() const { return visionAlgorithms_; }
    VisionLiveService *visionLive() const { return visionLive_; }
    VideoUnderstandingService *videoUnderstanding() const { return videoUnderstanding_; }
    FaceAccessService *faceAccess() const { return faceAccess_; }
    ConversationExportService *exports() const { return exports_; }
    SpeechService *speech() const { return speech_; }
    DictationService *dictation() const { return dictation_; }
    SignalAnalysisService *analysis()const{return analysis_;}
    SystemContextService *systemContext()const{return systemContext_;}
    VoiceAssistantService *voiceAssistant()const{return voiceAssistant_;}
    AgentService *agent()const{return agent_;}
    WorkspaceService *workspace()const{return workspace_;}
    ProjectWorkspace *projects() const { return projects_; }
    ProjectInference *projectInference() const { return projectInference_; }
    InspectionWorkflowService *workflow()const{return workflow_;}
    HardwareStatusService *hardware()const{return hardware_;}
    AcousticEvaluationService *evaluation()const{return evaluation_;}
    ChartTransferService *chartTransfer()const{return chartTransfer_;}
    KnowledgeGraphService *knowledge()const{return knowledge_;}
    InputController *input() const { return input_; }
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
    ProjectWorkspace *projects_ = nullptr;
    ProjectInference *projectInference_ = nullptr;
    DatabaseManager *db_ = nullptr;
    CameraService *camera_ = nullptr;
    InspectionService *inspection_ = nullptr;
    AssetService *assets_ = nullptr;
    DefectService *defects_ = nullptr;
    EvidenceService *evidence_ = nullptr;
    SyncService *sync_ = nullptr;
    AiInferenceService *ai_ = nullptr;
    VisionAlgorithmService *visionAlgorithms_ = nullptr;
    VisionLiveService *visionLive_ = nullptr;
    VideoUnderstandingService *videoUnderstanding_ = nullptr;
    FaceAccessService *faceAccess_ = nullptr;
    ConversationExportService *exports_ = nullptr;
    SpeechService *speech_ = nullptr;
    DictationService *dictation_ = nullptr;
    SignalAnalysisService *analysis_ = nullptr;
    SystemContextService *systemContext_=nullptr;
    VoiceAssistantService *voiceAssistant_=nullptr;
    AgentService *agent_ = nullptr;
    KnowledgeGraphService *knowledge_ = nullptr;
    WorkspaceService *workspace_ = nullptr;
    InspectionWorkflowService *workflow_ = nullptr;
    HardwareStatusService *hardware_=nullptr;
    ChartTransferService *chartTransfer_ = nullptr;
    AcousticEvaluationService *evaluation_=nullptr;
    InputController *input_ = nullptr;
    SessionManager *session_ = nullptr;
    NavigationController *nav_ = nullptr;
    TaskListModel *taskModel_ = nullptr;
    DeviceListModel *deviceModel_ = nullptr;
    DefectListModel *defectModel_ = nullptr;
    bool started_ = false;
};
