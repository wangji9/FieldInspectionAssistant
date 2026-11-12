#include "AcousticImagingEngine.h"

#include <QElapsedTimer>
#include <QPolygonF>
#include <QtEndian>
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>

namespace {
constexpr int MicCount = AcousticImagingEngine::Channels;
constexpr int N = AcousticImagingEngine::Window;
constexpr int PairCount = MicCount*(MicCount-1)/2;
constexpr int Interpolation = 4;
constexpr int CorrelationSize = N*Interpolation;
constexpr int Width = 72;
constexpr int Height = 44;
constexpr int TargetHalfBins = 0;
constexpr int TargetBins = 2 * TargetHalfBins + 1;
constexpr int TimeWindow = AcousticImagingEngine::TimeWindow;
constexpr int IntegrationWindow = 512;
constexpr float Pi = 3.14159265358979323846f;
constexpr double Degree = Pi/180.0;

float dbfs(double squares, int count)
{
    return float(20*std::log10(std::max(std::sqrt(squares/count),1e-6)));
}

QVector3D rotateToArray(const QVector3D &v, const AcousticArrayConfig &c)
{
    const auto sr=std::sin(c.roll*Degree), cr=std::cos(c.roll*Degree);
    const auto sp=std::sin(c.pitch*Degree), cp=std::cos(c.pitch*Degree);
    const auto sy=std::sin(c.yaw*Degree), cy=std::cos(c.yaw*Degree);
    const double x=cr*v.x()-sr*v.y(), y=sr*v.x()+cr*v.y(), z=v.z();
    const double py=cp*y-sp*z, pz=sp*y+cp*z;
    return QVector3D(cy*x+sy*pz, py, -sy*x+cy*pz).normalized();
}

QRgb palette(float value, float strength = 1.f)
{
    constexpr std::array<std::array<int,3>,7> rgb{{
        {0,0,128}, {0,0,255}, {0,219,255}, {84,255,104}, {255,255,0}, {255,104,0}, {187,0,0}
    }};
    const float n = std::clamp(value,0.f,1.f)*(rgb.size()-1);
    const int first = std::min(int(n),int(rgb.size()-2));
    const float t = n-first;
    const auto c = [&](int index) { return int(rgb[first][index]*(1-t)+rgb[first+1][index]*t); };
    const int opacity = qRound(60 + 182 * value * strength);
    return qRgba(c(0),c(1),c(2),opacity);
}

QImage powerHeatmap(const std::vector<float> &power, float peak, double rangeDb, float strength = 1.f)
{
    QImage image(Width, Height, QImage::Format_ARGB32);
    if (peak <= 0) {
        image.fill(palette(0));
        return image;
    }
    // Equivalent to clipping 10*log10(power) at peak dB minus rangeDb.
    const float floorRatio = std::pow(10., -rangeDb / 10.);
    for (int y = 0; y < Height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < Width; ++x) {
            const float ratio = std::max(floorRatio, power[y * Width + x] / peak);
            line[x] = palette(std::clamp(float(1 + 10 * std::log10(ratio) / rangeDb), 0.f, 1.f), strength);
        }
    }
    return image;
}

qint16 sample(const QByteArray &pcm,int frame,int channel)
{
    return qFromLittleEndian<qint16>(pcm.constData()+2*(frame*MicCount+channel));
}

