#include "AcousticImagingEngine.h"

#include <QElapsedTimer>
#include <QtEndian>
#include <opencv2/core.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace {
constexpr int MicCount = AcousticImagingEngine::Channels;
constexpr int N = AcousticImagingEngine::Window;
constexpr int PairCount = MicCount*(MicCount-1)/2;
constexpr int Interpolation = 4;
constexpr int CorrelationSize = N*Interpolation;
constexpr int Width = 72;
constexpr int Height = 44;
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

QRgb palette(float value)
{
    constexpr std::array<std::array<int,3>,6> rgb{{
        {38,44,118}, {37,103,165}, {37,152,176}, {58,176,146}, {173,191,79}, {246,208,55}
    }};
    const float n = std::clamp(value,0.f,1.f)*(rgb.size()-1);
    const int first = std::min(int(n),int(rgb.size()-2));
    const float t = n-first;
    const auto c = [&](int index) { return int(rgb[first][index]*(1-t)+rgb[first+1][index]*t); };
    const int opacity = int(230*std::clamp((value-.22f)/.78f,0.f,1.f));
    return qRgba(c(0),c(1),c(2),opacity);
}

qint16 sample(const QByteArray &pcm,int frame,int channel)
{
    return qFromLittleEndian<qint16>(pcm.constData()+2*(frame*MicCount+channel));
}
}

struct AcousticImagingEngine::State {
    AcousticArrayConfig config;
    int sampleRate = 16000;
    float samplesPerMeter = 0;
    int firstBin = 0, lastBin = 0;
    std::array<QVector3D,MicCount> positions;
    std::array<std::pair<int,int>,PairCount> pairs;
    std::array<float,N> window;
    std::array<cv::Mat,MicCount> input, spectra;
    std::array<cv::Mat,PairCount> correlations;
    cv::Mat cross;
    std::vector<std::array<float,PairCount>> delays;
    std::vector<QVector3D> arrayDirections, cameraDirections;
    std::vector<float> previous;

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
    const double r=configuration.radiusMm/1000.;
    for (int slot=0;slot<MicCount;++slot) {
        const auto angle=(configuration.channelOneDegrees+(configuration.clockwise?-60.:60.)*slot)*Degree;
        s.positions[configuration.channelOrder[slot]]=QVector3D(r*std::cos(angle),r*std::sin(angle),0);
    }

    s.delays.resize(Width*Height);
    s.arrayDirections.resize(Width*Height);
    s.cameraDirections.resize(Width*Height);
    s.previous.clear();
    const float tx=std::tan(configuration.horizontalFov*Degree/2), ty=std::tan(configuration.verticalFov*Degree/2);
    for (int y=0;y<Height;++y) {
        for (int x=0;x<Width;++x) {
            const int cell=y*Width+x;
            const auto camera=QVector3D((2.f*x/(Width-1)-1)*tx,(1-2.f*y/(Height-1))*ty,1).normalized();
            const auto direction=rotateToArray(camera,configuration);
            s.arrayDirections[cell]=direction;
            s.cameraDirections[cell]=camera;
            for (int p=0;p<PairCount;++p) {
                const auto [i,j]=s.pairs[p];
                s.delays[cell][p]=QVector3D::dotProduct(s.positions[j]-s.positions[i],direction)*
                    s.samplesPerMeter*Interpolation;
            }
        }
    }
}

AcousticImageFrame AcousticImagingEngine::analyze(const QByteArray &pcm)
{
    AcousticImageFrame output;
    auto &s=*state_;
    const int frames=int(pcm.size()/(2*MicCount));
    if (frames<N || s.delays.empty() || s.firstBin>s.lastBin)
        return output;
    QElapsedTimer time;
    time.start();
    const int start=frames-N;
    std::array<double,MicCount> squares{};
    std::array<bool,MicCount> distinct{};
    for (int t=0;t<N;++t) {
        const auto first=sample(pcm,start+t,0);
        for (int mic=0;mic<MicCount;++mic) {
            const auto raw=sample(pcm,start+t,mic);
            const auto x=raw/32768.f;
            squares[mic]+=x*x;
            distinct[mic] |= raw!=first;
            s.input[mic].at<float>(t)=x*s.window[t];
        }
    }
    QVariantList levels;
    for (const double sum : squares)
        levels.append(dbfs(sum,N));
    const auto level=dbfs(std::accumulate(squares.cbegin(),squares.cend(),0.),N*MicCount);
    output.values={{"level_dbfs",level},{"channels_dbfs",levels}};
    if (level<s.config.gateDbfs || std::count(distinct.begin(),distinct.end(),true)<3) {
        s.previous.clear();
        output.values["state"]=level<s.config.gateDbfs ? "below_gate" : "duplicate_channels";
        return output;
    }

    for (int i=0;i<MicCount;++i)
        cv::dft(s.input[i],s.spectra[i],cv::DFT_COMPLEX_OUTPUT);
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
    std::vector<float> scores(s.delays.size());
    const float previous=s.previous.size()==scores.size()?float(s.config.smoothing):0.f;
    for (size_t cell=0;cell<s.delays.size();++cell) {
        float value=0;
        for (int p=0;p<PairCount;++p) {
            const float lag=s.delays[cell][p];
            const int lo=int(std::floor(lag));
            const auto *r=s.correlations[p].ptr<float>();
            const auto a=r[(CorrelationSize+lo)%CorrelationSize];
            const auto b=r[(CorrelationSize+lo+1)%CorrelationSize];
            value+=a+(b-a)*(lag-lo);
        }
        scores[cell]=(1-previous)*value/PairCount+(previous>0?previous*s.previous[cell]:0);
    }

    const auto maximum=std::max_element(scores.begin(),scores.end());
    const int peak=int(std::distance(scores.begin(),maximum));
    const auto average=std::accumulate(scores.cbegin(),scores.cend(),0.f)/scores.size();
    const float prominence=*maximum-average;
    output.values["response"]=*maximum;
    output.values["contrast"]=prominence;
    output.values["processing_ms"]=time.nsecsElapsed()/1e6;
    if (*maximum<.08f || prominence<.035f) {
        s.previous.clear();
        output.values["state"]="diffuse";
        return output;
    }
    const auto camera=s.cameraDirections[peak];
    output.values["azimuth_deg"]=std::atan2(camera.x(),camera.z())/Degree;
    output.values["elevation_deg"]=std::atan2(camera.y(),std::hypot(camera.x(),camera.z()))/Degree;
    output.values["x"]=double(peak%Width)/(Width-1);
    output.values["y"]=double(peak/Width)/(Height-1);
    output.values["state"]="localized";
    output.arrayDirection=s.arrayDirections[peak];
    output.valid=true;
    output.heatmap=QImage(Width,Height,QImage::Format_ARGB32);
    for (int y=0;y<Height;++y) {
        auto *line=reinterpret_cast<QRgb*>(output.heatmap.scanLine(y));
        for (int x=0;x<Width;++x)
            line[x]=palette((scores[y*Width+x]-average)/prominence);
    }
    s.previous=std::move(scores);
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
