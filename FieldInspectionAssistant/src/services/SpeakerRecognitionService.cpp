#include "SpeakerRecognitionService.h"
#include "NativeSignalPipeline.h"
#include "../app/SessionManager.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QtConcurrentRun>
#include <QtEndian>
#include <alsa/asoundlib.h>
#include <fvad.h>
#include <opencv2/core.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <limits>
#include <optional>
#include <utility>

namespace {
constexpr int Rate = 16000;
constexpr int Dimension = 192;
constexpr int VadFrame = Rate * 30 / 1000;
constexpr int HandoffSilence = 100;
constexpr int Window = Rate * 6;
constexpr int MinVoicedFrames = 45;
constexpr int MaxSamples = 3;
constexpr int MaxProfiles = 250;
constexpr int SmartMinVoicedFrames = 12;
constexpr int SmartEnrollVoicedFrames = 40;
constexpr int SmartPauseFrames = 25;
constexpr int SmartWindowFrames = 250;
constexpr double SmartMatchThreshold = .60;
constexpr double SmartNoveltyThreshold = .45;
constexpr double SmartAdaptThreshold = .74;
constexpr double SmartAdaptMargin = .12;
constexpr int LiveWindowBytes = 2 * Rate * 24 / 10;
constexpr int CaptureFrames = Rate * 80 / 1000;
constexpr int ArrayChannels = 6;
constexpr int AsrChunkSamples = 24 * Rate;
constexpr auto AsrName = "Qwen3-ASR-0.6B";
constexpr int WaveformBins = 640;
constexpr int LiveRecognitionSeconds = 120;
constexpr int LiveMeetingSeconds = 900;
constexpr int LiveSpectrumStep = 1280;
constexpr int MeetingMinimumSamples = 25 * Rate;
constexpr int LiveEnrollmentSeconds = 30;
constexpr float MinimumMargin = 0.05f;
const auto ModelName = QStringLiteral("3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx");
using Embedding = std::array<float, Dimension>;
struct SmartWindow { int first = 0, last = 0, voiced = 0; };

// Speaker extraction ABI from sherpa-onnx c-api.h v2.0.1 (Apache-2.0).
// Resolve these symbols at runtime so other application features can start without the model.
struct SherpaExtractor;
struct SherpaStream;
struct SherpaConfig {
    const char *model;
    qint32 num_threads;
    qint32 debug;
    const char *provider;
};
struct Diarizer;
struct DiarizationResult;
struct DiarizationSegment { float start, end; qint32 speaker; float confidence; };
struct DiarizationConfig {
    struct {
        struct { const char *model; float shift; } pyannote;
        qint32 threads, debug;
        const char *provider;
    } segmentation;
    SherpaConfig embedding;
    struct { qint32 clusters; float distance; qint32 confidence; } clustering;
    float minimumSpeech, minimumGap;
};
constexpr auto DiarizationModel = "models/pyannote-segmentation-3.0.int8.onnx";
constexpr auto PreciseDiarizationModel = "models/pyannote-segmentation-3.0.onnx";
constexpr auto DiarizationEmbeddingModel = "models/3dspeaker_speech_campplus_sv_zh_en_16k-common_advanced.onnx";
constexpr auto MeetingVerificationModel = "models/3dspeaker_speech_eres2netv2_sv_zh-cn_16k-common.onnx";
constexpr float PreciseClusteringDistance = .68f;
constexpr float TrackMergeSimilarity = .45f;
constexpr int ShortTrackSamples = 15*Rate;

struct SpeechTrack { int first, last, speaker; };

QByteArray highPass(const QByteArray &pcm)
{
    QByteArray filtered(pcm.size(), Qt::Uninitialized);
    float previous = 0, state = 0;
    for (qsizetype i = 0; i + 1 < pcm.size(); i += 2) {
        const float sample = qFromLittleEndian<qint16>(pcm.constData()+i);
        state = .9615f*(state + sample - previous);
        previous = sample;
        qToLittleEndian<qint16>(qint16(std::clamp(state,-32768.f,32767.f)), filtered.data()+i);
    }
    return filtered;
}

QByteArray suppressStationaryNoise(const QByteArray &pcm)
{
    constexpr int size = 512, hop = 256;
    const int length = int(pcm.size()/2);
    if (length < Rate)
        return pcm;
    const int frames = (length-size)/hop+1;
    std::vector<std::pair<float,int>> quiet;
    quiet.reserve(frames);
    for (int frame=0; frame<frames; ++frame) {
        float power = 0;
        for (int i=0; i<size; ++i) {
            const float x = qFromLittleEndian<qint16>(pcm.constData()+2*(frame*hop+i));
            power += x*x;
        }
        quiet.emplace_back(power/size,frame);
    }
    const int count = std::max(1,frames/8);
    std::partial_sort(quiet.begin(),quiet.begin()+count,quiet.end());
    if (quiet[count-1].first < 100.f*100.f)
        return pcm;
    // Estimate a stationary noise spectrum from the quietest 12.5% of frames.
    // A gain floor limits voice distortion when the estimate includes soft speech.
    std::array<float,size> window{}, noise{};
    for (int i=0; i<size; ++i)
        window[i] = std::sqrt(.5f-.5f*std::cos(6.2831853f*(i+.5f)/size));
    cv::Mat input(1,size,CV_32F), spectrum, output;
    const auto transform = [&](int frame) {
        for (int i=0; i<size; ++i)
            input.ptr<float>()[i] = window[i]*qFromLittleEndian<qint16>(pcm.constData()+2*(frame*hop+i));
        cv::dft(input,spectrum,cv::DFT_COMPLEX_OUTPUT);
        return spectrum.ptr<cv::Vec2f>();
    };
    for (int i=0; i<count; ++i) {
        const auto *bins = transform(quiet[i].second);
        for (int j=0; j<size; ++j)
            noise[j] += bins[j].dot(bins[j])/count;
    }
    QVector<float> clean(length,0.f), weights(length,0.f);
    for (int frame=0; frame<frames; ++frame) {
        auto *bins = transform(frame);
        for (int j=0; j<size; ++j) {
            const float power = bins[j].dot(bins[j]);
            bins[j] *= std::sqrt(std::clamp(1.f-1.2f*noise[j]/std::max(1.f,power),.16f,1.f));
        }
        cv::dft(spectrum,output,cv::DFT_INVERSE|cv::DFT_REAL_OUTPUT|cv::DFT_SCALE);
        for (int i=0; i<size; ++i) {
            clean[frame*hop+i] += output.ptr<float>()[i]*window[i];
            weights[frame*hop+i] += window[i]*window[i];
        }
    }
    QByteArray result = pcm;
    char *data = result.data();
    for (int i=0; i<length; ++i)
        if (weights[i] >= .5f)
            qToLittleEndian<qint16>(qint16(std::clamp(clean[i]/weights[i],-32768.f,32767.f)),data+2*i);
    return result;
}

QVariantMap error(const QString &message) { return {{"error", message}}; }

QString sourcePath(const QUrl &url)
{
    const QFileInfo file(url.toLocalFile());
    return url.isLocalFile() && file.isFile() && file.isReadable() ? file.canonicalFilePath() : QString();
}

bool validRange(double start, double duration, double maximum)
{
    return std::isfinite(start) && std::isfinite(duration) &&
        start >= 0 && start <= 86400 && duration >= 1 && duration <= maximum;
}

bool validLanguage(const QString &language)
{
    static const QRegularExpression code(QStringLiteral("^[a-z]{2,3}$"));
    return language.isEmpty() || code.match(language).hasMatch();
}

bool validProfile(const QString &name, const QString &language)
{
    return !name.trimmed().isEmpty() && name.trimmed().size() <= 80 && validLanguage(language);
}

bool normalize(Embedding &data)
{
    const float norm = std::sqrt(std::inner_product(data.begin(), data.end(), data.begin(), 0.0f));
    if (!std::isfinite(norm) || norm < 1e-8f)
        return false;
    for (auto &element : data)
        element /= norm;
    return true;
}

QByteArray runProcess(const QString &exe, const QStringList &args, const std::atomic_bool &cancelled, int timeout)
{
    QProcess process;
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(exe, args, QIODevice::ReadOnly);
    if (!process.waitForStarted(3000))
        return {};
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning && !cancelled && elapsed.elapsed() < timeout)
        process.waitForFinished(150);
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(1000);
        return {};
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
        ? process.readAllStandardOutput() : QByteArray();
}

QByteArray decode(const QString &path, double start, double length, const std::atomic_bool &cancelled)
{
    return runProcess("ffmpeg", {"-v","error","-nostdin","-threads","1","-ss",QString::number(start,'f',3),
        "-protocol_whitelist","file,pipe","-i",path,"-t",QString::number(length,'f',3),"-map","0:a:0",
        "-ac","1","-ar","16000","-c:a","pcm_s16le","-f","s16le","pipe:1"}, cancelled, 90000);
}

struct VoiceRegion { int first; int last; };
struct SpeechActivity {
    QList<VoiceRegion> regions;
    QVector<int> voicedCount;
    int frames(int first, int last) const {
        return voicedCount.value(last / VadFrame) - voicedCount.value(first / VadFrame);
    }
};

SpeechActivity detectSpeech(const QByteArray &pcm, const std::atomic_bool &cancelled)
{
    SpeechActivity result;
    std::unique_ptr<Fvad, decltype(&fvad_free)> vad(fvad_new(), &fvad_free);
    if (!vad || fvad_set_sample_rate(vad.get(), Rate) || fvad_set_mode(vad.get(), 2))
        return result;
    const int count = int(pcm.size()/2) / VadFrame;
    result.voicedCount.reserve(count + 1);
    result.voicedCount.append(0);
    int first = -1, latest = -1;
    std::array<qint16, VadFrame> samples{};
    for (int i = 0; i < count && !cancelled; ++i) {
        const auto *raw = pcm.constData() + 2 * i * VadFrame;
        qint64 squares = 0;
        for (int j = 0; j < VadFrame; ++j) {
            samples[j] = qFromLittleEndian<qint16>(raw + 2*j);
            squares += qint64(samples[j]) * samples[j];
        }
        const bool speech = fvad_process(vad.get(), samples.data(), samples.size()) > 0
            && squares > qint64(180)*180*VadFrame;
        result.voicedCount.append(result.voicedCount.last() + int(speech));
        if (!speech)
            continue;
        if (latest >= 0 && i - latest > HandoffSilence) {
            result.regions.append({first * VadFrame,(latest + 1) * VadFrame});
            first = -1;
        }
        if (first < 0)
            first = i;
        latest = i;
    }
    if (first >= 0)
        result.regions.append({first * VadFrame,(latest + 1) * VadFrame});
    return result;
}

QVariantList waveformEnvelope(const QByteArray &pcm)
{
    const int samples = int(pcm.size()/2);
    const int bins = qMin(WaveformBins, samples);
    QVariantList result;
    result.reserve(bins);
    for (int i = 0; i < bins; ++i) {
        const int first = int(qint64(i)*samples/bins);
        const int last = int(qint64(i+1)*samples/bins);
        qint16 low = 0, high = 0;
        for (int sample = first; sample < last; ++sample) {
            const auto amplitude = qFromLittleEndian<qint16>(pcm.constData()+2*sample);
            low = qMin(low,amplitude);
            high = qMax(high,amplitude);
        }
        result.append(QVariant(QVariantList{low/32768.0,high/32768.0}));
    }
    return result;
}

