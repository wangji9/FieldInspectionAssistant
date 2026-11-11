#include "ThermalWindow.h"

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int StartupTimeoutMs = 12000;
const QString ThermalTitle = QStringLiteral("热像仪实时数据分析软件");

QString thermalPort()
{
    const auto devices = QDir(QStringLiteral("/dev/serial/by-id")).entryInfoList(
        {QStringLiteral("usb-1a86_USB_Single_Serial_*-if00")}, QDir::System | QDir::Files);
    return devices.size() == 1 ? devices.first().canonicalFilePath() : QString();
}

bool portIsBusy(const QString &device)
{
    if (device.isEmpty())
        return false;
    QProcess check;
    check.start(QStringLiteral("fuser"), {device});
    if (!check.waitForFinished(500))
        return false;
    return check.exitStatus() == QProcess::NormalExit && check.exitCode() == 0;
}
}

ThermalWindow::ThermalWindow(QQuickItem *parent) : QQuickItem(parent)
{
    poll_.setInterval(100);
    connect(&poll_, &QTimer::timeout, this, &ThermalWindow::findWindow);
    connect(this, &QQuickItem::visibleChanged, this, &ThermalWindow::updateGeometry);
    connect(&process_, &QProcess::stateChanged, this, &ThermalWindow::stateChanged);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            clearWindow();
            setStatus(tr("热像分析软件启动失败。请检查板端安装。"));
        }
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus exit) {
        clearWindow();
        if (!stopping_ && (code != 0 || exit != QProcess::NormalExit))
            setStatus(tr("热像分析进程已退出。请重新打开。"));
        else if (!stopping_)
            setStatus(tr("热像分析已关闭。"));
        stopping_ = false;
    });
    process_.setStandardOutputFile(QProcess::nullDevice());
    process_.setStandardErrorFile(QProcess::nullDevice());
}

ThermalWindow::~ThermalWindow()
{
    requestClose();
    if (active() && !process_.waitForFinished(800)) {
        process_.kill();
        process_.waitForFinished(500);
    }
    clearWindow();
}

xcb_atom_t ThermalWindow::atom(const QByteArray &name) const
{
    auto *reply = xcb_intern_atom_reply(x11_, xcb_intern_atom(x11_, 0, name.size(), name.data()), nullptr);
    const auto result = reply ? reply->atom : XCB_ATOM_NONE;
    std::free(reply);
    return result;
}

QByteArray ThermalWindow::property(xcb_window_t window, xcb_atom_t name, xcb_atom_t type) const
{
    auto *reply = xcb_get_property_reply(x11_, xcb_get_property(x11_, 0, window, name, type, 0, 65536), nullptr);
    if (!reply)
        return {};
    const QByteArray result(static_cast<const char *>(xcb_get_property_value(reply)),
                            xcb_get_property_value_length(reply));
    std::free(reply);
    return result;
}

QList<xcb_window_t> ThermalWindow::clientWindows() const
{
    const QByteArray clients = property(root_, atom("_NET_CLIENT_LIST"), XCB_ATOM_WINDOW);
    QList<xcb_window_t> result(clients.size() / sizeof(xcb_window_t));
    if (!result.isEmpty())
        std::memcpy(result.data(), clients.data(), result.size() * sizeof(xcb_window_t));
    return result;
}

