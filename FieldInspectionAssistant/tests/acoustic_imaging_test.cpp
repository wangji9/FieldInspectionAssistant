#include "../src/services/AcousticImagingEngine.h"

#include <QCoreApplication>
#include <QDebug>
#include <QPolygonF>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <random>

namespace {
constexpr int Rate=16000;
constexpr double Pi=3.14159265358979323846;

QByteArray source(double azimuth, double elevation, double radiusMm, const QVector3D &pointM = {}, int toneHz = 0)
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
            const auto mic = QVector3D(std::cos(angle), std::sin(angle), 0) * (radiusMm / 1000.);
            const auto pathAdvance = pointM.isNull() ? x * mic.x() + y * mic.y() : pointM.length() - (pointM - mic).length();
            const double delta = pathAdvance * Rate / (331.3 + .606 * 20);
            const double t=frame+24+delta;
            const int index=int(t);
            const double value = toneHz > 0 ? std::sin(2 * Pi * toneHz * t / Rate)
                : data[index]+(t-index)*(data[index+1]-data[index]);
            qToLittleEndian<qint16>(qint16(value*14000),pcm.data()+2*(6*frame+channel));
        }
    }
    return pcm;
}

QByteArray twoTones(const AcousticArrayConfig &geometry, double firstAmplitude)
{
    constexpr int channels = AcousticImagingEngine::Channels;
    constexpr int count = AcousticImagingEngine::Window;
    const auto direction = [](double azimuth, double elevation) {
        const double az = azimuth * Pi / 180, el = elevation * Pi / 180;
        return QVector3D(std::sin(az) * std::cos(el), std::sin(el), std::cos(az) * std::cos(el));
    };
    const auto first = direction(17, 7), second = direction(-21, -9);
    QByteArray pcm(count * channels * 2, Qt::Uninitialized);
    for (int channel = 0; channel < channels; ++channel) {
        const double angle = -channel * Pi / 3;
        const QVector3D mic(std::cos(angle), std::sin(angle), 0);
        const double delay = geometry.radiusMm / (1000 * (331.3 + .606 * geometry.temperature));
        for (int frame = 0; frame < count; ++frame) {
            const double t = double(frame) / Rate;
            const double sample = firstAmplitude * std::sin(2 * Pi * 2513 * (t + delay * QVector3D::dotProduct(mic, first)))
                + .36 * std::sin(2 * Pi * 3800 * (t + delay * QVector3D::dotProduct(mic, second)));
            qToLittleEndian<qint16>(qint16(std::lround(sample * 32768)), pcm.data() + 2 * (channels * frame + channel));
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

    constexpr int channels = AcousticImagingEngine::Channels;
    QByteArray tones(AcousticImagingEngine::Window * channels * 2, Qt::Uninitialized);
    for (int frame = 0; frame < AcousticImagingEngine::Window; ++frame) {
        for (int channel = 0; channel < channels; ++channel) {
            const int hz = 1000 + 125 * channel;
            const auto amplitude = qint16(std::lround(16384 * std::sin(2 * Pi * hz * frame / Rate)));
            qToLittleEndian<qint16>(amplitude, tones.data() + 2 * (frame * channels + channel));
        }
    }
    const auto plots = engine.analyze(tones, true).plots;
    const auto time = plots.value("time").toMap().value("series").toList();
    const auto frequency = plots.value("frequency").toMap().value("series").toList();
    if (time.size() != channels || frequency.size() != channels)
        return 5;
    for (int channel = 0; channel < channels; ++channel) {
        const auto points = frequency.at(channel).toMap().value("native_points").value<QPolygonF>();
        if (points.size() != AcousticImagingEngine::Window / 2 + 1 ||
            time.at(channel).toMap().value("native_points").value<QPolygonF>().size() != AcousticImagingEngine::TimeWindow)
            return 6;
        const auto peak = *std::max_element(points.cbegin(), points.cend(), [](const QPointF &a, const QPointF &b) {
            return a.y() < b.y();
        });
        if (peak.x() != 1000 + 125 * channel || std::abs(peak.y() + 6.02) > .1)
            return 7;
    }
    const auto quiet = engine.analyze(silence, true);
    if (quiet.valid || quiet.plots.isEmpty())
        return 8;

    geometry.yaw = 0;
    const auto mixed = twoTones(geometry, .13);
    for (const auto &target : {QPoint(2513, 17), QPoint(3800, -21)}) {
        geometry.targetHz = target.x();
        engine.configure(geometry, Rate);
        const auto result = engine.analyze(mixed);
        const auto azimuth = result.values.value("azimuth_deg").toDouble();
        const auto elevation = result.values.value("elevation_deg").toDouble();
        qInfo() << "Target Hz" << target.x() << "azimuth" << azimuth << "elevation" << elevation;
        if (!result.valid || result.heatmap.isNull() || result.values.value("target_hz").toInt() != target.x()
            || std::abs(azimuth - target.y()) > 3 || std::abs(elevation - (target.y() > 0 ? 7 : -9)) > 3)
            return 9;
    }
    geometry.targetHz = 2513;
    engine.configure(geometry, Rate);
    const auto otherFrequency = engine.analyze(twoTones(geometry, 0));
    if (otherFrequency.valid || otherFrequency.heatmap.isNull() || otherFrequency.values.value("state") != "target_absent")
        return 10;
    const auto countHot = [](const QImage &map) {
        int pixels = 0;
        for (int y = 0; y < map.height(); ++y)
            for (int x = 0; x < map.width(); ++x)
                pixels += qRed(map.pixel(x, y)) > 160;
        return pixels;
    };
    engine.setDynamicRange(1.0);
    const auto narrow = engine.analyze(mixed);
    engine.setDynamicRange(15.0);
    const auto wide = engine.analyze(mixed);
    if (!narrow.valid || !wide.valid || countHot(narrow.heatmap) >= countHot(wide.heatmap)
        || narrow.values.value("x") != wide.values.value("x") || narrow.values.value("y") != wide.values.value("y"))
        return 11;
    geometry.timeDomain = true;
    engine.configure(geometry, Rate);
    for (const auto target : {QPointF(20, 10), QPointF(-22, -8)}) {
        const auto result = engine.analyze(source(target.x(), target.y(), geometry.radiusMm), true);
        const auto azimuth = result.values.value("azimuth_deg").toDouble();
        const auto elevation = result.values.value("elevation_deg").toDouble();
        qInfo() << "Time-domain" << target << "observed" << azimuth << elevation
                << "milliseconds" << result.values.value("processing_ms");
        if (!result.valid || result.heatmap.isNull() || result.plots.contains("frequency") ||
            std::abs(azimuth - target.x()) > 8 || std::abs(elevation - target.y()) > 8)
            return 12;
    }
    const auto quietMap = engine.analyze(silence);
    if (quietMap.valid || quietMap.heatmap.isNull() || countHot(quietMap.heatmap))
        return 13;
    geometry.timeDomain = false;
    geometry.mvdr = true;
    geometry.targetHz = 2513;
    engine.configure(geometry, Rate);
    for (int frame = 0; frame < 6; ++frame) {
        const auto result = engine.analyze(mixed, true);
        if (!result.valid || result.heatmap.isNull()
            || std::abs(result.values.value("azimuth_deg").toDouble() - 17) > 3
            || std::abs(result.values.value("elevation_deg").toDouble() - 7) > 3)
            return 14;
    }
    geometry.mvdr = false;
    geometry.targetHz = 0;
    engine.configure(geometry, Rate);
    const auto broadband = engine.analyze(source(-19, 11, geometry.radiusMm), true);
    if (!broadband.valid || broadband.heatmap.isNull()
        || std::abs(broadband.values.value("azimuth_deg").toDouble() + 19) > 3
        || std::abs(broadband.values.value("elevation_deg").toDouble() - 11) > 3)
        return 15;
    if (engine.analyze(silence).heatmap.isNull())
        return 16;

    // A sound at the optical centre is off-axis from the microphone ring.
    geometry.fixedMount = true;
    geometry.cameraOriginMm = {70, -30, 15};
    geometry.focusDepthM = .25;
    const QVector3D centreSource(.07, -.03, .265);
    for (int mode = 0; mode < 4; ++mode) {
        geometry.targetHz = mode == 0 || mode == 3 ? 2500 : 0;
        geometry.timeDomain = mode == 1;
        geometry.mvdr = mode == 3;
        engine.configure(geometry, Rate);
        const auto pcm = source(0, 0, geometry.radiusMm, centreSource, geometry.targetHz);
        const auto result = engine.analyze(pcm);
        const auto u = result.values.value("x").toDouble(), v = result.values.value("y").toDouble();
        qInfo() << "Fixed mount mode" << mode << "optical centre" << u << v;
        if (!result.valid || std::abs(u - .5) > .055 || std::abs(v - .5) > .055
            || (result.arraySourceM - centreSource).length() > .022
            || engine.beamform(pcm, result.arrayDirection, result.arraySourceM).isEmpty())
            return 17 + mode;
    }
    return 0;
}
