#pragma once

#include <QByteArray>
#include <QImage>
#include <QVariantMap>
#include <QVector3D>
#include <array>
#include <memory>

struct AcousticArrayConfig {
    double radiusMm = 35.0;
    double channelOneDegrees = 0.0;
    bool clockwise = true;
    std::array<int,6> channelOrder{0,1,2,3,4,5};
    double horizontalFov = 70.0, verticalFov = 44.0;
    double yaw = 0.0, pitch = 0.0, roll = 0.0;
    double temperature = 20.0;
    int lowerHz = 450, upperHz = 3800;
    double gateDbfs = -55.0;
    double smoothing = 0.55;
};

struct AcousticImageFrame {
    bool valid = false;
    QImage heatmap;
    QVector3D arrayDirection;
    QVariantMap values;
    QVariantMap plots;
};

class AcousticImagingEngine {
public:
    static constexpr int Channels = 6;
    static constexpr int Window = 2048;
    AcousticImagingEngine();
    ~AcousticImagingEngine();
    void configure(const AcousticArrayConfig &configuration, int sampleRate);
    AcousticImageFrame analyze(const QByteArray &interleaved, bool includePlots = false);
    QByteArray beamform(const QByteArray &interleaved, const QVector3D &direction) const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
