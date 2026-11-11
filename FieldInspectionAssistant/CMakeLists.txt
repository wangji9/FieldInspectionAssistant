cmake_minimum_required(VERSION 3.16)
project(FieldInspectionAssistant VERSION 1.7.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Sql Network Concurrent)
find_package(OpenCV REQUIRED COMPONENTS core imgproc video)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FIELDOPS_VKB REQUIRED IMPORTED_TARGET Qt6VirtualKeyboard)
pkg_check_modules(FIELDOPS_ALSA REQUIRED IMPORTED_TARGET alsa)
pkg_check_modules(FIELDOPS_FFTW REQUIRED IMPORTED_TARGET fftw3)
pkg_check_modules(FIELDOPS_XCB REQUIRED IMPORTED_TARGET xcb)

qt_standard_project_setup()

add_subdirectory(third_party/libfvad/src)
set_target_properties(fvad PROPERTIES AUTOMOC OFF)
target_include_directories(fvad PUBLIC third_party/libfvad/include)

qt_add_executable(field_inspection_assistant
    src/main.cpp
    src/app/AppController.h src/app/AppController.cpp
    src/app/NavigationController.h src/app/NavigationController.cpp
    src/app/SessionManager.h src/app/SessionManager.cpp
    src/database/DatabaseManager.h src/database/DatabaseManager.cpp
    src/models/InspectionModels.h
    src/models/TaskListModel.h src/models/TaskListModel.cpp
    src/models/DeviceListModel.h src/models/DeviceListModel.cpp
    src/models/DefectListModel.h src/models/DefectListModel.cpp
    src/services/InspectionService.h src/services/InspectionService.cpp
    src/services/AssetService.h src/services/AssetService.cpp
    src/services/DefectService.h src/services/DefectService.cpp
    src/services/EvidenceService.h src/services/EvidenceService.cpp
    src/services/CameraService.h src/services/CameraService.cpp
    src/services/NativeCameraPipeline.h src/services/NativeCameraPipeline.cpp
    src/services/SyncService.h src/services/SyncService.cpp
    src/services/AiInferenceService.h src/services/AiInferenceService.cpp
    src/services/VisionAlgorithmService.h src/services/VisionAlgorithmService.cpp
    src/services/VisionLiveService.h src/services/VisionLiveService.cpp
    src/services/VideoUnderstandingService.h src/services/VideoUnderstandingService.cpp
    src/services/FaceAccessService.h src/services/FaceAccessService.cpp
    src/services/SegmentationOverlay.h src/services/SegmentationOverlay.cpp
    src/ui/LiveVisionItem.h src/ui/LiveVisionItem.cpp
    src/services/WebSearchService.h src/services/WebSearchService.cpp
    src/services/ConversationExportService.h src/services/ConversationExportService.cpp
    src/services/SpeechService.h src/services/SpeechService.cpp
    src/services/DictationService.h src/services/DictationService.cpp
    src/services/SignalAnalysisService.h src/services/SignalAnalysisService.cpp
    src/services/NativeSignalPipeline.h src/services/NativeSignalPipeline.cpp
    src/services/WaveCsvExport.h src/services/WaveCsvExport.cpp
    src/services/RecordingCatalog.h src/services/RecordingCatalog.cpp
    src/services/ConversationContext.h src/services/ConversationContext.cpp
    src/services/AssistantStorage.h src/services/AssistantStorage.cpp
    src/services/AccessPolicy.h src/services/AccessPolicy.cpp
    src/services/WorkspaceService.h src/services/WorkspaceService.cpp
    src/services/ProjectWorkspace.h src/services/ProjectWorkspace.cpp
    src/services/ProjectInference.h src/services/ProjectInference.cpp
    src/services/HardwareStatusService.h src/services/HardwareStatusService.cpp
    src/services/InspectionWorkflowService.h src/services/InspectionWorkflowService.cpp
    src/services/NativeVoiceInput.h src/services/NativeVoiceInput.cpp
    src/services/SystemContextService.h src/services/SystemContextService.cpp
    src/services/VoiceAssistantService.h src/services/VoiceAssistantService.cpp
    src/services/AcousticEvaluationService.h src/services/AcousticEvaluationService.cpp
    src/services/AcousticReportRenderer.h src/services/AcousticReportRenderer.cpp
    src/services/ChartTransferService.h src/services/ChartTransferService.cpp
    src/services/AgentService.h src/services/AgentService.cpp
    src/services/KnowledgeGraphService.h src/services/KnowledgeGraphService.cpp
    src/services/GraphExtraction.h src/services/GraphExtraction.cpp
    src/ui/KnowledgeGraphItem.h src/ui/KnowledgeGraphItem.cpp
    src/ui/InputController.h src/ui/InputController.cpp
    src/ui/CameraPreviewItem.h src/ui/CameraPreviewItem.cpp
    src/ui/NativeHeatmapItem.h src/ui/NativeHeatmapItem.cpp
    src/ui/UiAudit.h src/ui/UiAudit.cpp
    src/ui/DesktopWindow.h src/ui/DesktopWindow.cpp
    src/ui/ThermalWindow.h src/ui/ThermalWindow.cpp
    src/ui/ThermalCapture.h src/ui/ThermalCapture.cpp
)

target_link_libraries(field_inspection_assistant PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::Sql Qt6::Network PkgConfig::FIELDOPS_VKB PkgConfig::FIELDOPS_ALSA PkgConfig::FIELDOPS_FFTW
    PkgConfig::FIELDOPS_XCB Qt6::Concurrent opencv_core opencv_imgproc opencv_video
)

# Ubuntu's keyboard package omits the Chinese decoder. Build the matching
# upstream Qt 6.4.2 Pinyin implementation and dictionary with the application.
file(GLOB PINYIN_ENGINE CONFIGURE_DEPENDS third_party/pinyin/engine/share/*.cpp)
target_sources(field_inspection_assistant PRIVATE ${PINYIN_ENGINE}
    third_party/pinyin/pinyininputmethod.cpp third_party/pinyin/pinyininputmethod_p.h
    third_party/pinyin/pinyindecoderservice.cpp third_party/pinyin/pinyindecoderservice_p.h)
target_include_directories(field_inspection_assistant PRIVATE third_party/pinyin/engine/include)
target_compile_definitions(field_inspection_assistant PRIVATE HAVE_PINYIN)
qt_add_resources(pinyin_resources third_party/pinyin/pinyin.qrc)
target_sources(field_inspection_assistant PRIVATE ${pinyin_resources})

qt_add_resources(project_resources resources.qrc)
target_sources(field_inspection_assistant PRIVATE ${project_resources})

install(TARGETS field_inspection_assistant RUNTIME DESTINATION bin)

target_link_libraries(field_inspection_assistant PRIVATE fvad)
option(FIELDOPS_VOICE_TESTS "Build hardware-free voice protocol and endpoint checks" OFF)
if(FIELDOPS_VOICE_TESTS)
    enable_testing()
    add_executable(voice_input_test tests/voice_input_test.cpp src/services/NativeVoiceInput.cpp src/services/NativeVoiceInput.h src/services/NativeSignalPipeline.cpp src/services/NativeSignalPipeline.h)
    target_link_libraries(voice_input_test PRIVATE Qt6::Core PkgConfig::FIELDOPS_ALSA PkgConfig::FIELDOPS_FFTW fvad)
    add_test(NAME voice_input COMMAND voice_input_test)
endif()
