#include "CameraPreviewItem.h"
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
namespace {
struct PreviewNode:QSGNode {
    QSGImageNode *image=nullptr;qint64 key=0;
    explicit PreviewNode(QQuickWindow *window){image=window->createImageNode();image->setOwnsTexture(true);image->setFiltering(QSGTexture::Linear);appendChildNode(image);}
};
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
    if(frame_.isNull()||bounds.isEmpty()||!window()){delete old;return nullptr;}
    auto *node=static_cast<PreviewNode*>(old);if(!node)node=new PreviewNode(window());
    if(node->key!=frame_.cacheKey()){
        const auto options = frame_.hasAlphaChannel() ? QQuickWindow::TextureHasAlphaChannel : QQuickWindow::TextureIsOpaque;
        auto *texture=window()->createTextureFromImage(frame_,options);if(!texture)return node;
        // QSGImageNode owns the texture and releases its previous texture here.
        node->image->setTexture(texture);node->key=frame_.cacheKey();
    }
    node->image->setFiltering(smooth() ? QSGTexture::Linear : QSGTexture::Nearest);
    const auto frame = frameRect(bounds, frameAspectSize_.isEmpty() ? frame_.size() : frameAspectSize_, fillCrop_);
    if (fillCrop_) {
        const qreal sx = frame_.width() / frame.width();
        const qreal sy = frame_.height() / frame.height();
        node->image->setSourceRect(QRectF((bounds.left() - frame.left()) * sx,
            (bounds.top() - frame.top()) * sy, bounds.width() * sx, bounds.height() * sy));
        node->image->setRect(bounds);
    } else {
        node->image->setSourceRect(QRectF(QPointF(), frame_.size()));
        node->image->setRect(frame);
    }
    return node;
}
