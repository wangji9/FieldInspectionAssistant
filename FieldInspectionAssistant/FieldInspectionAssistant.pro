# qmake 备选构建（推荐使用 CMake；本文件用于 IDE 与快速验证）
QT += core gui quick sql network concurrent
CONFIG += c++17
CONFIG += link_pkgconfig
PKGCONFIG += xcb
CONFIG -= app_bundle
TARGET = field_inspection_assistant
TEMPLATE = app
DEFINES += QT_DEPRECATED_WARNINGS
SOURCES += \
    src/main.cpp \
    src/app/AppController.cpp \
    src/app/NavigationController.cpp \
    src/app/SessionManager.cpp \
    src/database/DatabaseManager.cpp \
    src/models/TaskListModel.cpp \
    src/models/DeviceListModel.cpp \
    src/models/DefectListModel.cpp \
    src/services/InspectionService.cpp \
    src/services/AssetService.cpp \
    src/services/DefectService.cpp \
    src/services/EvidenceService.cpp \
    src/services/CameraService.cpp \
    src/services/VideoUnderstandingService.cpp \
    src/services/FaceAccessService.cpp \
    src/services/SyncService.cpp \
    src/services/AiInferenceService.cpp \
    src/services/WebSearchService.cpp \
    src/ui/CameraPreviewItem.cpp \
    src/ui/ThermalWindow.cpp
HEADERS += \
    src/app/AppController.h \
    src/app/NavigationController.h \
    src/app/SessionManager.h \
    src/database/DatabaseManager.h \
    src/models/InspectionModels.h \
    src/models/TaskListModel.h \
    src/models/DeviceListModel.h \
    src/models/DefectListModel.h \
    src/services/InspectionService.h \
    src/services/AssetService.h \
    src/services/DefectService.h \
    src/services/EvidenceService.h \
    src/services/CameraService.h \
    src/services/VideoUnderstandingService.h \
    src/services/FaceAccessService.h \
    src/services/SyncService.h \
    src/services/AiInferenceService.h \
    src/services/WebSearchService.h \
    src/ui/CameraPreviewItem.h \
    src/ui/ThermalWindow.h
RESOURCES += resources.qrc
