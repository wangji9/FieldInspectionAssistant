#include "ThermalCapture.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QtEndian>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <limits>
#include <numeric>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace {
constexpr int Width = 80, Height = 60, Pixels = Width * Height;
constexpr int DataBytes = Pixels * 2, PacketBytes = 4 + DataBytes + 2;
const QByteArray Prefix = QByteArray::fromHex("5a0806");
const QByteArray Start = QByteArray::fromHex("5a0101"), Stop = QByteArray::fromHex("5a0100");
const double Missing = std::numeric_limits<double>::quiet_NaN();

quint16 crc(const uchar *data)
{
    quint16 sum = 0xffff;
    for (int i = 0; i < DataBytes; ++i) {
        sum ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            sum = (sum >> 1) ^ ((sum & 1) ? 0xa001 : 0);
    }
    return sum;
}

QString timestamp() { return QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz"); }
bool write(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
}

ThermalCapture::ThermalCapture(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAntialiasing(true);
    options_ = defaults();
    auto merged = options_;
    const auto saved = QSettings().value("thermal/workbenchOptions").toMap();
    for (auto it = saved.cbegin(); it != saved.cend(); ++it)
        if (merged.contains(it.key()))
            merged[it.key()] = it.value();
    applyOptions(merged, false);
    connect(&ticker_, &QTimer::timeout, this, [this] {
        if (testing_ && lastFrame_.elapsed() > 4000) {
            disconnectPort(tr("通信测试未收到有效热像帧"));
            return;
        }
        const auto elapsed = rate_.restart();
        fps_ = elapsed > 0 ? frames_ * 1000.0 / elapsed : 0;
        frames_ = 0;
        if (!paused_ && lastFrame_.elapsed() > 3000)
            status_ = tr("串口已打开，等待有效热像帧");
        if (!paused_ && !testing_ && fps_ > 0 && options_.value("autoSave").toBool() &&
            autoSaveClock_.elapsed() >= options_.value("autoSeconds").toInt() * 1000LL) {
            autoSaveClock_.restart();
            const auto saved = saveFrame(QUrl(options_.value("folder").toString()));
            autoSaveStatus_ = saved.isEmpty() ? tr("自动保存失败，请检查目录与文件类型")
                                              : tr("自动保存 %1").arg(QTime::currentTime().toString("HH:mm:ss"));
        }
        emit changed();
    });
}

ThermalCapture::~ThermalCapture() { disconnectPort({}); }

const QStringList &ThermalCapture::seriesNames()
{
    static const QStringList names{"MAX", "MIN", "AVG", "CENTER", "S1", "S2", "S3", "S4", "S5",
                                   "ROI Max", "ROI Min", "ROI Avg"};
    return names;
}

QVariantList ThermalCapture::ports() const
{
    QVariantList ports;
    const auto entries = QDir("/dev/serial/by-id").entryInfoList(
        {"usb-1a86_USB_Single_Serial_*-if00"}, QDir::System | QDir::Files);
    for (const auto &file : entries) {
        if (file.exists())
            ports << QVariantMap{{"name", QFileInfo(file.canonicalFilePath()).fileName()},
                                 {"path", file.absoluteFilePath()}};
    }
    return ports;
}

void ThermalCapture::start(const QString &port, bool testOnly)
{
    if (connected())
        return;
    const auto list = ports();
    if (std::none_of(list.cbegin(), list.cend(), [&port](const QVariant &entry) {
            return entry.toMap().value("path").toString() == port;
        })) {
        disconnectPort(tr("未找到 LEPTON 热像仪，请检查 USB 接线后扫描设备"));
        return;
    }
    const auto path = QFileInfo(port).canonicalFilePath();
    QProcess inUse;
    inUse.start("fuser", {path});
    if (inUse.waitForFinished(300) && inUse.exitStatus() == QProcess::NormalExit && inUse.exitCode() == 0) {
        disconnectPort(tr("热像串口正在使用，请先关闭另一热像窗口的数据流"));
        return;
    }
    fd_ = ::open(QFile::encodeName(path).constData(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
        disconnectPort(tr("无法打开热像串口，请检查设备占用与访问权限"));
        return;
    }
    termios settings{};
    if (ioctl(fd_, TIOCEXCL) < 0 || tcgetattr(fd_, &settings) < 0) {
        disconnectPort(tr("无法独占热像串口"));
        return;
    }
    cfmakeraw(&settings);
    settings.c_cflag &= ~CRTSCTS;
    settings.c_cflag |= CLOCAL | CREAD;
    settings.c_cc[VMIN] = 1;
    settings.c_cc[VTIME] = 0;
    const int dtr = TIOCM_DTR;
    if (cfsetspeed(&settings, B921600) < 0 || tcsetattr(fd_, TCSANOW, &settings) < 0 ||
        ioctl(fd_, TIOCMBIS, &dtr) < 0 || ::write(fd_, Start.constData(), Start.size()) != Start.size()) {
        disconnectPort(tr("热像数据流启动失败"));
        return;
    }
    device_ = QFileInfo(path).fileName();
    paused_ = false;
    testing_ = testOnly;
    streaming_ = true;
    raw_.clear();
    stats_.clear();
    history_.clear();
    clearMarks();
    notifier_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] { receive(); });
    status_ = tr("串口已连接，等待有效帧");
    frames_ = 0;
    rate_.start();
    lastFrame_.start();
    autoSaveClock_.start();
    autoSaveStatus_.clear();
    ticker_.start(1000);
    emit changed();
    emit historyChanged();
}

