#include "LiveVisionItem.h"
#include "CameraPreviewItem.h"

#include <QGuiApplication>
#include <QPainter>
#include <cmath>
#include <utility>

namespace {
constexpr qreal TransitionMs = 40;

QRectF interpolateBox(const QRectF &from, const QRectF &to, qreal progress)
{
    return QRectF(from.topLeft() * (1.0 - progress) + to.topLeft() * progress,
                  from.bottomRight() * (1.0 - progress) + to.bottomRight() * progress);
}

QPointF interpolatePoint(const QVariantList &previous, const QVariantList &current, int index, qreal progress)
{
    const auto point = current[index].toPointF();
    if (index < previous.size() && previous[index].isValid())
        return previous[index].toPointF() * (1.0 - progress) + point * progress;
    return point;
}

void paintLandmarks(QPainter &painter, const QVariantList &points, const QString &kind,
                    const QRectF &frame, qreal sx, qreal sy, const QColor &color,
                    const QVariantList &previous, qreal progress)
{
    static constexpr int body[][2] = {{0,1},{0,2},{1,3},{2,4},{5,6},{5,7},{7,9},{6,8},{8,10},
                                      {5,11},{6,12},{11,12},{11,13},{13,15},{12,14},{14,16}};
    static constexpr int hand[][2] = {{0,1},{1,2},{2,3},{3,4},{0,5},{5,6},{6,7},{7,8},
                                      {5,9},{9,10},{10,11},{11,12},{9,13},{13,14},{14,15},
                                      {15,16},{13,17},{0,17},{17,18},{18,19},{19,20}};
    const auto map = [&](int index) {
        const auto p = interpolatePoint(previous, points, index, progress);
        return QPointF(frame.x() + p.x() * sx, frame.y() + p.y() * sy);
    };
    const auto line = [&](int a, int b) {
        if (a < points.size() && b < points.size() && points[a].isValid() && points[b].isValid())
            painter.drawLine(map(a), map(b));
    };
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
    if (kind == "body") {
        for (const auto &edge : body)
            line(edge[0], edge[1]);
    } else if (kind == "hand") {
        for (const auto &edge : hand)
            line(edge[0], edge[1]);
    } else if (kind == "faceMesh") {
        QPolygonF mesh;
        mesh.reserve(points.size());
        for (int i = 0; i < points.size(); ++i)
            if (points[i].isValid()) mesh.append(map(i));
        QColor tint(color);
        tint.setAlpha(150);
        painter.setPen(QPen(tint, 1.25, Qt::SolidLine, Qt::RoundCap));
        painter.drawPoints(mesh);
        painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap));
        static constexpr int details[][2] = {{33,160},{160,158},{158,133},{133,153},{153,144},{144,33},
            {362,385},{385,387},{387,263},{263,373},{373,380},{380,362},{61,0},{0,291},{291,17},{17,61}};
        for (const auto &edge : details)
            line(edge[0], edge[1]);
        return;
    }
    painter.setPen(QPen(QColor(10, 18, 24), 1));
    painter.setBrush(color);
    for (int i = 0; i < points.size(); ++i)
        if (points[i].isValid())
            painter.drawEllipse(map(i), 2.8, 2.8);
    painter.setBrush(Qt::NoBrush);
}
}

void LiveVisionItem::setSource(VisionLiveService *source)
{
    if (source_ == source)
        return;
    disconnect(overlayConnection_);
    disconnect(previewConnection_);
    marks_.clear();
    lastMaskRows_.clear();
    source_ = source;
    if (source_) {
        overlayConnection_ = connect(source_, &VisionLiveService::overlayChanged, this, &LiveVisionItem::refreshAnnotations);
        previewConnection_ = connect(source_, &VisionLiveService::previewChanged, this, [this] {
            if (interpolating_) {
                interpolating_ = blendProgress() < 1.0;
                update();
            }
        });
    }
    refreshAnnotations();
    emit sourceChanged();
}

qreal LiveVisionItem::blendProgress() const
{
    return interpolating_ ? qMin(1.0, transition_.elapsed() / TransitionMs) : 1.0;
}

void LiveVisionItem::refreshAnnotations()
{
    const bool visible = source_ && source_->overlayVisible();
    const auto rows = visible ? source_->annotationRows() : QVariantList();
    const bool masked = visible && !source_->maskFrame().isNull();
    if (masked && !rows.isEmpty() && rows == lastMaskRows_ && marksSize_ == source_->resultSize())
        return;
    lastMaskRows_ = masked ? rows : QVariantList();
    const auto progress = blendProgress();
    const auto previous = std::exchange(marks_, QList<Mark>());
    interpolating_ = false;
    if (visible) {
        const bool canBlend = source_->smoothLiveAnnotations() && marksSize_ == source_->resultSize() && !masked;
        marksSize_ = source_->resultSize();
        QList<bool> used(previous.size(), false);
        for (const auto &value : rows) {
            const auto row = value.toMap();
            Mark mark{row.value("id").toInt(), row.value("box").toRect(), {}, row.value("points").toList(), {},
                      row.value("landmarkKind").toString(),
                      row.value("label").toString() + "  " + row.value("confidence").toString(),
                      QColor(row.value("color").toString())};
            mark.faceAngles = row.contains("gazeYaw");
            if (mark.kind == "faceMesh")
                mark.label = row.value("label").toString();
            const auto gaze = row.value("gazeVector").toList();
            const auto head = row.value("headVector").toList();
            mark.gaze = {gaze.value(0).toDouble(), gaze.value(1).toDouble()};
            mark.head = {head.value(0).toDouble(), head.value(1).toDouble()};
            mark.gazeOrigin = row.value("gazeOrigin").toPointF();
            mark.headOrigin = row.value("headOrigin").toPointF();
            mark.previousBox = mark.box;
            int best = -1;
            qreal bestOverlap = 0.25;
            if (canBlend) {
                for (int i = 0; i < previous.size(); ++i) {
                    const auto &last = previous[i];
                    if (used[i] || last.category != mark.category)
                        continue;
                    const auto intersection = mark.box.intersected(last.box);
                    const auto area = intersection.width() * intersection.height();
                    const auto overlap = area / (mark.box.width() * mark.box.height()
                        + last.box.width() * last.box.height() - area);
                    if (overlap > bestOverlap) {
                        bestOverlap = overlap;
                        best = i;
                    }
                }
            }
            if (best >= 0) {
                const auto &last = previous[best];
                used[best] = true;
                mark.previousBox = interpolateBox(last.previousBox, last.box, progress);
                for (int i = 0; i < last.points.size(); ++i)
                    mark.previousPoints.append(last.points[i].isValid()
                        ? QVariant(interpolatePoint(last.previousPoints, last.points, i, progress)) : QVariant());
                interpolating_ |= mark.previousBox != mark.box || mark.previousPoints != mark.points;
            }
            marks_.append(std::move(mark));
        }
    }
    if (interpolating_)
        transition_.start();
    update();
}

