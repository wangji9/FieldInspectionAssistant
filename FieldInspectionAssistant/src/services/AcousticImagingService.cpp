#include "AcousticImagingService.h"
#include "CameraService.h"
#include "EvidenceService.h"
#include "SignalAnalysisService.h"
#include "SpeakerRecognitionService.h"
#include "../app/SessionManager.h"

#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include <QTransform>
#include <QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace {
constexpr float StableDirection = .98480775f; // cos(10 degrees)
constexpr qint64 RefreshMs = 33;
constexpr qint64 AudioExpiryMs = 800;
constexpr qint64 LocalizationGapMs = 340;
constexpr double VoiceWindow = 3.3;
constexpr double VoiceInterval = 1.8;
constexpr double PhotoVerticalOffsetRatio = -.13;
const auto SourceId = QStringLiteral("ListenGo_raw_six");
const auto SettingsKey = QStringLiteral("acousticImaging/parameters");

bool closeDirection(const QVector3D &a, const QVector3D &b)
{
    return !a.isNull() && !b.isNull() && QVector3D::dotProduct(a,b)>=StableDirection;
}

QString stateLabel(const QString &state)
{
    if (state=="below_gate") return QObject::tr("低于电平门限");
    if (state=="duplicate_channels") return QObject::tr("六路采样重复，无法定位");
    if (state=="diffuse") return QObject::tr("空间响应未形成稳定峰值");
    if (state=="target_absent") return QObject::tr("目标频率没有足够信号");
    if (state=="ambiguous") return QObject::tr("目标频率存在多个相近峰值");
    return state=="localized" ? QObject::tr("声源方向已更新") : QObject::tr("等待六路采样");
}

}

AcousticImagingService::AcousticImagingService(SignalAnalysisService *audio, CameraService *camera,
                                             EvidenceService *evidence, SpeakerRecognitionService *speakers,
                                             SessionManager *session, QObject *parent)
    : QObject(parent), audio_(audio), camera_(camera), evidence_(evidence), speakers_(speakers), session_(session),
      engine_(std::make_shared<AcousticImagingEngine>())
{
    adopt({});
    adopt(QSettings().value(SettingsKey).toMap());
    status_=tr("待开始");
    pool_.setMaxThreadCount(1);
    timer_.setInterval(RefreshMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &AcousticImagingService::sample);
    connect(&watcher_, &QFutureWatcher<Analysis>::finished, this, &AcousticImagingService::receive);
    connect(session_, &SessionManager::sessionEnding, this, &AcousticImagingService::leave);
    connect(audio_, &SignalAnalysisService::changed, this, &AcousticImagingService::changed);
    connect(speakers_, &SpeakerRecognitionService::beamClassified, this, [this](quint64 request, const QVariantMap &match) {
        if (!running_ || request!=beamRequest_ || !closeDirection(direction_,voiceDirection_))
            return;
        match_=match;
        updateSpeaker();
        emit changed();
    });
    connect(camera_, &CameraService::closed, this, [this] {
        ownsCamera_ = false;
        keepPreview_ = false;
        observation_["aligned"]=false;
        emit changed();
    });
}

AcousticImagingService::~AcousticImagingService()
{
    leave();
    pool_.waitForDone();
}

void AcousticImagingService::refresh()
{
    if (!session_->can("inspect"))
        return;
    cameras_=CameraService::availableDevices();
    emit changed();
}

bool AcousticImagingService::arrayAvailable() const
{
    if (QFileInfo::exists("/dev/lg_speech_serial") && QFileInfo::exists("/dev/lg_speech_uac"))
        return true;
    for (const auto &entry : audio_->devices()) {
        if (entry.toMap().value("id") == SourceId)
            return true;
    }
    return false;
}