QString captureMicrophone(int seconds, const std::function<void(QByteArray)> &deliver)
{
    const char *device = snd_card_get_index("L6Microphone") >= 0 ? "plughw:CARD=L6Microphone,DEV=0" : "default";
    snd_pcm_t *handle = nullptr;
    int result = snd_pcm_open(&handle, device, SND_PCM_STREAM_CAPTURE, SND_PCM_NONBLOCK);
    if (result < 0)
        return QObject::tr("无法打开麦克风：%1").arg(QString::fromLocal8Bit(snd_strerror(result)));
    const std::unique_ptr<snd_pcm_t, decltype(&snd_pcm_close)> pcm(handle, &snd_pcm_close);
    result = snd_pcm_set_params(pcm.get(), SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                                1, Rate, 1, 60000);
    if (result < 0)
        return QObject::tr("麦克风无法提供 16 kHz 单声道：%1").arg(QString::fromLocal8Bit(snd_strerror(result)));

    QElapsedTimer lastRead;
    lastRead.start();
    std::array<char, CaptureFrames * 2> block{};
    int frames = 0;
    int remaining = seconds * Rate;
    while (remaining > 0 && !QThread::currentThread()->isInterruptionRequested()) {
        const auto count = snd_pcm_readi(pcm.get(), block.data() + 2 * frames, CaptureFrames - frames);
        if (count > 0) {
            lastRead.restart();
            frames += int(count);
            if (frames == CaptureFrames) {
                deliver(QByteArray(block.data(), block.size()));
                remaining -= CaptureFrames;
                frames = 0;
            }
            continue;
        }
        if (count == -EAGAIN || count == 0)
            result = snd_pcm_wait(pcm.get(), 50);
        else
            result = snd_pcm_recover(pcm.get(), int(count), 1);
        if (result < 0)
            return QObject::tr("麦克风采集已中断：%1").arg(QString::fromLocal8Bit(snd_strerror(result)));
        if (lastRead.elapsed() > 3000)
            return QObject::tr("麦克风 3 秒内未返回音频，请检查连接。");
    }
    return {};
}

class SixMicVoice
{
public:
    explicit SixMicVoice(int seconds) : blocksLeft_(seconds * Rate / CaptureFrames) {}

    bool consume(const QByteArray &pcm, const std::function<void(QByteArray)> &deliver)
    {
        pending_.append(pcm);
        constexpr int blockBytes = CaptureFrames * ArrayChannels * 2;
        int offset = 0;
        while (blocksLeft_ && pending_.size() - offset >= blockBytes) {
            deliver(monoBlock(pending_.constData() + offset));
            offset += blockBytes;
            --blocksLeft_;
        }
        pending_.remove(0, offset);
        return blocksLeft_ > 0;
    }

private:
    QByteArray monoBlock(const char *raw)
    {
        std::array<float,ArrayChannels> energy{};
        std::array<qint16,CaptureFrames*ArrayChannels> samples{};
        for (int i=0; i<CaptureFrames; ++i) {
            for (int channel=0; channel<ArrayChannels; ++channel) {
                const int index = i*ArrayChannels+channel;
                const float value = qFromLittleEndian<qint16>(raw+2*index);
                // Exclude DC and low-frequency rumble when comparing microphones.
                filtered_[channel] = .9615f*(filtered_[channel]+value-previous_[channel]);
                previous_[channel] = value;
                const float clean = std::clamp(filtered_[channel],-32768.f,32767.f);
                samples[index] = qint16(clean);
                energy[channel] += clean*clean;
            }
        }
        int best = selected_;
        for (int channel=0; channel<ArrayChannels; ++channel) {
            const float rms = std::sqrt(energy[channel]/CaptureFrames);
            energy[channel] = rms;
            const float rate = rms < noise_[channel] ? .2f : .004f;
            noise_[channel] = qMax(24.f,noise_[channel]+rate*(rms-noise_[channel]));
        }
        const auto score = [&](int channel) { return energy[channel]/noise_[channel]; };
        for (int channel=0; channel<ArrayChannels; ++channel)
            if (score(channel)>score(best))
                best = channel;
        if (score(best) < score(selected_)*1.25f || energy[best] < 65.f)
            best = selected_;
        QByteArray mono(CaptureFrames*2,Qt::Uninitialized);
        constexpr int fadeFrames = Rate/100;
        for (int i=0; i<CaptureFrames; ++i) {
            const int index = i*ArrayChannels;
            const float mix = qMin(i,fadeFrames)/float(fadeFrames);
            const auto value = qint16((1.f-mix)*samples[index+selected_]+mix*samples[index+best]);
            qToLittleEndian<qint16>(value,mono.data()+2*i);
        }
        selected_ = best;
        return mono;
    }

    QByteArray pending_;
    std::array<float,ArrayChannels> previous_{}, filtered_{};
    std::array<float,ArrayChannels> noise_{36,36,36,36,36,36};
    int selected_ = 0, blocksLeft_;
};

QString timeLabel(qint64 ms)
{
    return QStringLiteral("%1:%2:%3.%4").arg(ms/3600000,2,10,QLatin1Char('0'))
        .arg(ms/60000%60,2,10,QLatin1Char('0')).arg(ms/1000%60,2,10,QLatin1Char('0'))
        .arg(ms%1000,3,10,QLatin1Char('0'));
}

class BoardAsr
{
public:
    explicit BoardAsr(const std::atomic_bool &cancelled) : cancelled_(cancelled)
    {
        network_.setProxy(QNetworkProxy::NoProxy);
    }

    QString transcribe(const QByteArray &pcm, int first, int last)
    {
        if (cancelled_ || unavailable_)
            return {};
        QTemporaryDir directory;
        if (!directory.isValid())
            return {};
        const auto path = directory.filePath("segment.wav");
        if (!writePcmWav(path,pcm.mid(first*2,(last-first)*2),Rate,1))
            return {};
        const QJsonObject job{{"mode","asr"},{"model","qwen3_asr"},{"path",path},{"max_tokens",512},
                              {"online",false},{"realtime_audio",true}};
        QElapsedTimer elapsed;
        elapsed.start();
        QJsonObject current;
        int status = 0;
        do {
            current = request("/api/jobs",job,3000,&status,false);
            if (status != 409 || cancelled_)
                break;
            QThread::msleep(150);
        } while (elapsed.elapsed() < 2500);
        const QString id = current.value("id").toString();
        static const QRegularExpression jobId(QStringLiteral("^[a-f0-9]{32}$"));
        if (!jobId.match(id).hasMatch()) {
            unavailable_ = true;
            return {};
        }
        const auto endpoint = QStringLiteral("/api/jobs/")+id;
        while (!cancelled_ && elapsed.elapsed() < 12000) {
            const auto state = current.value("state").toString();
            if (state == "done")
                return current.value("text").toString().trimmed();
            if (state == "error" || state == "cancelled")
                return {};
            QThread::msleep(110);
            current = request(endpoint,{},2000);
            if (current.isEmpty())
                break;
        }
        request(endpoint+"/cancel",QJsonObject{},1500,nullptr,false);
        unavailable_ = !cancelled_;
        return {};
    }

private:
    QJsonObject request(const QString &path, const std::optional<QJsonObject> &body, int timeout,
                        int *status = nullptr, bool interruptible = true)
    {
        QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1828")+path));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
        request.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");
        request.setTransferTimeout(timeout);
        QNetworkReply *reply = body ? network_.post(request,QJsonDocument(*body).toJson(QJsonDocument::Compact))
                                    : network_.get(request);
        QEventLoop loop;
        QTimer timer;
        QElapsedTimer deadline;
        deadline.start();
        QObject::connect(&timer,&QTimer::timeout,&loop,[&] {
            if (deadline.elapsed() >= timeout || (interruptible && cancelled_))
                reply->abort();
        });
        QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);
        timer.start(50);
        if (!reply->isFinished())
            loop.exec();
        if (status)
            *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto response = reply->isReadable() ? QJsonDocument::fromJson(reply->readAll()).object() : QJsonObject();
        delete reply;
        return response;
    }

    QNetworkAccessManager network_;
    const std::atomic_bool &cancelled_;
    bool unavailable_ = false;
};

int transcriptBoundary(const QByteArray &pcm, int first, int limit)
{
    const int target = first+AsrChunkSamples;
    if (limit <= target)
        return limit;
    constexpr int stride = Rate/50;
    int best = target;
    qint64 quietest = std::numeric_limits<qint64>::max();
    for (int cut = target-Rate; cut <= target; cut += stride) {
        qint64 power = 0;
        for (int i=cut-stride/2; i<cut+stride/2; ++i) {
            const auto sample = qFromLittleEndian<qint16>(pcm.constData()+2*i);
            power += qint64(sample)*sample;
        }
        if (power <= quietest) {
            quietest = power;
            best = cut;
        }
    }
    return best;
}

void setSpan(QVariantMap &row, qint64 start, qint64 end)
{
    row["start_ms"] = start;
    row["end_ms"] = end;
    row["range"] = timeLabel(start) + " – " + timeLabel(end);
}

struct Speaker {
    QString id, name, language;
    QList<Embedding> samples;
};

QList<Speaker> speakers(const QJsonArray &profiles)
{
    QList<Speaker> result;
    for (const auto &value : profiles) {
        const auto profile = value.toObject();
        Speaker item{profile.value("id").toString(),profile.value("name").toString(),profile.value("language").toString(),{}};
        for (const auto &sample : profile.value("samples").toArray()) {
            const auto values = sample.toArray();
            if (values.size() != Dimension)
                continue;
            Embedding vector;
            for (int i=0; i<Dimension; ++i)
                vector[i] = float(values[i].toDouble(std::nan("")));
            if (normalize(vector))
                item.samples.append(vector);
        }
        if (!item.samples.isEmpty())
            result.append(item);
    }
    return result;
}
}

class SmartSpeech
{
public:
    SmartSpeech()
    {
        if (vad_ && (fvad_set_sample_rate(vad_.get(), Rate) || fvad_set_mode(vad_.get(), 2)))
            vad_.reset();
    }