void ThermalCapture::disconnectPort(const QString &message)
{
    ticker_.stop();
    if (notifier_) {
        notifier_->setEnabled(false);
        notifier_->deleteLater();
        notifier_ = nullptr;
    }
    if (fd_ >= 0) {
        if (streaming_ && ::write(fd_, Stop.constData(), Stop.size()) < 0 && errno != ENODEV)
            qDebug("LEPTON: could not send the stop command");
        ::close(fd_);
        fd_ = -1;
    }
    streaming_ = testing_ = paused_ = false;
    fps_ = 0;
    status_ = message;
    buffer_.clear();
    emit changed();
}

void ThermalCapture::stop() { disconnectPort(tr("采集已停止")); }

void ThermalCapture::togglePause()
{
    if (connected() && !testing_) {
        paused_ = !paused_;
        fps_ = 0;
        frames_ = 0;
        status_ = paused_ ? tr("采集已暂停") : tr("正在接收热像数据");
        emit changed();
    }
}

void ThermalCapture::receive()
{
    char data[16384];
    const auto count = ::read(fd_, data, sizeof(data));
    if (count < 0 && (errno == EAGAIN || errno == EINTR))
        return;
    if (count <= 0) {
        disconnectPort(tr("热像仪已断开"));
        return;
    }
    buffer_.append(data, count);
    while (true) {
        const auto start = buffer_.indexOf(Prefix);
        if (start < 0) {
            buffer_ = buffer_.right(Prefix.size() - 1);
            return;
        }
        buffer_.remove(0, start);
        if (buffer_.size() < PacketBytes)
            return;
        const auto *bytes = reinterpret_cast<const uchar *>(buffer_.constData()) + 4;
        if (crc(bytes) != qFromBigEndian<quint16>(bytes + DataBytes)) {
            buffer_.remove(0, 1);
            continue;
        }
        if (!paused_)
            frame(buffer_.left(PacketBytes));
        buffer_.remove(0, PacketBytes);
        if (testing_) {
            disconnectPort(tr("通信测试通过，收到 CRC 校验有效的热像帧"));
            return;
        }
    }
}

