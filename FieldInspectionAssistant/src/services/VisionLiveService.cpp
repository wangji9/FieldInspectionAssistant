#include "VisionLiveService.h"
#include "FaceObservationPacket.h"
#include "CameraService.h"
#include "../app/SessionManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QtConcurrentRun>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <utility>

namespace {
struct Model {
    const char *id, *name;
    bool segmentation;
    const char *landmarks = "";
    int labelCount = 80;
    const char *extra = "";
    const char *inspection = "";
};
constexpr Model Models[]{{"yolov6", "YOLOv6n", false}, {"yolov5", "YOLOv5n", false},
                         {"yolov8", "YOLOv8", false}, {"yolov8_seg", "YOLOv8n-Seg", true},
                         {"yolov8_pose", "YOLOv8n-Pose", false, "body", 1},
                         {"retinaface", "RetinaFace", false, "face", 1},
                         {"expression", "MobileFaceNet", false, "face", 7, "emotion"},
                         {"handpose", "MediaPipe Hands", false, "hand", 1, "hand"},
                         {"yolo26", "YOLO26n", false}, {"yolo26_segment", "YOLO26n-Seg", true},
                         {"yolo26_pose", "YOLO26n-Pose", false, "body", 1},
                         {"industrial_ppe", "SafetyVision YOLOv8n", false, "", 13, "", "ppe"},
                         {"industrial_steel", "NEU-DET YOLOv8n", false, "", 6, "", "steel"},
                         {"face_access", "Face ID", false, "faceMesh", 1, "identity"}};
constexpr qint64 MaxOverlayAgeMs = 160;
const char *const Colors[] = {"#53ddcc", "#ffd067", "#7cafff", "#ff91af", "#bb9aff", "#b9e76b",
                              "#fa9e65", "#75def2", "#d9c985", "#a5a4ff"};

QDir bundle(const QString &id)
{
    const auto root = qEnvironmentVariable("FIELDOPS_VISION_MODELS",
        QCoreApplication::applicationDirPath() + "/../vision-models");
    return QDir(QDir(root).absoluteFilePath(id));
}

QString labelFile(const Model &model)
{
    return *model.landmarks || *model.inspection ? QStringLiteral("model/labels.txt")
                                                : QStringLiteral("model/coco_80_labels_list.txt");
}

bool modelReady(const QDir &dir, const Model &model)
{
    const auto filesReady = [&dir](const QString &stem) {
        return QFileInfo(dir.filePath("model/" + stem + ".rknn")).size() > 0
            && QFileInfo(dir.filePath("model/" + stem + ".weight")).size() > 0;
    };
    return QFileInfo(dir.filePath("stream-runner")).isExecutable()
        && filesReady(QStringLiteral("model"))
        && (!*model.extra || filesReady(QString::fromLatin1(model.extra)))
        && (qstrcmp(model.id, "face_access") != 0 || (filesReady("pose") && filesReady("gaze") && filesReady("mesh")))
        && QFileInfo(dir.filePath(labelFile(model))).size() > 0
        && QFileInfo(dir.filePath("lib/librknn3_api.so")).isFile();
}

QByteArray readCounter(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(256).trimmed() : QByteArray();
}

}

