#pragma once

#include <QPointer>
#include <QColor>
#include <QQuickPaintedItem>
#include "../services/VisionLiveService.h"

class LiveVisionItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(VisionLiveService *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool boxes READ boxes WRITE setBoxes NOTIFY displayChanged)
    Q_PROPERTY(bool fillCrop READ fillCrop WRITE setFillCrop NOTIFY displayChanged)

public:
    using QQuickPaintedItem::QQuickPaintedItem;
    VisionLiveService *source() const { return source_; }
    void setSource(VisionLiveService *source);
    bool boxes() const { return boxes_; }
    void setBoxes(bool value);
    bool fillCrop() const { return fillCrop_; }
    void setFillCrop(bool value);
    void paint(QPainter *painter) override;

signals:
    void sourceChanged();
    void displayChanged();

private:
    struct Mark {
        int category;
        QRectF box, previousBox;
        QVariantList points, previousPoints;
        QString kind, label;
        QColor color;
        QPointF gaze, head, gazeOrigin, headOrigin;
        bool faceAngles = false;
    };
    void refreshAnnotations();
    qreal blendProgress() const;

    QPointer<VisionLiveService> source_;
    QMetaObject::Connection overlayConnection_;
    QMetaObject::Connection previewConnection_;
    QList<Mark> marks_;
    QVariantList lastMaskRows_;
    QSize marksSize_;
    QElapsedTimer transition_;
    bool interpolating_ = false;
    bool boxes_ = true;
    bool fillCrop_ = true;
};