void ThermalCapture::frame(const QByteArray &packet)
{
    raw_ = packet.mid(4, DataBytes);
    deviceId_ = static_cast<uchar>(packet[3]);
    const auto *data = reinterpret_cast<const uchar *>(raw_.constData());
    for (int i = 0; i < Pixels; ++i)
        values_[i] = int(qFromLittleEndian<quint16>(data + 2 * i)) - 4000;
    lastFrame_.restart();
    ++frames_;
    status_ = tr("正在接收热像数据");
    refreshMeasurements();
    rebuildImage();
    Reading reading{QDateTime::currentMSecsSinceEpoch(), {}};
    for (auto it = history_.crbegin(); it != history_.crend(); ++it) {
        if (reading.time - it->time >= 10000) {
            if (reading.time - it->time <= 11500)
                stats_["trendDelta"] = stats_.value("AVG").toDouble() - it->value[2];
            break;
        }
    }
    for (int i = 0; i < int(reading.value.size()); ++i) {
        const auto name = seriesNames().at(i);
        reading.value[i] = stats_.contains(name) ? stats_.value(name).toDouble() : Missing;
    }
    history_.append(reading);
    if (history_.size() > 3600)
        history_.removeFirst();
    emit changed();
    emit historyChanged();
    update();
}

double ThermalCapture::temperature(int x, int y) const
{
    return values_[y * Width + x] / 100.0 + options_.value("offset").toDouble();
}

void ThermalCapture::refreshMeasurements()
{
    if (!hasFrame())
        return;
    const auto minmax = std::minmax_element(values_.begin(), values_.end());
    const auto minIndex = int(minmax.first - values_.begin());
    const auto maxIndex = int(minmax.second - values_.begin());
    minPoint_ = {minIndex % Width, minIndex / Width};
    maxPoint_ = {maxIndex % Width, maxIndex / Width};
    stats_ = {{"id", deviceId_}, {"MIN", temperature(minPoint_.x(), minPoint_.y())},
              {"MAX", temperature(maxPoint_.x(), maxPoint_.y())}, {"CENTER", temperature(Width / 2, Height / 2)},
              {"AVG", std::accumulate(values_.begin(), values_.end(), 0.0) / (Pixels * 100.0)
                  + options_.value("offset").toDouble()}};
    for (int i = 0; i < points_.size(); ++i)
        stats_[seriesNames().at(4 + i)] = temperature(points_[i].x(), points_[i].y());
    if (roi_.isValid()) {
        double lo = std::numeric_limits<double>::infinity(), hi = -lo, total = 0;
        for (int y = roi_.top(); y <= roi_.bottom(); ++y) {
            for (int x = roi_.left(); x <= roi_.right(); ++x) {
                const double value = temperature(x, y);
                lo = std::min(lo, value);
                hi = std::max(hi, value);
                total += value;
            }
        }
        stats_["ROI Min"] = lo;
        stats_["ROI Max"] = hi;
        stats_["ROI Avg"] = total / (roi_.width() * roi_.height());
        stats_["roi"] = QString("%1,%2  %3 × %4").arg(roi_.x() + 1).arg(roi_.y() + 1)
                                             .arg(roi_.width()).arg(roi_.height());
    }
}

void ThermalCapture::rebuildImage()
{
    if (!hasFrame())
        return;
    const bool automatic = options_.value("automatic").toBool();
    const double lo = automatic ? stats_.value("MIN").toDouble() : options_.value("low").toDouble();
    const double hi = automatic ? stats_.value("MAX").toDouble() : options_.value("high").toDouble();
    const double span = std::max(0.01, hi - lo);
    const double contrast = options_.value("contrast").toDouble();
    const auto palette = options_.value("palette").toString();
    const int radius = options_.value("filter").toInt();
    std::array<double, Pixels> horizontal{};
    std::array<double, 9> weights{};
    double divisor = 0;
    for (int n = -radius; n <= radius; ++n) {
        weights[n + radius] = std::exp(-0.5 * n * n / std::max(1, radius));
        divisor += weights[n + radius];
    }
    for (int y = 0; y < Height; ++y)
        for (int x = 0; x < Width; ++x)
            for (int n = -radius; n <= radius; ++n)
                horizontal[y * Width + x] += weights[n + radius] * temperature(qBound(0, x + n, Width - 1), y) / divisor;

    for (int y = 0; y < Height; ++y) {
        auto *rgb = reinterpret_cast<QRgb *>(image_.scanLine(y));
        auto *gray = reinterpret_cast<QRgb *>(gray_.scanLine(y));
        for (int x = 0; x < Width; ++x) {
            double value = 0;
            for (int n = -radius; n <= radius; ++n)
                value += weights[n + radius] * horizontal[qBound(0, y + n, Height - 1) * Width + x] / divisor;
            const double level = qBound(0.0, ((value - lo) / span - 0.5) * contrast + 0.5, 1.0);
            const int intensity = qRound(255 * level);
            gray[x] = qRgb(intensity, intensity, intensity);
            rgb[x] = palette == "gray" ? gray[x] : palette == "blue-red"
                ? QColor::fromRgbF(level, 0.22 + 0.46 * (1 - std::abs(level * 2 - 1)), 1 - level).rgb()
                : QColor::fromHsvF(0.67 * (1 - level), 1, 1).rgb();
        }
    }
}