    QList<SmartWindow> scan(const QByteArray &pcm, bool flush, const std::atomic_bool *cancelled = nullptr)
    {
        QList<SmartWindow> windows;
        if (!vad_)
            return windows;
        const int totalFrames = int(pcm.size() / (2 * VadFrame));
        std::array<qint16, VadFrame> samples{};
        for (; cursor_ < totalFrames && (!cancelled || !*cancelled); ++cursor_) {
            const auto *raw = pcm.constData() + 2 * cursor_ * VadFrame;
            qint64 squares = 0;
            for (int i = 0; i < VadFrame; ++i) {
                samples[i] = qFromLittleEndian<qint16>(raw + 2*i);
                squares += qint64(samples[i]) * samples[i];
            }
            const auto rms = std::sqrt(double(squares)/VadFrame);
            const bool vadSpeech = fvad_process(vad_.get(), samples.data(), samples.size()) > 0;
            if (!vadSpeech || rms < noise_*1.6)
                noise_ += .035*(std::clamp(rms, 24., 550.)-noise_);
            const bool speech = vadSpeech && rms > std::max(65., noise_*1.65);
            if (speech) {
                if (voiced_ == 0)
                    first_ = cursor_;
                last_ = cursor_ + 1;
                ++voiced_;
            }
            if (voiced_ && (cursor_ + 1 - last_ >= SmartPauseFrames ||
                           cursor_ + 1 - first_ >= SmartWindowFrames))
                finish(windows);
        }
        if (flush)
            finish(windows);
        return windows;
    }

private:
    void finish(QList<SmartWindow> &windows)
    {
        if (voiced_ >= SmartMinVoicedFrames)
            windows.append({first_ * VadFrame, last_ * VadFrame, voiced_});
        voiced_ = 0;
    }

    std::unique_ptr<Fvad, decltype(&fvad_free)> vad_{fvad_new(), &fvad_free};
    int cursor_ = 0, first_ = 0, last_ = 0, voiced_ = 0;
    double noise_ = 36.;
};

class SpeakerSpectrum
{
public:
    explicit SpeakerSpectrum(int step) : step_(step)
    {
        for (int i = 0; i < N; ++i)
            window_[i] = .5f - .5f*std::cos(6.2831853f*i/(N-1));
    }

    QImage update(const QByteArray &pcm, bool flush = false)
    {
        const int samples = int(pcm.size()/2);
        const int end = flush ? (samples+step_-1)/step_ : qMax(0,(samples-N/2)/step_);
        if (end <= columns_)
            return {};
        if (end > image_.width()) {
            QImage next(qMax(end, image_.width()+256), Rows, QImage::Format_Indexed8);
            next.setColorTable(colors());
            next.fill(0);
            for (int y = 0; y < Rows && columns_ > 0; ++y)
                std::copy_n(image_.constScanLine(y), columns_, next.scanLine(y));
            image_ = std::move(next);
        }
        cv::Mat input(1,N,CV_32F);
        cv::Mat output;
        const int hops = qMax(1,(step_+319)/320);
        while (columns_ < end) {
            std::array<float, Rows> peaks{};
            for (int hop=0; hop<hops; ++hop) {
                const int first = columns_*step_ + (2*hop+1)*step_/(2*hops) - N/2;
                auto *in = input.ptr<float>();
                for (int i = 0; i < N; ++i) {
                    const int index = first+i;
                    in[i] = index >= 0 && index < samples
                        ? window_[i]*qFromLittleEndian<qint16>(pcm.constData()+2*index)/32768.f : 0.f;
                }
                cv::dft(input, output, cv::DFT_COMPLEX_OUTPUT);
                const auto *bins = output.ptr<cv::Vec2f>();
                for (int y = 0; y < Rows; ++y) {
                    const int bin = (Rows-1-y)*2;
                    const float power = (bins[bin].dot(bins[bin])+bins[bin+1].dot(bins[bin+1]))/(N*N/16.f);
                    peaks[y] = qMax(peaks[y],power);
                }
            }
            for (int y = 0; y < Rows; ++y) {
                const float db = 10.f*std::log10(std::max(1e-10f,peaks[y]));
                image_.scanLine(y)[columns_] = uchar(std::clamp((db+85.f)*(255.f/65.f),0.f,255.f));
            }
            ++columns_;
        }
        return image_.copy(0,0,columns_,Rows);
    }

private:
    static QList<QRgb> colors()
    {
        // Interpolate the dark blue, cyan and yellow stops of a MATLAB-style color scale.
        constexpr std::array<std::array<int,3>,6> stops{{{37,32,93},{34,71,156},{19,126,175},
            {49,171,151},{161,198,82},{249,231,44}}};
        QList<QRgb> palette;
        for (int i=0; i<256; ++i) {
            const double pos = i*5./255.;
            const int k = std::min(4,int(pos));
            const auto mix = [&](int channel) {return qRound(stops[k][channel]+(pos-k)*(stops[k+1][channel]-stops[k][channel]));};
            palette.append(qRgb(mix(0),mix(1),mix(2)));
        }
        return palette;
    }
    static constexpr int N=512, Rows=128;
    QImage image_;
    std::array<float,N> window_{};
    int step_, columns_ = 0;
};

class SpeakerRecognitionEngine
{
public:
    SpeakerRecognitionEngine()
        : root_(qEnvironmentVariable("FIELDOPS_SPEAKER_HOME", "/userdata/fieldops-speaker"))
    {}
    ~SpeakerRecognitionEngine() {
        if (diarizer_)
            destroyDiarizer_(diarizer_);
        if (meetingVerifier_)
            destroyExtractor_(meetingVerifier_);
        if (meetingEmbedder_)
            destroyExtractor_(meetingEmbedder_);
        if (extractor_)
            destroyExtractor_(extractor_);
    }

    QVariantMap load()
    {
        const auto modelPath = QFileInfo(root_.filePath("models/" + ModelName));
        if (!modelPath.isFile() || modelPath.size() < 1000000)
            return error(QObject::tr("缺少 CAM++ 模型。请运行板端声纹安装脚本。"));
        if (!extractor_) {
            library_.setFileName(root_.filePath("lib/libsherpa-onnx-c-api.so"));
            if (!library_.load())
                return error(QObject::tr("无法加载 sherpa-onnx ARM64 运行库。"));
            if (!resolve(createExtractor_, "SherpaOnnxCreateSpeakerEmbeddingExtractor") ||
                !resolve(destroyExtractor_, "SherpaOnnxDestroySpeakerEmbeddingExtractor") ||
                !resolve(dimension_, "SherpaOnnxSpeakerEmbeddingExtractorDim") ||
                !resolve(createStream_, "SherpaOnnxSpeakerEmbeddingExtractorCreateStream") ||
                !resolve(accept_, "SherpaOnnxOnlineStreamAcceptWaveform") ||
                !resolve(inputFinished_, "SherpaOnnxOnlineStreamInputFinished") ||
                !resolve(isReady_, "SherpaOnnxSpeakerEmbeddingExtractorIsReady") ||
                !resolve(compute_, "SherpaOnnxSpeakerEmbeddingExtractorComputeEmbedding") ||
                !resolve(destroyEmbedding_, "SherpaOnnxSpeakerEmbeddingExtractorDestroyEmbedding") ||
                !resolve(destroyStream_, "SherpaOnnxDestroyOnlineStream"))
                return error(QObject::tr("sherpa-onnx C API 版本不匹配。"));
            const auto model = QFile::encodeName(modelPath.absoluteFilePath());
            const SherpaConfig config{model.constData(), 2, 0, "cpu"};
            extractor_ = createExtractor_(&config);
            if (!extractor_ || dimension_(extractor_) != Dimension) {
                if (extractor_)
                    destroyExtractor_(extractor_);
                extractor_ = nullptr;
                return error(QObject::tr("CAM++ 模型无法加载或声纹维度不符。"));
            }
        }
        if (!diarizer_ && QFileInfo::exists(root_.filePath(DiarizationModel)) &&
            resolve(createDiarizer_, "SherpaOnnxCreateOfflineSpeakerDiarization") &&
            resolve(destroyDiarizer_, "SherpaOnnxDestroyOfflineSpeakerDiarization") &&
            resolve(diarize_, "SherpaOnnxOfflineSpeakerDiarizationProcess") &&
            resolve(setDiarizationConfig_, "SherpaOnnxOfflineSpeakerDiarizationSetConfig") &&
            resolve(diarizationCount_, "SherpaOnnxOfflineSpeakerDiarizationResultGetNumSegments") &&
            resolve(diarizationSegments_, "SherpaOnnxOfflineSpeakerDiarizationResultSortByStartTime") &&
            resolve(destroySegments_, "SherpaOnnxOfflineSpeakerDiarizationDestroySegment") &&
            resolve(destroyDiarization_, "SherpaOnnxOfflineSpeakerDiarizationDestroyResult")) {
            const auto create = [&](const QString &segmentationPath, const QString &embeddingPath, bool precise) {
                const auto segmentation = QFile::encodeName(segmentationPath);
                const auto embedding = QFile::encodeName(embeddingPath);
                const DiarizationConfig config{{{segmentation.constData(),precise ? .20f : .25f},2,0,"cpu"},
                    {embedding.constData(),2,0,"cpu"},{-1,precise ? PreciseClusteringDistance : .48f,0},
                    precise ? .16f : .23f,precise ? .10f : .12f};
                return createDiarizer_(&config);
            };
            const auto preciseModel = root_.filePath(PreciseDiarizationModel);
            const auto clusteringModel = root_.filePath(DiarizationEmbeddingModel);
            if (QFileInfo::exists(preciseModel) && QFileInfo::exists(clusteringModel)) {
                diarizer_ = create(preciseModel, clusteringModel, true);
                preciseDiarizer_ = diarizer_ != nullptr;
            }
            if (!diarizer_)
                diarizer_ = create(root_.filePath(DiarizationModel), modelPath.absoluteFilePath(), false);
        }
        return {{"loaded",true},{"whisper",whisperAvailable()},{"diarization",bool(diarizer_)}};
    }

