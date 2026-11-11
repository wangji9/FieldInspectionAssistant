#include "rk1828_model.h"
#include "../../src/services/FaceObservationPacket.h"
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
using Model = Rk1828Model;

struct Detection {
    Detection() = default;
    Detection(cv::Rect2f region, float confidence) : box(region), score(confidence) {}

    cv::Rect2f box;
    std::vector<cv::Point2f> points;
    float score = 0;
    int category = 0;
    std::vector<cv::Point2f> mesh;
    std::array<float, 128> embedding{};
    cv::Vec3f pose{};
    cv::Vec2f gaze{};
    cv::Vec2f rawHead{};
    cv::Vec3f headDirection{}, gazeDirection{}, rawHeadDirection{}, rawGazeDirection{};
    cv::Point2f headOrigin, gazeOrigin;
    bool analyzed = false;
    bool identified = false;
};

struct Letterbox {
    float scale;
    cv::Point2f offset;

    Letterbox(const cv::Mat &source, cv::Mat &target, int size, int color)
        : scale(float(size) / std::max(source.cols, source.rows)) {
        const int width = std::max(1, int(source.cols * scale));
        const int height = std::max(1, int(source.rows * scale));
        const cv::Rect roi((size - width) / 2, (size - height) / 2, width, height);
        offset = cv::Point2f(roi.x, roi.y);
        target.create(size, size, CV_8UC3);
        target.setTo(cv::Scalar::all(color));
        cv::resize(source, target(roi), roi.size(), 0, 0, cv::INTER_LINEAR);
    }

    cv::Point2f restore(cv::Point2f point) const { return (point - offset) / scale; }
};

std::vector<Detection> suppress(std::vector<Detection> candidates, size_t limit, float overlap)
{
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
    std::vector<Detection> kept;
    for (auto &row : candidates) {
        const auto near = [&](const Detection &other) {
            const float common = (row.box & other.box).area();
            return common > overlap * (row.box.area() + other.box.area() - common);
        };
        if (std::none_of(kept.begin(), kept.end(), near))
            kept.push_back(std::move(row));
        if (kept.size() == limit)
            break;
    }
    return kept;
}

class FacePipeline {
    using Clock = std::chrono::steady_clock;
    struct Angles {
        cv::Rect2f box, crop;
        cv::Point2f leftEye, rightEye;
        cv::Vec3f pose;
        cv::Vec2f rawHead;
        cv::Vec3f headDirection, gazeDirection, rawHeadDirection, rawGazeDirection;
        cv::Point2f headOrigin, gazeOrigin;
        Clock::time_point poseTime, gazeTime;
    };
public:
    bool load(const char *detector, const char *classifier) {
        const std::string secondary(classifier);
        expression_ = secondary.find("emotion.rknn") != std::string::npos;
        access_ = secondary.find("identity.rknn") != std::string::npos;
        const auto directory = secondary.substr(0, secondary.find_last_of('/') + 1);
        return face_.load(detector, {4200 * 4, 4200 * 2, 4200 * 10})
            && (!expression_ || expressionModel_.load(classifier, {7}))
            && (!access_ || (identity_.load(classifier, {128})
                && mesh_.load((directory + "mesh.rknn").c_str(), {478 * 3, 1})
                && pose_.load((directory + "pose.rknn").c_str(), {9})
                && gaze_.load((directory + "gaze.rknn").c_str(), {90, 90})));
    }

