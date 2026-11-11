#include "VideoUnderstandingService.h"
#include "CameraService.h"
#include "../app/SessionManager.h"

#include <QBuffer>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrentRun>

namespace {
constexpr int FrameCount = 3;
constexpr int SampleIntervalMs = 720;
constexpr int MaxResults = 120;
}

VideoUnderstandingService::VideoUnderstandingService(CameraService *camera, SessionManager *session, QObject *parent)
    : QObject(parent), camera_(camera), session_(session), status_(tr("选择摄像头后开始分析"))
{
    encoderPool_.setMaxThreadCount(1);
    connect(camera_, &CameraService::frameReady, this, &VideoUnderstandingService::onFrame);
    connect(camera_, &CameraService::error, this, [this](const QString &message) {
        if (active_)
            finish(message);
    });
    connect(camera_, &CameraService::closed, this, [this] {
        if (active_)
            finish(tr("摄像头已关闭"));
    });
    connect(session_, &SessionManager::sessionEnding, this, [this] {
        stop();
        events_.clear();
        delayMs_ = 0;
        emit eventAdded();
    });
    connect(&encoding_, &QFutureWatcher<QByteArray>::finished, this, [this] {
        if (!active_ || generation_ != encodingGeneration_)
            return;
        const auto data = encoding_.result();
        if (data.isEmpty()) {
            finish(tr("无法编码摄像头采样帧"));
            return;
        }
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1828/api/video/clip")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setTransferTimeout(70000);
        auto *reply = network_.post(request, data);
        reply_ = reply;
        connect(reply, &QNetworkReply::finished, this, [this, reply, clip = pending_] {
            receive(reply, clip);
        });
    });
}

bool VideoUnderstandingService::start(const QString &device, const QString &model)
{
    if (!session_->can("inspect") || active_ || encoding_.isRunning() || device.isEmpty())
        return false;
    if (model != "qwen3_video" && model != "qwen3_video_4b")
        return false;
    if (camera_->isOpen() || camera_->opening() || camera_->closing() || camera_->finalizing()) {
        setStatus(tr("摄像头正在使用，请先关闭当前采集"));
        return false;
    }
    ++generation_;
    model_ = model;
    active_ = true;
    inferenceEnabled_ = true;
    events_.clear();
    samples_.clear();
    submittedMs_ = -1;
    delayMs_ = 0;
    clock_.start();
    status_ = tr("正在采集摄像头画面");
    emit eventAdded();
    emit changed();
    if (!camera_->openNativePreview(device)) {
        finish(camera_->status());
        return false;
    }
    QNetworkRequest prepare(QUrl(QStringLiteral("http://127.0.0.1:1828/api/models/prepare")));
    prepare.setHeader(QNetworkRequest::ContentTypeHeader,QStringLiteral("application/json"));
    prepare.setTransferTimeout(3000);
    auto *warm=network_.post(prepare,QJsonDocument(QJsonObject{{"model",model_}}).toJson(QJsonDocument::Compact));
    connect(warm,&QNetworkReply::finished,warm,&QObject::deleteLater);
    return true;
}

void VideoUnderstandingService::stop()
{
    if (active_)
        finish(tr("已停止分析"));
}

void VideoUnderstandingService::finish(const QString &status)
{
    active_ = false;
    ++generation_;
    samples_.clear();
    pending_.clear();
    if (auto *reply = reply_.data()) {
        reply_.clear();
        reply->abort();
    }
    if (!camera_->closing() && (camera_->isOpen() || camera_->opening()))
        camera_->closeDevice();
    preview_ = {};
    status_ = status;
    emit previewChanged();
    emit changed();
}

void VideoUnderstandingService::setStatus(const QString &status)
{
    if (status_ != status) {
        status_ = status;
        emit changed();
    }
}

void VideoUnderstandingService::onFrame(const QImage &image)
{
    if (!active_ || image.isNull())
        return;
    preview_ = image;
    emit previewChanged();
    const auto now = clock_.elapsed();
    if (samples_.isEmpty() || now - samples_.last().elapsedMs >= SampleIntervalMs) {
        samples_.append({image, camera_->frameCapturedMs(), now});
        if (samples_.size() > FrameCount)
            samples_.removeFirst();
        submitLatest();
    }
}

void VideoUnderstandingService::submitLatest()
{
    if (!active_ || !inferenceEnabled_ || reply_ || encoding_.isRunning() || samples_.size() != FrameCount ||
        samples_.last().elapsedMs <= submittedMs_)
        return;
    submittedMs_ = samples_.last().elapsedMs;
    pending_ = samples_;
    encodingGeneration_ = generation_;
    setStatus(events_.isEmpty() ? tr("正在加载模型并分析首个片段") : tr("正在分析最新片段"));
    encoding_.setFuture(QtConcurrent::run(&encoderPool_, [clip = pending_, model = model_,
                                                            generation = generation_, cache = encodedSamples_] {
        if (cache->generation != generation) {
            cache->images.clear();
            cache->generation = generation;
        }
        QJsonArray images, offsets;
        QHash<qint64,QByteArray> current;
        for (const auto &sample : clip) {
            auto encoded = cache->images.value(sample.elapsedMs);
            if (encoded.isEmpty()) {
                QByteArray jpeg;
                QBuffer output(&jpeg);
                if (!output.open(QIODevice::WriteOnly) ||
                    !sample.image.scaled(640, 640, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&output, "JPEG", 88))
                    return QByteArray();
                encoded = jpeg.toBase64();
            }
            images.append(QString::fromLatin1(encoded));
            current.insert(sample.elapsedMs,std::move(encoded));
            offsets.append(int(sample.elapsedMs - clip.first().elapsedMs));
        }
        cache->images = std::move(current);
        return QJsonDocument(QJsonObject{{"frames", images}, {"offsets", offsets}, {"model", model}}).toJson(QJsonDocument::Compact);
    }));
}

void VideoUnderstandingService::receive(QNetworkReply *reply, const QList<Sample> &clip)
{
    reply->deleteLater();
    if (!active_ || reply_ != reply)
        return;
    reply_.clear();
    const auto result = QJsonDocument::fromJson(reply->readAll()).object();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (code == 409 || code == 422) {
        setStatus(result["error"].toString());
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        inferenceEnabled_ = false;
        setStatus(result["error"].toString(tr("视频推理服务不可用：%1").arg(reply->errorString())));
        return;
    }
    const int first = result["first"].toInt() - 1, last = result["last"].toInt() - 1;
    const auto text = result["text"].toString();
    if (first < 0 || last < first || last >= clip.size() || text.isEmpty()) {
        setStatus(tr("该片段未返回有效事件"));
        return;
    }
    const auto timestamp = [](qint64 ms) { return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm:ss.zzz"); };
    delayMs_ = clock_.elapsed() - clip.last().elapsedMs;
    const bool event = result["event"].toBool();
    events_.append(QVariantMap{{"text", text}, {"event", event}, {"start", timestamp(clip[first].wallMs)},
        {"end", timestamp(clip[last].wallMs)}, {"clipStart", timestamp(clip.first().wallMs)},
        {"clipEnd", timestamp(clip.last().wallMs)}, {"delayMs", delayMs_}, {"inferenceMs", result["inference_ms"].toDouble()}});
    if (events_.size() > MaxResults)
        events_.removeFirst();
    emit eventAdded();
    setStatus(tr("正在跟踪现场变化"));
    submitLatest();
}
