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
#include <QUuid>
#include <QtConcurrentRun>
#include <QtEndian>
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
constexpr float MinimumMargin = 0.05f;
const auto ModelName = QStringLiteral("3dspeaker_speech_campplus_sv_zh-cn_16k-common.onnx");
using Embedding = std::array<float, Dimension>;

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
    const int bins = qMin(640, samples);
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

QString timeLabel(qint64 ms)
{
    return QStringLiteral("%1:%2:%3.%4").arg(ms/3600000,2,10,QLatin1Char('0'))
        .arg(ms/60000%60,2,10,QLatin1Char('0')).arg(ms/1000%60,2,10,QLatin1Char('0'))
        .arg(ms%1000,3,10,QLatin1Char('0'));
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
        const auto loaded = load();
        if (!loaded.value("loaded").toBool())
            return loaded;
        QElapsedTimer elapsed;
        elapsed.start();
        const auto pcm = decode(path, start, length, cancelled);
        if (pcm.size() < 2*Rate)
            return error(QObject::tr("所选时段没有可解码音频，或音频不足 1 秒。"));
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
            {"source",QFileInfo(path).fileName()},{"transcription",transcribe}}}};
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
        emit changed();
    });
}

SpeakerRecognitionService::~SpeakerRecognitionService()
{
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
    status_ = tr("正在初始化 sherpa-onnx / CAM++");
    watcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_] { return engine->load(); }));
    emit changed();
}

void SpeakerRecognitionService::cancel()
{
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
    const auto index = profileIndex(id);
    if ((!id.isEmpty() && index < 0) || (index < 0 && profiles_.size() >= 250) ||
        (index >= 0 && profiles_[index].toObject().value("samples").toArray().size() >= MaxSamples) ||
        (index < 0 && !validProfile(name,language)))
        return report(tr("请检查档案名称、语种和样本数。每人最多 3 组样本。"));
    if (!storageValid_)
        return report(tr("声纹库读取失败，请修复文件后重试。"));
    pendingProfile_ = index >= 0 ? id : QUuid::createUuid().toString(QUuid::WithoutBraces);
    pendingName_ = name.trimmed();
    pendingLanguage_ = language;
    owner_ = session_->userId();
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    status_ = tr("正在提取声纹特征");
    watcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_,path,start,length,stop=cancelled_] {
        return engine->run(path,start,length,true,0,false,{},*stop);
    }));
    emit changed();
    return true;
}

bool SpeakerRecognitionService::analyze(const QUrl &source, double start, double length, double threshold, bool transcribe)
{
    if (busy() || !ready_ || !session_->can("inspect"))
        return false;
    const auto path = sourcePath(source);
    if (path.isEmpty() || !validRange(start,length,600) || !std::isfinite(threshold) || threshold < .45 || threshold > .99)
        return report(tr("请选择音频、有效时段和 0.45–0.99 的余弦阈值。"));
    if (!storageValid_ || (transcribe && !whisperReady_))
        return report(tr("声纹库或 Whisper 模型不可用。"));
    segments_.clear();
    waveform_.clear();
    metrics_.clear();
    owner_ = session_->userId();
    cancelled_ = std::make_shared<std::atomic_bool>(false);
    status_ = tr("正在分析选定音频");
    watcher_.setFuture(QtConcurrent::run(&pool_, [engine=engine_,path,start,length,threshold,transcribe,
                                                profiles=profiles_,stop=cancelled_] {
        return engine->run(path,start,length,false,threshold,transcribe,profiles,*stop);
    }));
    emit changed();
    return true;
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
        report(result.value("error").toString());
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
        status_ = segments_.isEmpty() ? tr("未检测到有效语音")
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
            {"language",entry.value("language").toString()},{"samples",entry.value("samples").toArray().size()}});
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
        return report(tr("声纹库目录不可写。"));
    QLockFile lock(libraryPath_ + ".lock");
    if (!lock.tryLock())
        return report(tr("声纹库正在保存，请稍后重试。"));
    const auto previous = profiles_;
    readProfiles();
    if (!storageValid_ || previous != profiles_)
        return report(tr("声纹库已更新，请重新选择档案。"));
    QSaveFile file(libraryPath_);
    const auto json = QJsonDocument(QJsonObject{{"schema",1},{"model",ModelName},{"dimension",Dimension},
                                               {"profiles",candidate}}).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner) ||
        file.write(json) != json.size() || !file.commit())
        return report(tr("声纹库保存失败。"));
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
