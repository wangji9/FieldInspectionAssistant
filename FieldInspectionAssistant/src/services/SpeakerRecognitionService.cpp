#include "SpeakerRecognitionService.h"
#include "../app/SessionManager.h"

#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QtConcurrentRun>
#include <QtEndian>
#include <alsa/asoundlib.h>
#include <fvad.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
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
constexpr int SmartMinVoicedFrames = 30;
constexpr int SmartEnrollVoicedFrames = 40;
constexpr int SmartPauseFrames = 25;
constexpr int SmartWindowFrames = 160;
constexpr double SmartMatchThreshold = .60;
constexpr double SmartNoveltyThreshold = .45;
constexpr int LiveWindowBytes = 2 * Rate * 24 / 10;
constexpr int CaptureFrames = Rate * 80 / 1000;
constexpr int WaveformBins = 640;
constexpr int LiveRecognitionSeconds = 120;
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

bool validProfile(const QString &name, const QString &language)
{
    static const QRegularExpression code(QStringLiteral("^[a-z]{2,3}$"));
    return !name.trimmed().isEmpty() && name.trimmed().size() <= 80 &&
        (language.isEmpty() || code.match(language).hasMatch());
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

QString timeLabel(qint64 ms)
{
    return QStringLiteral("%1:%2:%3.%4").arg(ms/3600000,2,10,QLatin1Char('0'))
        .arg(ms/60000%60,2,10,QLatin1Char('0')).arg(ms/1000%60,2,10,QLatin1Char('0'))
        .arg(ms%1000,3,10,QLatin1Char('0'));
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
            const bool speech = fvad_process(vad_.get(), samples.data(), samples.size()) > 0
                && squares > qint64(90)*90*VadFrame;
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
};

class SpeakerRecognitionEngine
{
public:
    SpeakerRecognitionEngine()
        : root_(qEnvironmentVariable("FIELDOPS_SPEAKER_HOME", "/userdata/fieldops-speaker"))
    {}
    ~SpeakerRecognitionEngine() {
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
        return {{"loaded",true},{"whisper",whisperAvailable()}};
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

    QVariantMap smartWindow(const QByteArray &pcm, const SmartWindow &window, const QJsonArray &profiles,
                            qint64 base, bool allowCreate, QJsonObject &created)
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
            const auto score = row.value("similarity");
            const bool novel = !score.isValid() || score.toDouble() < SmartNoveltyThreshold;
            if (row.value("state") != "matched") {
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

    QVariantMap smartFile(const QString &path, double start, double length, bool transcribe, QJsonArray profiles,
                          bool allowCreate, const std::atomic_bool &cancelled)
    {
        QElapsedTimer elapsed;
        elapsed.start();
        const auto pcm = decode(path, start, length, cancelled);
        if (pcm.isEmpty())
            return error(QObject::tr("无法解码选定音频。"));
        SmartSpeech speech;
        const auto windows = speech.scan(pcm, true, &cancelled);
        QVariantList rows;
        QJsonArray additions;
        qint64 embeddingMs = 0;
        int embeddings = 0;
        const auto base = qRound64(start*1000);
        for (const auto &window : windows) {
            if (cancelled)
                return {};
            QJsonObject created;
            auto row = smartWindow(pcm, window, profiles, base, allowCreate, created);
            if (!created.isEmpty()) {
                additions.append(created);
                profiles.append(created);
            }
            if (row.value("state") != "insufficient") {
                ++embeddings;
                embeddingMs += row.value("inference_ms").toLongLong();
            }
            if (transcribe && row.value("state") == "matched" && !row.value("language").toString().isEmpty()) {
                const auto value = whisper(pcm, window.first, window.last, row.value("language").toString(), cancelled);
                row["transcript"] = value;
                row["transcript_status"] = value.isEmpty() ? "empty" : "completed";
            }
            rows.append(row);
        }
        return {{"segments",rows},{"waveform",waveformEnvelope(pcm)},{"additions",additions},
                {"metrics",QVariantMap{{"smart",true},{"total_ms",elapsed.elapsed()},{"embedding_ms",embeddingMs},
                {"embeddings",embeddings},{"offset_ms",base},{"audio_ms",qint64(pcm.size())*1000/(2*Rate)},
                {"threshold",SmartMatchThreshold},{"source",QFileInfo(path).fileName()},{"transcription",transcribe}}}};
    }

private:
    template<typename Function>
    bool resolve(Function &function, const char *symbol) {
        function = reinterpret_cast<Function>(library_.resolve(symbol));
        return function;
    }

    bool embed(const QByteArray &pcm, int first, int last, Embedding &out)
    {
        const auto *stream = createStream_(extractor_);
        if (!stream)
            return false;
        QVector<float> samples(last-first);
        for (int i=0; i<samples.size(); ++i)
            samples[i] = qFromLittleEndian<qint16>(pcm.constData()+2*(first+i))/32768.0f;
        accept_(stream, Rate, samples.constData(), int(samples.size()));
        inputFinished_(stream);
        const auto *result = isReady_(extractor_, stream) ? compute_(extractor_, stream) : nullptr;
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
        operationError_.clear();
        emit visualsChanged();
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
    return analyzeFile(source, start, length, SmartMatchThreshold, transcribe, true);
}

bool SpeakerRecognitionService::analyzeFile(const QUrl &source, double start, double length, double threshold,
                                          bool transcribe, bool smart)
{
    if (busy() || !ready_ || !session_->can("inspect"))
        return false;
    const auto path = sourcePath(source);
    if (path.isEmpty() || !validRange(start,length,600) || !std::isfinite(threshold) || threshold < .45 || threshold > .99)
        return fail(tr("请选择可读的音频和有效时段。"));
    if (!storageValid_ || (transcribe && !whisperReady_))
        return fail(tr("声纹库或 Whisper 模型不可用。"));
    operationError_.clear();
    segments_.clear();
    waveform_.clear();
    metrics_.clear();
    liveMatch_.clear();
    emit visualsChanged();
    owner_ = session_->userId();
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    status_ = tr("正在分析选定音频");
    watcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_,path,start,length,threshold,transcribe,smart,
                                                profiles=profiles_,stop=cancelled_] {
        if (smart)
            return engine->smartFile(path,start,length,transcribe,profiles,true,*stop);
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
    if (busy() || !ready_ || !session_->can("inspect"))
        return false;
    if (!storageValid_)
        return fail(tr("声纹库无法读取。"));
    return startCapture(QStringLiteral("smart"), SmartMatchThreshold);
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

bool SpeakerRecognitionService::startCapture(const QString &mode, double threshold)
{
    if (captureGuard_ && captureGuard_())
        return fail(tr("音频设备正在使用，请结束当前采集后再试。"));
    operationError_.clear();
    owner_ = session_->userId();
    liveMode_ = mode;
    captureStopping_ = false;
    discardCapture_ = false;
    captureError_.clear();
    smartSaveError_.clear();
    speechWindows_.clear();
    smartSpeech_ = mode == "smart" ? std::make_unique<SmartSpeech>() : nullptr;
    liveCursor_ = 0;
    liveMatch_.clear();
    segments_.clear();
    waveform_.clear();
    liveEnvelope_.clear();
    livePcm_.clear();
    inputLevel_ = -90.;
    metrics_ = {{"source",tr("实时麦克风")},{"live",true},{"operation",mode},{"offset_ms",0},{"audio_ms",0},{"threshold",threshold},
                {"embeddings",0},{"embedding_ms",0},{"total_ms",0},{"new_profiles",0},{"smart",bool(smartSpeech_)}};
    const int seconds = mode == "enrollment" ? LiveEnrollmentSeconds : LiveRecognitionSeconds;
    livePcm_.reserve(seconds * 2 * Rate);
    liveEnvelope_.reserve(seconds * Rate / (CaptureFrames / 2));
    status_ = tr("正在打开麦克风");
    capture_ = QThread::create([this, seconds] {
        const auto error = captureMicrophone(seconds, [this](QByteArray block) {
            QMetaObject::invokeMethod(this, [this, block = std::move(block)] { receiveAudio(block); }, Qt::QueuedConnection);
        });
        if (!error.isEmpty())
            QMetaObject::invokeMethod(this, [this, error] { captureError_ = error; }, Qt::QueuedConnection);
    });
    capture_->setParent(this);
    connect(capture_, &QThread::finished, this, &SpeakerRecognitionService::captureFinished);
    capture_->start();
    emit visualsChanged();
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
        const auto pcm = livePcm_.mid(window.first*2, (window.last-window.first)*2);
        const qint64 start = qint64(window.first)*1000/Rate;
        livePending_ = true;
        liveWatcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_,pcm,start,window,profiles=profiles_,
                                                         allowCreate=smartSaveError_.isEmpty()] {
            QJsonObject created;
            auto row = engine->smartWindow(pcm, {0,window.last-window.first,window.voiced},
                                           profiles, start, allowCreate, created);
            if (!created.isEmpty())
                row["_new_profile"] = created;
            return row;
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
        liveMode_.clear();
        livePcm_.clear();
        liveEnvelope_.clear();
        smartSpeech_.reset();
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
        const auto created = row.take("_new_profile").toJsonObject();
        if (!created.isEmpty()) {
            auto candidate = profiles_;
            candidate.append(created);
            if (saveProfiles(candidate)) {
                metrics_["new_profiles"] = metrics_.value("new_profiles").toInt() + 1;
            } else {
                smartSaveError_ = status_;
                row["state"] = "unknown";
                row["status"] = tr("建档未保存");
                row.remove("profile_id");
                row.remove("speaker");
            }
        }
        segments_.append(row);
        liveMatch_ = row;
        const auto elapsed = row.value("inference_ms").toLongLong();
        metrics_["total_ms"] = metrics_.value("total_ms").toLongLong() + elapsed;
        if (row.value("state") != "insufficient") {
            metrics_["embeddings"] = metrics_.value("embeddings").toInt() + 1;
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
        metrics_ = result.value("metrics").toMap();
        const auto additions = result.value("additions").toJsonArray();
        if (!additions.isEmpty()) {
            auto candidate = profiles_;
            for (const auto &entry : additions)
                candidate.append(entry);
            if (!session_->can("inspect") || !saveProfiles(candidate)) {
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