    bool run(const cv::Mat &image, std::vector<Detection> &results) {
        const Letterbox geometry(image, input_, 320, 114);
        if (!face_.infer(input_))
            return false;
        const auto *boxes = face_.output(0);
        const auto *scores = face_.output(1);
        const auto *points = face_.output(2);
        std::vector<Detection> candidates;
        for (unsigned i = 0; i < anchors_.size(); ++i) {
            const float score = scores[2 * i + 1];
            if (!std::isfinite(score) || score < 0.40f || score > 1)
                continue;
            const auto [x, y, size] = anchors_[i];
            const float dx = x + 0.1f * size * boxes[4 * i];
            const float dy = y + 0.1f * size * boxes[4 * i + 1];
            const float w = size * std::exp(std::clamp(boxes[4 * i + 2] * 0.2f, -6.f, 4.f));
            const float h = size * std::exp(std::clamp(boxes[4 * i + 3] * 0.2f, -6.f, 4.f));
            const auto corner = geometry.restore({dx - w / 2, dy - h / 2});
            Detection row({corner.x, corner.y, w / geometry.scale, h / geometry.scale}, score);
            for (unsigned p = 0; p < 5; ++p)
                row.points.push_back(geometry.restore({x + 0.1f * size * points[10 * i + 2 * p],
                                                       y + 0.1f * size * points[10 * i + 2 * p + 1]}));
            candidates.push_back(std::move(row));
        }
        results = suppress(std::move(candidates), access_ ? 3 : expression_ ? 6 : 32, 0.4f);
        std::vector<Angles> nextAngles;
        const bool trackAngles = access_ && results.size() == 1 && previousAngles_.size() == 1;
        for (auto &row : results) {
            if ((expression_ && !classify(image, row)) ||
                (access_ && !analyze(image, row, trackAngles ? &previousAngles_.front() : nullptr, nextAngles)))
                return false;
        }
        if (access_) previousAngles_ = std::move(nextAngles);
        return true;
    }

private:
    static cv::Mat alignment(const Detection &row) {
        static const std::vector<cv::Point2f> reference{{38.2946f,51.6963f},{73.5318f,51.5014f},
            {56.0252f,71.7366f},{41.5493f,92.3655f},{70.7299f,92.2041f}};
        return cv::estimateAffinePartial2D(row.points, reference, cv::noArray(), cv::LMEDS);
    }

    static float angle(const float *values) {
        const float peak = *std::max_element(values, values + 90);
        float sum = 0, mean = 0;
        for (int i = 0; i < 90; ++i) {
            const float weight = std::exp(values[i] - peak);
            sum += weight;
            mean += i * weight;
        }
        return 4 * mean / sum - 180;
    }

    static constexpr float Rad = float(CV_PI / 180);

    static float response(const cv::Vec3f &before, const cv::Vec3f &after, const cv::Vec3f &lastSample,
                          Clock::duration elapsed, float noiseFloor, float rollDelta = 0.f) {
        const cv::Vec3f change = after - before, lastChange = lastSample - before;
        const float angleChange = float(cv::norm(change)) / Rad;
        const auto difference = std::max(std::abs(rollDelta), angleChange);
        const float motion = std::clamp((difference - noiseFloor) / (14.f - noiseFloor), 0.f, 1.f);
        const float tau = .32f - .275f * motion;
        const float dt = std::clamp(std::chrono::duration<float>(elapsed).count(), .025f, .25f);
        const float weight = 1.f - std::exp(-dt / tau);
        // Accelerate a large change once two samples agree. Limit the effect
        // of a single excursion, including detector or eye-crop glitches.
        if (angleChange > 8.f && lastChange.dot(change) < .30f * change.dot(change))
            return std::min(weight, .18f);
        return weight;
    }

    static cv::Vec3f blendDirection(const cv::Vec3f &before, const cv::Vec3f &after, float weight) {
        const cv::Vec3f mixed = before + (after - before) * weight;
        const float norm = cv::norm(mixed);
        return norm > 1e-4f ? mixed / norm : after;
    }

    static bool blinking(const std::vector<cv::Point2f> &mesh) {
        if (mesh.empty())
            return false;
        const auto closed = [&](int upper, int lower, int outer, int inner) {
            const auto width = cv::norm(mesh[outer] - mesh[inner]);
            return width > 3 && cv::norm(mesh[upper] - mesh[lower]) < width * .10;
        };
        return closed(159, 145, 33, 133) && closed(386, 374, 362, 263);
    }