void ThermalCapture::setMatrix(bool value) { matrix_ = value; update(); }
void ThermalCapture::clearTrend() { history_.clear(); emit historyChanged(); }
void ThermalCapture::clearMarks()
{
    points_.clear();
    roi_ = {};
    refreshMeasurements();
    emit changed();
    update();
}
void ThermalCapture::fit() { zoom_ = 1; pan_ = {}; update(); }
void ThermalCapture::setZoom(double factor) { zoom_ = qBound(1.0, zoom_ * factor, 10.0); if (zoom_ == 1) pan_ = {}; update(); }

QRectF ThermalCapture::imageRect(const QRectF &bounds) const
{
    const QSizeF fitted = QSizeF(Width, Height).scaled(bounds.size(), Qt::KeepAspectRatio);
    const QSizeF size = fitted * zoom_;
    const QPointF center(bounds.center().x(), bounds.top() + fitted.height() / 2);
    return {center - QPointF(size.width() / 2, size.height() / 2) + pan_, size};
}

void ThermalCapture::paint(QPainter *painter) { drawImage(*painter, boundingRect(), false); }

void ThermalCapture::drawImage(QPainter &p, const QRectF &bounds, bool fitted) const
{
    p.setFont(QGuiApplication::font());
    p.fillRect(bounds, Qt::white);
    if (!hasFrame()) {
        p.setPen(QColor("#555555"));
        p.drawText(bounds, Qt::AlignCenter, tr("等待有效热像帧"));
        return;
    }
    const auto area = fitted ? bounds : imageRect(bounds);
    if (area.width() < 16 || area.height() < 16)
        return;
    p.setClipRect(bounds);
    p.setRenderHint(QPainter::SmoothPixmapTransform, options_.value("smooth").toBool());
    p.drawImage(area, matrix_ && !fitted ? gray_ : image_);
    const double w = area.width() / Width, h = area.height() / Height;
    if (matrix_ && !fitted && w >= 39 && h >= 17) {
        const int firstX = qBound(0, int((bounds.left() - area.left()) / w), Width - 1);
        const int lastX = qBound(0, int((bounds.right() - area.left()) / w), Width - 1);
        const int firstY = qBound(0, int((bounds.top() - area.top()) / h), Height - 1);
        const int lastY = qBound(0, int((bounds.bottom() - area.top()) / h), Height - 1);
        for (int y = firstY; y <= lastY; ++y)
            for (int x = firstX; x <= lastX; ++x) {
                p.setPen(qGray(gray_.pixel(x, y)) > 140 ? Qt::black : Qt::white);
                p.drawText(QRectF(area.x() + x * w, area.y() + y * h, w, h), Qt::AlignCenter,
                           QString::number(temperature(x, y), 'f', 2));
            }
    }
    if (!options_.value("annotations").toBool())
        return;
    const QColor color(options_.value("markColor").toString());
    QFont font = p.font();
    font.setPixelSize(options_.value("markSize").toInt());
    p.setFont(font);
    const auto mark = [&](const QPoint &pixel, const QString &label) {
        const QPointF at(area.x() + (pixel.x() + 0.5) * w, area.y() + (pixel.y() + 0.5) * h);
        p.setPen(QPen(Qt::black, 3));
        p.drawLine(at - QPointF(5, 0), at + QPointF(5, 0));
        p.drawLine(at - QPointF(0, 5), at + QPointF(0, 5));
        p.setPen(QPen(color, 1.5));
        p.drawLine(at - QPointF(5, 0), at + QPointF(5, 0));
        p.drawLine(at - QPointF(0, 5), at + QPointF(0, 5));
        const QString text = label + QString::number(temperature(pixel.x(), pixel.y()), 'f', 2) + " °C";
        const QPointF origin(qBound(area.left() + 2, at.x() + 7,
                                   std::max(area.left() + 2, area.right() - p.fontMetrics().horizontalAdvance(text) - 3)),
                             qBound(area.top() + 13, at.y() + 13, area.bottom() - 3));
        p.setPen(Qt::black);
        p.drawText(origin + QPointF(1, 1), text);
        p.setPen(color);
        p.drawText(origin, text);
    };
    mark(maxPoint_, "MAX ");
    mark(minPoint_, "MIN ");
    mark({Width / 2, Height / 2}, "CENTER ");
    for (int i = 0; i < points_.size(); ++i)
        mark(points_[i], options_.value("numbered").toBool() ? seriesNames().at(4 + i) + ' ' : QString());
    if (roi_.isValid() && options_.value("showRoi").toBool()) {
        p.setPen(QPen(color, 1, Qt::DashLine));
        p.drawRect(QRectF(area.x() + roi_.x() * w, area.y() + roi_.y() * h, roi_.width() * w, roi_.height() * h));
    }
}

