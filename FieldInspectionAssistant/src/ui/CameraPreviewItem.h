#pragma once
#include <QQuickItem>
#include <QImage>

class CameraPreviewItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QImage frame READ frame WRITE setFrame NOTIFY frameChanged)
    Q_PROPERTY(bool fillCrop READ fillCrop WRITE setFillCrop NOTIFY fillCropChanged)
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY contentRectChanged)
    Q_PROPERTY(QSize frameAspectSize READ frameAspectSize WRITE setFrameAspectSize NOTIFY frameAspectSizeChanged)
public:
    explicit CameraPreviewItem(QQuickItem *parent=nullptr);
    QImage frame()const{return frame_;}
    void setFrame(const QImage &frame);
    bool fillCrop() const { return fillCrop_; }
    void setFillCrop(bool fill);
    QSize frameAspectSize() const { return frameAspectSize_; }
    void setFrameAspectSize(const QSize &size);
    QRectF contentRect() const {
        return frameRect(boundingRect(), frameAspectSize_.isValid() ? frameAspectSize_ : frame_.size(), fillCrop_);
    }
    static QRectF frameRect(const QRectF &bounds, const QSize &source, bool fillCrop);
protected:
    QSGNode *updatePaintNode(QSGNode *oldNode,UpdatePaintNodeData*)override;
    void geometryChange(const QRectF &newGeometry,const QRectF &oldGeometry)override;
signals:
    void frameChanged();
    void contentRectChanged();
    void fillCropChanged();
    void frameAspectSizeChanged();
private:
    QImage frame_;
    QSize frameAspectSize_;
    bool fillCrop_ = false;
};