VisionLiveService::VisionLiveService(CameraService *camera, SessionManager *session, QObject *parent)
    : QObject(parent), camera_(camera), session_(session), status_(tr("选择模型与视频源"))
{
    preparationPool_.setMaxThreadCount(1);
    overlayPool_.setMaxThreadCount(1);
    connect(&overlayWatcher_, &QFutureWatcher<OverlaySnapshot>::finished, this, [this] {
        overlayRunning_ = false;
        auto result = overlayWatcher_.result();
        if (result.generation == overlayGeneration_) {
            overlayWorkerMs_ = result.workerMs;
            const bool changed = overlayImage_.cacheKey() != result.image.cacheKey() ||
                                 overlayAnnotations_ != result.annotations;
            overlayImage_ = std::move(result.image);
            overlayAnnotations_ = std::move(result.annotations);
            if (changed)
                emit overlayChanged();
        }
        scheduleOverlay();
    });
    watchdog_.setSingleShot(true);
    acceleratorTimeout_.setSingleShot(true);
    connect(&acceleratorTimeout_, &QTimer::timeout, &acceleratorMonitor_, &QProcess::kill);
    connect(&acceleratorMonitor_, &QProcess::finished, this, [this](int, QProcess::ExitStatus status) {
        acceleratorTimeout_.stop();
        const auto report = acceleratorMonitor_.readAllStandardOutput();
        // This firmware can return a nonzero code for an unsupported field
        // (e.g. power) while still printing a complete online chip row.
        updateAcceleratorStatus(status == QProcess::NormalExit ? report : QByteArray());
    });
    connect(&acceleratorMonitor_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            acceleratorTimeout_.stop();
            updateAcceleratorStatus({});
        }
    });
    connect(camera_, &CameraService::frameReady, this, &VisionLiveService::cameraFrame);
    connect(camera_, &CameraService::error, this, [this](const QString &message) {
        if (active_ && ownsCamera_)
            finish(message);
    });
    connect(camera_, &CameraService::closed, this, [this] {
        if (active_ && ownsCamera_)
            finish(tr("摄像头已断开"));
    });
    connect(session_, &SessionManager::sessionEnding, this, [this] {
        stop();
        latest_ = displayed_ = {};
        resetOverlay();
        detected_.clear();
        mask_.clear();
        trend_.clear();
        plotHistory_.clear();
        updateResults();
        emit previewChanged();
        emit overlayChanged();
        emit changed();
    });
    connect(&worker_, &QProcess::readyReadStandardOutput, this, &VisionLiveService::consume);
    connect(&preparedFrame_, &QFutureWatcher<PreparedFrame>::finished, this, [this] {
        preparingFrame_ = false;
        auto frame = preparedFrame_.result();
        if (active_ && frame.generation == generation_) {
            if (frame.input.isNull()) {
                finish(tr("无法缩放视频帧"));
                return;
            }
            if (!readyFrame_.input.isNull())
                ++skipped_;
            readyFrame_ = std::move(frame);
        }
        sendNext();
    });
    connect(&worker_, &QProcess::readyReadStandardError, this, [this] {
        diagnostics_.append(worker_.readAllStandardError());
        if (diagnostics_.size() > 4096)
            diagnostics_ = diagnostics_.right(4096);
    });
    connect(&worker_, &QProcess::stateChanged, this, &VisionLiveService::changed);
    connect(&worker_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (active_) {
            qWarning().noquote() << "Live vision worker:" << diagnostics_.right(500);
            finish(tr("视觉推理已中断，请检查模型与协处理器连接"));
        }
    });
    connect(&worker_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (active_ && error == QProcess::FailedToStart)
            finish(tr("无法启动视觉模型"));
    });
    connect(&watchdog_, &QTimer::timeout, this, [this] {
        finish(!ready_ ? tr("模型加载超时") : sent_.isNull() ? tr("等待摄像头画面超时") : tr("等待推理结果超时"));
    });
}

VisionLiveService::~VisionLiveService()
{
    preparedFrame_.disconnect(this);
    overlayWatcher_.disconnect(this);
    acceleratorTimeout_.stop();
    acceleratorMonitor_.disconnect(this);
    if (acceleratorMonitor_.state() != QProcess::NotRunning) {
        acceleratorMonitor_.kill();
        acceleratorMonitor_.waitForFinished(500);
    }
    worker_.disconnect(this);
    if (worker_.state() != QProcess::NotRunning) {
        worker_.closeWriteChannel();
        if (!worker_.waitForFinished(250)) {
            worker_.kill();
            worker_.waitForFinished(800);
        }
    }
}

QVariantList VisionLiveService::models() const
{
    QVariantList rows;
    for (const auto &m : Models) {
        if (qstrcmp(m.id, "face_access") == 0)
            continue;
        const auto dir = bundle(QString::fromLatin1(m.id));
        const auto kind = QString::fromLatin1(m.landmarks);
        const auto inspection = QString::fromLatin1(m.inspection);
        const auto task = inspection == "ppe" ? tr("人员防护装备") : inspection == "steel" ? tr("钢材表面缺陷")
            : kind == "body" ? tr("人体关键点") : kind == "hand" ? tr("手部关键点")
            : kind == "face" ? (m.labelCount == 7 ? tr("表情分类") : tr("面部关键点"))
            : m.segmentation ? tr("实例分割") : tr("目标检测");
        rows.append(QVariantMap{{"id", QString::fromLatin1(m.id)}, {"title", QString::fromLatin1(m.name)},
            {"task", task}, {"ready", modelReady(dir, m)}, {"landmarks", kind}, {"industrial", !inspection.isEmpty()},
            {"sample", QFileInfo(dir.filePath("model/sample.jpg")).isFile()}, {"segmentation", m.segmentation}});
    }
    return rows;
}

QVariantMap VisionLiveService::summary() const
{
    const auto size = previewFrame().size();
    return {{"model", model_}, {"source", source_}, {"sourceKind", sourceKind_}, {"landmarks", landmarkKind_},
            {"backend", backend_}, {"frames", frames_}, {"skipped", skipped_},
            {"cameraFps", cameraFps_}, {"inferenceFps", inferenceFps_}, {"inferenceMs", inferenceMs_},
            {"latencyMs", latencyMs_}, {"coverage", coverage_}, {"width", size.width()}, {"height", size.height()},
            {"inputWidth", resultSize_.width()}, {"inputHeight", resultSize_.height()},
            {"maskTracking", followsMasks()}, {"trackedMasks", followsMasks() ? overlayAnnotations_.size() : 0},
            {"maskWorkerMs", overlayWorkerMs_}};
}

QImage VisionLiveService::previewFrame() const
{
    if (latest_.isNull() || (synchronizedPreview_ && !displayed_.isNull()))
        return displayed_;
    return latest_;
}