QPoint ThermalCapture::sensorPoint(const QPointF &position) const
{
    const auto area = imageRect(boundingRect());
    return {qBound(0, int((position.x() - area.left()) * Width / area.width()), Width - 1),
            qBound(0, int((position.y() - area.top()) * Height / area.height()), Height - 1)};
}

void ThermalCapture::mousePressEvent(QMouseEvent *event)
{
    if (!hasFrame() || !imageRect(boundingRect()).contains(event->position())) {
        event->ignore();
        return;
    }
    press_ = event->position();
    oldPan_ = pan_;
    startPoint_ = sensorPoint(press_);
    event->accept();
}

void ThermalCapture::mouseMoveEvent(QMouseEvent *event)
{
    if (tool_ == "pan")
        pan_ = oldPan_ + event->position() - press_;
    else if (tool_ == "roi") {
        roi_ = QRect(startPoint_, sensorPoint(event->position())).normalized();
        refreshMeasurements();
        emit changed();
    }
    update();
}

void ThermalCapture::mouseReleaseEvent(QMouseEvent *event)
{
    if (!hasFrame())
        return;
    if (tool_ == "point") {
        if (points_.size() == 5)
            points_.removeFirst();
        points_.append(sensorPoint(event->position()));
    } else if (tool_ == "roi") {
        roi_ = QRect(startPoint_, sensorPoint(event->position())).normalized();
    }
    refreshMeasurements();
    emit changed();
    update();
}

void ThermalCapture::wheelEvent(QWheelEvent *event)
{
    if (hasFrame()) {
        setZoom(event->angleDelta().y() > 0 ? 1.25 : 0.8);
        event->accept();
    } else event->ignore();
}

QVariantMap ThermalCapture::defaults() const
{
    return {{"palette", "rainbow"}, {"automatic", true}, {"low", 20.0}, {"high", 40.0},
            {"contrast", 1.0}, {"offset", 0.0}, {"filter", 1}, {"smooth", true},
            {"markColor", "#ffffff"}, {"markSize", 12}, {"numbered", true}, {"showRoi", true},
            {"annotations", true}, {"png", true}, {"raw", false}, {"csv", false},
            {"autoSave", false}, {"autoSeconds", 30},
            {"folder", QUrl::fromLocalFile(QDir::homePath()).toString()},
            {"series", seriesNames()}, {"seconds", 60}};
}

