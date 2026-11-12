#include "../src/services/AcousticImagingEngine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QtEndian>
#include <cmath>
#include <random>

namespace {
constexpr int Rate=16000;
constexpr double Pi=3.14159265358979323846;

QByteArray source(double azimuth, double elevation, double radiusMm)
{
    constexpr int frames=AcousticImagingEngine::Window;
    std::mt19937 rng(17);
    std::uniform_real_distribution<double> noise(-1,1);
    std::array<double,frames+48> data{};
    double low=0;
    for (auto &v : data) {
        low=.42*low+.58*noise(rng);
        v=low;
    }
    const double az=azimuth*Pi/180, el=elevation*Pi/180;
    const double x=std::sin(az)*std::cos(el), y=std::sin(el);
    QByteArray pcm(frames*12,Qt::Uninitialized);
    for (int frame=0;frame<frames;++frame) {
        for (int channel=0;channel<6;++channel) {
            const double angle=-channel*Pi/3;
            const double delta=(x*std::cos(angle)+y*std::sin(angle))*radiusMm*Rate/(1000*(331.3+.606*20));
            const double t=frame+24+delta;
            const int index=int(t);
            const double value=data[index]+(t-index)*(data[index+1]-data[index]);
            qToLittleEndian<qint16>(qint16(value*14000),pcm.data()+2*(6*frame+channel));
        }
    }
    return pcm;
}
}

int main(int argc,char **argv)
{
    QCoreApplication app(argc,argv);
    AcousticImagingEngine engine;
    AcousticArrayConfig geometry;
    geometry.smoothing=0;
    engine.configure(geometry,Rate);
    for (const auto &target : {QPointF(20,10),QPointF(-22,-8),QPointF(14,-12)}) {
        const auto pcm=source(target.x(),target.y(),geometry.radiusMm);
        const auto result=engine.analyze(pcm);
        const auto az=result.values.value("azimuth_deg").toDouble();
        const auto el=result.values.value("elevation_deg").toDouble();
        qInfo() << "Expected" << target << "observed" << az << el
                << "milliseconds" << result.values.value("processing_ms");
        if (!result.valid || result.heatmap.isNull() || std::abs(az-target.x())>4 || std::abs(el-target.y())>4 ||
            engine.beamform(pcm,result.arrayDirection).isEmpty())
            return 1;
    }
    QByteArray silence(AcousticImagingEngine::Window*12,0);
    if (engine.analyze(silence).values.value("state")!="below_gate")
        return 2;
    const auto sound=source(15,6,geometry.radiusMm);
    QByteArray repeated(sound.size(),0);
    for (int f=0;f<AcousticImagingEngine::Window;++f)
        for (int channel=0;channel<6;++channel) {
            repeated[12*f+2*channel]=sound[12*f];
            repeated[12*f+2*channel+1]=sound[12*f+1];
        }
    if (engine.analyze(repeated).values.value("state")!="duplicate_channels")
        return 3;
    geometry.yaw=15;
    engine.configure(geometry,Rate);
    const auto corrected=engine.analyze(source(20,4,geometry.radiusMm));
    if (!corrected.valid || std::abs(corrected.values.value("azimuth_deg").toDouble()-5)>4)
        return 4;
    return 0;
}