    QVariantMap run(const QString &path, double start, double length, bool enrollment, double threshold,
                    bool transcribe, const QJsonArray &profiles, const std::atomic_bool &cancelled)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        auto result = runPcm(decode(path, start, length, cancelled), QFileInfo(path).fileName(), start,
                             enrollment, threshold, transcribe, profiles, cancelled);
        if (result.contains("metrics")) {
            auto metrics = result.value("metrics").toMap();
            metrics["total_ms"] = elapsed.elapsed();
            result["metrics"] = metrics;
        } else if (result.contains("embedding")) {
            result["elapsed_ms"] = elapsed.elapsed();
        }
        return result;
    }

    QVariantMap runPcm(const QByteArray &pcm, const QString &source, double start, bool enrollment,
                       double threshold, bool transcribe, const QJsonArray &profiles,
                       const std::atomic_bool &cancelled)
    {
        const auto loaded = load();
        if (!loaded.value("loaded").toBool())
            return loaded;
        QElapsedTimer elapsed;
        elapsed.start();
        if (pcm.size() < 2*Rate)
            return error(QObject::tr("没有可分析的音频，或音频不足 1 秒。"));
        const auto activity = detectSpeech(pcm, cancelled);
        if (cancelled)
            return {};
        if (enrollment) {
            int count = 0;
            VoiceRegion best{0,0};
            for (const auto &region : activity.regions) {
                const int voiced = activity.frames(region.first, region.last);
                if (voiced > count) {
                    count = voiced;
                    best = region;
                }
            }
            Embedding vector;
            if (count < MinVoicedFrames || !embed(pcm, best.first, best.last, vector))
                return error(QObject::tr("样本中有效语音不足。请选一段清晰的单人发言。"));
            QVariantList embedding;
            for (float value : vector)
                embedding.append(value);
            return {{"embedding",embedding},{"elapsed_ms",elapsed.elapsed()}};
        }
        const auto library = speakers(profiles);
        QVariantList rows;
        int embeddings = 0;
        qint64 embeddingMs = 0;
        const auto base = qRound64(start*1000);
        for (const auto &region : activity.regions) {
            for (int first = region.first; first < region.last && !cancelled;) {
                int last = qMin(first + Window, region.last);
                if (region.last - last < 2*Rate)
                    last = region.last;
                const auto begin = base + qint64(first)*1000/Rate;
                const auto end = base + qint64(last)*1000/Rate;
                Embedding vector;
                QVariantMap row{{"start_ms",begin},{"end_ms",end},{"range",timeLabel(begin)+" – "+timeLabel(end)},
                    {"speaker","—"},{"state","insufficient"},{"status",QObject::tr("语音不足")},{"score","—"}};
                QElapsedTimer inference;
                inference.start();
                if (activity.frames(first, last) >= MinVoicedFrames && embed(pcm, first, last, vector)) {
                    ++embeddings;
                    embeddingMs += inference.elapsed();
                    row = compare(library, vector, row, threshold);
                }
                // Keep the score visible per window and avoid merging across silence or a handoff.
                if (transcribe && row.value("state") == "matched" && !row.value("language").toString().isEmpty()) {
                    const auto text = whisper(pcm, first, last, row.value("language").toString(), cancelled);
                    row["transcript"] = text;
                    row["transcript_status"] = text.isEmpty() ? "empty" : "completed";
                }
                rows.append(row);
                first = last;
            }
        }
        return {{"segments",rows},{"waveform",waveformEnvelope(pcm)},{"metrics",QVariantMap{{"total_ms",elapsed.elapsed()},{"embedding_ms",embeddingMs},
            {"embeddings",embeddings},{"offset_ms",base},{"audio_ms",qint64(pcm.size())*1000/(2*Rate)},{"threshold",threshold},
            {"source",source},{"transcription",transcribe}}}};
    }

    QVariantMap classify(const QByteArray &pcm, double threshold, const QJsonArray &profiles)
    {
        const std::atomic_bool keepRunning{false};
        const auto activity=detectSpeech(pcm,keepRunning);
        int mostVoiced=0;
        VoiceRegion candidate{0,0};
        for (const auto &region : activity.regions) {
            const auto voiced=activity.frames(region.first,region.last);
            if (voiced>mostVoiced) {
                mostVoiced=voiced;
                candidate=region;
            }
        }
        Embedding voice;
        if (mostVoiced<MinVoicedFrames || !embed(pcm,candidate.first,candidate.last,voice))
            return {{"state","insufficient"},{"status",QObject::tr("语音不足")}};
        return compare(speakers(profiles),voice,{},threshold);
    }

    QVariantMap smartWindow(const QByteArray &pcm, const SmartWindow &window, QJsonArray &profiles,
                            qint64 base, bool allowCreate, QJsonObject &created, const QStringList &excluded = {})
    {
        QElapsedTimer elapsed;
        elapsed.start();
        QVariantMap row{{"voiced_ms", window.voiced * 30}};
        setSpan(row, base + qint64(window.first)*1000/Rate, base + qint64(window.last)*1000/Rate);
        Embedding voice;
        if (!embed(pcm, window.first, window.last, voice)) {
            row["state"] = "insufficient";
            row["status"] = QObject::tr("特征提取失败");
        } else {
            const auto library = speakers(profiles);
            row = compare(library, voice, row, SmartMatchThreshold);
            if (row.value("state") == "matched" && excluded.contains(row.value("profile_id").toString())) {
                row.remove("speaker");
                row.remove("profile_id");
                row.remove("language");
                row["state"] = "unknown";
                row["status"] = QObject::tr("身份冲突待确认");
                row["inference_ms"] = elapsed.elapsed();
                return row;
            }
            const auto score = row.value("similarity");
            const bool novel = !score.isValid() || score.toDouble() < SmartNoveltyThreshold;
            if (row.value("state") == "matched") {
                const double similarity = score.toDouble();
                if (allowCreate && window.voiced >= SmartEnrollVoicedFrames && similarity >= SmartAdaptThreshold &&
                    similarity < .95 && row.value("score_margin").toDouble() >= SmartAdaptMargin) {
                    const auto id = row.value("profile_id").toString();
                    for (int i=0; i<profiles.size(); ++i) {
                        auto profile = profiles[i].toObject();
                        if (profile.value("id").toString() != id || !profile.contains("auto_number"))
                            continue;
                        auto samples = profile.value("samples").toArray();
                        if (samples.size() < MaxSamples) {
                            QJsonArray vector;
                            for (float value : voice)
                                vector.append(value);
                            samples.append(vector);
                            profile["samples"] = samples;
                            profiles[i] = profile;
                        }
                        break;
                    }
                }
            } else {
                if (novel && window.voiced >= SmartEnrollVoicedFrames && allowCreate && profiles.size() < MaxProfiles) {
                    int number = 0;
                    for (const auto &entry : profiles)
                        number = qMax(number, entry.toObject().value("auto_number").toInt());
                    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    const auto name = QObject::tr("发言人 %1").arg(number + 1, 3, 10, QLatin1Char('0'));
                    QJsonArray vector;
                    for (float value : voice)
                        vector.append(value);
                    created = {{"id",id},{"name",name},{"language",QString()},{"auto_number",number+1},
                               {"samples",QJsonArray{vector}}};
                    row["state"] = "new";
                    row["status"] = QObject::tr("已自动建档");
                    row["speaker"] = name;
                    row["profile_id"] = id;
                } else if (!novel) {
                    row["status"] = QObject::tr("待确认");
                } else if (!allowCreate) {
                    row["status"] = QObject::tr("未知发言人");
                } else if (profiles.size() >= MaxProfiles) {
                    row["status"] = QObject::tr("档案已满");
                } else {
                    row["status"] = QObject::tr("建档语音偏短");
                }
            }
        }
        row["inference_ms"] = elapsed.elapsed();
        return row;
    }

    QVariantMap analyzeConversation(const QByteArray &source, qint64 base, QJsonArray profiles, bool allowCreate,
                                    bool transcribe, const std::atomic_bool &cancelled,
                                    int expectedSpeakers = 0)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        QVariantList rows, overlaps;
        QJsonArray additions;
        const auto originalProfiles = profiles;
        const auto pcm = highPass(source);
        const int samples = int(pcm.size()/2);
        const bool meeting = samples >= MeetingMinimumSamples || expectedSpeakers > 0;
        QList<SpeechTrack> tracks;
        if (diarizer_ && samples >= Rate/4) {
            DiarizationConfig configuration{};
            configuration.clustering = {expectedSpeakers > 0 ? expectedSpeakers : -1,
                preciseDiarizer_ ? PreciseClusteringDistance : .48f,0};
            setDiarizationConfig_(diarizer_, &configuration);
            const auto *result = diarizeChunk(preciseDiarizer_ ? source : pcm, 0, samples);
            if (!result)
                return error(QObject::tr("多人语音分析失败。"));
            if (const auto count = diarizationCount_(result); count > 0) {
                const auto *segments = diarizationSegments_(result);
                for (int i=0; segments && i<count; ++i) {
                    const int first = std::clamp(qRound(segments[i].start*Rate),0,samples);
                    const int last = std::clamp(qRound(segments[i].end*Rate),first,samples);
                    if (last-first >= (preciseDiarizer_ ? Rate*15/100 : Rate/6))
                        tracks.append({first,last,segments[i].speaker});
                }
                if (segments)
                    destroySegments_(segments);
            }
            destroyDiarization_(result);
        } else if (!diarizer_) {
            SmartSpeech speech;
            for (const auto &window : speech.scan(pcm,true,&cancelled))
                tracks.append({window.first,window.last,int(tracks.size())});
        }
        // Only single-speaker spans may contribute to a persistent voiceprint.
        QMap<int,QList<QPair<int,int>>> events;
        for (const auto &track : tracks) {
            events[track.first].append({track.speaker,1});
            events[track.last].append({track.speaker,-1});
        }
        QMap<int,QList<VoiceRegion>> soloRanges;
        QList<VoiceRegion> overlapRanges;
        QMap<int,QSet<int>> coincident;
        QList<int> speakersInOrder;
        for (const auto &track : tracks) {
            if (!speakersInOrder.contains(track.speaker))
                speakersInOrder.append(track.speaker);
        }
        int first = 0;
        QMap<int,int> active;
        for (auto event=events.cbegin(); event!=events.cend(); ++event) {
            const int last = event.key();
            if (active.size() == 1 && last-first >= Rate/12) {
                const int speaker = active.firstKey();
                auto &ranges = soloRanges[speaker];
                if (!ranges.isEmpty() && ranges.last().last == first)
                    ranges.last().last = last;
                else
                    ranges.append({first,last});
            } else if (active.size() > 1) {
                overlapRanges.append({first,last});
                for (const auto speaker : active.keys())
                    for (const auto other : active.keys())
                        if (speaker != other)
                            coincident[speaker].insert(other);
                QVariantMap overlap{{"speakers",active.size()}};
                setSpan(overlap,base+qint64(first)*1000/Rate,base+qint64(last)*1000/Rate);
                overlaps.append(overlap);
            }
            for (const auto &delta : event.value()) {
                const int count = active.value(delta.first)+delta.second;
                if (count)
                    active[delta.first] = count;
                else
                    active.remove(delta.first);
            }
            first = last;
        }
        if (preciseDiarizer_ && meeting && expectedSpeakers == 0 && speakersInOrder.size() > 1) {
            QMap<int,int> durations;
            for (const auto &track : tracks)
                durations[track.speaker] += track.last-track.first;
            const auto aliases = verifyMeetingTracks(source,soloRanges,coincident,durations,cancelled);
            if (cancelled)
                return {};
            if (!aliases.isEmpty()) {
                for (auto &track : tracks)
                    track.speaker = aliases.value(track.speaker,track.speaker);
                QMap<int,QList<VoiceRegion>> mergedRanges;
                for (auto it=soloRanges.cbegin(); it!=soloRanges.cend(); ++it)
                    mergedRanges[aliases.value(it.key(),it.key())].append(it.value());
                for (auto &ranges : mergedRanges)
                    std::sort(ranges.begin(),ranges.end(),[](const VoiceRegion &a, const VoiceRegion &b) {
                        return a.first < b.first;
                    });
                soloRanges = std::move(mergedRanges);
                speakersInOrder.clear();
                for (const auto &track : tracks)
                    if (!speakersInOrder.contains(track.speaker))
                        speakersInOrder.append(track.speaker);
            }
        }
        QMap<int,QVariantMap> identities;
        int embeddings = 0;
        for (const int speaker : speakersInOrder) {
            if (cancelled)
                return {};
            QStringList excluded;
            for (auto other=identities.cbegin(); other!=identities.cend(); ++other)
                if (meeting || coincident.value(speaker).contains(other.key()))
                    excluded.append(other.value().value("profile_id").toString());
            QByteArray sample;
            for (const auto &range : soloRanges.value(speaker)) {
                const auto bytes = std::min(2*(range.last-range.first),6*2*Rate-int(sample.size()));
                sample.append(pcm.constData()+2*range.first,bytes);
                if (sample.size() >= 6*2*Rate)
                    break;
            }
            QVariantMap identity;
            if (sample.size() >= Rate*12/10) {
                QJsonObject created;
                const int voiced = int(sample.size()/2/VadFrame);
                identity = smartWindow(sample,{0,int(sample.size()/2),voiced},profiles,
                                       0,allowCreate && (!meeting || voiced >= 100),created,excluded);
                if (!created.isEmpty()) {
                    additions.append(created);
                    profiles.append(created);
                }
                if (identity.value("state") != "insufficient")
                    ++embeddings;
            }
            if (identity.value("speaker").toString().isEmpty()) {
                identity["speaker"] = QObject::tr("会议轨道 %1").arg(speakersInOrder.indexOf(speaker)+1,2,10,QLatin1Char('0'));
                identity["track_id"] = QStringLiteral("unconfirmed-%1-%2").arg(base).arg(speaker);
                identity["state"] = "unknown";
                if (identity.value("status").toString().isEmpty())
                    identity["status"] = QObject::tr("单人语音偏短");
            }
            identities.insert(speaker,identity);
        }
        const auto transcriptPcm = transcribe && !tracks.isEmpty() && !cancelled
            ? suppressStationaryNoise(pcm) : QByteArray();
        BoardAsr asr(cancelled);
        for (const auto &track : tracks) {
            if (cancelled)
                return {};
            auto row = identities.value(track.speaker);
            row["voiced_ms"] = (track.last-track.first)*1000/Rate;
            setSpan(row,base+qint64(track.first)*1000/Rate,base+qint64(track.last)*1000/Rate);
            const auto overlapIt = std::lower_bound(overlapRanges.cbegin(),overlapRanges.cend(),track.first,
                [](const VoiceRegion &range, int start) { return range.last <= start; });
            const bool overlap = overlapIt != overlapRanges.cend() && overlapIt->first < track.last;
            row["overlap"] = overlap;
            row["mode"] = overlap ? QObject::tr("含同讲") : QObject::tr("单人");
            if (transcribe) {
                QStringList phrases;
                QVariantList words;
                bool attempted = false, failed = false;
                const auto &ranges = soloRanges[track.speaker];
                auto soloIt = std::lower_bound(ranges.cbegin(),ranges.cend(),track.first,
                    [](const VoiceRegion &range, int start) { return range.last <= start; });
                const auto decodeSpan = [&](const QByteArray &audio, int offset, int length, int first, int last) {
                    attempted = true;
                    const auto value = asr.transcribe(audio,offset,offset+length);
                    if (value.isEmpty()) {
                        failed = true;
                        return;
                    }
                    phrases.append(value);
                    QVariantMap phrase{{"text",value},{"model",AsrName}};
                    setSpan(phrase,base+qint64(first)*1000/Rate,base+qint64(last)*1000/Rate);
                    words.append(phrase);
                };
                for (; soloIt != ranges.cend() && soloIt->first < track.last && !cancelled;) {
                    const int start = qMax(soloIt->first,track.first);
                    int limit = qMin(soloIt->last,track.last);
                    ++soloIt;
                    if (soloIt != ranges.cend() && soloIt->first < track.last &&
                        soloIt->first-limit <= Rate*35/100 &&
                        qMin(soloIt->last,track.last)-start <= AsrChunkSamples) {
                        // Replace brief interruptions with silence to preserve context and timestamps.
                        auto joined = transcriptPcm.mid(start*2,(limit-start)*2);
                        int voiced = limit-start;
                        do {
                            const int next = soloIt->first;
                            joined.append(QByteArray((next-limit)*2,'\0'));
                            limit = qMin(soloIt->last,track.last);
                            joined.append(transcriptPcm.constData()+next*2,(limit-next)*2);
                            voiced += limit-next;
                            ++soloIt;
                        } while (soloIt != ranges.cend() && soloIt->first < track.last &&
                                 soloIt->first-limit <= Rate*35/100 &&
                                 qMin(soloIt->last,track.last)-start <= AsrChunkSamples);
                        if (voiced >= Rate/2)
                            decodeSpan(joined,0,limit-start,start,limit);
                        continue;
                    }
                    for (int first = start; limit-first >= Rate/2 && !cancelled;) {
                        const int last = transcriptBoundary(transcriptPcm,first,limit);
                        decodeSpan(transcriptPcm,first,last-first,first,last);
                        first = last;
                    }
                }
                row["transcript"] = phrases.join(QStringLiteral(" … "));
                row["transcript_spans"] = words;
                row["transcript_model"] = AsrName;
                if (phrases.isEmpty())
                    row["transcript_status"] = attempted && failed ? "unavailable" : overlap ? "overlap" : "empty";
                else
                    row["transcript_status"] = overlap || failed ? "partial" : "completed";
                row["transcript_incomplete"] = failed;
            }
            rows.append(row);
        }
        return {{"segments",rows},{"additions",additions},{"overlaps",overlaps},{"embeddings",embeddings},
                {"profile_bank",profiles != originalProfiles ? QVariant(profiles) : QVariant()},
                {"total_ms",elapsed.elapsed()},{"diarization",bool(diarizer_)}};
    }

    QVariantMap smartFile(const QString &path, double start, double length, bool transcribe, const QJsonArray &profiles,
                          bool allowCreate, const std::atomic_bool &cancelled,
                          int expectedSpeakers)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        const auto pcm = decode(path, start, length, cancelled);
        if (pcm.isEmpty())
            return error(QObject::tr("无法解码选定音频。"));
        const auto base = qRound64(start*1000);
        auto result = analyzeConversation(pcm,base,profiles,allowCreate,transcribe,cancelled,expectedSpeakers);
        if (cancelled || result.contains("error"))
            return result;
        const int step = qMax(320,int(pcm.size()/2+1799)/1800);
        result["spectrum"] = SpeakerSpectrum(step).update(pcm,true);
        result["waveform"] = waveformEnvelope(pcm);
        result["metrics"] = QVariantMap{{"smart",true},{"total_ms",elapsed.elapsed()},
            {"embeddings",result.value("embeddings")},{"offset_ms",base},{"audio_ms",qint64(pcm.size())*1000/(2*Rate)},
            {"threshold",SmartMatchThreshold},{"source",QFileInfo(path).fileName()},{"transcription",transcribe},
            {"transcript_model",transcribe ? AsrName : ""},
            {"expected_speakers",expectedSpeakers},
            {"overlaps",result.value("overlaps")},{"diarization",result.value("diarization")},{"spectrum_step_ms",step/16.}};
        return result;
    }