QVariantMap AcousticImagingService::plotForDisplay(const QString &kind, int channel) const
{
    const bool frequency = kind == "frequency";
    auto plot = frequency && config_.timeDomain ? QVariantMap{} : plots_.value(kind).toMap();
    if (plot.isEmpty()) {
        const auto rate = AcousticImagingEngine::SampleRate;
        plot = frequency
            ? QVariantMap{{"xLabel", tr("频率 (Hz)")}, {"yLabel", tr("幅值 (dBFS)")},
                {"xRange", QVariantList{0, rate / 2.}}, {"yRange", QVariantList{-115, 0}}}
            : QVariantMap{{"xLabel", tr("采样点")}, {"yLabel", tr("幅值")},
                {"xRange", QVariantList{0, AcousticImagingEngine::TimeWindow - 1}}, {"yRange", QVariantList{-1000, 1000}}};
    }
    plot["title"] = frequency ? (config_.timeDomain ? tr("频谱图（时域模式下禁用）") : tr("频谱图")) : tr("时域信号");
    const auto series = plot.value("series").toList();
    if (channel >= 0 && channel < series.size()) {
        auto trace = series.at(channel).toMap();
        trace["color"] = frequency ? "#71e3ff" : "#7cf441";
        plot["series"] = QVariantList{trace};
        plot["title"] = plot["title"].toString() + tr("（通道 %1）").arg(channel + 1);
    } else if (!series.isEmpty()) {
        static const QStringList colors{"#6583f1", "#4d64be", "#8095d9", "#6258b4", "#3e82cc", "#8999fc"};
        QVariantList overview;
        for (int i = 0; i < series.size(); ++i) {
            auto trace = series.at(i).toMap();
            trace["color"] = colors.at(i % colors.size());
            overview.append(trace);
        }
        overview.append(QVariantMap{{"native_points", plot.take("average")}, {"color", "#ff3434"}});
        plot["series"] = overview;
    }
    return plot;
}

bool AcousticImagingService::openPreview(const QString &cameraPath)
{
    if (!session_->can("inspect") || cameraPath.isEmpty() || camera_->isOpen() || camera_->opening() || camera_->closing())
        return false;
    ownsCamera_ = camera_->openNativePreview(cameraPath);
    keepPreview_ = ownsCamera_;
    emit changed();
    return ownsCamera_;
}

void AcousticImagingService::closePreview()
{
    if (!ownsCamera_)
        return;
    ownsCamera_ = false;
    keepPreview_ = false;
    evidence_->closeCamera();
    emit changed();
}

void AcousticImagingService::leave()
{
    stop();
    closePreview();
}

bool AcousticImagingService::reject(const QString &message)
{
    status_=message;
    emit changed();
    return false;
}