    bool analyze(const cv::Mat &image, Detection &row, const Angles *previous, std::vector<Angles> &nextAngles) {
        const auto box = row.box & cv::Rect2f(0, 0, image.cols, image.rows);
        if (row.score < .60f || box.width < 30 || box.height < 30)
            return true;
        const auto center = (row.box.tl() + row.box.br()) * .5f;
        const auto eyes = row.points[1] - row.points[0];
        const float roll = std::atan2(eyes.y, eyes.x) * 180 / CV_PI;
        auto warp = cv::getRotationMatrix2D(center, roll, 256.0 / (1.5 * std::max(row.box.width, row.box.height)));
        warp.at<double>(0, 2) += 128 - center.x;
        warp.at<double>(1, 2) += 128 - center.y;
        cv::warpAffine(image, patch_, warp, cv::Size(256,256));
        if (!mesh_.infer(patch_))
            return false;
        if (mesh_.output(1)[0] >= 0.f) {
            const auto *points = mesh_.output(0);
            row.mesh.reserve(FaceObservationPacket::PointCount);
            for (int i = 0; i < FaceObservationPacket::PointCount; ++i)
                row.mesh.emplace_back(points[3 * i], points[3 * i + 1]);
            cv::Mat inverse;
            cv::invertAffineTransform(warp, inverse);
            cv::transform(row.mesh, row.mesh, inverse);
        }

        const auto now = Clock::now();
        auto poseTime = now, gazeTime = now;
        const float stableDistance = std::max(2.5, cv::norm(eyes) * .10);
        const bool stable = previous &&
            (previous->box & row.box).area() >= .82f * std::max(previous->box.area(), row.box.area()) &&
            cv::norm(previous->leftEye - row.points[0]) < stableDistance &&
            cv::norm(previous->rightEye - row.points[1]) < stableDistance;
        const bool continuous = previous && now - std::max(previous->poseTime, previous->gazeTime)
                < std::chrono::milliseconds(450) &&
            (previous->box & row.box).area() >= .65f * std::max(previous->box.area(), row.box.area());
        auto crop = box;
        if (stable) {
            crop = cv::Rect2f(previous->crop.tl() * .55f + box.tl() * .45f,
                              previous->crop.br() * .55f + box.br() * .45f);
        }
        row.headOrigin = row.points[2];
        row.gazeOrigin = (row.points[0] + row.points[1]) * .5f;
        if (continuous) {
            const float weight = stable ? .38f : .76f;
            row.headOrigin = previous->headOrigin + (row.headOrigin - previous->headOrigin) * weight;
            row.gazeOrigin = previous->gazeOrigin + (row.gazeOrigin - previous->gazeOrigin) * weight;
        }
        // A stable face can refresh the older auxiliary estimate first. Large
        // movement or a new face immediately refreshes both estimates.
        constexpr auto MaxAngleAge = std::chrono::milliseconds(160);
        if (stable && previous->poseTime >= previous->gazeTime && now - previous->poseTime < MaxAngleAge) {
            row.pose = previous->pose;
            row.rawHead = previous->rawHead;
            row.headDirection = previous->headDirection;
            row.rawHeadDirection = previous->rawHeadDirection;
            poseTime = previous->poseTime;
        } else if (!estimatePose(image, row, crop)) {
            return false;
        } else if (continuous && cv::checkRange(cv::Mat(row.pose))) {
            const float rollDelta = std::remainder(row.pose[2] - previous->pose[2], 360.f);
            const auto weight = response(previous->headDirection, row.headDirection, previous->rawHeadDirection,
                                         now - previous->poseTime, 2.f, rollDelta);
            row.headDirection = blendDirection(previous->headDirection, row.headDirection, weight);
            for (int axis = 0; axis < 3; ++axis)
                row.pose[axis] = std::remainder(previous->pose[axis] +
                    std::remainder(row.pose[axis] - previous->pose[axis], 360.f) * weight, 360.f);
        }
        if ((stable && previous->gazeTime > previous->poseTime && now - previous->gazeTime < MaxAngleAge) ||
            (continuous && blinking(row.mesh) && now - previous->gazeTime < std::chrono::milliseconds(330))) {
            row.gazeDirection = previous->gazeDirection;
            row.rawGazeDirection = previous->rawGazeDirection;
            gazeTime = previous->gazeTime;
        } else if (!estimateGaze(image, row, crop)) {
            return false;
        } else if (continuous && cv::checkRange(cv::Mat(row.gazeDirection))) {
            const auto weight = response(previous->gazeDirection, row.gazeDirection, previous->rawGazeDirection,
                                         now - previous->gazeTime, 3.f);
            row.gazeDirection = blendDirection(previous->gazeDirection, row.gazeDirection, weight);
        }
        row.gaze = {std::atan2(-row.gazeDirection[0], -row.gazeDirection[2]) / Rad,
                    std::asin(std::clamp(-row.gazeDirection[1], -1.f, 1.f)) / Rad};
        row.analyzed = cv::checkRange(cv::Mat(row.pose)) && cv::checkRange(cv::Mat(row.gaze)) &&
            cv::checkRange(cv::Mat(row.headDirection)) && cv::checkRange(cv::Mat(row.gazeDirection));
        if (row.analyzed)
            nextAngles.push_back({row.box, crop, row.points[0], row.points[1], row.pose, row.rawHead,
                                  row.headDirection, row.gazeDirection, row.rawHeadDirection, row.rawGazeDirection,
                                  row.headOrigin, row.gazeOrigin, poseTime, gazeTime});

        const bool profile = std::abs(row.rawHead[0]) >= 57;
        if (!row.analyzed || cv::norm(eyes) < (profile ? 8 : 14) ||
            (profile && std::min(box.width, box.height) < 50) ||
            std::abs(row.rawHead[0]) > 88 || std::abs(row.rawHead[1]) > 42)
            return true;
        const auto aligned = alignment(row);
        if (aligned.empty())
            return true;
        cv::warpAffine(image, aligned_, aligned, cv::Size(112,112));
        if (!identity_.infer(aligned_))
            return false;
        const auto *feature = identity_.output(0);
        const double norm = cv::norm(cv::Mat(1, 128, CV_32F, const_cast<float *>(feature)));
        if (std::isfinite(norm) && norm > 1e-5) {
            std::transform(feature, feature + 128, row.embedding.begin(), [norm](float v) { return v / norm; });
            row.identified = true;
        }
        return true;
    }