bool VisionLiveService::overlayVisible() const
{
    if (followsMasks())
        return !overlayImage_.isNull();
    return !displayed_.isNull() && (synchronizedPreview_ || latest_.isNull()
        || (latest_.size() == displayed_.size() && latestTime_ - resultTime_ <= MaxOverlayAgeMs));
}

QVariantList VisionLiveService::annotationRows() const
{
    return followsMasks() ? overlayAnnotations_ : rows_;
}

void VisionLiveService::setSynchronizedPreview(bool enabled)
{
    if (synchronizedPreview_ == enabled)
        return;
    synchronizedPreview_ = enabled;
    if (segmentation_ && sourceKind_ == "camera")
        updateResults(true);
    emit previewChanged();
    emit overlayChanged();
    emit changed();
}

QStringList VisionLiveService::cameras() const { return CameraService::availableDevices(); }
void VisionLiveService::refresh() { emit modelsChanged(); }

void VisionLiveService::refreshHardware()
{
    const auto fields = readCounter("/proc/stat").split('\n').value(0).simplified().split(' ');
    if (fields.size() >= 9 && fields[0] == "cpu") {
        quint64 total = 0;
        for (int i = 1; i <= 8; ++i)
            total += fields[i].toULongLong();
        const auto idle = fields[4].toULongLong() + fields[5].toULongLong();
        if (cpuTotal_ && total > cpuTotal_ && idle >= cpuIdle_) {
            const double load = 100.0 * (1.0 - double(idle - cpuIdle_) / double(total - cpuTotal_));
            hardware_.insert("cpuLoad", qBound(0, qRound(load), 100));
        }
        cpuTotal_ = total;
        cpuIdle_ = idle;
    } else {
        hardware_.remove("cpuLoad");
        cpuTotal_ = cpuIdle_ = 0;
    }
    int cpuMhz = 0;
    const QDir cpuDir("/sys/devices/system/cpu/cpufreq");
    for (const auto &policy : cpuDir.entryList({"policy*"}, QDir::Dirs))
        cpuMhz = qMax(cpuMhz, readCounter(cpuDir.filePath(policy + "/scaling_cur_freq")).toInt() / 1000);
    if (cpuMhz > 0)
        hardware_.insert("cpuMhz", cpuMhz);
    else
        hardware_.remove("cpuMhz");

    // Rockchip's GPU devfreq load reports a percentage followed by @ and Hz.
    const auto gpu = readCounter("/sys/class/devfreq/fb000000.gpu/load").split('@');
    bool loadOk = false, freqOk = false;
    const int gpuLoad = gpu.value(0).toInt(&loadOk);
    const auto gpuHz = gpu.value(1).replace("Hz", "").toLongLong(&freqOk);
    hardware_.remove("gpuLoad");
    hardware_.remove("gpuMhz");
    if (loadOk && freqOk && gpuLoad >= 0 && gpuLoad <= 100 && gpuHz > 0) {
        hardware_.insert("gpuLoad", gpuLoad);
        hardware_.insert("gpuMhz", gpuHz / 1000000);
    }
    emit hardwareChanged();
    if (acceleratorMonitor_.state() == QProcess::NotRunning && QFileInfo("/usr/bin/rknn-smi").isExecutable()) {
        acceleratorTimeout_.start(1200);
        acceleratorMonitor_.start("/usr/bin/rknn-smi", {"info"});
    }
}

void VisionLiveService::updateAcceleratorStatus(const QByteArray &report)
{
    static const QRegularExpression device(QStringLiteral(R"(^\s*\d+\s+Online\s*$)"));
    static const QRegularExpression chip(QStringLiteral(R"(^\s*\d+\s+RK1828\s*$)"));
    hardware_.remove("npuLoad");
    hardware_.remove("npuTemp");
    hardware_.remove("npuMemory");
    hardware_.remove("npuOnline");
    QList<QByteArray> previous;
    for (const auto &line : report.split('\n')) {
        const auto cells = line.split('|');
        if (cells.size() < 6)
            continue;
        if (chip.match(QString::fromLatin1(cells[1])).hasMatch() && previous.size() >= 6
            && device.match(QString::fromLatin1(previous[1])).hasMatch()) {
            hardware_.insert("npuOnline", true);
            bool valid = false;
            const auto load = previous[4].trimmed().toInt(&valid);
            if (valid && load >= 0 && load <= 100)
                hardware_.insert("npuLoad", load);
            const auto temperature = cells[3].trimmed().toInt(&valid);
            if (valid)
                hardware_.insert("npuTemp", temperature);
            const auto memory = cells[4].trimmed();
            if (memory.contains('/'))
                hardware_.insert("npuMemory", QString::fromLatin1(memory));
            break;
        }
        previous = cells;
    }
    emit hardwareChanged();
}

bool VisionLiveService::startCamera(const QString &model, const QString &camera)
{
    return model != "face_access" && start(model, camera);
}