private:
    const SherpaExtractor *openMeetingExtractor(const QString &path)
    {
        if (!QFileInfo::exists(path))
            return nullptr;
        const auto model = QFile::encodeName(path);
        const SherpaConfig config{model.constData(),2,0,"cpu"};
        const auto *extractor = createExtractor_(&config);
        if (extractor && dimension_(extractor) != Dimension) {
            destroyExtractor_(extractor);
            return nullptr;
        }
        return extractor;
    }

    QMap<int,int> verifyMeetingTracks(const QByteArray &pcm, const QMap<int,QList<VoiceRegion>> &solo,
                                    const QMap<int,QSet<int>> &coincident, const QMap<int,int> &durations,
                                    const std::atomic_bool &cancelled)
    {
        if (!meetingVerifier_)
            meetingVerifier_ = openMeetingExtractor(root_.filePath(MeetingVerificationModel));
        if (meetingVerifier_ && !meetingEmbedder_)
            meetingEmbedder_ = openMeetingExtractor(root_.filePath(DiarizationEmbeddingModel));
        if (!meetingEmbedder_)
            return {};

        struct TrackVoice { int id; Embedding verify, cluster; };
        QList<TrackVoice> voices;
        for (auto it=solo.cbegin(); it!=solo.cend() && !cancelled; ++it) {
            auto ranges = it.value();
            std::sort(ranges.begin(),ranges.end(),[](const VoiceRegion &a, const VoiceRegion &b) {
                return a.last-a.first > b.last-b.first;
            });
            QByteArray sample;
            for (const auto &range : ranges) {
                if (range.last-range.first < Rate*4/10)
                    break;
                constexpr int trim = Rate/20;
                const int size = std::min(range.last-range.first-2*trim,5*Rate-int(sample.size()/2));
                if (size < Rate/4)
                    break;
                sample.append(pcm.constData()+2*(range.first+trim),2*size);
            }
            TrackVoice voice{it.key(),{},{}};
            if (sample.size() >= Rate*12/10 &&
                embed(sample,0,int(sample.size()/2),voice.verify,meetingVerifier_) &&
                embed(sample,0,int(sample.size()/2),voice.cluster,meetingEmbedder_))
                voices.append(voice);
        }
        const int n = int(voices.size());
        if (n < 2 || cancelled)
            return {};
        // Complete linkage requires every pair in a merged group to agree.
        // Simultaneous speech makes the corresponding pair ineligible.
        QVector<float> similarity(qsizetype(n)*n,-1.f);
        for (int i=0; i<n; ++i) {
            for (int j=0; j<i; ++j) {
                const auto &a = voices[i];
                const auto &b = voices[j];
                if (coincident.value(a.id).contains(b.id) ||
                    std::min(durations.value(a.id),durations.value(b.id)) > ShortTrackSamples)
                    continue;
                const float verification = std::inner_product(a.verify.begin(),a.verify.end(),b.verify.begin(),0.f);
                const float clustering = std::inner_product(a.cluster.begin(),a.cluster.end(),b.cluster.begin(),0.f);
                similarity[i*n+j] = similarity[j*n+i] = (verification+clustering)*.5f;
            }
        }
        QList<QList<int>> groups;
        for (int i=0; i<n; ++i)
            groups.append(QList<int>{i});
        while (!cancelled) {
            int left = -1, right = -1;
            float best = TrackMergeSimilarity;
            for (int i=1; i<groups.size(); ++i) {
                for (int j=0; j<i; ++j) {
                    float score = 1.f;
                    for (const int a : groups[i])
                        for (const int b : groups[j])
                            score = std::min(score,similarity[a*n+b]);
                    if (score >= best) {
                        best = score;
                        left = j;
                        right = i;
                    }
                }
            }
            if (left < 0)
                break;
            groups[left].append(groups.takeAt(right));
        }
        QMap<int,int> aliases;
        for (const auto &group : groups)
            for (int i=1; i<group.size(); ++i)
                aliases.insert(voices[group[i]].id,voices[group.first()].id);
        return aliases;
    }

    const DiarizationResult *diarizeChunk(const QByteArray &pcm, int first, int last)
    {
        QVector<float> signal(last-first);
        for (int i=first; i<last; ++i)
            signal[i-first] = qFromLittleEndian<qint16>(pcm.constData()+2*i)/32768.f;
        return diarize_(diarizer_,signal.constData(),int(signal.size()));
    }

    template<typename Function>
    bool resolve(Function &function, const char *symbol) {
        function = reinterpret_cast<Function>(library_.resolve(symbol));
        return function;
    }

    bool embed(const QByteArray &pcm, int first, int last, Embedding &out, const SherpaExtractor *extractor = nullptr)
    {
        if (!extractor)
            extractor = extractor_;
        const auto *stream = createStream_(extractor);
        if (!stream)
            return false;
        QVector<float> samples(last-first);
        for (int i=0; i<samples.size(); ++i)
            samples[i] = qFromLittleEndian<qint16>(pcm.constData()+2*(first+i))/32768.0f;
        accept_(stream, Rate, samples.constData(), int(samples.size()));
        inputFinished_(stream);
        const auto *result = isReady_(extractor, stream) ? compute_(extractor, stream) : nullptr;
        if (result) {
            std::copy_n(result, Dimension, out.begin());
            destroyEmbedding_(result);
        }
        destroyStream_(stream);
        return result && normalize(out);
    }

    static QVariantMap compare(const QList<Speaker> &profiles, const Embedding &embedding,
                               QVariantMap row, double threshold)
    {
        float first = -1, second = -1;
        const Speaker *match = nullptr;
        for (const auto &profile : profiles) {
            float best = -1;
            for (const auto &sample : profile.samples)
                best = qMax(best, std::inner_product(embedding.begin(), embedding.end(), sample.begin(), 0.0f));
            if (best > first) {
                second = first;
                first = best;
                match = &profile;
            } else {
                second = qMax(second, best);
            }
        }
        row["score"] = match ? QString::number(qBound(-1.f,first,1.f),'f',3) : QStringLiteral("—");
        row["similarity"] = match ? QVariant(qBound(-1.f,first,1.f)) : QVariant();
        if (match)
            row["score_margin"] = first-second;
        if (!match || first < threshold || first-second < MinimumMargin) {
            row["state"] = "unknown";
            row["status"] = match && first >= threshold ? QObject::tr("候选接近") : QObject::tr("未知发言人");
            return row;
        }
        row["state"] = "matched";
        row["status"] = QObject::tr("已匹配");
        row["speaker"] = match->name;
        row["profile_id"] = match->id;
        row["language"] = match->language;
        return row;
    }

    bool whisperAvailable() const
    {
        return QFileInfo(root_.filePath("bin/sherpa-onnx-offline")).isExecutable() &&
            QFileInfo(root_.filePath("whisper/tiny-encoder.int8.onnx")).isFile() &&
            QFileInfo(root_.filePath("whisper/tiny-decoder.int8.onnx")).isFile() &&
            QFileInfo(root_.filePath("whisper/tiny-tokens.txt")).isFile();
    }

    QString whisper(const QByteArray &pcm, int first, int last, const QString &language, const std::atomic_bool &cancelled)
    {
        QTemporaryDir directory;
        QFile wav(directory.filePath("segment.wav"));
        const int size = (last-first)*2;
        if (!directory.isValid() || !wav.open(QIODevice::WriteOnly))
            return {};
        QDataStream output(&wav);
        output.setByteOrder(QDataStream::LittleEndian);
        output.writeRawData("RIFF",4);
        output << quint32(size+36);
        output.writeRawData("WAVEfmt ",8);
        output << quint32(16) << quint16(1) << quint16(1) << quint32(Rate) << quint32(Rate*2) << quint16(2) << quint16(16);
        output.writeRawData("data",4);
        output << quint32(size);
        output.writeRawData(pcm.constData()+2*first,size);
        wav.close();
        if (output.status() != QDataStream::Ok)
            return {};
        const auto result = runProcess(root_.filePath("bin/sherpa-onnx-offline"),
            {"--num-threads=2","--provider=cpu","--whisper-language="+language,"--whisper-task=transcribe",
             "--whisper-encoder="+root_.filePath("whisper/tiny-encoder.int8.onnx"),
             "--whisper-decoder="+root_.filePath("whisper/tiny-decoder.int8.onnx"),
             "--tokens="+root_.filePath("whisper/tiny-tokens.txt"),wav.fileName()},cancelled,60000);
        for (const auto &line : result.split('\n')) {
            const auto json = QJsonDocument::fromJson(QString::fromUtf8(line.trimmed()).toUtf8()).object();
            const auto text = json.value("text").toString().trimmed();
            if (!text.isEmpty())
                return text;
        }
        return {};
    }

    QDir root_;
    QLibrary library_;
    const SherpaExtractor *extractor_ = nullptr;
    const SherpaExtractor *meetingVerifier_ = nullptr;
    const SherpaExtractor *meetingEmbedder_ = nullptr;
    const Diarizer *diarizer_ = nullptr;
    const Diarizer *(*createDiarizer_)(const DiarizationConfig *) = nullptr;
    bool preciseDiarizer_ = false;
    void (*destroyDiarizer_)(const Diarizer *) = nullptr;
    const DiarizationResult *(*diarize_)(const Diarizer *,const float *,qint32) = nullptr;
    void (*setDiarizationConfig_)(const Diarizer *,const DiarizationConfig *) = nullptr;
    qint32 (*diarizationCount_)(const DiarizationResult *) = nullptr;
    const DiarizationSegment *(*diarizationSegments_)(const DiarizationResult *) = nullptr;
    void (*destroySegments_)(const DiarizationSegment *) = nullptr;
    void (*destroyDiarization_)(const DiarizationResult *) = nullptr;
    const SherpaExtractor *(*createExtractor_)(const SherpaConfig *) = nullptr;
    void (*destroyExtractor_)(const SherpaExtractor *) = nullptr;
    qint32 (*dimension_)(const SherpaExtractor *) = nullptr;
    const SherpaStream *(*createStream_)(const SherpaExtractor *) = nullptr;
    void (*accept_)(const SherpaStream *,qint32,const float *,qint32) = nullptr;
    void (*inputFinished_)(const SherpaStream *) = nullptr;
    qint32 (*isReady_)(const SherpaExtractor *,const SherpaStream *) = nullptr;
    const float *(*compute_)(const SherpaExtractor *,const SherpaStream *) = nullptr;
    void (*destroyEmbedding_)(const float *) = nullptr;
    void (*destroyStream_)(const SherpaStream *) = nullptr;
};