    bool estimateGaze(const cv::Mat &image, Detection &row, const cv::Rect2f &box) {
        const cv::Rect imageBounds(0, 0, image.cols, image.rows);
        cv::resize(image(cv::Rect(box) & imageBounds), patch_, cv::Size(448,448));
        if (!gaze_.infer(patch_))
            return false;
        const float yaw = angle(gaze_.output(0)) * Rad, pitch = angle(gaze_.output(1)) * Rad;
        row.gazeDirection = {-std::sin(yaw) * std::cos(pitch), -std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
        row.rawGazeDirection = row.gazeDirection;
        return true;
    }

    bool estimatePose(const cv::Mat &image, Detection &row, const cv::Rect2f &box) {
        const cv::Rect imageBounds(0, 0, image.cols, image.rows);
        const auto margin = box.size() * .20f;
        const cv::Rect2f expanded(box.tl() - cv::Point2f(margin.width, margin.height),
                                  box.size() + margin * 2.f);
        cv::resize(image(cv::Rect(expanded) & imageBounds), patch_, cv::Size(224,224));
        if (!pose_.infer(patch_))
            return false;
        const auto *r = pose_.output(0);
        const float sy = std::hypot(r[0], r[3]);
        row.pose = {std::atan2(-r[6], sy), sy < 1e-6f ? std::atan2(-r[5], r[4]) : std::atan2(r[7], r[8]),
                    sy < 1e-6f ? 0.f : std::atan2(r[3], r[0])};
        row.pose *= float(180 / CV_PI);
        row.rawHead = {row.pose[0], row.pose[1]};
        // Rotate the head's forward axis into the camera frame, where it points
        // toward the camera (-Z) at a neutral pose.
        const cv::Vec3f forward(-r[2], -r[5], -r[8]);
        const float norm = cv::norm(forward);
        row.headDirection = norm > .5f ? forward / norm :
            cv::Vec3f::all(std::numeric_limits<float>::quiet_NaN());
        row.rawHeadDirection = row.headDirection;
        return true;
    }

    bool classify(const cv::Mat &image, Detection &row) {
        // The OpenCV Zoo classifier was trained on these aligned 112×112 BGR patches.
        static const std::vector<cv::Point2f> reference{{38.2946f,51.6963f},{73.5318f,51.5014f},
            {56.0252f,71.7366f},{41.5493f,92.3655f},{70.7299f,92.2041f}};
        const auto warp = cv::estimateAffine2D(row.points, reference);
        if (warp.empty()) {
            row.score = 0;
            return true;
        }
        cv::warpAffine(image, aligned_, warp, cv::Size(112,112));
        cv::cvtColor(aligned_, bgr_, cv::COLOR_RGB2BGR);
        if (!expressionModel_.infer(bgr_))
            return false;
        const auto *logits = expressionModel_.output(0);
        const auto *best = std::max_element(logits, logits + 7);
        float total = 0;
        for (unsigned j = 0; j < 7; ++j)
            total += std::exp(logits[j] - *best);
        row.category = best - logits;
        row.score = std::isfinite(total) && total >= 1 ? 1.f / total : 0.f;
        return true;
    }

    struct Anchor { float x, y, size; };
    static std::vector<Anchor> faceAnchors() {
        std::vector<Anchor> result;
        for (int step : {8, 16, 32}) {
            const float side = step == 8 ? 16 : step == 16 ? 64 : 256;
            for (int y = 0; y < 320 / step; ++y)
                for (int x = 0; x < 320 / step; ++x)
                    for (float size : {side, side * 2})
                        result.push_back({(x + .5f) * step, (y + .5f) * step, size});
        }
        return result;
    }

    Model face_, expressionModel_, identity_, mesh_, pose_, gaze_;
    cv::Mat input_, aligned_, bgr_, patch_;
    std::vector<Angles> previousAngles_;
    const std::vector<Anchor> anchors_ = faceAnchors();
    bool expression_ = false;
    bool access_ = false;
};

class HandPipeline {
public:
    bool load(const char *palm, const char *hand) {
        return palm_.load(palm, {2016 * 18, 2016}) && hand_.load(hand, {63, 1, 1, 63});
    }

