#include "DesktopWindow.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QTimer>
#include <memory>
#include <algorithm>

namespace {
QRect usableArea(QScreen *screen) {
    const auto area = screen->availableGeometry();
    // Leave room for native frame/title bar as well as desktop panels.
    return area.adjusted(12, 36, -12, -12);
}
QRect fit(QRect rect, const QRect &area) {
    rect.setWidth(std::clamp(rect.width(), std::min(800, area.width()), area.width()));
    rect.setHeight(std::clamp(rect.height(), std::min(520, area.height()), area.height()));
    rect.moveLeft(std::clamp(rect.x(), area.left(), area.right()-rect.width()+1));
    rect.moveTop(std::clamp(rect.y(), area.top(), area.bottom()-rect.height()+1));
    return rect;
}
}

void configureDesktopWindow(QQuickWindow *window) {
    // UI audits resize explicitly and must never change the user's saved window.
    if (!window || qEnvironmentVariableIsSet("FIELDOPS_QA_SCRIPT")) return;
    QSettings settings;
    const auto saved = settings.value("window/normalGeometry").toRect();
    auto *screen = saved.isValid() ? QGuiApplication::screenAt(saved.center()) : window->screen();
    if (!screen) screen = QGuiApplication::primaryScreen();
    const auto area = usableArea(screen);
    QSize initial(std::min(1280, qRound(area.width()*0.92)),
                  std::min(780, qRound(area.height()*0.92)));
    QRect rect = saved.isValid() ? saved : QRect(area.center()-QPoint(initial.width()/2, initial.height()/2), initial);
    rect = fit(rect, area);
    window->setMinimumSize(QSize(std::min(800, area.width()), std::min(520, area.height())));
    window->setGeometry(rect);
    auto normal = std::make_shared<QRect>(rect);
    auto maximized = std::make_shared<bool>(settings.value("window/maximized",false).toBool());
    if (*maximized) window->showMaximized();
    else window->showNormal();
    auto *timer = new QTimer(window);
    timer->setSingleShot(true); timer->setInterval(350);
    auto save = [window,normal,maximized] {
        if (window->windowState()==Qt::WindowNoState) {
            *normal = window->geometry(); *maximized = false;
        } else if (window->windowState()==Qt::WindowMaximized) *maximized = true;
        QSettings settings;
        settings.setValue("window/normalGeometry",*normal);
        settings.setValue("window/maximized",*maximized);
    };
    QObject::connect(timer,&QTimer::timeout,window,save);
    QObject::connect(qGuiApp,&QGuiApplication::aboutToQuit,window,save);
    QObject::connect(window,&QWindow::xChanged,timer,[timer]{timer->start();});
    QObject::connect(window,&QWindow::yChanged,timer,[timer]{timer->start();});
    QObject::connect(window,&QWindow::widthChanged,timer,[timer]{timer->start();});
    QObject::connect(window,&QWindow::heightChanged,timer,[timer]{timer->start();});
    QObject::connect(window,&QWindow::visibilityChanged,timer,[timer]{timer->start();});
    auto adapt = [window] {
        auto *screen = window->screen(); if (!screen) return;
        const auto area = usableArea(screen);
        window->setMinimumSize(QSize(std::min(800,area.width()),std::min(520,area.height())));
        if (window->visibility()==QWindow::Windowed)
            window->setGeometry(fit(window->geometry(),area));
    };
    QObject::connect(window,&QWindow::screenChanged,window,adapt);
    for (auto *s : QGuiApplication::screens())
        QObject::connect(s,&QScreen::availableGeometryChanged,window,adapt);
    QObject::connect(qGuiApp,&QGuiApplication::screenAdded,window,[window,adapt](QScreen *s){
        QObject::connect(s,&QScreen::availableGeometryChanged,window,adapt); adapt();
    });
}
