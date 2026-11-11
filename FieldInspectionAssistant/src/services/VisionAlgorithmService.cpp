#include "VisionAlgorithmService.h"
#include "../app/SessionManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QVariantMap>

namespace {
struct Model {
    QString id;
    QString title;
    QString labels;
};

const Model Detection{QStringLiteral("yolov8"), QStringLiteral("YOLOv8"),
                      QStringLiteral("coco_80_labels_list.txt")};
const Model Classification{QStringLiteral("mobilenet_v2"), QStringLiteral("MobileNetV2"),
                           QStringLiteral("synset.txt")};

QDir modelDirectory(const Model &model)
{
    const auto fallback = QCoreApplication::applicationDirPath() + "/../vision-models";
    return QDir(QDir(qEnvironmentVariable("FIELDOPS_VISION_MODELS", fallback)).filePath(model.id));
}

bool ready(const QDir &dir, const Model &model)
{
    return QFileInfo(dir.filePath("runner")).isExecutable()
        && QFileInfo(dir.filePath("lib/librknn3_api.so")).isFile()
        && QFileInfo(dir.filePath("model/model.rknn")).size() > 0
        && QFileInfo(dir.filePath("model/model.weight")).size() > 0
        && QFileInfo(dir.filePath("model/" + model.labels)).size() > 0;
}

const QRegularExpression BoxPattern(
    R"(^(.+?) @ \((-?\d+) (-?\d+) (-?\d+) (-?\d+)\) (\d+(?:\.\d+)?)\s*$)",
    QRegularExpression::MultilineOption);
const QRegularExpression ClassPattern(
    R"(^\[\d+\] score=(\d+(?:\.\d+)?) class=(.+?)\s*$)", QRegularExpression::MultilineOption);
const QRegularExpression SynsetPrefix(R"(^n\d{8}\s+)");
}

VisionAlgorithmService::VisionAlgorithmService(SessionManager *session, QObject *parent)
    : QObject(parent), session_(session), status_(tr("请选择模型和图片。"))
{
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        interruption_ = tr("推理超时，请检查 RK1828 连接。");
        process_.kill();
    });
    connect(&process_, &QProcess::finished, this, &VisionAlgorithmService::finish);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            fail(tr("无法启动 RKNN3 推理程序。"));
    });
    connect(session_, &SessionManager::sessionEnding, this, [this] {
        cancel();
        input_.clear();
        result_.clear();
        rows_.clear();
        elapsedMs_ = 0;
        if (!busy_)
            runDirectory_.reset();
        emit changed();
    });
}

VisionAlgorithmService::~VisionAlgorithmService()
{
    if (process_.state() != QProcess::NotRunning) {
        process_.disconnect(this);
        process_.kill();
        process_.waitForFinished(1500);
    }
}

QVariantList VisionAlgorithmService::models() const
{
    QVariantList result;
    for (const auto &model : {Detection, Classification}) {
        const auto dir = modelDirectory(model);
        const auto sample = dir.absoluteFilePath("model/sample.jpg");
        result.append(QVariantMap{{"id", model.id}, {"title", model.title}, {"ready", ready(dir, model)},
            {"task", model.id == Detection.id ? tr("目标检测") : tr("图像分类")},
            {"sample", QFileInfo(sample).isFile() ? QUrl::fromLocalFile(sample) : QUrl()}});
    }
    return result;
}

void VisionAlgorithmService::refresh() { emit changed(); }