    bool run(const cv::Mat &image, std::vector<Detection> &results) {
        results.clear();
        if (image.size() == trackedSize_ && framesUntilScan_ > 0 && !tracked_.empty()) {
            // Reuse the previous hand geometry between periodic palm scans.
            for (const auto &hand : tracked_) {
                Detection palm;
                for (int point : {0, 5, 9, 13, 17, 1, 2})
                    palm.points.push_back(hand.points[point]);
                if (!landmarks(image, palm, results))
                    return false;
            }
            if (results.size() == tracked_.size() && std::all_of(results.begin(), results.end(),
                [](const Detection &row) { return row.score >= .65f; })) {
                tracked_ = results;
                --framesUntilScan_;
                return true;
            }
            results.clear();
        }
        const Letterbox geometry(image, input_, 192, 0);
        if (!palm_.infer(input_))
            return false;
        std::vector<Detection> candidates;
        const auto *regression = palm_.output(0);
        const auto *logits = palm_.output(1);
        for (unsigned i = 0; i < anchors_.size(); ++i) {
            const float score = 1.f / (1.f + std::exp(-logits[i]));
            if (!std::isfinite(score) || score < .55f)
                continue;
            const auto *delta = regression + 18 * i;
            if (delta[2] <= 0 || delta[3] <= 0)
                continue;
            const auto &anchor = anchors_[i];
            const auto origin = geometry.restore(anchor + cv::Point2f(delta[0] - delta[2] / 2, delta[1] - delta[3] / 2));
            Detection row({origin.x, origin.y, delta[2] / geometry.scale, delta[3] / geometry.scale}, score);
            for (unsigned p = 0; p < 7; ++p)
                row.points.push_back(geometry.restore(anchor + cv::Point2f(delta[4 + 2 * p], delta[5 + 2 * p])));
            candidates.push_back(std::move(row));
        }
        for (const auto &palm : suppress(std::move(candidates), 2, .3f))
            if (!landmarks(image, palm, results))
                return false;
        tracked_ = results;
        trackedSize_ = image.size();
        framesUntilScan_ = 3;
        return true;
    }

private:
    bool landmarks(const cv::Mat &image, const Detection &palm, std::vector<Detection> &results) {
        // Rotate the wrist-to-middle-finger direction upright and enlarge around the palm.
        const auto direction = palm.points[2] - palm.points[0];
        if (cv::norm(direction) < 3)
            return true;
        const double angle = (CV_PI / 2 - std::atan2(-direction.y, direction.x)) * 180 / CV_PI;
        auto warp = cv::getRotationMatrix2D(palm.points[0], angle, 1.0);
        std::vector<cv::Point2f> rotated;
        cv::transform(palm.points, rotated, warp);
        const auto region = cv::boundingRect(rotated);
        const float side = 3.f * std::max(region.width, region.height);
        if (side < 12)
            return true;
        const double scale = 224.0 / side;
        warp *= scale;
        warp.at<double>(0, 2) += 112 - scale * (region.x + .5 * region.width);
        warp.at<double>(1, 2) += 112 - scale * (region.y + .1 * region.height);
        cv::warpAffine(image, patch_, warp, cv::Size(224,224));
        if (!hand_.infer(patch_))
            return false;
        const float score = hand_.output(1)[0];
        if (!std::isfinite(score) || score < .55f || score > 1)
            return true;
        const auto *landmarks = hand_.output(0);
        Detection row;
        row.score = score;
        for (unsigned p = 0; p < 21; ++p)
            row.points.emplace_back(landmarks[3 * p], landmarks[3 * p + 1]);
        cv::Mat inverse;
        cv::invertAffineTransform(warp, inverse);
        cv::transform(row.points, row.points, inverse);
        row.box = cv::boundingRect(row.points);
        results.push_back(std::move(row));
        return true;
    }