bool ThermalCapture::applyOptions(const QVariantMap &options, bool persist)
{
    const auto low = options.value("low").toDouble(), high = options.value("high").toDouble();
    const auto contrast = options.value("contrast").toDouble(), offset = options.value("offset").toDouble();
    const auto color = QColor(options.value("markColor").toString());
    if (!std::isfinite(low) || !std::isfinite(high) || low >= high || !std::isfinite(contrast)
        || contrast < 0.1 || contrast > 5 || !std::isfinite(offset) || std::abs(offset) > 100
        || !color.isValid() || options.value("filter").toInt() < 0 || options.value("filter").toInt() > 4
        || options.value("markSize").toInt() < 8 || options.value("markSize").toInt() > 24
        || options.value("autoSeconds").toInt() < 10 || options.value("autoSeconds").toInt() > 300
        || (options.value("autoSave").toBool() && !options.value("png").toBool()
            && !options.value("raw").toBool() && !options.value("csv").toBool()))
        return false;
    options_ = options;
    autoSaveStatus_.clear();
    autoSaveClock_.start();
    if (persist)
        QSettings().setValue("thermal/workbenchOptions", options_);
    refreshMeasurements();
    rebuildImage();
    emit optionsChanged();
    emit changed();
    update();
    return true;
}

QString ThermalCapture::saveFrame(const QUrl &folder)
{
    return saveFrameFiles(folder).join("，");
}

QStringList ThermalCapture::saveFrameFiles(const QUrl &folder)
{
    if (!hasFrame() || !folder.isLocalFile() || !QDir(folder.toLocalFile()).exists())
        return {};
    const auto path = QDir(folder.toLocalFile()).filePath("thermal-" + timestamp());
    QStringList outputs;
    if (options_.value("png").toBool()) {
        QImage rendered(800, 600, QImage::Format_RGB32);
        QPainter painter(&rendered);
        drawImage(painter, rendered.rect(), true);
        painter.end();
        QSaveFile file(path + ".png");
        if (file.open(QIODevice::WriteOnly) && rendered.save(&file, "PNG") && file.commit())
            outputs << path + ".png";
    }
    if (options_.value("raw").toBool() && write(path + ".raw", raw_))
        outputs << path + ".raw";
    if (options_.value("csv").toBool()) {
        QByteArray csv;
        for (int y = 0; y < Height; ++y) {
            for (int x = 0; x < Width; ++x) {
                if (x) csv += ',';
                csv += QByteArray::number(temperature(x, y), 'f', 2);
            }
            csv += '\n';
        }
        if (write(path + "-celsius.csv", csv))
            outputs << path + "-celsius.csv";
    }
    return outputs;
}

QString ThermalCapture::exportTrend(const QUrl &folder)
{
    if (history_.isEmpty() || !folder.isLocalFile() || !QDir(folder.toLocalFile()).exists())
        return {};
    const auto path = QDir(folder.toLocalFile()).filePath("thermal-trend-" + timestamp() + ".csv");
    QByteArray csv = ("UTC," + seriesNames().join(',') + '\n').toUtf8();
    for (const auto &reading : history_) {
        csv += QDateTime::fromMSecsSinceEpoch(reading.time, Qt::UTC).toString(Qt::ISODateWithMs).toUtf8();
        for (const auto value : reading.value) {
            csv += ',';
            if (std::isfinite(value)) csv += QByteArray::number(value, 'f', 2);
        }
        csv += '\n';
    }
    return write(path, csv) ? path : QString();
}

void ThermalTrend::setSource(ThermalCapture *source)
{
    if (source == source_)
        return;
    disconnect(historyConnection_);
    source_ = source;
    if (source_)
        historyConnection_ = connect(source_, &ThermalCapture::historyChanged, this, [this] { update(); });
    emit changed();
    update();
}

void ThermalTrend::setSeries(const QStringList &series) { series_ = series; emit changed(); update(); }
void ThermalTrend::setSeconds(int seconds) { seconds_ = qBound(5, seconds, 300); emit changed(); update(); }