void ThermalWindow::start()
{
    if (active() || !isVisible() || !window())
        return;
    if (QGuiApplication::platformName() != QStringLiteral("xcb")) {
        setStatus(tr("请在板端的 X11 桌面打开巡检助手后再使用热像分析。"));
        return;
    }
    const QString launcher = QDir::home().filePath(QStringLiteral("ubuntu/launch-app.sh"));
    if (!QFileInfo(launcher).isExecutable()) {
        setStatus(tr("没有找到板端热像分析软件的启动程序。"));
        return;
    }
    int screen = 0;
    x11_ = xcb_connect(nullptr, &screen);
    if (xcb_connection_has_error(x11_)) {
        clearWindow();
        setStatus(tr("无法连接板端 X11 桌面。"));
        return;
    }
    auto roots = xcb_setup_roots_iterator(xcb_get_setup(x11_));
    for (int i = 0; i < screen; ++i)
        xcb_screen_next(&roots);
    root_ = roots.data->root;
    const auto titleAtom = atom("_NET_WM_NAME");
    const auto utf8 = atom("UTF8_STRING");
    for (const auto client : clientWindows()) {
        if (QString::fromUtf8(property(client, titleAtom, utf8)).contains(ThermalTitle)) {
            clearWindow();
            setStatus(tr("热像分析的独立窗口已打开。关闭后即可在此页使用。"));
            return;
        }
    }
    const QString device = thermalPort();
    if (portIsBusy(device)) {
        clearWindow();
        setStatus(tr("热像仪串口正在使用。请关闭其他热像预览后重试。"));
        return;
    }

    // Store the embedded instance's serial and display settings separately.
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/thermal-analysis");
    QSettings settings(configHome + QStringLiteral("/LEPTON2.5/BEIQIAN.conf"), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("mainwindow"));
    settings.setValue(QStringLiteral("COM"), QFileInfo(device).fileName());
    settings.setValue(QStringLiteral("COMOPEN"), device.isEmpty() ? 0 : 1);
    settings.setValue(QStringLiteral("windowsize"), 0);
    settings.setValue(QStringLiteral("hidebotom"), 0);
    for (const auto &key : {QStringLiteral("hidethermal"), QStringLiteral("hidechart")}) {
        if (!settings.contains(key))
            settings.setValue(key, 0);
    }
    settings.endGroup();
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        clearWindow();
        setStatus(tr("无法保存热像分析页的配置。"));
        return;
    }

    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("XDG_CONFIG_HOME"), configHome);
    process_.setProcessEnvironment(environment);
    setStatus(tr("正在打开热像分析软件…"));
    startup_.start();
    poll_.start();
    process_.start(launcher, {});
}

void ThermalWindow::findWindow()
{
    if (startup_.elapsed() > StartupTimeoutMs) {
        stop();
        setStatus(tr("热像分析窗口打开超时。请重试。"));
        return;
    }
    const auto windows = clientWindows();
    if (guest_ != XCB_WINDOW_NONE) {
        // The window manager must release the client before we reparent it.
        if (!windows.contains(guest_))
            embedWindow();
        return;
    }
    if (process_.processId() <= 0)
        return;
    const auto pidAtom = atom("_NET_WM_PID");
    const auto titleAtom = atom("_NET_WM_NAME");
    const auto utf8 = atom("UTF8_STRING");
    for (const auto client : windows) {
        const auto data = property(client, pidAtom, XCB_ATOM_CARDINAL);
        uint32_t pid = 0;
        if (data.size() == sizeof(pid))
            std::memcpy(&pid, data.data(), sizeof(pid));
        if (pid != process_.processId() ||
            !QString::fromUtf8(property(client, titleAtom, utf8)).contains(ThermalTitle))
            continue;
        guest_ = client;
        const auto hints = property(guest_, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS);
        if (hints.size() >= 7 * int(sizeof(uint32_t))) {
            uint32_t geometry[7];
            std::memcpy(geometry, hints.data(), sizeof(geometry));
            if (geometry[0] & (1 << 4)) // PMinSize
                minimum_ = QSize(geometry[5], geometry[6]) / window()->devicePixelRatio();
        }
        xcb_unmap_window(x11_, guest_);
        xcb_unmap_notify_event_t withdrawal{};
        withdrawal.response_type = XCB_UNMAP_NOTIFY;
        withdrawal.event = root_;
        withdrawal.window = guest_;
        xcb_send_event(x11_, false, root_, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, reinterpret_cast<const char *>(&withdrawal));
        xcb_flush(x11_);
        break;
    }
}