    static std::vector<cv::Point2f> palmAnchors() {
        std::vector<cv::Point2f> result;
        // MediaPipe SSD anchors share a center across the scales at each grid cell.
        for (int step : {8, 16})
            for (int y = 0; y < 192 / step; ++y)
                for (int x = 0; x < 192 / step; ++x)
                    for (int i = 0; i < (step == 8 ? 2 : 6); ++i)
                        result.emplace_back((x + .5f) * step, (y + .5f) * step);
        return result;
    }

    Model palm_, hand_;
    cv::Mat input_, patch_;
    const std::vector<cv::Point2f> anchors_ = palmAnchors();
    std::vector<Detection> tracked_;
    cv::Size trackedSize_;
    int framesUntilScan_ = 0;
};

template<class Pipeline>
int stream(FILE *wire, const char *primary, const char *secondary)
{
    Pipeline pipeline;
    const bool faceAccess = std::string(secondary).find("identity.rknn") != std::string::npos;
    if (!pipeline.load(primary, secondary)) {
        std::fputs("ERROR model-init\n", wire);
        return 4;
    }
    std::fputs("READY\n", wire);
    std::fflush(wire);
    cv::Mat image;
    std::string header;
    std::vector<Detection> rows;
    while (std::getline(std::cin, header)) {
        unsigned sequence;
        int width, height;
        std::istringstream fields(header);
        if (!(fields >> sequence >> width >> height) || width < 16 || height < 16 || width > 1280 || height > 1280)
            return 2;
        image.create(height, width, CV_8UC3);
        if (!std::cin.read(reinterpret_cast<char *>(image.data), image.total() * image.elemSize()))
            return 2;
        const auto start = std::chrono::steady_clock::now();
        if (!pipeline.run(image, rows)) {
            std::fputs("ERROR inference\n", wire);
            return 5;
        }
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        const size_t payloadSize = faceAccess ? rows.size() * sizeof(FaceObservationPacket) : 0;
        std::fprintf(wire, "F %u %lld %zu %zu\n", sequence, static_cast<long long>(micros), rows.size(), payloadSize);
        for (const auto &row : rows) {
            if (faceAccess) {
                FaceObservationPacket packet;
                packet.score = row.score;
                packet.bounds = {row.box.x, row.box.y, row.box.br().x, row.box.br().y};
                if (row.mesh.size() == FaceObservationPacket::PointCount) {
                    packet.flags |= FaceObservationPacket::Mesh;
                    for (size_t i = 0; i < row.mesh.size(); ++i) {
                        packet.points[i * 2] = row.mesh[i].x;
                        packet.points[i * 2 + 1] = row.mesh[i].y;
                    }
                }
                if (row.analyzed) {
                    packet.flags |= FaceObservationPacket::Angles;
                    packet.angles = {row.pose[0], row.pose[1], row.pose[2], row.gaze[0], row.gaze[1]};
                    packet.rawHead = {row.rawHead[0], row.rawHead[1]};
                    packet.headDirection = {row.headDirection[0], row.headDirection[1], row.headDirection[2]};
                    packet.gazeDirection = {row.gazeDirection[0], row.gazeDirection[1], row.gazeDirection[2]};
                    packet.headOrigin = {row.headOrigin.x, row.headOrigin.y};
                    packet.gazeOrigin = {row.gazeOrigin.x, row.gazeOrigin.y};
                }
                if (row.identified) {
                    packet.flags |= FaceObservationPacket::Embedding;
                    packet.embedding = row.embedding;
                }
                if (std::fwrite(&packet, sizeof(packet), 1, wire) != 1)
                    return 2;
            } else {
                std::fprintf(wire, "%d %.6f %d %d %d %d", row.category, row.score, cvFloor(row.box.x),
                    cvFloor(row.box.y), cvCeil(row.box.br().x), cvCeil(row.box.br().y));
                for (const auto &point : row.points)
                    std::fprintf(wire, " %.3f %.3f 1", point.x, point.y);
                std::fputc('\n', wire);
            }
        }
        if (std::fflush(wire) != 0)
            break;
    }
    return 0;
}
}

int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    cv::setNumThreads(1);
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
#ifdef F_SETPIPE_SZ
    fcntl(STDIN_FILENO, F_SETPIPE_SZ, 1024 * 1024);
#endif
    const int descriptor = dup(STDOUT_FILENO);
    if (descriptor < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
        return 3;
    auto *wire = fdopen(descriptor, "wb");
    if (!wire) {
        close(descriptor);
        return 3;
    }
    int status;
    try {
        const std::string secondary = argv[2];
        const auto basename = secondary.substr(secondary.find_last_of('/') + 1);
        status = basename == "hand.rknn" ? stream<HandPipeline>(wire, argv[1], argv[2])
            : stream<FacePipeline>(wire, argv[1], argv[2]);
    } catch (const cv::Exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        std::fputs("ERROR inference\n", wire);
        status = 5;
    }
    std::fclose(wire);
    return status;
}