void ThermalTrend::paint(QPainter *p)
{
    p->setFont(QGuiApplication::font());
    p->fillRect(boundingRect(), Qt::white);
    QList<int> indices;
    int legendRows = 1;
    qreal rowWidth = 0;
    const qreal legendWidth = std::max(60.0, width() - 65);
    for (int i = 0; i < ThermalCapture::seriesNames().size(); ++i) {
        const auto name = ThermalCapture::seriesNames().at(i);
        if (!series_.contains(name))
            continue;
        indices << i;
        const qreal itemWidth = p->fontMetrics().horizontalAdvance(name) + 22;
        if (rowWidth && rowWidth + itemWidth > legendWidth) {
            ++legendRows;
            rowWidth = 0;
        }
        rowWidth += itemWidth;
    }
    const QRectF area = boundingRect().adjusted(51, 16, -14, -(22 + legendRows * 17));
    if (area.width() < 20 || area.height() < 20)
        return;
    p->setPen(QColor("#242424"));
    p->drawLine(area.bottomLeft(), area.bottomRight());
    p->drawLine(area.bottomLeft(), area.topLeft());
    p->drawText(QRectF(5, 0, 43, 17), Qt::AlignLeft, "°C");
    if (!source_ || source_->history().isEmpty()) {
        p->drawText(area, Qt::AlignCenter, tr("等待温度数据"));
        return;
    }
    const auto &history = source_->history();
    const qint64 right = history.last().time, left = right - seconds_ * 1000LL;
    static const std::array<QColor, 12> colors{QColor("#e00000"), QColor("#108000"), Qt::black,
        QColor("#008d96"), QColor("#2044c6"), QColor("#a20ba1"), QColor("#7c6e00"),
        QColor("#8f2700"), QColor("#7936ad"), QColor("#147345"), QColor("#166dba"), QColor("#586b26")};
    double low = std::numeric_limits<double>::infinity(), high = -low;
    for (const auto &reading : history)
        if (reading.time >= left)
            for (const int i : indices)
                if (std::isfinite(reading.value[i])) {
                    low = std::min(low, reading.value[i]);
                    high = std::max(high, reading.value[i]);
                }
    if (!std::isfinite(low)) {
        p->drawText(area, Qt::AlignCenter, tr("所选曲线暂无测量值"));
        return;
    }
    low = std::floor(low - 0.5);
    high = std::ceil(high + 0.5);
    for (int step = 0; step <= 5; ++step) {
        const double ratio = step / 5.0;
        const qreal y = area.bottom() - ratio * area.height();
        const qreal x = area.left() + ratio * area.width();
        p->setPen(QPen(QColor("#c4c4c4"), 1, Qt::DotLine));
        p->drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p->drawLine(QPointF(x, area.top()), QPointF(x, area.bottom()));
        p->setPen(Qt::black);
        p->drawText(QRectF(0, y - 8, 47, 16), Qt::AlignRight | Qt::AlignVCenter,
                    QString::number(low + ratio * (high - low), 'f', 1));
        if (step < 5 || area.width() >= 370)
            p->drawText(QRectF(x - (step == 5 ? 44 : 20), area.bottom() + 3, 54, 15), Qt::AlignCenter,
                        QDateTime::fromMSecsSinceEpoch(left + qRound64((right - left) * ratio)).toString("HH:mm:ss"));
    }
    p->save();
    p->setClipRect(area.adjusted(-1, -1, 1, 1));
    p->setRenderHint(QPainter::Antialiasing);
    for (const int i : indices) {
        QPainterPath curve;
        bool drawing = false;
        QPointF last;
        for (const auto &reading : history) {
            if (reading.time < left)
                continue;
            const double value = reading.value[i];
            if (!std::isfinite(value)) {
                drawing = false;
                continue;
            }
            last = {area.left() + (reading.time - left) * area.width() / (right - left),
                    area.bottom() - (value - low) * area.height() / (high - low)};
            if (drawing) curve.lineTo(last); else curve.moveTo(last);
            drawing = true;
        }
        p->setPen(QPen(colors[i], 1.4));
        p->drawPath(curve);
        if (drawing) p->drawPoint(last);
    }
    p->restore();
    qreal x = area.left();
    qreal y = area.bottom() + 26;
    for (const int i : indices) {
        const auto label = ThermalCapture::seriesNames().at(i);
        const qreal width = p->fontMetrics().horizontalAdvance(label) + 22;
        if (x > area.left() && x + width > area.right()) {
            x = area.left();
            y += 17;
        }
        p->setPen(QPen(colors[i], 2));
        p->drawLine(QPointF(x, y), QPointF(x + 12, y));
        p->setPen(Qt::black);
        p->drawText(QPointF(x + 15, y + 4), label);
        x += width;
    }
}