bool AcousticImagingService::adopt(const QVariantMap &values)
{
    AcousticArrayConfig next;
    bool valid=true;
    const auto number=[&](const char *key,double fallback,double low,double high) {
        bool ok=false;
        const auto v=values.value(key,fallback).toDouble(&ok);
        valid &= ok && std::isfinite(v) && v>=low && v<=high;
        return v;
    };
    next.radiusMm=number("radius_mm",next.radiusMm,8,180);
    next.channelOneDegrees=number("channel_one_deg",next.channelOneDegrees,-180,180);
    next.clockwise=values.value("clockwise",next.clockwise).toBool();
    next.horizontalFov=number("horizontal_fov",next.horizontalFov,15,140);
    next.verticalFov=number("vertical_fov",next.verticalFov,10,125);
    next.yaw=number("yaw",next.yaw,-180,180);
    next.pitch=number("pitch",next.pitch,-180,180);
    next.roll=number("roll",next.roll,-180,180);
    next.fixedMount = values.value("fixed_mount", false).toBool();
    const auto cameraX = number("camera_x_mm", 0, -250, 250);
    const auto cameraY = number("camera_y_mm", std::round(PhotoVerticalOffsetRatio * next.radiusMm * 10) / 10, -250, 250);
    const auto cameraZ = number("camera_z_mm", 0, -250, 250);
    next.cameraOriginMm = QVector3D(cameraX, cameraY, cameraZ);
    next.focusDepthM = number("focus_depth_m", next.focusDepthM, .15, 20.);
    next.temperature=number("temperature",next.temperature,-25,60);
    next.lowerHz=int(number("lower_hz",next.lowerHz,80,5500));
    next.upperHz=int(number("upper_hz",next.upperHz,200,7400));
    const auto targetHz = int(number("target_hz", (next.lowerHz + next.upperHz) / 2,
        AcousticImagingEngine::MinimumTargetHz, AcousticImagingEngine::MaximumTargetHz));
    const auto algorithm = int(number("algorithm", values.value("time_domain").toBool() ? 1 : 0, 0, 3));
    next.timeDomain = algorithm == 1;
    next.mvdr = algorithm == 3;
    next.targetHz = algorithm == 2 ? 0 : targetHz;
    next.dynamicRangeDb = number("dynamic_range_db", next.dynamicRangeDb, .1, 30.);
    next.gateDbfs=number("gate_dbfs",next.gateDbfs,-100,-12);
    next.smoothing=number("smoothing",next.smoothing,0,.95);
    const auto opacity=number("opacity",.56,.08,.95);
    const auto threshold=number("speaker_threshold",.77,.45,.99);
    const auto order=values.value("channel_order","1,2,3,4,5,6").toString().split(',');
    std::array<bool,6> used{};
    if (!valid || order.size()!=6 || next.upperHz-next.lowerHz<100)
        return false;
    for (int i=0;i<6;++i) {
        bool ok=false;
        const auto channel=order[i].trimmed().toInt(&ok)-1;
        if (!ok || channel<0 || channel>=6 || used[channel])
            return false;
        used[channel]=true;
        next.channelOrder[i]=channel;
    }
    QVariantList positions;
    QStringList orderText;
    for (int i=0;i<6;++i) {
        const auto angle=(next.channelOneDegrees+(next.clockwise?-60.:60.)*i)*3.141592653589793/180.0;
        positions.append(QVariantMap{{"channel",next.channelOrder[i]+1},{"x",std::cos(angle)},{"y",std::sin(angle)}});
        orderText.append(QString::number(next.channelOrder[i]+1));
    }
    config_=next;
    parameters_={{"radius_mm",next.radiusMm},{"channel_one_deg",next.channelOneDegrees},{"clockwise",next.clockwise},
        {"channel_order",orderText.join(',')},{"horizontal_fov",next.horizontalFov},{"vertical_fov",next.verticalFov},
        {"yaw",next.yaw},{"pitch",next.pitch},{"roll",next.roll},{"temperature",next.temperature},
        {"fixed_mount",next.fixedMount},{"camera_x_mm",cameraX},
        {"camera_y_mm",cameraY},{"camera_z_mm",cameraZ},
        {"focus_depth_m",next.focusDepthM},{"photo_vertical_offset_ratio",PhotoVerticalOffsetRatio},
        {"lower_hz",next.lowerHz},{"upper_hz",next.upperHz},{"target_hz",targetHz},
        {"algorithm",algorithm},{"time_domain",next.timeDomain},{"target_frequency_enabled",algorithm == 0 || algorithm == 3},
        {"dynamic_range_db",next.dynamicRangeDb},
        {"target_min_hz",AcousticImagingEngine::MinimumTargetHz},{"target_max_hz",AcousticImagingEngine::MaximumTargetHz},
        {"gate_dbfs",next.gateDbfs},{"smoothing",next.smoothing},
        {"opacity",opacity},{"speaker_threshold",threshold},{"speaker",values.value("speaker",true).toBool()},
        {"calibrated",values.value("calibrated",false).toBool()},{"positions",positions}};
    return true;
}

bool AcousticImagingService::configure(const QVariantMap &values)
{
    if (!session_->can("inspect"))
        return false;
    auto merged=parameters_;
    for (auto it=values.cbegin();it!=values.cend();++it)
        merged.insert(it.key(),it.value());
    if (values.contains("time_domain") && !values.contains("algorithm"))
        merged["algorithm"] = values.value("time_domain").toBool() ? 1 : 0;
    const auto previous = parameters_;
    if (!adopt(merged))
        return reject(tr("请检查频带、阵列尺寸和通道顺序。通道 1–6 每个只能出现一次。"));
    QSettings().setValue(SettingsKey,parameters_);
    bool displayOnly = true;
    for (auto it = parameters_.cbegin(); it != parameters_.cend(); ++it) {
        if (it.value() != previous.value(it.key()) && it.key() != "dynamic_range_db" && it.key() != "opacity")
            displayOnly = false;
    }
    if (!displayOnly) {
        ++revision_;
        clearObservation(!running_, !running_);
        if (previous.value("time_domain") != parameters_.value("time_domain"))
            emit plotsChanged();
        status_ = running_ ? tr("参数已更新，重新定位") : tr("参数已保存");
    }
    emit parametersChanged();
    emit changed();
    return true;
}