bool VisionAlgorithmService::run(const QString &id, const QUrl &file)
{
    if (busy_ || !session_->can("inspect"))
        return false;
    input_.clear();
    result_.clear();
    rows_.clear();
    elapsedMs_ = 0;
    runDirectory_.reset();
    if (id != Detection.id && id != Classification.id) {
        fail(tr("请选择已支持的视觉模型。"));
        return false;
    }
    const auto &model = id == Detection.id ? Detection : Classification;
    const auto bundle = modelDirectory(model);
    if (!ready(bundle, model)) {
        fail(tr("模型文件或 RKNN3 运行程序尚未安装。"));
        return false;
    }
    if (!file.isLocalFile() || !QFileInfo(file.toLocalFile()).isFile()) {
        fail(tr("请选择本地图片。"));
        return false;
    }
    QImageReader reader(file.toLocalFile());
    reader.setAutoTransform(true);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 60000000) {
        fail(tr("图片无法读取或分辨率过高。"));
        return false;
    }
    if (size.width() > 2048 || size.height() > 2048)
        reader.setScaledSize(size.scaled(QSize(2048, 2048), Qt::KeepAspectRatio));
    const auto image = reader.read();
    auto temporary = std::make_unique<QTemporaryDir>();
    const QDir work(temporary->path());
    const auto normalized = work.filePath("input.png");
    if (image.isNull() || !temporary->isValid() || !work.mkpath("model")
        || !image.convertToFormat(QImage::Format_RGB888).save(normalized)
        || !QFile::copy(bundle.filePath("model/" + model.labels), work.filePath("model/" + model.labels))) {
        fail(tr("无法准备图片或类别标签。"));
        return false;
    }
    runDirectory_ = std::move(temporary);
    model_ = id;
    input_ = QUrl::fromLocalFile(normalized);
    interruption_.clear();
    status_ = tr("RK1828 正在执行%1…").arg(model.id == Detection.id ? tr("目标检测") : tr("图像分类"));
    busy_ = true;
    auto env = QProcessEnvironment::systemEnvironment();
    const auto existing = env.value("LD_LIBRARY_PATH");
    env.insert("LD_LIBRARY_PATH", bundle.absoluteFilePath("lib") + (existing.isEmpty() ? "" : ":" + existing));
    process_.setProcessEnvironment(env);
    process_.setWorkingDirectory(work.absolutePath());
    process_.setProcessChannelMode(QProcess::MergedChannels);
    elapsed_.start();
    timeout_.start(75000);
    // These SDK bundles are single-core models. Keep core 0 available to the board's resident services.
    const auto coreMask = qEnvironmentVariable("FIELDOPS_VISION_CORE_MASK", QStringLiteral("0x2"));
    process_.start(bundle.absoluteFilePath("runner"), {bundle.absoluteFilePath("model/model.rknn"),
        bundle.absoluteFilePath("model/model.weight"), normalized, coreMask});
    emit changed();
    return true;
}

void VisionAlgorithmService::cancel()
{
    if (busy_) {
        interruption_ = tr("已停止视觉推理。");
        process_.kill();
    }
}

void VisionAlgorithmService::fail(const QString &message)
{
    timeout_.stop();
    busy_ = false;
    status_ = message;
    emit changed();
}

void VisionAlgorithmService::finish(int code, QProcess::ExitStatus exitStatus)
{
    const auto output = QString::fromUtf8(process_.readAll());
    if (!interruption_.isEmpty()) {
        if (input_.isEmpty())
            runDirectory_.reset();
        fail(interruption_);
        return;
    }
    if (code != 0 || exitStatus != QProcess::NormalExit) {
        fail(tr("RKNN3 推理失败，请检查 RK1828 运行状态。"));
        return;
    }
    const bool detection = model_ == Detection.id;
    auto matches = (detection ? BoxPattern : ClassPattern).globalMatch(output);
    QVariantList rows;
    while (matches.hasNext()) {
        const auto match = matches.next();
        const double score = match.captured(detection ? 6 : 1).toDouble();
        if (score < 0 || score > 1)
            continue;
        auto label = match.captured(detection ? 1 : 2);
        if (!detection)
            label.remove(SynsetPrefix);
        QVariantMap row{{"rank", rows.size() + 1}, {"label", label},
                        {"confidence", QString::number(score * 100, 'f', 1) + "%"}};
        if (detection)
            row.insert("position", QStringLiteral("(%1, %2) – (%3, %4)")
                .arg(match.captured(2), match.captured(3), match.captured(4), match.captured(5)));
        rows.append(row);
    }
    const auto overlay = runDirectory_->filePath("out.png");
    if ((detection && !QImageReader(overlay).canRead()) || (!detection && rows.size() != 5)) {
        const auto lines = output.split('\n', Qt::SkipEmptyParts);
        for (const auto &line : lines) {
            if (line.contains("fail", Qt::CaseInsensitive) || line.contains("error", Qt::CaseInsensitive)) {
                qWarning().noquote() << "RKNN3 vision:" << line.left(180);
                break;
            }
        }
        fail(tr("模型没有返回完整结果，请检查 RKNN3 模型和运行时版本。"));
        return;
    }
    timeout_.stop();
    elapsedMs_ = elapsed_.elapsed();
    rows_ = rows;
    result_ = detection ? QUrl::fromLocalFile(overlay) : input_;
    busy_ = false;
    status_ = detection ? tr("检测完成，找到 %1 个目标。").arg(rows_.size()) : tr("分类完成，显示前 5 项。");
    emit changed();
}