bool VisionLiveService::runSample(const QString &model)
{
    return model != "face_access" && start(model, {}, {}, true);
}

bool VisionLiveService::runImage(const QString &model, const QUrl &file)
{
    if (file.isEmpty())
        return false;
    return model != "face_access" && start(model, {}, file);
}

bool VisionLiveService::faceAccessReady() const { return modelReady(bundle("face_access"), Models[std::size(Models) - 1]); }

bool VisionLiveService::startFaceAccess(const QString &device) { return start("face_access", device); }

void VisionLiveService::setFaceLabels(const QStringList &labels)
{
    for (qsizetype i = 0; i < detected_.size() && i < labels.size(); ++i) {
        auto row = detected_[i].toMap();
        row["label"] = labels[i];
        row["color"] = labels[i] == tr("未识别") ? "#81b7df" : "#63e7bc";
        detected_[i] = row;
    }
}

bool VisionLiveService::start(const QString &model, const QString &device, const QUrl &file, bool sample)
{
    if (running() || !session_->can("inspect"))
        return false;
    const auto spec = std::find_if(std::begin(Models), std::end(Models),
        [&](const Model &candidate) { return model == QString::fromLatin1(candidate.id); });
    const auto dir = spec == std::end(Models) ? QDir() : bundle(model);
    if (spec == std::end(Models) || !modelReady(dir, *spec)) {
        finish(tr("该视觉模型尚未安装"));
        return false;
    }
    const bool still = sample || !file.isEmpty();
    if (!still && (camera_->isOpen() || camera_->opening() || camera_->closing()
                    || camera_->recording() || camera_->finalizing())) {
        finish(tr("摄像头正在使用，请关闭原采集后再运行"));
        return false;
    }
    QSize size;
    QImage image;
    if (still) {
        QImageReader reader(sample ? dir.filePath("model/sample.jpg") : file.toLocalFile());
        reader.setAutoTransform(true);
        const auto sourceSize = reader.size();
        if ((sample || file.isLocalFile()) && sourceSize.isValid()
            && qint64(sourceSize.width()) * sourceSize.height() <= 60000000) {
            if (sourceSize.width() > 2048 || sourceSize.height() > 2048)
                reader.setScaledSize(sourceSize.scaled(QSize(2048, 2048), Qt::KeepAspectRatio));
            image = reader.read();
        }
        if (image.isNull()) {
            finish(tr("图片不可读取或分辨率过高"));
            return false;
        }
    } else {
        if (!cameras().contains(device)) {
            finish(tr("请选择已连接的摄像头"));
            return false;
        }
        const auto formats = camera_->resolutions(device);
        auto format = formats.value(0);
        const QStringList preferredFormats = model == "face_access" ? QStringList{"1280×720", "1920×1080"}
                                                                   : QStringList{"1920×1080", "1280×720"};
        for (const auto &preferred : preferredFormats) {
            if (formats.contains(preferred)) {
                format = preferred;
                break;
            }
        }
        const auto dimensions = format.split(QChar(0x00d7));
        if (dimensions.size() == 2)
            size = QSize(dimensions[0].toInt(), dimensions[1].toInt());
        if (!size.isValid() || !camera_->configure(size.width(), size.height(), 30)) {
            finish(tr("摄像头无可用的 MJPEG 采集格式"));
            return false;
        }
    }
    QFile labels(dir.filePath(labelFile(*spec)));
    if (!labels.open(QIODevice::ReadOnly)) {
        finish(tr("类别文件不可读取"));
        return false;
    }
    labels_ = QString::fromUtf8(labels.readAll()).split('\n', Qt::SkipEmptyParts);
    for (auto &label : labels_)
        label = label.trimmed();
    if (labels_.size() != spec->labelCount || labels_.contains(QString())
        || (spec->labelCount == 80 && labels_.constFirst() != QStringLiteral("person"))) {
        finish(tr("模型类别文件不匹配"));
        return false;
    }
    pending_ = image;
    latest_ = sent_ = displayed_ = {};
    resetOverlay();
    inputSize_ = resultSize_ = QSize();
    latestTime_ = sentTime_ = resultTime_ = 0;
    mask_.clear();
    buffer_.clear();
    diagnostics_.clear();
    detected_.clear();
    rows_.clear();
    categories_.clear();
    trend_.clear();
    plotHistory_.clear();
    nextPlotSample_ = 0;
    oneShot_ = still;
    ready_ = false;
    active_ = true;
    ownsCamera_ = !still;
    frames_ = skipped_ = 0;
    cameraCount_ = inferenceCount_ = 0;
    cameraFps_ = inferenceFps_ = inferenceMs_ = latencyMs_ = coverage_ = 0;
    model_ = QString::fromLatin1(spec->name);
    backend_ = QStringLiteral("RK1828");
    source_ = sample ? tr("样图") : still ? QFileInfo(file.toLocalFile()).fileName() : device;
    sourceKind_ = sample ? "sample" : still ? "image" : "camera";
    segmentation_ = spec->segmentation;
    industrial_ = *spec->inspection;
    faceAccess_ = model == "face_access";
    landmarkKind_ = QString::fromLatin1(spec->landmarks);
    status_ = tr("正在加载 %1").arg(model_);
    rateClock_.start();
    streamClock_.start();
    auto env = QProcessEnvironment::systemEnvironment();
    const auto previous = env.value("LD_LIBRARY_PATH");
    env.insert("LD_LIBRARY_PATH", dir.filePath("lib") + (previous.isEmpty() ? "" : ":" + previous));
    worker_.setProcessEnvironment(env);
    worker_.setWorkingDirectory(dir.absolutePath());
    const auto secondary = *spec->extra ? "model/" + QString::fromLatin1(spec->extra) + ".rknn"
                                       : QStringLiteral("model/model.weight");
    worker_.start(dir.filePath("stream-runner"), {dir.filePath("model/model.rknn"),
        dir.filePath(secondary), qEnvironmentVariable("FIELDOPS_VISION_CORE_MASK", "0x2")});
    watchdog_.start(12000);
    emit previewChanged();
    emit overlayChanged();
    if (!still && !camera_->openNativePreview(device)) {
        finish(camera_->status());
        return false;
    }
    emit changed();
    return true;
}