SpeakerRecognitionService::SpeakerRecognitionService(SessionManager *session, QObject *parent)
    : QObject(parent), session_(session), engine_(std::make_shared<SpeakerRecognitionEngine>()),
      libraryPath_(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/voiceprints/profiles.json"),
      status_(tr("等待初始化声纹模型"))
{
    pool_.setMaxThreadCount(1);
    connect(&watcher_, &QFutureWatcher<QVariantMap>::finished, this, &SpeakerRecognitionService::complete);
    connect(&liveWatcher_, &QFutureWatcher<QVariantMap>::finished, this, &SpeakerRecognitionService::completeLiveWindow);
    connect(&beamWatcher_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        beamActive_=false;
        if (session_->can("inspect") && session_->userId()==beamOwner_)
            emit beamClassified(beamRequest_,beamWatcher_.result());
    });
    connect(session_, &SessionManager::sessionChanged, this, [this] {
        if (session_->can("inspect")) {
            readProfiles();
            if (!ready_ && !busy())
                warmup();
        }
        emit changed();
    });
    connect(session_, &SessionManager::sessionEnding, this, [this] {
        cancel();
        segments_.clear();
        waveform_.clear();
        metrics_.clear();
        liveMatch_.clear();
        spectrogram_ = {};
        operationError_.clear();
        emit visualsChanged();
        emit spectrumChanged();
        emit changed();
    });
}

SpeakerRecognitionService::~SpeakerRecognitionService()
{
    if (capture_) {
        capture_->requestInterruption();
        capture_->wait();
    }
    if (cancelled_)
        *cancelled_ = true;
    if (smartCancel_)
        *smartCancel_ = true;
    pool_.waitForDone();
}

bool SpeakerRecognitionService::canClassifyBeam() const
{
    return ready_ && !busy() && !beamActive_ && storageValid_ &&
        !profiles_.isEmpty() && session_->can("inspect");
}

bool SpeakerRecognitionService::classifyBeam(const QByteArray &pcm, double threshold, quint64 request)
{
    if (!canClassifyBeam() || pcm.size()<3*Rate || pcm.size()>8*Rate)
        return false;
    beamActive_=true;
    beamOwner_=session_->userId();
    beamRequest_=request;
    beamWatcher_.setFuture(QtConcurrent::run(&pool_,[engine=engine_,pcm,threshold,profiles=profiles_] {
        return engine->classify(pcm,threshold,profiles);
    }));
    return true;
}

void SpeakerRecognitionService::warmup()
{
    if (busy() || !session_->can("inspect"))
        return;
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    owner_ = session_->userId();
    operationError_.clear();
    status_ = tr("正在初始化 sherpa-onnx / CAM++");
    watcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_] { return engine->load(); }));
    emit changed();
}

void SpeakerRecognitionService::cancel()
{
    if (!liveMode_.isEmpty()) {
        discardCapture_ = true;
        if (smartCancel_)
            *smartCancel_ = true;
        stopLive();
    }
    if (cancelled_) {
        *cancelled_ = true;
        status_ = tr("正在停止");
        emit changed();
    }
}

bool SpeakerRecognitionService::report(const QString &status)
{
    status_ = status;
    emit changed();
    return false;
}

bool SpeakerRecognitionService::fail(const QString &message)
{
    operationError_ = message;
    return report(message);
}

int SpeakerRecognitionService::profileIndex(const QString &id) const
{
    for (int i=0; i<profiles_.size(); ++i)
        if (profiles_[i].toObject().value("id").toString() == id)
            return i;
    return -1;
}

bool SpeakerRecognitionService::enroll(const QUrl &source, double start, double length, const QString &id,
                                       const QString &name, const QString &language)
{
    if (busy() || !ready_ || !session_->can("settings"))
        return false;
    const auto path = sourcePath(source);
    if (path.isEmpty() || !validRange(start,length,30))
        return report(tr("请选择可读音频，并选取 1–30 秒的单人发言。"));
    if (!prepareEnrollment(id, name, language))
        return false;
    owner_ = session_->userId();
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    status_ = tr("正在提取声纹特征");
    watcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_,path,start,length,stop=cancelled_] {
        return engine->run(path,start,length,true,0,false,{},*stop);
    }));
    emit changed();
    return true;
}

bool SpeakerRecognitionService::prepareEnrollment(const QString &id, const QString &name, const QString &language)
{
    const auto index = profileIndex(id);
    if ((!id.isEmpty() && index < 0) || (index < 0 && profiles_.size() >= MaxProfiles) ||
        (index >= 0 && profiles_[index].toObject().value("samples").toArray().size() >= MaxSamples) ||
        (index < 0 && !validProfile(name,language)))
        return report(tr("请检查档案名称、语种和样本数。每人最多 3 组样本。"));
    if (!storageValid_)
        return report(tr("声纹库读取失败，请修复文件后重试。"));
    pendingProfile_ = index >= 0 ? id : QUuid::createUuid().toString(QUuid::WithoutBraces);
    pendingName_ = name.trimmed();
    pendingLanguage_ = language;
    return true;
}

bool SpeakerRecognitionService::analyze(const QUrl &source, double start, double length, double threshold, bool transcribe)
{
    return analyzeFile(source, start, length, threshold, transcribe, false);
}

