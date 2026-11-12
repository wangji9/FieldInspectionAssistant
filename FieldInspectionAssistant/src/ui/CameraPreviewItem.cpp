#include "CameraPreviewItem.h"
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGOpacityNode>
#include <QSGTexture>
namespace {
struct PreviewNode:QSGNode {
    QSGImageNode *image = nullptr;
    QSGImageNode *overlay = nullptr;
    QSGOpacityNode *overlayOpacity = nullptr;
    qint64 key = 0, overlayKey = 0;
};

void updateImageNode(QQuickWindow *window, QSGNode *parent, QSGImageNode *&node,
                     qint64 &key, const QImage &image, const QRectF &target,
                     const QRectF &source, bool smooth)
{
    if (image.isNull()) {
        delete node;
        node = nullptr;
        key = 0;
        return;
    }
    if (!node || key != image.cacheKey()) {
        const auto options = image.hasAlphaChannel() ? QQuickWindow::TextureHasAlphaChannel : QQuickWindow::TextureIsOpaque;
        auto *texture = window->createTextureFromImage(image, options);
        if (!texture)
            return;
        if (!node) {
            node = window->createImageNode();
            node->setOwnsTexture(true);
            parent->prependChildNode(node);
        }
        node->setTexture(texture);
        key = image.cacheKey();
    }
    node->setFiltering(smooth ? QSGTexture::Linear : QSGTexture::Nearest);
    node->setSourceRect(source);
    node->setRect(target);
}
}
CameraPreviewItem::CameraPreviewItem(QQuickItem *parent):QQuickItem(parent){setFlag(ItemHasContents,true);connect(this,&QQuickItem::visibleChanged,this,[this]{if(isVisible())update();});connect(this,&QQuickItem::smoothChanged,this,[this]{update();});}
void CameraPreviewItem::setFrame(const QImage &frame)
{
    if (frame.cacheKey() == frame_.cacheKey())
        return;
    const bool geometryChanged = !frameAspectSize_.isValid() && frame.size() != frame_.size();
    frame_ = frame;
    if (isVisible())
        update();
    emit frameChanged();
    if (geometryChanged)
        emit contentRectChanged();
}
void CameraPreviewItem::setFillCrop(bool fill)
{
    if (fillCrop_ == fill)
        return;
    fillCrop_ = fill;
    update();
    emit fillCropChanged();
    emit contentRectChanged();
}

void CameraPreviewItem::setOverlayFrame(const QImage &frame)
{
    if (frame.cacheKey() == overlayFrame_.cacheKey())
        return;
    overlayFrame_ = frame;
    update();
    emit overlayFrameChanged();
}

void CameraPreviewItem::setOverlayOpacity(qreal opacity)
{
    const auto bounded = qBound(0., opacity, 1.);
    if (qFuzzyCompare(bounded, overlayOpacity_))
        return;
    overlayOpacity_ = bounded;
    update();
    emit overlayOpacityChanged();
}

void CameraPreviewItem::setFrameAspectSize(const QSize &size)
{
    if (size == frameAspectSize_)
        return;
    frameAspectSize_ = size;
    update();
    emit frameAspectSizeChanged();
    emit contentRectChanged();
}

QRectF CameraPreviewItem::frameRect(const QRectF &bounds, const QSize &source, bool fillCrop)
{
    QRectF frame(QPointF(), QSizeF(source).scaled(bounds.size(),
        fillCrop ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio));
    frame.moveCenter(bounds.center());
    return frame;
}

void CameraPreviewItem::geometryChange(const QRectF&next,const QRectF&old){QQuickItem::geometryChange(next,old);if(next.size()!=old.size()){update();emit contentRectChanged();}}
QSGNode *CameraPreviewItem::updatePaintNode(QSGNode *old,UpdatePaintNodeData*){
    const auto bounds = boundingRect();
    if ((frame_.isNull() && overlayFrame_.isNull()) || bounds.isEmpty() || !window()) {
        delete old;
        return nullptr;
    }
    auto *node = old ? static_cast<PreviewNode *>(old) : new PreviewNode;
    const auto frame = frame_.isNull() ? bounds : contentRect();
    const auto target = fillCrop_ ? bounds : frame;
    const QRectF uv((target.left() - frame.left()) / frame.width(),
                    (target.top() - frame.top()) / frame.height(),
                    target.width() / frame.width(), target.height() / frame.height());
    updateImageNode(window(), node, node->image, node->key, frame_, target,
                    QRectF(uv.x() * frame_.width(), uv.y() * frame_.height(),
                           uv.width() * frame_.width(), uv.height() * frame_.height()), smooth());
    if (!overlayFrame_.isNull() || node->overlayOpacity) {
        if (!node->overlayOpacity) {
            node->overlayOpacity = new QSGOpacityNode;
            node->appendChildNode(node->overlayOpacity);
        }
        node->overlayOpacity->setOpacity(overlayOpacity_);
        const auto w = overlayFrame_.width() - 1, h = overlayFrame_.height() - 1;
        // Acoustic grid endpoints are defined at the first and last pixel centres.
        updateImageNode(window(), node->overlayOpacity, node->overlay, node->overlayKey, overlayFrame_, target,
                        QRectF(.5 + uv.x() * w, .5 + uv.y() * h, uv.width() * w, uv.height() * h), true);
    }
    return node;
}