QVariantMap capturePlots(const QByteArray &pcm, int start, int rate,
                         const std::array<cv::Mat, MicCount> &spectra, double windowSum, bool includeSpectrum)
{
    QVariantList timeSeries, frequencySeries;
    QPolygonF timeAverage, frequencyAverage;
    timeAverage.reserve(TimeWindow);
    frequencyAverage.reserve(N / 2 + 1);
    std::array<double, N / 2 + 1> sumMagnitude{};
    std::array<double, TimeWindow> sumSample{};
    const int waveStart = start + N - TimeWindow;
    const auto toDb = [](double magnitude) { return 20 * std::log10(std::max(1e-6, magnitude)); };
    for (int mic = 0; mic < MicCount; ++mic) {
        QPolygonF time;
        time.reserve(TimeWindow);
        for (int frame = 0; frame < TimeWindow; ++frame) {
            const int value = sample(pcm, waveStart + frame, mic);
            time.append(QPointF(frame, value));
            sumSample[frame] += value;
        }
        timeSeries.append(QVariantMap{{"native_points", QVariant::fromValue(std::move(time))}});
        if (!includeSpectrum)
            continue;
        QPolygonF frequency;
        frequency.reserve(N / 2 + 1);
        const auto *bins = spectra[mic].ptr<cv::Vec2f>();
        for (int bin = 0; bin <= N / 2; ++bin) {
            const int scale = bin == 0 || bin == N / 2 ? 1 : 2;
            const double magnitude = scale * std::hypot(bins[bin][0], bins[bin][1]) / windowSum;
            frequency.append(QPointF(double(bin) * rate / N, toDb(magnitude)));
            sumMagnitude[bin] += magnitude;
        }
        frequencySeries.append(QVariantMap{{"native_points", QVariant::fromValue(std::move(frequency))}});
    }
    for (int frame = 0; frame < TimeWindow; ++frame)
        timeAverage.append(QPointF(frame, sumSample[frame] / MicCount));
    QVariantMap plots{{"time", QVariantMap{{"series", timeSeries}, {"average", QVariant::fromValue(std::move(timeAverage))},
        {"xRange", QVariantList{0, TimeWindow - 1}}, {"xLabel", QStringLiteral("采样点")},
        {"yLabel", QStringLiteral("幅值")}}}};
    if (includeSpectrum) {
        for (int bin = 0; bin <= N / 2; ++bin)
            frequencyAverage.append(QPointF(double(bin) * rate / N, toDb(sumMagnitude[bin] / MicCount)));
        plots["frequency"] = QVariantMap{{"series", frequencySeries}, {"average", QVariant::fromValue(std::move(frequencyAverage))},
            {"xRange", QVariantList{0, rate / 2.}}, {"xLabel", QStringLiteral("频率 (Hz)")},
            {"yLabel", QStringLiteral("幅值 (dBFS)")}, {"method_id", "fft"}};
    }
    return plots;
}
}

struct AcousticImagingEngine::State {
    AcousticArrayConfig config;
    int sampleRate = 16000;
    float samplesPerMeter = 0;
    int firstBin = 0, lastBin = 0;
    int targetFirstBin = 0, targetLastBin = 0;
    std::array<QVector3D,MicCount> positions;
    std::array<std::pair<int,int>,PairCount> pairs;
    std::array<float,N> window;
    double windowSum = 0;
    std::array<cv::Mat,MicCount> input, spectra;
    std::array<cv::Mat,PairCount> correlations;
    cv::Mat cross;
    struct Lag { int lo, hi; float fraction; };
    std::vector<std::array<Lag,PairCount>> delays;
    std::vector<QVector3D> cameraDirections;
    std::vector<float> previous;
    using Steering = std::array<std::array<cv::Vec2f, MicCount>, TargetBins>;
    std::vector<Steering> steering;
    std::vector<std::array<float, MicCount>> timeShifts;
    std::array<std::array<float, N>, MicCount> raw;
    std::vector<float> crossPrefix;
    int shiftLimit = 0;
    std::vector<float> scores;
    using Complex = std::complex<float>;
    std::array<std::array<Complex, MicCount>, MicCount> covariance{};
    bool covarianceReady = false;

    State() : cross(CorrelationSize,1,CV_32FC2)
    {
        for (int i=0; i<MicCount; ++i)
            input[i].create(N,1,CV_32F);
        int p=0;
        for (int i=0; i<MicCount; ++i)
            for (int j=i+1; j<MicCount; ++j)
                pairs[p++]={i,j};
        for (int i=0;i<N;++i)
            window[i]=0.5f-0.5f*std::cos(2*Pi*i/(N-1));
        windowSum = std::accumulate(window.cbegin(), window.cend(), 0.);
    }
};

AcousticImagingEngine::AcousticImagingEngine() : state_(std::make_unique<State>()) {}
AcousticImagingEngine::~AcousticImagingEngine() = default;