void VisionLiveService::stop()
{
    if (active_)
        finish(tr("已停止采集"));
}

void VisionLiveService::finish(const QString &status)
{
    const bool closeCamera = ownsCamera_;
    ownsCamera_ = active_ = ready_ = false;
    ++generation_;
    watchdog_.stop();
    pending_ = sent_ = {};
    readyFrame_ = {};
    status_ = status;
    if (closeCamera)
        camera_->closeDevice();
    if (worker_.state() != QProcess::NotRunning) {
        worker_.closeWriteChannel();
        QTimer::singleShot(350, this, [this] {
            if (!active_ && worker_.state() != QProcess::NotRunning)
                worker_.kill();
        });
    }
    emit overlayChanged();
    emit changed();
}

void VisionLiveService::cameraFrame(const QImage &frame)
{
    if (!active_ || oneShot_ || frame.isNull())
        return;
    const bool hadPreview = !latest_.isNull();
    const bool previousOverlay = overlayVisible();
    latest_ = frame;
    latestTime_ = streamClock_.elapsed();
    latestWallMs_ = camera_->frameCapturedMs();
    if (segmentation_) {
        overlayPendingFrame_ = frame;
        overlayPendingTime_ = latestTime_;
        scheduleOverlay();
    }
    ++cameraCount_;
    if (!pending_.isNull())
        ++skipped_;
    pending_ = frame;
    sendNext();
    if (!synchronizedPreview_ || displayed_.isNull())
        emit previewChanged();
    if (!hadPreview)
        emit changed();
    if (previousOverlay != overlayVisible())
        emit overlayChanged();
}

void VisionLiveService::resetOverlay()
{
    ++overlayGeneration_;
    overlayNeedsReset_ = true;
    overlayPendingFrame_ = overlayImage_ = {};
    overlayAnnotations_.clear();
    overlayCorrection_.reset();
    overlayWorkerMs_ = 0;
}

void VisionLiveService::scheduleOverlay()
{
    if (overlayRunning_ || (overlayPendingFrame_.isNull() && !overlayCorrection_))
        return;
    overlayRunning_ = true;
    overlayWatcher_.setFuture(QtConcurrent::run(&overlayPool_,
        [overlay = maskOverlay_, generation = overlayGeneration_, reset = std::exchange(overlayNeedsReset_, false),
         frame = std::exchange(overlayPendingFrame_, QImage()), time = overlayPendingTime_,
         correction = std::exchange(overlayCorrection_, std::nullopt)] {
            QElapsedTimer timer;
            timer.start();
            if (reset)
                overlay->reset();
            if (!frame.isNull())
                overlay->advance(frame, time);
            if (correction)
                overlay->correct(correction->labels, correction->size, correction->regions, correction->time,
                                 correction->follow, correction->resetTargets, correction->reference);
            return OverlaySnapshot{overlay->image(), overlay->annotations(), generation, timer.nsecsElapsed() / 1e6};
        }));
}