bool SpeakerRecognitionService::analyzeSmart(const QUrl &source, double start, double length, bool transcribe)
{
    return analyzeSmart(source, start, length, transcribe, {});
}

bool SpeakerRecognitionService::analyzeSmart(const QUrl &source, double start, double length, bool transcribe,
                                             const QString &language)
{
    return analyzeSmartMeeting(source, start, length, transcribe, language, 0);
}

bool SpeakerRecognitionService::analyzeSmartMeeting(const QUrl &source, double start, double length, bool transcribe,
                                                    const QString &language, int expectedSpeakers)
{
    return analyzeFile(source, start, length, SmartMatchThreshold, transcribe, true, language, expectedSpeakers);
}

bool SpeakerRecognitionService::analyzeFile(const QUrl &source, double start, double length, double threshold,
                                          bool transcribe, bool smart, const QString &language, int expectedSpeakers)
{
    if (busy() || !ready_ || !session_->can("inspect"))
        return false;
    const auto path = sourcePath(source);
    if (path.isEmpty() || !validRange(start,length,600) || !std::isfinite(threshold) || threshold < .45 || threshold > .99)
        return fail(tr("请选择可读的音频和有效时段。"));
    if (!storageValid_ || (!smart && transcribe && !whisperReady_))
        return fail(tr("声纹库或 Whisper 模型不可用。"));
    if (!validLanguage(language))
        return fail(tr("转写语言无效。"));
    if (expectedSpeakers < 0 || expectedSpeakers > 20)
        return fail(tr("会议人数须为自动或 1–20 人。"));
    operationError_.clear();
    segments_.clear();
    waveform_.clear();
    spectrogram_ = {};
    metrics_.clear();
    liveMatch_.clear();
    emit visualsChanged();
    emit spectrumChanged();
    owner_ = session_->userId();
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    status_ = tr("正在分析选定音频");
    watcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_,path,start,length,threshold,transcribe,smart,expectedSpeakers,
                                                profiles=profiles_,stop=cancelled_] {
        if (smart)
            return engine->smartFile(path,start,length,transcribe,profiles,true,*stop,expectedSpeakers);
        return engine->run(path,start,length,false,threshold,transcribe,profiles,*stop);
    }));
    emit changed();
    return true;
}

void SpeakerRecognitionService::setCaptureGuard(std::function<bool()> guard)
{
    captureGuard_ = std::move(guard);
}

bool SpeakerRecognitionService::startLiveRecognition(double threshold)
{
    if (busy() || !ready_ || !session_->can("inspect"))
        return false;
    if (!storageValid_ || !std::isfinite(threshold) || threshold < .45 || threshold > .99)
        return report(tr("请检查声纹库和 0.45–0.99 的余弦阈值。"));
    return startCapture(QStringLiteral("recognition"), threshold);
}

bool SpeakerRecognitionService::startSmartAnalysis()
{
    return startSmartAnalysisWithTranscript(false);
}

bool SpeakerRecognitionService::startSmartAnalysisWithTranscript(bool transcribe)
{
    return startSmartAnalysisWithTranscript(transcribe, {});
}

bool SpeakerRecognitionService::startSmartAnalysisWithTranscript(bool transcribe, const QString &language)
{
    if (busy() || !ready_ || !session_->can("inspect"))
        return false;
    if (!storageValid_)
        return fail(tr("声纹库无法读取。"));
    if (!validLanguage(language))
        return fail(tr("转写语言无效。"));
    return startCapture(QStringLiteral("smart"), SmartMatchThreshold, transcribe);
}

bool SpeakerRecognitionService::startLiveEnrollment(const QString &id, const QString &name, const QString &language)
{
    if (busy() || !ready_ || !session_->can("settings") || !prepareEnrollment(id, name, language))
        return false;
    if (startCapture(QStringLiteral("enrollment"), .77))
        return true;
    pendingProfile_.clear();
    return false;
}

bool SpeakerRecognitionService::startCapture(const QString &mode, double threshold, bool transcribe, const QString &language)
{
    const bool arrayInput = mode == "smart";
    if (captureGuard_ && captureGuard_())
        return fail(tr("音频设备正在使用，请结束当前采集后再试。"));
    if (arrayInput && (!QFileInfo::exists("/dev/lg_speech_serial") || !QFileInfo::exists("/dev/lg_speech_uac")))
        return fail(tr("未检测到 ListenGo 六麦，请连接设备后重试。"));
    operationError_.clear();
    owner_ = session_->userId();
    liveMode_ = mode;
    captureStopping_ = false;
    discardCapture_ = false;
    captureError_.clear();
    smartSaveError_.clear();
    speechWindows_.clear();
    smartSpeech_ = mode == "smart" ? std::make_unique<SmartSpeech>() : nullptr;
    smartCancel_ = smartSpeech_ ? std::make_shared<std::atomic_bool>(false) : nullptr;
    spectrum_ = smartSpeech_ ? std::make_unique<SpeakerSpectrum>(LiveSpectrumStep) : nullptr;
    liveCursor_ = 0;
    liveMatch_.clear();
    segments_.clear();
    waveform_.clear();
    spectrogram_ = {};
    liveEnvelope_.clear();
    livePcm_.clear();
    inputLevel_ = -90.;
    metrics_ = {{"source",arrayInput ? tr("ListenGo 六麦阵列") : tr("实时麦克风")},
                {"input_channels",arrayInput ? ArrayChannels : 1},
                {"live",true},{"operation",mode},{"offset_ms",0},{"audio_ms",0},{"threshold",threshold},
                {"embeddings",0},{"embedding_ms",0},{"total_ms",0},{"new_profiles",0},{"smart",bool(smartSpeech_)},
                {"transcription",transcribe},{"asr_language",language},{"transcript_model",transcribe ? AsrName : ""},
                {"spectrum_step_ms",LiveSpectrumStep/16.}};
    const int seconds = mode == "enrollment" ? LiveEnrollmentSeconds : smartSpeech_ ? LiveMeetingSeconds : LiveRecognitionSeconds;
    livePcm_.reserve(qMin(seconds,LiveRecognitionSeconds) * 2 * Rate);
    liveEnvelope_.reserve(qMin(seconds,LiveRecognitionSeconds) * Rate / (CaptureFrames / 2));
    status_ = tr("正在打开麦克风");
    const auto deliver = [this](QByteArray block) {
        QMetaObject::invokeMethod(this,[this,block=std::move(block)]{receiveAudio(block);},Qt::QueuedConnection);
    };
    if (arrayInput) {
        auto *native = new NativeAudioCapture({{"name",tr("ListenGo 六麦阵列")},{"transport","six_serial"},
            {"rate",Rate},{"channels",ArrayChannels}}, {}, this,
            [voice=SixMicVoice(seconds),deliver](const QByteArray &pcm) mutable {return voice.consume(pcm,deliver);});
        capture_ = native;
        connect(native,&QThread::finished,this,[this,native]{captureError_ = native->info().value("error").toString();});
    } else {
        capture_ = QThread::create([this,seconds,deliver] {
            const auto error = captureMicrophone(seconds,deliver);
            if (!error.isEmpty())
                QMetaObject::invokeMethod(this,[this,error]{captureError_ = error;},Qt::QueuedConnection);
        });
    }
    capture_->setParent(this);
    connect(capture_, &QThread::finished, this, &SpeakerRecognitionService::captureFinished);
    capture_->start();
    emit visualsChanged();
    emit spectrumChanged();
    emit changed();
    return true;
}

void SpeakerRecognitionService::receiveAudio(const QByteArray &block)
{
    if (captureStopping_ || discardCapture_ || owner_ != session_->userId())
        return;
    const bool first = livePcm_.isEmpty();
    livePcm_.append(block);
    qint64 squares = 0;
    for (int offset = 0; offset < CaptureFrames; offset += CaptureFrames / 2) {
        int low = 0, high = 0;
        for (int i = offset; i < offset + CaptureFrames / 2; ++i) {
            const int sample = qFromLittleEndian<qint16>(block.constData() + 2 * i);
            squares += qint64(sample) * sample;
            low = qMin(low, sample);
            high = qMax(high, sample);
        }
        liveEnvelope_.append(QPoint(low, high));
    }
    const auto rms = std::sqrt(double(squares) / CaptureFrames) / 32768.;
    inputLevel_ = rms > 0. ? qMax(-90., 20. * std::log10(rms)) : -90.;
    const int count = int(liveEnvelope_.size());
    const int bins = qMin(WaveformBins, count);
    QVariantList envelope;
    envelope.reserve(bins);
    for (int i = 0; i < bins; ++i) {
        int low = 0, high = 0;
        for (int j = i * count / bins; j < (i + 1) * count / bins; ++j) {
            low = qMin(low, liveEnvelope_[j].x());
            high = qMax(high, liveEnvelope_[j].y());
        }
        envelope.append(QVariant(QVariantList{low / 32768., high / 32768.}));
    }
    waveform_ = std::move(envelope);
    metrics_["audio_ms"] = qint64(livePcm_.size()) * 1000 / (2 * Rate);
    if (spectrum_ && liveEnvelope_.size()%8 == 0) {
        spectrogram_ = spectrum_->update(livePcm_);
        emit spectrumChanged();
    }
    emit visualsChanged();
    if (first) {
        status_ = liveMode_ == "enrollment" ? tr("正在录入，请连续清晰地说 5–10 秒")
                  : smartSpeech_ ? tr("正在采集，等待发言")
                  : profiles_.isEmpty() ? tr("正在识别，声纹库暂无档案") : tr("正在识别麦克风语音");
        emit changed();
    }
    if (smartSpeech_)
        scanLiveSpeech(false);
    if (liveMode_ != "enrollment")
        analyzeLiveWindow();
}

void SpeakerRecognitionService::scanLiveSpeech(bool flush)
{
    if (!discardCapture_) {
        const auto windows = smartSpeech_->scan(livePcm_, flush);
        for (const auto &window : windows)
            speechWindows_.enqueue({window.first,window.last,window.voiced});
    }
}

void SpeakerRecognitionService::stopLive()
{
    if (!capture_ || captureStopping_)
        return;
    captureStopping_ = true;
    capture_->requestInterruption();
    report(tr("正在停止麦克风"));
}

void SpeakerRecognitionService::captureFinished()
{
    std::exchange(capture_, nullptr)->deleteLater();
    captureStopping_ = false;
    inputLevel_ = -90.;
    emit visualsChanged();
    if (liveMode_ != "enrollment") {
        if (smartSpeech_)
            scanLiveSpeech(true);
        if (spectrum_) {
            auto image = spectrum_->update(livePcm_, true);
            if (!image.isNull()) {
                spectrogram_ = std::move(image);
                emit spectrumChanged();
            }
        }
        emit changed();
        analyzeLiveWindow();
        return;
    }
    liveMode_.clear();
    liveEnvelope_.clear();
    auto pcm = std::exchange(livePcm_, QByteArray());
    if (discardCapture_ || !session_->can("settings") || owner_ != session_->userId() || !captureError_.isEmpty()) {
        pendingProfile_.clear();
        report(discardCapture_ ? tr("录入已取消") : captureError_);
        return;
    }
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    status_ = tr("正在提取声纹特征");
    watcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_,pcm=std::move(pcm),stop=cancelled_] {
        return engine->runPcm(pcm, {}, 0, true, 0, false, {}, *stop);
    }));
    emit changed();
}