void AcousticImagingEngine::configure(const AcousticArrayConfig &configuration,int sampleRate)
{
    auto &s=*state_;
    s.config=configuration;
    s.sampleRate=sampleRate;
    s.samplesPerMeter=sampleRate/(331.3+0.606*s.config.temperature);
    s.firstBin=std::max(1,int(std::ceil(double(s.config.lowerHz)*N/sampleRate)));
    s.lastBin=std::min(N/2-1,int(double(s.config.upperHz)*N/sampleRate));
    const int targetBin = qRound(double(configuration.targetHz) * N / sampleRate);
    s.targetFirstBin = std::max(1, targetBin - TargetHalfBins);
    s.targetLastBin = std::min(N / 2 - 1, targetBin + TargetHalfBins);
    const double r=configuration.radiusMm/1000.;
    for (int slot=0;slot<MicCount;++slot) {
        const auto angle=(configuration.channelOneDegrees+(configuration.clockwise?-60.:60.)*slot)*Degree;
        s.positions[configuration.channelOrder[slot]]=QVector3D(r*std::cos(angle),r*std::sin(angle),0);
    }

    s.delays.resize(Width*Height);
    s.cameraDirections.resize(Width*Height);
    s.previous.clear();
    s.covarianceReady = false;
    s.scores.resize(Width * Height);
    s.steering.resize(!configuration.timeDomain && configuration.targetHz > 0 ? Width * Height : 0);
    s.timeShifts.resize(configuration.timeDomain ? Width * Height : 0);
    s.shiftLimit = int(std::ceil(r * s.samplesPerMeter)) + 1;
    s.crossPrefix.resize(configuration.timeDomain ? (4 * s.shiftLimit + 1) * (N + 1) : 0);
    const float tx=std::tan(configuration.horizontalFov*Degree/2), ty=std::tan(configuration.verticalFov*Degree/2);
    for (int y=0;y<Height;++y) {
        for (int x=0;x<Width;++x) {
            const int cell=y*Width+x;
            const auto camera=QVector3D((2.f*x/(Width-1)-1)*tx,(1-2.f*y/(Height-1))*ty,1).normalized();
            const auto direction=rotateToArray(camera,configuration);
            s.cameraDirections[cell]=camera;
            if (configuration.timeDomain) {
                for (int mic = 0; mic < MicCount; ++mic)
                    s.timeShifts[cell][mic] = -QVector3D::dotProduct(s.positions[mic], direction) * s.samplesPerMeter;
                continue;
            }
            if (!s.steering.empty()) {
                for (int mic = 0; mic < MicCount; ++mic) {
                    const float delay = QVector3D::dotProduct(s.positions[mic], direction) * s.samplesPerMeter;
                    for (int bin = s.targetFirstBin; bin <= s.targetLastBin; ++bin) {
                        const float phase = 2 * Pi * bin * delay / N;
                        s.steering[cell][bin - s.targetFirstBin][mic] = {std::cos(phase), std::sin(phase)};
                    }
                }
                continue;
            }
            for (int p=0;p<PairCount;++p) {
                const auto [i,j]=s.pairs[p];
                const auto delay = QVector3D::dotProduct(s.positions[j]-s.positions[i],direction)
                    * s.samplesPerMeter * Interpolation;
                const int lo = int(std::floor(delay));
                s.delays[cell][p] = {(CorrelationSize + lo) % CorrelationSize,
                    (CorrelationSize + lo + 1) % CorrelationSize, delay - lo};
            }
        }
    }
}

void AcousticImagingEngine::setDynamicRange(double rangeDb)
{
    state_->config.dynamicRangeDb = std::clamp(rangeDb, .1, 60.);
}