void VisionLiveService::sendNext()
{
    if (!active_ || !ready_)
        return;
    if (sent_.isNull() && !readyFrame_.input.isNull()) {
        auto frame = std::exchange(readyFrame_, PreparedFrame());
        sent_ = std::move(frame.original);
        sentTime_ = frame.capturedAt;
        sentWallMs_ = frame.captureWallMs;
        sentPreparationMs_ = frame.preparationMs;
        inputSize_ = frame.input.size();
        frameTime_.start();
        QByteArray packet = QByteArray::number(++sequence_) + ' ' + QByteArray::number(inputSize_.width())
            + ' ' + QByteArray::number(inputSize_.height()) + '\n';
        packet.append(reinterpret_cast<const char *>(frame.input.constBits()), frame.input.sizeInBytes());
        if (worker_.write(packet) != packet.size()) {
            finish(tr("无法向模型送入视频帧"));
            return;
        }
        watchdog_.start(6000);
    }
    if (preparingFrame_ || pending_.isNull())
        return;
    preparingFrame_ = true;
    // Prepare one latest frame while the coprocessor is still working.
    // Neither the preview nor the worker waits behind a growing queue.
    preparedFrame_.setFuture(QtConcurrent::run(&preparationPool_, [frame = std::exchange(pending_, QImage()),
                                                 capturedAt = latestTime_, wallMs = latestWallMs_, generation = generation_] {
        QElapsedTimer cost;
        cost.start();
        auto size = frame.size().scaled(QSize(640, 640), Qt::KeepAspectRatio);
        size.setWidth(qMax(16, (size.width() + 2) & ~3));
        size.setHeight(qMax(16, (size.height() + 2) & ~3));
        auto input = frame.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB888);
        return PreparedFrame{frame, std::move(input), capturedAt, wallMs, generation, cost.nsecsElapsed() / 1e6};
    }));
}

void VisionLiveService::consume()
{
    const auto incoming = worker_.readAllStandardOutput();
    if (!active_)
        return;
    buffer_.append(incoming);
    if (buffer_.size() > 640 * 640 + 128 * 2048) {
        finish(tr("视觉推理响应过大"));
        return;
    }
    const auto newline = buffer_.indexOf('\n');
    if (newline < 0)
        return;
    const auto line = buffer_.left(newline);
    if (line == "READY" && !ready_) {
        buffer_.remove(0, newline + 1);
        ready_ = true;
        status_ = oneShot_ ? tr("正在分析图片") : tr("等待摄像头画面");
        watchdog_.start(6000);
        sendNext();
        emit changed();
        return;
    }
    if (line.startsWith("ERROR")) {
        qWarning().noquote() << "Live vision worker:" << diagnostics_.right(500);
        finish(tr("模型加载或推理失败"));
        return;
    }
    auto offset = newline + 1;
    if (!receiveFrame(line, offset))
        return;
    buffer_.remove(0, offset);
    if (faceAccess_) {
        emit faceObservations(detected_, sentWallMs_);
        for (auto &value : detected_) {
            auto row = value.toMap();
            row.remove("embedding");
            value = row;
        }
    }
    watchdog_.stop();
    displayed_ = std::exchange(sent_, QImage());
    resultSize_ = inputSize_;
    resultTime_ = sentTime_;
    latencyMs_ = sentPreparationMs_ + frameTime_.nsecsElapsed() / 1e6;
    ++frames_;
    ++inferenceCount_;
    const auto interval = rateClock_.elapsed();
    if (interval >= 1000) {
        cameraFps_ = cameraCount_ * 1000.0 / interval;
        inferenceFps_ = inferenceCount_ * 1000.0 / interval;
        cameraCount_ = inferenceCount_ = 0;
        rateClock_.restart();
    }
    trend_.append(latencyMs_);
    if (trend_.size() > 60)
        trend_.removeFirst();
    const auto elapsed = streamClock_.elapsed();
    if (!oneShot_ && elapsed >= nextPlotSample_) {
        plotHistory_.append(QVariant(QVariantList{elapsed / 1000.0, inferenceMs_, latencyMs_}));
        while (plotHistory_.size() > 1 && elapsed / 1000.0 - plotHistory_.constFirst().toList()[0].toDouble() > 30)
            plotHistory_.removeFirst();
        nextPlotSample_ = elapsed + 250;
    }
    if (!oneShot_) {
        status_ = tr("实时推理");
        watchdog_.start(6000);
        // Let the worker start the next frame while the GUI prepares
        // annotations and publishes the completed result.
        sendNext();
    }
    updateResults();
    emit overlayChanged();
    if (oneShot_ || synchronizedPreview_)
        emit previewChanged();
    if (oneShot_) {
        finish(sourceKind_ == "sample" ? tr("样图推理完成") : tr("图片推理完成"));
        return;
    }
    emit changed();
}

