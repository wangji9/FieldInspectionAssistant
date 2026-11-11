#pragma once

#include <QQuickPaintedItem>
#include <QImage>

// 摄像头实时预览项：监听 CameraService::frameReady，直接在 QML 中绘制。
class CameraPreviewItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QImage frame READ frame WRITE setFrame NOTIFY frameChanged)
public:
    explicit CameraPreviewItem(QQuickItem *parent = nullptr);

    QImage frame() const { return frame_; }
    void setFrame(const QImage &frame);

protected:
    void paint(QPainter *painter) override;

signals:
    void frameChanged();

private:
    QImage frame_;
};
