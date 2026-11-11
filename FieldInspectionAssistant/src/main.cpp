#include <QGuiApplication>
#include <QFontDatabase>
#include "services/AcousticReportRenderer.h"
#include "ui/NativeHeatmapItem.h"
#include "ui/LiveVisionItem.h"
#include "ui/KnowledgeGraphItem.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QCommandLineParser>
#include <QtQml>
#include <QScreen>
#include <QQuickWindow>
#include <QPalette>
#include "app/AppController.h"
#include "ui/CameraPreviewItem.h"
#include "ui/UiAudit.h"
#include "ui/DesktopWindow.h"
#include "ui/ThermalWindow.h"
#include "ui/ThermalCapture.h"
#include "../third_party/pinyin/pinyininputmethod_p.h"

int main(int argc, char *argv[]) {
    qputenv("QT_QUICK_CONTROLS_STYLE","Fusion");
    qputenv("QT_IM_MODULE","qtvirtualkeyboard");
    qputenv("QT_VIRTUALKEYBOARD_DESKTOP_DISABLE","1");
    QGuiApplication app(argc, argv);
    const auto families = QFontDatabase::families();
    const auto songFamily = families.contains(QStringLiteral("SimSun"))
        ? QStringLiteral("SimSun") : QStringLiteral("Noto Serif CJK SC");
    app.setFont(QFont(songFamily, 9, QFont::Normal));
    QPalette palette = app.palette();
    palette.setColor(QPalette::Window,QColor("#F0F0F0"));
    palette.setColor(QPalette::Base,QColor("#FFFFFF"));
    palette.setColor(QPalette::AlternateBase,QColor("#F3F3F3"));
    palette.setColor(QPalette::Text,QColor("#202020"));
    palette.setColor(QPalette::WindowText,QColor("#202020"));
    palette.setColor(QPalette::Button,QColor("#E5E5E5"));
    palette.setColor(QPalette::ButtonText,QColor("#202020"));
    palette.setColor(QPalette::Highlight,QColor("#2E5F91"));
    palette.setColor(QPalette::HighlightedText,QColor("#FFFFFF"));
    app.setPalette(palette);
    app.setOrganizationName(QStringLiteral("FieldOps"));
    app.setApplicationName(QStringLiteral("FieldInspectionAssistant"));
    app.setApplicationVersion(QStringLiteral("1.7.0"));

    if(app.arguments().size()==4 && app.arguments()[1]=="--render-acoustic-report")
        return renderAcousticReport(app.arguments()[2],app.arguments()[3]);

    if(app.arguments().size()==4 && app.arguments()[1]=="--render-acoustic-charts")
        return renderAcousticCharts(app.arguments()[2],app.arguments()[3]);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("现场巡检助手 - RK3588 工业巡检终端"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    AppController controller;
    if (!controller.start()){
        qCritical() << QStringLiteral("应用启动失败：数据库不可用，请检查存储空间和迁移日志后重试");
        return 2;
    }

    qmlRegisterType<CameraPreviewItem>("FieldInspection", 1, 0, "CameraPreview");
    qmlRegisterType<LiveVisionItem>("FieldInspection", 1, 0, "LiveVisionView");
    qmlRegisterType<ThermalWindow>("FieldInspection", 1, 0, "ThermalWindow");
    qmlRegisterType<ThermalCapture>("FieldInspection", 1, 0, "ThermalCapture");
    qmlRegisterType<ThermalTrend>("FieldInspection", 1, 0, "ThermalTrend");
    qmlRegisterType<NativeHeatmapItem>("FieldInspection",1,0,"NativeHeatmap");
    qmlRegisterType<KnowledgeGraphItem>("FieldInspection",1,0,"KnowledgeGraphView");
    qmlRegisterType<QtVirtualKeyboard::PinyinInputMethod>("FieldOps.Input",1,0,"PinyinInputMethod");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("FieldOpsFontFamily"), songFamily);
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &controller);
    const QUrl url(QStringLiteral("qrc:/qml/Main.qml"));
    engine.load(url);
    if (engine.rootObjects().isEmpty())
        return -1;
    configureDesktopWindow(qobject_cast<QQuickWindow *>(engine.rootObjects().first()));
    if (qEnvironmentVariableIsSet("FIELDOPS_PAGE")) {
        QTimer::singleShot(0, &app, [&] { controller.nav()->push(qEnvironmentVariable("FIELDOPS_PAGE")); });
    }
    if (qEnvironmentVariableIsSet("FIELDOPS_SCREENSHOT")) {
        QTimer::singleShot(4000, &app, [&] {
            auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            if (window) {
                if (qEnvironmentVariableIsSet("FIELDOPS_SCREENSHOT_FRAME") && window->screen()) {
                    const auto frame=window->frameGeometry();
                    window->screen()->grabWindow(0,frame.x(),frame.y(),frame.width(),frame.height())
                        .save(qEnvironmentVariable("FIELDOPS_SCREENSHOT"));
                } else window->grabWindow().save(qEnvironmentVariable("FIELDOPS_SCREENSHOT"));
            }
        });
    }
    runUiAudit(engine);
    return app.exec();
}