void ThermalWindow::embedWindow()
{
    if (!window()) {
        stop();
        return;
    }
    const auto parent = xcb_window_t(window()->winId());
    const auto container = xcb_generate_id(x11_);
    auto *error = xcb_request_check(x11_, xcb_create_window_checked(
        x11_, XCB_COPY_FROM_PARENT, container, parent, 0, 0, 1, 1, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT, XCB_COPY_FROM_PARENT, 0, nullptr));
    if (!error) {
        container_ = container;
        error = xcb_request_check(x11_, xcb_reparent_window_checked(x11_, guest_, container_, 0, 0));
    }
    if (error) {
        std::free(error);
        stop();
        setStatus(tr("无法将热像窗口嵌入巡检助手。"));
        return;
    }
    poll_.stop();
    emit minimumSizeChanged();
    emit stateChanged();
    emit contentSizeChanged();
    updateGeometry();
    setStatus({});
}

void ThermalWindow::geometryChange(const QRectF &next, const QRectF &previous)
{
    QQuickItem::geometryChange(next, previous);
    updateGeometry();
    if (next.size() != previous.size())
        emit contentSizeChanged();
}

void ThermalWindow::setScrollX(qreal offset)
{
    scrollX_ = offset;
    updateGeometry();
}

void ThermalWindow::setScrollY(qreal offset)
{
    scrollY_ = offset;
    updateGeometry();
}

void ThermalWindow::updateGeometry()
{
    if (!embedded() || !window())
        return;
    if (!isVisible() || width() < 1 || height() < 1) {
        xcb_unmap_window(x11_, container_);
        xcb_flush(x11_);
        return;
    }
    const qreal dpr = window()->devicePixelRatio();
    const auto origin = mapToScene({0, 0});
    const uint16_t mask = XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                          XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT;
    const uint32_t bounds[] = {uint32_t(qRound(origin.x() * dpr)), uint32_t(qRound(origin.y() * dpr)),
                               uint32_t(qRound(width() * dpr)), uint32_t(qRound(height() * dpr))};
    const qreal x = qBound(0.0, scrollX_, qMax(0.0, contentWidth() - width()));
    const qreal y = qBound(0.0, scrollY_, qMax(0.0, contentHeight() - height()));
    const uint32_t content[] = {uint32_t(-qRound(x * dpr)), uint32_t(-qRound(y * dpr)),
                                uint32_t(qRound(contentWidth() * dpr)), uint32_t(qRound(contentHeight() * dpr))};
    xcb_configure_window(x11_, container_, mask, bounds);
    xcb_configure_window(x11_, guest_, mask, content);
    xcb_map_window(x11_, guest_);
    xcb_map_window(x11_, container_);
    xcb_flush(x11_);
}

void ThermalWindow::requestClose()
{
    poll_.stop();
    if (!active())
        return;
    if (guest_ == XCB_WINDOW_NONE) {
        process_.terminate();
        return;
    }
    xcb_client_message_event_t message{};
    message.response_type = XCB_CLIENT_MESSAGE;
    message.format = 32;
    message.window = guest_;
    message.type = atom("WM_PROTOCOLS");
    message.data.data32[0] = atom("WM_DELETE_WINDOW");
    message.data.data32[1] = XCB_CURRENT_TIME;
    xcb_send_event(x11_, false, guest_, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char *>(&message));
    xcb_flush(x11_);
}

void ThermalWindow::stop()
{
    if (!active() || stopping_)
        return;
    stopping_ = true;
    setStatus(tr("热像分析已关闭。"));
    requestClose();
    const auto pid = process_.processId();
    QTimer::singleShot(1800, this, [this, pid] {
        if (stopping_ && active() && process_.processId() == pid)
            process_.kill();
    });
}

void ThermalWindow::clearWindow()
{
    poll_.stop();
    if (x11_)
        xcb_disconnect(x11_);
    x11_ = nullptr;
    container_ = guest_ = root_ = XCB_WINDOW_NONE;
    minimum_ = QSize(0, 0);
    emit minimumSizeChanged();
    emit contentSizeChanged();
    emit stateChanged();
}

void ThermalWindow::setStatus(const QString &status)
{
    if (status_ == status)
        return;
    status_ = status;
    emit statusChanged();
}