bool VisionLiveService::receiveFrame(const QByteArray &line, qsizetype &offset)
{
    const auto fields = line.split(' ');
    const auto fail = [this] { finish(tr("模型返回了无效的帧")); return false; };
    if (fields.size() != 5 || fields[0] != "F" || sent_.isNull() || fields[1].toUInt() != sequence_)
        return fail();
    bool sizeOk, countOk, timeOk;
    const auto maskSize = fields[4].toInt(&sizeOk);
    const auto count = fields[3].toInt(&countOk);
    const auto time = fields[2].toLongLong(&timeOk);
    if (!sizeOk || !countOk || !timeOk || time < 0 || count < 0 || count > 128)
        return fail();
    if (faceAccess_)
        return receiveFacePacket(count, maskSize, time, offset);
    if (maskSize != 0 && maskSize != inputSize_.width() * inputSize_.height())
        return fail();
    // Wait for the full packet before parsing rows, since a large mask can
    // arrive in dozens of pipe reads.
    auto packetEnd = offset;
    for (int index = 0; index < count; ++index) {
        packetEnd = buffer_.indexOf('\n', packetEnd);
        if (packetEnd < 0)
            return false;
        ++packetEnd;
    }
    if (buffer_.size() - packetEnd < maskSize)
        return false;
    QVariantList detections;
    for (int index = 0; index < count; ++index) {
        const auto end = buffer_.indexOf('\n', offset);
        const auto values = buffer_.mid(offset, end - offset).split(' ');
        const int pointCount = landmarkKind_ == "body" ? 17 : landmarkKind_ == "hand" ? 21 : landmarkKind_ == "face" ? 5 : 0;
        if (values.size() != 6 + 3 * pointCount)
            return fail();
        bool classOk, scoreOk;
        const int id = values[0].toInt(&classOk);
        const double score = values[1].toDouble(&scoreOk);
        if (!classOk || !scoreOk || id < 0 || id >= labels_.size() || !std::isfinite(score) || score < 0 || score > 1)
            return fail();
        const auto x = qBound(0, values[2].toInt(), inputSize_.width());
        const auto y = qBound(0, values[3].toInt(), inputSize_.height());
        const auto r = qBound(x, values[4].toInt(), inputSize_.width());
        const auto b = qBound(y, values[5].toInt(), inputSize_.height());
        QVariantMap row{{"id", id}, {"label", labels_[id]}, {"score", score},
            {"box", QRect(x, y, r-x, b-y)}, {"confidence", QString::number(score * 100, 'f', 1) + "%"},
            {"size", QStringLiteral("%1 × %2").arg(r-x).arg(b-y)},
            {"color", QString::fromLatin1(Colors[id % std::size(Colors)])}};
        if (pointCount) {
            QVariantList points;
            int visiblePoints = 0;
            for (int p = 0; p < pointCount; ++p) {
                const qsizetype i = 6 + 3 * p;
                bool xOk, yOk, confidenceOk;
                const qreal px = values[i].toDouble(&xOk), py = values[i + 1].toDouble(&yOk);
                const qreal confidence = values[i + 2].toDouble(&confidenceOk);
                if (!xOk || !yOk || !confidenceOk || !std::isfinite(px) || !std::isfinite(py)
                    || !std::isfinite(confidence))
                    return fail();
                const bool visible = confidence >= 0.35 && px >= 0 && py >= 0
                    && px < inputSize_.width() && py < inputSize_.height();
                points.append(visible ? QVariant::fromValue(QPointF(px, py)) : QVariant());
                visiblePoints += visible;
            }
            row.insert("points", points);
            row.insert("pointCount", visiblePoints);
            row.insert("landmarkKind", landmarkKind_);
        }
        detections.append(row);
        offset = end + 1;
    }
    mask_ = buffer_.mid(offset, maskSize);
    offset += maskSize;
    detected_ = detections;
    inferenceMs_ = time / 1000.0;
    return true;
}