bool AcousticImagingService::start(const QString &cameraPath)
{
    if (running_ || !session_->can("inspect"))
        return false;
    if (!audio_->running()) {
        if (!arrayAvailable()) {
            refresh();
            audio_->refreshDevices();
            return reject(tr("未发现六麦原始输入。请连接控制口和数据口后刷新。"));
        }
        audio_->startAcousticCapture(SourceId);
        if (!audio_->running())
            return reject(audio_->message());
        ownsAudio_=true;
    } else if (audio_->sixChannelWindow(.128).captureId.isEmpty()) {
        return reject(tr("当前采集源不是六麦原始输入。请停止后切换。"));
    }
    if (!cameraPath.isEmpty() && !camera_->isOpen() && !camera_->opening() && !camera_->closing())
        ownsCamera_=camera_->openNativePreview(cameraPath);
    ++revision_;
    captureId_.clear();
    lastFrame_=0;
    lastReceived_=0;
    clearObservation();
    running_=true;
    status_=tr("等待六路同步采样");
    timer_.start();
    emit changed();
    return true;
}

void AcousticImagingService::stop()
{
    if (!running_)
        return;
    running_=false;
    timer_.stop();
    ++revision_;
    clearObservation();
    if (ownsAudio_ && audio_->running())
        audio_->stop();
    if (!keepPreview_)
        closePreview();
    ownsAudio_=false;
    status_=tr("已停止");
    emit changed();
}

void AcousticImagingService::clearObservation(bool clearPlots, bool clearHeatmap)
{
    if (clearPlots && !plots_.isEmpty()) {
        plots_.clear();
        emit plotsChanged();
    }
    observation_.clear();
    if (clearHeatmap && hasHeatmap()) {
        heatmap_ = {};
        emit heatmapChanged();
    }
    match_.clear();
    direction_={};
    stableDirection_={};
    voiceDirection_={};
    stableSince_=0;
    lastLocalized_=0;
    lastVoice_=0;
    voiceCaptured_=0;
    ++beamRequest_;
}

void AcousticImagingService::sample()
{
    if (!audio_->running()) {
        const auto status=audio_->message();
        stop();
        reject(status.isEmpty() ? tr("六麦采集已结束") : status);
        return;
    }
    if (computing_)
        return;
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if (lastReceived_ && now-lastReceived_>AudioExpiryMs && !observation_.isEmpty()) {
        ++revision_;
        clearObservation();
        reject(tr("六麦数据已中断，等待新的采样"));
    }
    const bool speaker=parameters_.value("speaker").toBool();
    const auto rate=AcousticImagingEngine::SampleRate;
    const bool requestVoice=speaker && stableSince_ && lastFrame_>=stableSince_+quint64(rate*VoiceWindow) &&
        lastFrame_>=lastVoice_+quint64(rate*VoiceInterval) && speakers_->canClassifyBeam();
    auto window=audio_->sixChannelWindow(requestVoice ? VoiceWindow : .128);
    if (window.pcm.size()<AcousticImagingEngine::Window*12 || window.receivedMs<=0 || now-window.receivedMs>AudioExpiryMs)
        return;
    if (window.captureId!=captureId_) {
        captureId_=window.captureId;
        lastFrame_=0;
        ++revision_;
        clearObservation();
    }
    if (window.endFrame==lastFrame_)
        return;
    lastFrame_=window.endFrame;
    lastReceived_=window.receivedMs;
    const auto revision=revision_;
    const bool configure=engineRevision_!=revision;
    engineRevision_=revision;
    computing_=true;
    watcher_.setFuture(QtConcurrent::run(&pool_,[engine=engine_,window=std::move(window),revision,configure,
                                                config=config_,requestVoice,last=stableDirection_] {
        Analysis result;
        result.revision=revision;
        result.endFrame=window.endFrame;
        result.receivedMs=window.receivedMs;
        result.rate=window.sampleRate;
        if (configure)
            engine->configure(config,window.sampleRate);
        else
            engine->setDynamicRange(config.dynamicRangeDb);
        result.frame=engine->analyze(window.pcm, true);
        if (requestVoice && result.frame.valid && closeDirection(last,result.frame.arrayDirection)) {
            result.beam=engine->beamform(window.pcm,result.frame.arrayDirection,result.frame.arraySourceM);
            result.voiceEndMs=qint64(window.endFrame)*1000/window.sampleRate;
            result.voiceStartMs=result.voiceEndMs-qint64(window.pcm.size()/12)*1000/window.sampleRate;
        }
        return result;
    }));
}