void LiveVisionItem::setBoxes(bool value)
{
    if (boxes_ == value)
        return;
    boxes_ = value;
    emit displayChanged();
    update();
}

void LiveVisionItem::setFillCrop(bool value)
{
    if (fillCrop_ == value)
        return;
    fillCrop_ = value;
    emit displayChanged();
    update();
}

void LiveVisionItem::paint(QPainter *painter)
{
    const auto bounds = boundingRect();
    if (!source_ || !source_->overlayVisible() || bounds.isEmpty() || !source_->resultSize().isValid())
        return;
    const auto frame = CameraPreviewItem::frameRect(bounds, source_->previewFrame().size(), fillCrop_);
    const auto visibleFrame = frame.intersected(bounds);
    const bool drawBoxes = boxes_ || source_->maskFrame().isNull();
    const auto progress = blendProgress();
    const qreal sx = frame.width() / source_->resultSize().width();
    const qreal sy = frame.height() / source_->resultSize().height();
    auto font = QGuiApplication::font();
    font.setPixelSize(12);
    painter->setFont(font);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setClipRect(visibleFrame);
    QList<QRectF> labels;
    const auto collides = [&labels](const QRectF &rect) {
        for (const auto &other : labels)
            if (rect.intersects(other))
                return true;
        return false;
    };
    for (const auto &mark : marks_) {
        const auto box = interpolateBox(mark.previousBox, mark.box, progress);
        QRectF target(frame.left() + box.left() * sx, frame.top() + box.top() * sy,
                      box.width() * sx, box.height() * sy);
        if (!visibleFrame.intersects(target))
            continue;
        if (drawBoxes) {
            painter->setPen(QPen(QColor(10, 18, 24, 200), 3.0));
            painter->drawRect(target);
            painter->setPen(QPen(mark.color, 1.4));
            painter->drawRect(target);
        }
        if (!mark.points.isEmpty())
            paintLandmarks(*painter, mark.points, mark.kind, frame, sx, sy, mark.color, mark.previousPoints, progress);
        if (mark.faceAngles) {
            const auto point = [&](const QPointF &location) {
                return frame.topLeft() + QPointF(location.x() * sx, location.y() * sy);
            };
            const auto drawDirection = [&](const QPointF &origin, const QPointF &projection, const QColor &color) {
                const auto length = qBound(24.0, target.width() * .7, 110.0);
                const auto end = origin + projection * length;
                QLineF direction(origin, end);
                painter->setPen(QPen(color, 2.1, Qt::SolidLine, Qt::RoundCap));
                if (direction.length() < 3) {
                    painter->drawEllipse(origin, 3, 3);
                    return;
                }
                painter->drawLine(direction);
                const QPointF unit = (end - origin) / direction.length(), normal(-unit.y(), unit.x());
                painter->drawLine(end, end - unit * 6 + normal * 3);
                painter->drawLine(end, end - unit * 6 - normal * 3);
            };
            drawDirection(point(mark.gazeOrigin), mark.gaze, QColor("#f7da86"));
            drawDirection(point(mark.headOrigin), mark.head, QColor("#9dcbfa"));
        }
        const auto &caption = mark.label;
        const auto width = qMin(visibleFrame.width(), painter->fontMetrics().horizontalAdvance(caption) + 12.0);
        const qreal height = 19;
        const qreal x = qBound(visibleFrame.left(), target.left(), visibleFrame.right() - width);
        QRectF tag(x, qMax(visibleFrame.top(), target.top() - height), width, height);
        for (int n = 0; n < 4 && collides(tag); ++n)
            tag.translate(0, height + 1);
        if (tag.bottom() > visibleFrame.bottom() || collides(tag))
            continue;
        labels.append(tag);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(15, 26, 34, 232));
        painter->drawRoundedRect(tag, 3, 3);
        painter->setBrush(Qt::NoBrush);
        painter->fillRect(QRectF(tag.left() + 2, tag.top() + 3, 2, height - 6), mark.color);
        painter->setPen(Qt::white);
        painter->drawText(tag.adjusted(7, 0, -3, 0), Qt::AlignVCenter, caption);
    }
}
