#pragma once

#include <QImage>
#include <QVariantMap>
#include <memory>

class SegmentationOverlay {
public:
    struct Region {
        QVariantMap annotation;
        int maskIndex;
        bool confirmed;
    };

    SegmentationOverlay();
    ~SegmentationOverlay();
    void reset();
    bool advance(const QImage &frame, qint64 capturedAt);
    void correct(const QByteArray &labels, const QSize &size, const QList<Region> &regions,
                 qint64 capturedAt, bool follow, bool resetTargets = false, const QImage &referenceImage = {});
    QImage image() const;
    int count() const;
    QVariantList annotations() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