AcousticImageFrame AcousticImagingEngine::analyze(const QByteArray &pcm, bool includePlots, bool includeHeatmap)
{
    AcousticImageFrame output;
    auto &s=*state_;
    const int frames=int(pcm.size()/(2*MicCount));
    const bool targeted = !s.steering.empty();
    const bool timeDomain = s.config.timeDomain;
    if (frames < N || s.scores.empty() || (targeted && s.targetFirstBin > s.targetLastBin) ||
        (!targeted && !timeDomain && s.firstBin > s.lastBin))
        return output;
    QElapsedTimer time;
    time.start();
    const int start=frames-N;
    if (timeDomain && !includeHeatmap) {
        if (includePlots)
            output.plots = capturePlots(pcm, start, s.sampleRate, s.spectra, s.windowSum, false);
        return output;
    }
    std::array<double,MicCount> squares{}, sums{};
    std::array<bool,MicCount> distinct{};
    for (int t=0;t<N;++t) {
        const auto first=sample(pcm,start+t,0);
        for (int mic=0;mic<MicCount;++mic) {
            const auto raw=sample(pcm,start+t,mic);
            const auto x=raw/32768.f;
            squares[mic]+=x*x;
            distinct[mic] |= raw!=first;
            s.input[mic].at<float>(t)=x*s.window[t];
            if (timeDomain) {
                s.raw[mic][t] = x;
                sums[mic] += x;
            }
        }
    }
    QVariantList levels;
    for (const double sum : squares)
        levels.append(dbfs(sum,N));
    const auto level=dbfs(std::accumulate(squares.cbegin(),squares.cend(),0.),N*MicCount);
    output.values={{"level_dbfs",level},{"channels_dbfs",levels}};
    output.values["audio_offset_samples"] = timeDomain ? IntegrationWindow / 2 + N / 32 : N / 2;
    const bool belowGate = level < s.config.gateDbfs;
    const bool duplicate = std::count(distinct.begin(), distinct.end(), true) < 3;
    if (!timeDomain && (includePlots || targeted || (!belowGate && !duplicate))) {
        for (int i = 0; i < MicCount; ++i)
            cv::dft(s.input[i], s.spectra[i], cv::DFT_COMPLEX_OUTPUT);
    }
    if (includePlots)
        output.plots = capturePlots(pcm, start, s.sampleRate, s.spectra, s.windowSum, !timeDomain);
    if (!includeHeatmap)
        return output;
    if (duplicate || belowGate) {
        s.previous.clear();
        s.covarianceReady = false;
        output.values["state"] = belowGate ? "below_gate" : "duplicate_channels";
        output.heatmap = powerHeatmap(s.scores, 0, s.config.dynamicRangeDb);
        return output;
    }

    auto &scores = s.scores;
    bool targetAbsent = false;
    double strengthDb = level;
    if (timeDomain) {
        std::fill(scores.begin(), scores.end(), 0.f);
        constexpr int first = N - IntegrationWindow - N / 32;
        // (sum x)^2 - sum x^2 equals twice the sum of pair products.
        // Prefix sums let each direction reuse the exact shifted-window pair energy.
        const int maxLag = 2 * s.shiftLimit;
        for (const auto &[i, j] : s.pairs) {
            const float meanI = sums[i] / N, meanJ = sums[j] / N;
            for (int lag = -maxLag; lag <= maxLag; ++lag) {
                auto *prefix = s.crossPrefix.data() + (lag + maxLag) * (N + 1);
                const int begin = first - s.shiftLimit;
                const int end = first + IntegrationWindow + s.shiftLimit;
                prefix[begin] = 0.f;
                for (int t = begin; t < end; ++t)
                    prefix[t + 1] = prefix[t] + (s.raw[i][t] - meanI) * (s.raw[j][t + lag] - meanJ);
            }
            for (size_t cell = 0; cell < scores.size(); ++cell) {
                const auto &shift = s.timeShifts[cell];
                const float delay = shift[j] - shift[i];
                const int lag = qRound(delay);
                const float fraction = delay - lag;
                const int start = first + qRound(shift[i]);
                const auto integral = [&](int delta) {
                    const auto *prefix = s.crossPrefix.data() + (lag + delta + maxLag) * (N + 1);
                    return prefix[start + IntegrationWindow] - prefix[start];
                };
                // Interpolate the lag response rather than attenuating the
                // waveforms with two independent fractional-sample filters.
                const float before = integral(-1), at = integral(0), after = integral(1);
                scores[cell] += at + .5f * fraction * (after - before + fraction * (before + after - 2 * at));
            }
        }
        const float scale = 4.f / s.sampleRate;
        for (auto &score : scores)
            score = std::abs(score) * scale;
    } else if (targeted) {
        std::array<std::array<cv::Vec2f, MicCount>, TargetBins> bins{};
        double targetPower = 0;
        double noisePower = 0;
        int noiseBins = 0;
        for (int bin = s.targetFirstBin - 16; bin <= s.targetLastBin + 16; ++bin) {
            if (bin < 1 || bin >= N / 2)
                continue;
            const bool selected = bin >= s.targetFirstBin && bin <= s.targetLastBin;
            if (!selected && bin >= s.targetFirstBin - 3 && bin <= s.targetLastBin + 3)
                continue;
            double power = 0;
            for (int mic = 0; mic < MicCount; ++mic) {
                const auto value = s.spectra[mic].at<cv::Vec2f>(bin);
                power += value.dot(value);
                if (selected)
                    bins[bin - s.targetFirstBin][mic] = value;
            }
            if (selected)
                targetPower += power;
            else {
                noisePower += power;
                ++noiseBins;
            }
        }
        const double bandLevel = 10 * std::log10(std::max(1e-12, 2 * targetPower / (N * s.windowSum * .75 * MicCount)));
        strengthDb = bandLevel;
        output.values["target_hz"] = s.config.targetHz;
        output.values["target_dbfs"] = bandLevel;
        targetAbsent = bandLevel < s.config.gateDbfs || targetPower < 2.5 * TargetBins * noisePower / std::max(1, noiseBins);
        cv::Matx<float, 2 * MicCount, 2 * MicCount> inverse;
        bool useMvdr = false;
        if (s.config.mvdr && !targetAbsent) {
            const float weight = s.covarianceReady ? .25f : 1.f;
            const float normalize = float(1. / targetPower);
            cv::Matx<float, 2 * MicCount, 2 * MicCount> loaded;
            for (int i = 0; i < MicCount; ++i) {
                const State::Complex a(bins[0][i][0], bins[0][i][1]);
                for (int j = 0; j < MicCount; ++j) {
                    const State::Complex b(bins[0][j][0], bins[0][j][1]);
                    auto &entry = s.covariance[i][j];
                    entry = (1 - weight) * entry + weight * normalize * a * std::conj(b);
                    const float loading = i == j ? .08f / MicCount : 0.f;
                    loaded(i, j) = loaded(i + MicCount, j + MicCount) = entry.real() + loading;
                    loaded(i, j + MicCount) = -entry.imag();
                    loaded(i + MicCount, j) = entry.imag();
                }
            }
            s.covarianceReady = true;
            useMvdr = cv::invert(loaded, inverse, cv::DECOMP_CHOLESKY) != 0;
        } else if (targetAbsent) {
            s.covarianceReady = false;
        }
        float diagonal = 0;
        if (useMvdr)
            for (int i = 0; i < MicCount; ++i)
                diagonal += inverse(i, i);
        const float norm = targetPower > 0 ? float(1. / (MicCount * targetPower)) : 0.f;
        for (size_t cell = 0; cell < scores.size(); ++cell) {
            if (useMvdr) {
                const auto &steering = s.steering[cell][0];
                float denominator = diagonal;
                for (const auto &[i, j] : s.pairs) {
                    const float real = steering[i].dot(steering[j]);
                    const float imag = steering[i][0] * steering[j][1] - steering[i][1] * steering[j][0];
                    denominator += 2 * (inverse(i, j) * real - inverse(i + MicCount, j) * imag);
                }
                scores[cell] = MicCount / std::max(denominator, 1e-6f);
                continue;
            }
            float power = 0;
            for (int bin = 0; bin <= s.targetLastBin - s.targetFirstBin; ++bin) {
                float real = 0, imaginary = 0;
                for (int mic = 0; mic < MicCount; ++mic) {
                    const auto &signal = bins[bin][mic];
                    const auto &phase = s.steering[cell][bin][mic];
                    real += signal[0] * phase[0] + signal[1] * phase[1];
                    imaginary += signal[1] * phase[0] - signal[0] * phase[1];
                }
                power += real * real + imaginary * imaginary;
            }
            scores[cell] = power * norm;
        }
    } else {
        const float scale=CorrelationSize/(2.f*(s.lastBin-s.firstBin+1));
        for (int p=0;p<PairCount;++p) {
            const auto [i,j]=s.pairs[p];
            s.cross.setTo(cv::Scalar(0,0));
            for (int k=s.firstBin;k<=s.lastBin;++k) {
                const auto a=s.spectra[i].at<cv::Vec2f>(k), b=s.spectra[j].at<cv::Vec2f>(k);
                const float re=a[0]*b[0]+a[1]*b[1], im=a[1]*b[0]-a[0]*b[1];
                const float amp=std::sqrt(re*re+im*im);
                const float weight=amp>1e-10f ? scale/amp : 0.f;
                s.cross.at<cv::Vec2f>(k)={re*weight,im*weight};
                s.cross.at<cv::Vec2f>(CorrelationSize-k)={re*weight,-im*weight};
            }
            cv::dft(s.cross,s.correlations[p],cv::DFT_INVERSE|cv::DFT_REAL_OUTPUT|cv::DFT_SCALE);
        }
        for (size_t cell=0;cell<s.delays.size();++cell) {
            float value=0;
            for (int p=0;p<PairCount;++p) {
                const auto lag = s.delays[cell][p];
                const auto *r=s.correlations[p].ptr<float>();
                value += r[lag.lo] + (r[lag.hi] - r[lag.lo]) * lag.fraction;
            }
            scores[cell]=value/PairCount;
        }
    }
    if (s.previous.size() == scores.size()) {
        const float previous = float(s.config.smoothing);
        for (size_t cell = 0; cell < scores.size(); ++cell)
            scores[cell] = (1 - previous) * scores[cell] + previous * s.previous[cell];
    }

    const auto maximum=std::max_element(scores.begin(),scores.end());
    const int peak=int(std::distance(scores.begin(),maximum));
    const auto average=std::accumulate(scores.cbegin(),scores.cend(),0.f)/scores.size();
    const float prominence=*maximum-average;
    output.values["response"]=*maximum;
    output.values["contrast"]=prominence;
    output.values["processing_ms"]=time.nsecsElapsed()/1e6;
    const auto strength = float(std::clamp((strengthDb - s.config.gateDbfs + 12) / 30, .2, 1.));
    output.heatmap = powerHeatmap(scores, *maximum, s.config.dynamicRangeDb, strength);
    s.previous = scores;
    const bool diffuse = (timeDomain || s.config.mvdr) ? *maximum <= 0 || prominence < .035f * *maximum
                                    : *maximum < (targeted ? .45f : .08f) || prominence < .035f;
    if (belowGate || targetAbsent || diffuse) {
        output.values["state"] = belowGate ? "below_gate" : targetAbsent ? "target_absent" : "diffuse";
        return output;
    }
    if (targeted) {
        // A second separated maximum of comparable strength is an ambiguous tone.
        for (int y = 1; y < Height - 1; ++y) {
            for (int x = 1; x < Width - 1; ++x) {
                const int cell = y * Width + x;
                const float value = scores[cell];
                if (value >= *maximum - .12f * prominence &&
                    QVector3D::dotProduct(s.cameraDirections[cell], s.cameraDirections[peak]) < .94f &&
                    value > scores[cell-1] && value > scores[cell+1] &&
                    value > scores[cell-Width] && value > scores[cell+Width]) {
                    output.values["state"] = "ambiguous";
                    return output;
                }
            }
        }
    }
    const auto refine = [&](int axis, int limit, int stride) {
        if (axis == 0 || axis == limit - 1)
            return double(axis);
        const float before = scores[peak - stride], after = scores[peak + stride];
        const float curvature = before + after - 2 * *maximum;
        const float offset = curvature < -1e-12f ? std::clamp(.5f * (before - after) / curvature, -.5f, .5f) : 0.f;
        return double(axis) + offset;
    };
    const double x = refine(peak % Width, Width, 1) / (Width - 1);
    const double y = refine(peak / Width, Height, Width) / (Height - 1);
    const auto camera = QVector3D((2*x-1)*std::tan(s.config.horizontalFov*Degree/2),
                                 (1-2*y)*std::tan(s.config.verticalFov*Degree/2), 1).normalized();
    output.values["azimuth_deg"]=std::atan2(camera.x(),camera.z())/Degree;
    output.values["elevation_deg"]=std::atan2(camera.y(),std::hypot(camera.x(),camera.z()))/Degree;
    output.values["x"] = x;
    output.values["y"] = y;
    output.values["state"]="localized";
    output.arrayDirection=rotateToArray(camera, s.config);
    const auto &array = output.arrayDirection;
    output.values["array_azimuth_deg"] = std::atan2(array.x(), array.z()) / Degree;
    output.values["array_elevation_deg"] = std::atan2(array.y(), std::hypot(array.x(), array.z())) / Degree;
    output.valid=true;
    return output;
}

QByteArray AcousticImagingEngine::beamform(const QByteArray &pcm,const QVector3D &direction) const
{
    const auto &s=*state_;
    const int frames=int(pcm.size()/(2*MicCount));
    std::array<float,MicCount> delay;
    int margin=1;
    for (int mic=0;mic<MicCount;++mic) {
        delay[mic]=-QVector3D::dotProduct(s.positions[mic],direction)*s.samplesPerMeter;
        margin=std::max(margin,int(std::ceil(std::abs(delay[mic])))+1);
    }
    if (frames<=2*margin)
        return {};
    QByteArray out(2*(frames-2*margin),Qt::Uninitialized);
    for (int t=margin;t<frames-margin;++t) {
        float total=0;
        for (int mic=0;mic<MicCount;++mic) {
            const int index=int(std::floor(delay[mic]));
            const float a=sample(pcm,t+index,mic), b=sample(pcm,t+index+1,mic);
            total+=a+(b-a)*(delay[mic]-index);
        }
        qToLittleEndian<qint16>(qint16(std::clamp(std::lround(total/MicCount),-32768l,32767l)),
                                out.data()+2*(t-margin));
    }
    return out;
}