bool VisionLiveService::receiveFacePacket(int count, qsizetype bytes, qint64 time, qsizetype &offset)
{
    using Packet = FaceObservationPacket;
    const auto fail = [this] { finish(tr("人脸模型返回了无效的数据包")); return false; };
    if (count > 3 || bytes != count * qsizetype(sizeof(Packet)))
        return fail();
    if (buffer_.size() - offset < bytes)
        return false;
    const int width = inputSize_.width(), height = inputSize_.height();
    QVariantList detections;
    detections.reserve(count);
    const auto finiteWithin = [](float value, float limit) { return std::isfinite(value) && std::abs(value) <= limit; };
    for (int i = 0; i < count; ++i) {
        Packet packet;
        std::memcpy(&packet, buffer_.constData() + offset, sizeof(packet));
        offset += sizeof(packet);
        if (!finiteWithin(packet.score, 1) || packet.score < 0 ||
            !std::all_of(packet.bounds.begin(), packet.bounds.end(), [&](float v) { return finiteWithin(v, 10000); }) ||
            (packet.flags & ~(Packet::Mesh | Packet::Angles | Packet::Embedding)))
            return fail();
        const int x = qBound(0, int(std::floor(packet.bounds[0])), width);
        const int y = qBound(0, int(std::floor(packet.bounds[1])), height);
        const int right = qBound(x, int(std::ceil(packet.bounds[2])), width);
        const int bottom = qBound(y, int(std::ceil(packet.bounds[3])), height);
        QVariantList points;
        int visible = 0;
        if (packet.flags & Packet::Mesh) {
            points.reserve(Packet::PointCount);
            for (size_t p = 0; p < packet.points.size(); p += 2) {
                const float px = packet.points[p], py = packet.points[p + 1];
                const bool inside = px >= 0 && px < width && py >= 0 && py < height;
                points.append(inside ? QVariant(QPointF(px, py)) : QVariant());
                visible += inside;
            }
        }
        QVariantMap row{{"id",0},{"label",labels_.value(0)},{"score",packet.score},
            {"box",QRect(x,y,right-x,bottom-y)},{"confidence",QString::number(packet.score * 100, 'f', 1) + "%"},
            {"size",QStringLiteral("%1 × %2").arg(right-x).arg(bottom-y)},{"color","#53ddcc"},
            {"points",points},{"pointCount",visible},{"landmarkKind","faceMesh"}};
        if (packet.flags & Packet::Angles) {
            const auto unit = [](const std::array<float, 3> &v) {
                const float normSquared = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
                return std::isfinite(normSquared) && std::abs(normSquared - 1.f) < .02f;
            };
            const auto originValid = [&](const std::array<float, 2> &v) {
                return finiteWithin(v[0], 10000) && finiteWithin(v[1], 10000);
            };
            if (!std::all_of(packet.angles.begin(), packet.angles.end(), [&](float v) { return finiteWithin(v, 180); }) ||
                !finiteWithin(packet.rawHead[0], 180) || !finiteWithin(packet.rawHead[1], 180) ||
                !unit(packet.headDirection) || !unit(packet.gazeDirection) ||
                !originValid(packet.headOrigin) || !originValid(packet.gazeOrigin))
                return fail();
            row.insert("headYaw", packet.angles[0]); row.insert("headPitch", packet.angles[1]);
            row.insert("headRoll", packet.angles[2]); row.insert("gazeYaw", packet.angles[3]);
            row.insert("gazePitch", packet.angles[4]);
            row.insert("rawHeadYaw", packet.rawHead[0]); row.insert("rawHeadPitch", packet.rawHead[1]);
            row.insert("headVector", QVariantList{packet.headDirection[0], packet.headDirection[1], packet.headDirection[2]});
            row.insert("gazeVector", QVariantList{packet.gazeDirection[0], packet.gazeDirection[1], packet.gazeDirection[2]});
            row.insert("headOrigin", QPointF(packet.headOrigin[0], packet.headOrigin[1]));
            row.insert("gazeOrigin", QPointF(packet.gazeOrigin[0], packet.gazeOrigin[1]));
        }
        if (packet.flags & Packet::Embedding) {
            if (!(packet.flags & Packet::Angles) ||
                !std::all_of(packet.embedding.begin(), packet.embedding.end(), [&](float v) { return finiteWithin(v, 1); }))
                return fail();
            row.insert("embedding", QByteArray(reinterpret_cast<const char *>(packet.embedding.data()), sizeof(packet.embedding)));
        }
        detections.append(row);
    }
    detected_ = std::move(detections);
    mask_.clear();
    inferenceMs_ = time / 1000.0;
    return true;
}

void VisionLiveService::setThreshold(double threshold)
{
    if (!std::isfinite(threshold))
        return;
    threshold_ = qBound(0.25, threshold, 0.95);
    updateResults(true);
    emit overlayChanged();
    emit changed();
}

void VisionLiveService::setPeopleOnly(bool enabled)
{
    if (peopleOnly_ == enabled)
        return;
    peopleOnly_ = enabled;
    updateResults(true);
    emit overlayChanged();
    emit changed();
}

void VisionLiveService::updateResults(bool resetMaskTargets)
{
    rows_.clear();
    categories_.clear();
    coverage_ = 0;
    QMap<int, QVariantMap> categories;
    std::array<bool, 256> visibleLabels{};
    QList<SegmentationOverlay::Region> regions;
    int maskIndex = 0;
    const bool follow = followsMasks();
    const double continuationThreshold = qMax(0.25, threshold_ - 0.14);
    for (const auto &value : detected_) {
        const auto row = value.toMap();
        const auto id = row.value("id").toInt();
        const auto score = row.value("score").toDouble();
        const bool allowed = !peopleOnly_ || industrial_ || !landmarkKind_.isEmpty() || id == 0;
        const bool kept = allowed && score >= threshold_;
        visibleLabels[++maskIndex] = kept;
        if (!allowed || row.value("box").toRect().isEmpty())
            continue;
        if (!mask_.isEmpty() && (kept || (follow && score >= continuationThreshold)))
            regions.prepend({row, maskIndex, kept});
        if (!kept)
            continue;
        rows_.append(row);
        auto &group = categories[id];
        group.insert("label", row.value("label"));
        group.insert("color", row.value("color"));
        group.insert("count", group.value("count").toInt() + 1);
    }
    for (const auto &category : categories)
        categories_.append(category);
    std::sort(categories_.begin(), categories_.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("count").toInt() > b.toMap().value("count").toInt();
    });
    if (segmentation_) {
        if (!mask_.isEmpty()) {
            qsizetype pixels = 0;
            for (const uchar label : mask_)
                pixels += visibleLabels[label];
            coverage_ = 100.0 * pixels / mask_.size();
        }
        const bool reset = resetMaskTargets || (overlayCorrection_ && overlayCorrection_->resetTargets);
        overlayCorrection_ = OverlayCorrection{mask_, resultSize_, std::move(regions), displayed_, resultTime_, follow, reset};
        scheduleOverlay();
    }
}