void SpeakerRecognitionService::analyzeLiveWindow()
{
    if (livePending_)
        return;
    const int remaining = int(livePcm_.size()) - liveCursor_;
    if (smartSpeech_ && !discardCapture_ && !speechWindows_.isEmpty()) {
        const auto window = speechWindows_.dequeue();
        const int first = qMax(window.first-Rate/12,liveCursor_);
        const int end = qMin(window.last+Rate/12,int(livePcm_.size()/2));
        liveCursor_ = end;
        const auto pcm = livePcm_.mid(first*2, (end-first)*2);
        const qint64 start = qint64(first)*1000/Rate;
        livePending_ = true;
        liveWatcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_,pcm,start,profiles=profiles_,
                                                         allowCreate=smartSaveError_.isEmpty(),stop=smartCancel_,
                                                         transcribe=metrics_.value("transcription").toBool()] {
            return engine->analyzeConversation(pcm,start,profiles,allowCreate,transcribe,*stop);
        }));
        return;
    }
    if (!smartSpeech_ && !discardCapture_ && (remaining >= LiveWindowBytes ||
                            (!capture_ && remaining >= 2 * VadFrame * MinVoicedFrames))) {
        const int length = qMin(remaining, LiveWindowBytes);
        const auto pcm = livePcm_.mid(liveCursor_, length);
        const qint64 start = qint64(liveCursor_) * 1000 / (2 * Rate);
        liveCursor_ += length;
        const qint64 end = qint64(liveCursor_) * 1000 / (2 * Rate);
        livePending_ = true;
        liveWatcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_,pcm,start,end,profiles=profiles_,
                                                         threshold=metrics_.value("threshold").toDouble()] {
            QElapsedTimer elapsed;
            elapsed.start();
            auto row = engine->classify(pcm, threshold, profiles);
            setSpan(row, start, end);
            row["inference_ms"] = elapsed.elapsed();
            return row;
        }));
        return;
    }
    if (!capture_) {
        const bool smart = bool(smartSpeech_);
        if (smart && !discardCapture_) {
            metrics_["analyzed_ms"] = metrics_.value("audio_ms");
            emit visualsChanged();
        }
        liveMode_.clear();
        livePcm_.clear();
        liveEnvelope_.clear();
        smartSpeech_.reset();
        spectrum_.reset();
        smartCancel_.reset();
        speechWindows_.clear();
        if (discardCapture_)
            report(tr("识别已停止"));
        else if (!captureError_.isEmpty())
            fail(captureError_);
        else if (!smartSaveError_.isEmpty())
            fail(smartSaveError_);
        else if (smart)
            report(tr("分析完成"));
        else if (metrics_.value("embeddings").toInt() == 0)
            report(tr("采集结束，未检测到足够的有效语音"));
        else
            report(tr("采集结束，已分析 %1 个有效语音窗口").arg(metrics_.value("embeddings").toInt()));
    }
}

void SpeakerRecognitionService::completeLiveWindow()
{
    livePending_ = false;
    if (!discardCapture_ && owner_ == session_->userId() && session_->can("inspect")) {
        auto row = liveWatcher_.result();
        const auto additions = row.value("additions").toJsonArray();
        QVariantList rows = smartSpeech_ ? row.value("segments").toList() : QVariantList{row};
        const auto bank = row.value("profile_bank");
        if (bank.isValid()) {
            if (saveProfiles(bank.toJsonArray())) {
                metrics_["new_profiles"] = metrics_.value("new_profiles").toInt() + additions.size();
            } else {
                smartSaveError_ = status_;
                for (auto &value : rows) {
                    auto segment = value.toMap();
                    if (segment.value("state") == "new") {
                        segment["track_id"] = segment.take("profile_id");
                        segment["state"] = "unknown";
                        segment["status"] = tr("建档未保存");
                        segment["speaker"] = tr("未确认发言人");
                        value = segment;
                    }
                }
            }
        }
        if (smartSpeech_) {
            auto overlaps = metrics_.value("overlaps").toList();
            overlaps.append(row.value("overlaps").toList());
            metrics_["overlaps"] = overlaps;
            metrics_["diarization"] = row.value("diarization");
            metrics_["analyzed_ms"] = qint64(liveCursor_)*1000/Rate;
            if (row.contains("error")) {
                smartSaveError_ = row.value("error").toString();
                fail(row.value("error").toString());
            }
        }
        segments_.append(rows);
        if (!rows.isEmpty())
            liveMatch_ = rows.last().toMap();
        const auto elapsed = row.value(smartSpeech_ ? "total_ms" : "inference_ms").toLongLong();
        metrics_["total_ms"] = metrics_.value("total_ms").toLongLong() + elapsed;
        const int embeddings = smartSpeech_ ? row.value("embeddings").toInt() : int(row.value("state") != "insufficient");
        if (embeddings > 0) {
            metrics_["embeddings"] = metrics_.value("embeddings").toInt() + embeddings;
            metrics_["embedding_ms"] = metrics_.value("embedding_ms").toLongLong() + elapsed;
        }
        emit visualsChanged();
        emit changed();
    }
    analyzeLiveWindow();
}

void SpeakerRecognitionService::complete()
{
    const auto result = watcher_.result();
    const bool aborted = !cancelled_ || cancelled_->load() || owner_ != session_->userId();
    cancelled_.reset();
    const auto id = std::exchange(pendingProfile_, QString());
    if (aborted) {
        report(tr("任务已停止"));
        return;
    }
    if (result.contains("error")) {
        fail(result.value("error").toString());
        return;
    }
    if (result.value("loaded").toBool()) {
        ready_ = true;
        whisperReady_ = result.value("whisper").toBool();
        status_ = tr("CAM++ 已就绪 · RK3588 CPU");
    } else if (!id.isEmpty()) {
        auto candidate = profiles_;
        const int index = profileIndex(id);
        auto profile = index < 0
            ? QJsonObject{{"id",id},{"name",pendingName_},{"language",pendingLanguage_}} : profiles_[index].toObject();
        auto samples = profile.value("samples").toArray();
        samples.append(QJsonArray::fromVariantList(result.value("embedding").toList()));
        profile["samples"] = samples;
        if (index < 0)
            candidate.append(profile);
        else
            candidate[index] = profile;
        if (!saveProfiles(candidate))
            return;
        status_ = tr("声纹已保存，用时 %1 ms").arg(result.value("elapsed_ms").toString());
    } else {
        segments_ = result.value("segments").toList();
        waveform_ = result.value("waveform").toList();
        spectrogram_ = result.value("spectrum").value<QImage>();
        metrics_ = result.value("metrics").toMap();
        emit spectrumChanged();
        const auto additions = result.value("additions").toJsonArray();
        const auto bank = result.value("profile_bank");
        if (bank.isValid()) {
            if (!session_->can("inspect") || !saveProfiles(bank.toJsonArray())) {
                segments_.clear();
                metrics_["embeddings"] = 0;
                emit visualsChanged();
                emit changed();
                return;
            }
            metrics_["new_profiles"] = additions.size();
        }
        emit visualsChanged();
        status_ = metrics_.value("smart").toBool() ? tr("分析完成")
                  : segments_.isEmpty() ? tr("未检测到有效语音")
                                     : tr("完成 %1 个时段的识别").arg(segments_.size());
    }
    emit changed();
}

QVariantList SpeakerRecognitionService::profiles() const
{
    QVariantList result;
    if (!session_->can("inspect"))
        return result;
    for (const auto &value : profiles_) {
        const auto entry = value.toObject();
        result.append(QVariantMap{{"id",entry.value("id").toString()},{"name",entry.value("name").toString()},
            {"language",entry.value("language").toString()},{"samples",entry.value("samples").toArray().size()},
            {"automatic",entry.contains("auto_number")}});
    }
    return result;
}

void SpeakerRecognitionService::readProfiles()
{
    QFile file(libraryPath_);
    if (!file.exists()) {
        profiles_ = {};
        storageValid_ = true;
        return;
    }
    if (!file.open(QIODevice::ReadOnly) || file.size()>4*1024*1024) {
        storageValid_ = false;
        return;
    }
    const auto data = QJsonDocument::fromJson(file.readAll()).object();
    storageValid_ = data.value("schema").toInt() == 1 && data.value("model").toString() == ModelName &&
        data.value("dimension").toInt() == Dimension && data.value("profiles").isArray();
    if (storageValid_)
        profiles_ = data.value("profiles").toArray();
}

bool SpeakerRecognitionService::saveProfiles(const QJsonArray &candidate)
{
    if (!storageValid_ || !QDir().mkpath(QFileInfo(libraryPath_).absolutePath()))
        return fail(tr("声纹库目录不可写。"));
    QLockFile lock(libraryPath_ + ".lock");
    if (!lock.tryLock())
        return fail(tr("声纹库正在保存，请稍后重试。"));
    const auto previous = profiles_;
    readProfiles();
    if (!storageValid_ || previous != profiles_)
        return fail(tr("声纹库已更新，请重新开始分析。"));
    QSaveFile file(libraryPath_);
    const auto json = QJsonDocument(QJsonObject{{"schema",1},{"model",ModelName},{"dimension",Dimension},
                                               {"profiles",candidate}}).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner) ||
        file.write(json) != json.size() || !file.commit())
        return fail(tr("声纹库保存失败。"));
    profiles_ = candidate;
    return true;
}

bool SpeakerRecognitionService::updateProfile(const QString &id, const QString &name, const QString &language)
{
    const int index = profileIndex(id);
    if (!session_->can("settings") || busy() || index < 0 || !validProfile(name,language))
        return report(tr("档案无法修改，请检查名称和语种代码。"));
    auto candidate = profiles_;
    auto entry = candidate[index].toObject();
    entry["name"] = name.trimmed();
    entry["language"] = language;
    candidate[index] = entry;
    if (!saveProfiles(candidate))
        return false;
    status_ = tr("已更新档案");
    emit changed();
    return true;
}

bool SpeakerRecognitionService::removeProfile(const QString &id)
{
    const int index = profileIndex(id);
    if (!session_->can("settings") || busy() || index < 0)
        return false;
    auto candidate = profiles_;
    candidate.removeAt(index);
    if (!saveProfiles(candidate))
        return false;
    operationError_.clear();
    status_ = tr("已删除档案");
    emit changed();
    return true;
}

bool SpeakerRecognitionService::exportResult(const QUrl &destination) const
{
    if (!session_->can("inspect") || !destination.isLocalFile() || segments_.isEmpty() ||
        QFileInfo(destination.toLocalFile()).suffix().compare("json",Qt::CaseInsensitive))
        return false;
    QSaveFile file(destination.toLocalFile());
    const auto json = QJsonDocument::fromVariant(QVariantMap{{"model",ModelName},{"metrics",metrics_},
                                                           {"segments",segments_}}).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(json)==json.size() && file.commit();
}