void AcousticImagingService::receive()
{
    computing_=false;
    const auto result=watcher_.result();
    if (!running_ || result.revision!=revision_)
        return;
    const auto &frame=result.frame;
    if (!frame.plots.isEmpty()) {
        plots_ = frame.plots;
        emit plotsChanged();
    }
    observation_=frame.values;
    status_=stateLabel(frame.values.value("state").toString());
    const auto samplesBack = frame.values.value("audio_offset_samples", AcousticImagingEngine::Window / 2).toInt();
    const auto midpoint=result.receivedMs-qint64(samplesBack)*1000/result.rate;
    observation_["timestamp"]=QDateTime::fromMSecsSinceEpoch(midpoint,Qt::UTC).toString(Qt::ISODateWithMs);
    observation_["audio_frame"]=QVariant::fromValue(result.endFrame);
    observation_["audio_ms"]=qint64(result.endFrame)*1000/result.rate;
    observation_["audio_midpoint_ms"] = midpoint;
    const auto cameraTime = camera_->frameCapturedMs();
    if (cameraTime > 0)
        observation_["skew_ms"] = cameraTime - midpoint;
    if (!frame.heatmap.isNull()) {
        auto heatmap = frame.heatmap;
        if (camera_->hasFrame() && camera_->frameMirrored())
            heatmap = heatmap.mirrored(true, false);
        if (camera_->hasFrame() && camera_->frameRotation())
            heatmap = heatmap.transformed(QTransform().rotate(camera_->frameRotation()));
        heatmap_ = std::move(heatmap);
        emit heatmapChanged();
    }
    const bool gap=result.receivedMs-lastLocalized_>LocalizationGapMs;
    if (gap || (frame.valid && !closeDirection(frame.arrayDirection,stableDirection_))) {
        stableSince_=frame.valid ? result.endFrame : 0;
        stableDirection_=frame.arrayDirection;
        match_.clear();
        voiceDirection_={};
        ++beamRequest_;
    }
    if (frame.valid) {
        direction_=frame.arrayDirection;
        lastLocalized_=result.receivedMs;
        observation_["array_x"]=direction_.x();
        observation_["array_y"]=direction_.y();
        if (!result.beam.isEmpty() && speakers_->classifyBeam(result.beam,
            parameters_.value("speaker_threshold").toDouble(),++beamRequest_)) {
            lastVoice_=result.endFrame;
            voiceDirection_=direction_;
            voiceCaptured_=result.receivedMs;
            voiceFrom_=result.voiceStartMs;
            voiceTo_=result.voiceEndMs;
        }
    }
    updateSpeaker();
    emit changed();
}

void AcousticImagingService::updateSpeaker()
{
    observation_.remove("speaker_name");
    observation_.remove("speaker_score");
    observation_.remove("speaker_state");
    observation_.remove("speaker_from_ms");
    observation_.remove("speaker_to_ms");
    const auto now=QDateTime::currentMSecsSinceEpoch();
    if (observation_.value("state")!="localized" || match_.isEmpty() || now-voiceCaptured_>2500 ||
        !closeDirection(direction_,voiceDirection_))
        return;
    observation_["speaker_state"]=match_.value("status");
    if (match_.value("state")=="matched") {
        observation_["speaker_name"]=match_.value("speaker");
        observation_["speaker_score"]=match_.value("score");
        observation_["speaker_from_ms"]=voiceFrom_;
        observation_["speaker_to_ms"]=voiceTo_;
    }
}
