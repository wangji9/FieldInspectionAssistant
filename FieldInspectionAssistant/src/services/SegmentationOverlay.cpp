#include "SegmentationOverlay.h"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>
#include <QtMath>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <algorithm>
#include <cmath>
#include <deque>

namespace {
constexpr qint64 MaxPredictionAge = 560;
constexpr qint64 AppearanceCheckAge = 170;
constexpr qint64 MaxUntrackedAge = 135;
constexpr int TrackingWidth = 320;
constexpr int PointsPerTarget = 12;
constexpr int TrackingTargetLimit = 16;
const cv::Size FlowWindow(15, 15);

struct CameraFrame {
    cv::Mat gray;
    qint64 time;
    mutable std::vector<cv::Mat> pyramid;

    const std::vector<cv::Mat> &flowPyramid() const
    {
        if (pyramid.empty())
            cv::buildOpticalFlowPyramid(gray, pyramid, FlowWindow, 2);
        return pyramid;
    }
};

struct Target {
    QVariantMap annotation;
    QRectF box;
    QList<QPolygonF> contours;
    std::vector<cv::Point2f> points;
    std::vector<cv::Point2f> anchors;
    cv::Mat appearance;
    qint64 detectedAt;
    bool confirmed;
    bool movingReliably = false;
    bool plausible = true;
};

cv::Mat trackingImage(const QImage &frame)
{
    const auto size = frame.size().scaled(QSize(TrackingWidth, TrackingWidth), Qt::KeepAspectRatio);
    const auto rgb = frame.format() == QImage::Format_RGB32 ? frame : frame.convertToFormat(QImage::Format_RGB32);
    const cv::Mat view(rgb.height(), rgb.width(), CV_8UC4, const_cast<uchar *>(rgb.constBits()), rgb.bytesPerLine());
    cv::Mat small, gray;
    cv::resize(view, small, cv::Size(size.width(), size.height()), 0, 0, cv::INTER_LINEAR);
    cv::cvtColor(small, gray, cv::COLOR_BGRA2GRAY);
    return gray;
}

QPointF roundedCorner(const QPointF &corner, const QPointF &neighbor)
{
    const auto edge = neighbor - corner;
    return corner + edge * qMin(0.26, 3.0 / qMax(std::hypot(edge.x(), edge.y()), 1.0));
}

void appendContour(QPainterPath &path, const QPolygonF &polygon)
{
    path.moveTo(roundedCorner(polygon.front(), polygon.back()));
    for (qsizetype i = 0; i < polygon.size(); ++i) {
        const auto corner = polygon[i];
        path.lineTo(roundedCorner(corner, polygon[(i + polygon.size() - 1) % polygon.size()]));
        path.quadTo(corner, roundedCorner(corner, polygon[(i + 1) % polygon.size()]));
    }
    path.closeSubpath();
}

QImage render(const QSize &size, const QList<Target> &targets)
{
    if (targets.isEmpty())
        return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const auto &target : targets) {
        QPainterPath path;
        path.setFillRule(Qt::OddEvenFill);
        for (const auto &polygon : target.contours)
            appendContour(path, polygon);
        const QColor color(target.annotation.value("color").toString());
        auto fill = color;
        fill.setAlpha(64);
        painter.fillPath(path, fill);
        painter.strokePath(path, QPen(QColor(12, 22, 30, 160), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.strokePath(path, QPen(color, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    painter.end();
    return image;
}

QList<Target> extract(const QByteArray &labels, const QSize &size, const QList<SegmentationOverlay::Region> &regions,
                      const cv::Mat &reference, qint64 capturedAt)
{
    QList<Target> targets;
    if (!size.isValid() || labels.size() != qsizetype(size.width()) * size.height())
        return targets;
    const cv::Mat mask(size.height(), size.width(), CV_8UC1, const_cast<char *>(labels.constData()));
    cv::Mat trackingMask, instance;
    if (!reference.empty())
        cv::resize(mask, trackingMask, reference.size(), 0, 0, cv::INTER_NEAREST);
    for (const auto &region : regions) {
        const auto box = region.annotation.value("box").toRect();
        const cv::Rect roi(box.x(), box.y(), box.width(), box.height());
        cv::compare(mask(roi), cv::Scalar(region.maskIndex), instance, cv::CMP_EQ);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(instance, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE, roi.tl());
        Target target{region.annotation, box, {}, {}, {}, {}, capturedAt, region.confirmed};
        for (const auto &contour : contours) {
            if (contour.size() < 3 || cv::contourArea(contour) < 6)
                continue;
            std::vector<cv::Point> corners;
            cv::approxPolyDP(contour, corners, 1.6, true);
            const auto &vertices = corners.size() >= 3 ? corners : contour;
            QPolygonF polygon;
            polygon.reserve(vertices.size());
            for (const auto &vertex : vertices)
                polygon.append(QPointF(vertex.x + 0.5, vertex.y + 0.5));
            target.contours.append(std::move(polygon));
        }
        if (target.contours.isEmpty())
            continue;
        if (!trackingMask.empty() && targets.size() < TrackingTargetLimit) {
            const double sx = double(reference.cols) / size.width();
            const double sy = double(reference.rows) / size.height();
            const auto window = cv::Rect(0, 0, reference.cols, reference.rows)
                & cv::Rect(cv::Point(qFloor(box.left() * sx), qFloor(box.top() * sy)),
                           cv::Point(qCeil((box.right() + 1) * sx), qCeil((box.bottom() + 1) * sy)));
            cv::Mat features;
            cv::compare(trackingMask(window), cv::Scalar(region.maskIndex), features, cv::CMP_EQ);
            cv::goodFeaturesToTrack(reference(window), target.points, PointsPerTarget, 0.025, 5.0, features);
            for (auto &point : target.points)
                point += cv::Point2f(window.x, window.y);
            target.anchors = target.points;
            target.appearance = reference;
        }
        targets.append(std::move(target));
    }
    return targets;
}

float median(std::vector<float> &values)
{
    auto middle = values.begin() + values.size() / 2;
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

bool similarPatch(const QRectF &box, const CameraFrame &before, const CameraFrame &after, double sx, double sy)
{
    const cv::Rect visible(0, 0, after.gray.cols, after.gray.rows);
    const auto patch = visible & cv::Rect(cv::Point(qFloor(box.left() / sx), qFloor(box.top() / sy)),
        cv::Point(qCeil(box.right() / sx), qCeil(box.bottom() / sy)));
    return patch.area() > 0
        && cv::norm(before.gray(patch), after.gray(patch), cv::NORM_L1) / patch.area() < 12;
}

bool keepsAppearance(const cv::Mat &original, const cv::Point2f &anchor,
                     const cv::Mat &current, const cv::Point2f &point)
{
    constexpr int radius = 3;
    constexpr int pixels = (2 * radius + 1) * (2 * radius + 1);
    const cv::Point a(qRound(anchor.x), qRound(anchor.y)), b(qRound(point.x), qRound(point.y));
    const cv::Rect interior(radius, radius, original.cols - 2 * radius, original.rows - 2 * radius);
    if (!interior.contains(a) || !interior.contains(b))
        return false;
    int sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
    for (int y = -radius; y <= radius; ++y) {
        const auto *left = original.ptr<uchar>(a.y + y) + a.x;
        const auto *right = current.ptr<uchar>(b.y + y) + b.x;
        for (int x = -radius; x <= radius; ++x) {
            const int l = left[x], r = right[x];
            sumA += l;
            sumB += r;
            sumAA += l * l;
            sumBB += r * r;
            sumAB += l * r;
        }
    }
    const double covariance = pixels * sumAB - sumA * sumB;
    const double varianceA = pixels * sumAA - sumA * sumA;
    const double varianceB = pixels * sumBB - sumB * sumB;
    return covariance > 0 && varianceA > 0 && varianceB > 0
        && covariance * covariance >= 0.45 * varianceA * varianceB;
}

// Forward/backward agreement rejects points that drift onto the background.
bool project(QList<Target> &targets, const CameraFrame &before, const CameraFrame &after,
             const QSize &size, int levels)
{
    std::vector<cv::Point2f> points, forward, backward;
    for (const auto &target : targets)
        points.insert(points.end(), target.points.begin(), target.points.end());
    std::vector<uchar> status, backStatus;
    std::vector<float> error;
    if (!points.empty()) {
        const cv::TermCriteria criteria(cv::TermCriteria::COUNT | cv::TermCriteria::EPS, 12, 0.08);
        const auto &previous = before.flowPyramid();
        const auto &current = after.flowPyramid();
        cv::calcOpticalFlowPyrLK(previous, current, points, forward, status, error, FlowWindow, levels, criteria);
        for (size_t i = 0; i < forward.size(); ++i) {
            if (!status[i] || !std::isfinite(forward[i].x) || !std::isfinite(forward[i].y)) {
                status[i] = 0;
                forward[i] = points[i];
            }
        }
        cv::calcOpticalFlowPyrLK(current, previous, forward, backward, backStatus, cv::noArray(), FlowWindow, levels, criteria);
    }
    const double sx = double(size.width()) / after.gray.cols;
    const double sy = double(size.height()) / after.gray.rows;
    size_t offset = 0;
    bool moved = false;
    for (auto &target : targets) {
        std::vector<float> dx, dy;
        std::vector<cv::Point2f> from, to, original;
        const bool verifyAppearance = after.time - target.detectedAt > AppearanceCheckAge;
        for (size_t j = 0; j < target.points.size(); ++j) {
            const auto i = offset + j;
            if (!status[i] || !backStatus[i] || error[i] > 24 || cv::norm(points[i] - backward[i]) > 1.6
                || forward[i].x < 1 || forward[i].y < 1
                || forward[i].x >= after.gray.cols - 1 || forward[i].y >= after.gray.rows - 1)
                continue;
            if (verifyAppearance && !keepsAppearance(target.appearance, target.anchors[j], after.gray, forward[i]))
                continue;
            from.push_back(points[i]);
            to.push_back(forward[i]);
            original.push_back(target.anchors[j]);
            dx.push_back(forward[i].x - points[i].x);
            dy.push_back(forward[i].y - points[i].y);
        }
        offset += target.points.size();
        target.movingReliably = to.size() >= 3 && to.size() * 2 >= target.points.size();
        target.points.clear();
        target.anchors.clear();
        if (!target.movingReliably) {
            target.plausible &= similarPatch(target.box, before, after, sx, sy);
            continue;
        }
        const cv::Point2f movement(median(dx), median(dy));
        cv::Point2f center;
        std::vector<size_t> inliers;
        for (size_t i = 0; i < to.size(); ++i) {
            if (cv::norm(to[i] - from[i] - movement) <= 4) {
                inliers.push_back(i);
                center += from[i];
                target.points.push_back(to[i]);
                target.anchors.push_back(original[i]);
            }
        }
        if (inliers.size() < 3) {
            target.movingReliably = false;
            target.points.clear();
            target.anchors.clear();
            target.plausible &= similarPatch(target.box, before, after, sx, sy);
            continue;
        }
        center *= 1.0f / inliers.size();
        std::vector<float> ratios, angles;
        for (const auto i : inliers) {
            const auto a = from[i] - center;
            const auto b = to[i] - center - movement;
            const auto radius = cv::norm(a);
            if (radius >= 8) {
                ratios.push_back(cv::norm(b) / radius);
                angles.push_back(std::atan2(a.cross(b), a.dot(b)));
            }
        }
        const auto estimatedScale = ratios.size() >= 4 ? qBound(0.91f, median(ratios), 1.10f) : 1.0f;
        const auto scale = std::abs(estimatedScale - 1.0f) >= 0.004f ? estimatedScale : 1.0f;
        const auto estimatedAngle = angles.size() >= 4 ? qBound(-0.14f, median(angles), 0.14f) : 0.0f;
        const auto angle = std::abs(estimatedAngle) >= 0.012f ? estimatedAngle : 0.0f;
        const auto shift = cv::norm(movement) >= 0.18 ? movement : cv::Point2f();
        if (shift == cv::Point2f() && scale == 1.0f && angle == 0.0f)
            continue;
        const auto c = scale * std::cos(angle), s = scale * std::sin(angle);
        const QTransform motion(c, s * sy / sx, -s * sx / sy, c,
            (shift.x + (1 - c) * center.x + s * center.y) * sx,
            (shift.y + (1 - c) * center.y - s * center.x) * sy);
        for (auto &polygon : target.contours)
            polygon = motion.map(polygon);
        const auto boxCenter = motion.map(target.box.center());
        target.box.setSize(target.box.size() * scale);
        target.box.moveCenter(boxCenter);
        moved = true;
    }
    return moved;
}

double overlap(const QRectF &a, const QRectF &b)
{
    const auto intersection = a.intersected(b);
    const auto area = intersection.width() * intersection.height();
    return area / (a.width() * a.height() + b.width() * b.height() - area);
}

// Smooth small boundary corrections against the already projected previous
// contour. Large shape changes are accepted immediately.
void stabilize(Target &next, const Target &previous)
{
    qsizetype oldVertices = 0, newVertices = 0;
    for (const auto &polygon : previous.contours)
        oldVertices += polygon.size();
    for (const auto &polygon : next.contours)
        newVertices += polygon.size();
    if (oldVertices * newVertices > 32000)
        return;
    for (auto &polygon : next.contours) {
        for (auto &point : polygon) {
            QPointF correction;
            qreal distanceSquared = 16;
            for (const auto &old : previous.contours) {
                QPointF start = old.back();
                for (const auto &end : old) {
                    const auto edge = end - start;
                    const auto length = QPointF::dotProduct(edge, edge);
                    const auto t = length > 0 ? qBound(0.0, QPointF::dotProduct(point - start, edge) / length, 1.0) : 0.0;
                    const auto delta = start + edge * t - point;
                    const auto squared = QPointF::dotProduct(delta, delta);
                    if (squared < distanceSquared) {
                        distanceSquared = squared;
                        correction = delta;
                    }
                    start = end;
                }
            }
            point += correction * 0.42;
        }
    }
}

bool expired(const Target &target, qint64 now, const QSize &size)
{
    return !target.plausible
        || now - target.detectedAt > (target.movingReliably ? MaxPredictionAge : MaxUntrackedAge)
        || !target.box.intersects(QRectF(QPointF(), QSizeF(size)));
}
}

struct SegmentationOverlay::State {
    std::deque<CameraFrame> frames;
    QList<Target> targets;
    QImage image;
    QSize cameraSize, modelSize;
    bool following = false;
};

SegmentationOverlay::SegmentationOverlay() : state_(std::make_unique<State>())
{
    // The tracking images are small. A single OpenCV thread avoids competing
    // with camera decode, Qt rendering and the inference transport.
    cv::setNumThreads(1);
}

SegmentationOverlay::~SegmentationOverlay() = default;

void SegmentationOverlay::reset() { *state_ = State(); }
QImage SegmentationOverlay::image() const { return state_->image; }
int SegmentationOverlay::count() const { return int(state_->targets.size()); }

QVariantList SegmentationOverlay::annotations() const
{
    QVariantList rows;
    for (auto it = state_->targets.crbegin(); it != state_->targets.crend(); ++it) {
        auto row = it->annotation;
        row.insert("box", it->box.toRect());
        rows.append(std::move(row));
    }
    return rows;
}

bool SegmentationOverlay::advance(const QImage &frame, qint64 capturedAt)
{
    auto &s = *state_;
    const bool resetSize = !s.cameraSize.isEmpty() && frame.size() != s.cameraSize;
    if (resetSize)
        reset();
    s.cameraSize = frame.size();
    s.frames.push_back({trackingImage(frame), capturedAt, {}});
    while (s.frames.size() > 14)
        s.frames.pop_front();
    if (!s.following || s.targets.isEmpty() || s.frames.size() < 2)
        return resetSize;
    const bool moved = project(s.targets, s.frames[s.frames.size() - 2], s.frames.back(), s.modelSize, 1);
    const auto removed = s.targets.removeIf([&](const Target &target) { return expired(target, capturedAt, s.modelSize); });
    if (!moved && !removed)
        return false;
    s.image = render(s.modelSize, s.targets);
    return true;
}

void SegmentationOverlay::correct(const QByteArray &labels, const QSize &size, const QList<Region> &regions,
                                  qint64 capturedAt, bool follow, bool resetTargets)
{
    auto &s = *state_;
    if (resetTargets || !follow || !s.following || s.modelSize != size) {
        s.targets.clear();
        s.image = {};
    }
    s.modelSize = size;
    s.following = follow;
    if (!follow) {
        s.image = render(size, extract(labels, size, regions, {}, capturedAt));
        return;
    }
    const auto reference = std::find_if(s.frames.cbegin(), s.frames.cend(),
        [capturedAt](const CameraFrame &frame) { return frame.time == capturedAt; });
    if (reference == s.frames.cend())
        return;
    const auto &latest = s.frames.back();
    QList<Region> candidates;
    for (const auto &region : regions) {
        if (region.confirmed || std::any_of(s.targets.cbegin(), s.targets.cend(), [&](const Target &target) {
            return target.annotation.value("id") == region.annotation.value("id");
        }))
            candidates.append(region);
    }
    auto next = extract(labels, size, candidates, reference->gray, capturedAt);
    if (reference->time != latest.time)
        project(next, *reference, latest, size, 2);
    QList<bool> used(s.targets.size(), false);
    for (auto &target : next) {
        int match = -1;
        double best = 0.30;
        for (int i = 0; i < s.targets.size(); ++i) {
            const auto &previous = s.targets[i];
            if (used[i] || previous.annotation.value("id") != target.annotation.value("id"))
                continue;
            const auto iou = overlap(target.box, previous.box);
            if (iou > best) {
                best = iou;
                match = i;
            }
        }
        if (match >= 0) {
            const auto &previous = s.targets[match];
            // The lower confidence band can only continue a known instance
            // with visual support. It cannot start a new track.
            target.confirmed |= target.movingReliably || previous.movingReliably;
            if (!target.confirmed)
                continue;
            if (!target.movingReliably && previous.movingReliably) {
                target.box = previous.box;
                target.contours = previous.contours;
                target.points = previous.points;
                target.anchors = previous.anchors;
                target.appearance = previous.appearance;
                target.movingReliably = true;
                target.plausible = true;
            } else {
                stabilize(target, previous);
            }
            used[match] = !expired(target, latest.time, size);
        }
    }
    next.removeIf([&](const Target &target) { return !target.confirmed || expired(target, latest.time, size); });
    if (next.isEmpty()) {
        const auto removed = s.targets.removeIf([&](const Target &target) {
            return !target.movingReliably || expired(target, latest.time, size);
        });
        if (removed)
            s.image = render(size, s.targets);
        return;
    }
    // Keep only instances that are still visually tracked during a short
    // missed detection. An old contour must not stick to a different object.
    for (int i = 0; i < s.targets.size(); ++i)
        if (!used[i] && s.targets[i].movingReliably && !expired(s.targets[i], latest.time, size))
            next.prepend(s.targets[i]);
    s.targets = std::move(next);
    s.image = render(size, s.targets);
}
