#include "rk1828_model.h"

#include <opencv2/imgproc.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace {
constexpr int InputSide = 640;
constexpr float MinimumConfidence = 0.25f;
constexpr float NmsIou = 0.45f;
constexpr int DflBins = 16;
constexpr std::array<unsigned, 3> Grids{80, 40, 20};

float distance(const float *distribution, unsigned cells, unsigned cell)
{
    float maximum = distribution[cell];
    for (int bin = 1; bin < DflBins; ++bin)
        maximum = std::max(maximum, distribution[bin * cells + cell]);
    float sum = 0, weighted = 0;
    for (int bin = 0; bin < DflBins; ++bin) {
        const auto probability = std::exp(distribution[bin * cells + cell] - maximum);
        sum += probability;
        weighted += bin * probability;
    }
    return weighted / sum;
}

struct Detection {
    cv::Rect2f box;
    float confidence;
    unsigned category;
};

class Detector {
public:
    bool load(const char *path)
    {
        std::ifstream labels("model/labels.txt");
        for (std::string name; std::getline(labels, name); )
            if (name.find_first_not_of(" \t\r") != std::string::npos)
                ++classes_;
        if (!classes_ || classes_ > 128 || !model_.load(path, {
            64 * 80 * 80, classes_ * 80 * 80, 64 * 40 * 40, classes_ * 40 * 40, 64 * 20 * 20, classes_ * 20 * 20})
            || model_.width() != InputSide || model_.height() != InputSide)
            return false;
        for (unsigned i = 0; i < Grids.size() * 2; ++i) {
            const auto &shape = model_.outputAttr(i);
            if (shape.n_dims != 4 || shape.shape[0] != 1 || shape.shape[1] != (i % 2 ? classes_ : 64)
                || shape.shape[2] != Grids[i / 2] || shape.shape[3] != Grids[i / 2])
                return false;
        }
        return true;
    }

    bool run(const cv::Mat &frame, std::vector<Detection> &detections)
    {
        const auto scale = float(InputSide) / std::max(frame.cols, frame.rows);
        const int width = std::lround(frame.cols * scale), height = std::lround(frame.rows * scale);
        const cv::Rect roi((InputSide - width) / 2, (InputSide - height) / 2, width, height);
        input_.create(InputSide, InputSide, CV_8UC3);
        input_.setTo(cv::Scalar::all(114));
        cv::resize(frame, input_(roi), roi.size(), 0, 0, cv::INTER_LINEAR);
        if (!model_.infer(input_))
            return false;
        candidates_.clear();
        const cv::Rect2f imageBounds(0, 0, frame.cols, frame.rows);
        const auto minimumLogit = std::log(MinimumConfidence / (1 - MinimumConfidence));
        for (unsigned level = 0; level < Grids.size(); ++level) {
            const auto grid = Grids[level], cells = grid * grid;
            const auto stride = float(InputSide) / grid;
            const auto *boxes = model_.output(level * 2);
            const auto *scores = model_.output(level * 2 + 1);
            for (unsigned cell = 0; cell < cells; ++cell) {
                unsigned category = 0;
                float best = minimumLogit;
                for (unsigned c = 0; c < classes_; ++c) {
                    const auto logit = scores[c * cells + cell];
                    if (logit > best) {
                        best = logit;
                        category = c;
                    }
                }
                if (best <= minimumLogit || !std::isfinite(best))
                    continue;
                const auto cx = cell % grid + 0.5f, cy = cell / grid + 0.5f;
                const auto x = ((cx - distance(boxes, cells, cell)) * stride - roi.x) / scale;
                const auto y = ((cy - distance(boxes + DflBins * cells, cells, cell)) * stride - roi.y) / scale;
                const auto r = ((cx + distance(boxes + 2 * DflBins * cells, cells, cell)) * stride - roi.x) / scale;
                const auto b = ((cy + distance(boxes + 3 * DflBins * cells, cells, cell)) * stride - roi.y) / scale;
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(r) || !std::isfinite(b) || r <= x || b <= y)
                    continue;
                const auto box = cv::Rect2f(x, y, r - x, b - y) & imageBounds;
                if (!box.empty())
                    candidates_.push_back({box, 1.f / (1.f + std::exp(-best)), category});
            }
        }
        std::sort(candidates_.begin(), candidates_.end(), [](const Detection &a, const Detection &b) {
            return a.confidence > b.confidence;
        });
        detections.clear();
        for (const auto &candidate : candidates_) {
            const auto duplicate = [&](const Detection &other) {
                if (other.category != candidate.category)
                    return false;
                const auto common = (candidate.box & other.box).area();
                return common > NmsIou * (candidate.box.area() + other.box.area() - common);
            };
            if (std::none_of(detections.begin(), detections.end(), duplicate))
                detections.push_back(candidate);
            if (detections.size() == 128)
                break;
        }
        return true;
    }

private:
    Rk1828Model model_;
    cv::Mat input_;
    std::vector<Detection> candidates_;
    unsigned classes_ = 0;
};
}

int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    const int descriptor = dup(STDOUT_FILENO);
    if (descriptor < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
        return 3;
    auto *wire = fdopen(descriptor, "wb");
    if (!wire)
        return 3;
    setenv("FIELDOPS_VISION_CORE_MASK", argv[3], 1);
    cv::setNumThreads(1);
    Detector detector;
    if (!detector.load(argv[1])) {
        std::fputs("ERROR model-init\n", wire);
        std::fclose(wire);
        return 4;
    }
    std::fputs("READY\n", wire);
    std::fflush(wire);
    std::string header;
    cv::Mat frame;
    std::vector<Detection> detections;
    while (std::getline(std::cin, header)) {
        unsigned sequence;
        int width, height;
        std::istringstream fields(header);
        if (!(fields >> sequence >> width >> height) || width < 16 || height < 16
            || width > InputSide || height > InputSide)
            break;
        frame.create(height, width, CV_8UC3);
        if (!std::cin.read(reinterpret_cast<char *>(frame.data), frame.total() * frame.elemSize()))
            break;
        const auto start = std::chrono::steady_clock::now();
        if (!detector.run(frame, detections)) {
            std::fputs("ERROR inference\n", wire);
            break;
        }
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
        std::fprintf(wire, "F %u %lld %zu 0\n", sequence, static_cast<long long>(micros), detections.size());
        for (const auto &d : detections)
            std::fprintf(wire, "%u %.6f %d %d %d %d\n", d.category, d.confidence, int(d.box.x), int(d.box.y),
                int(std::ceil(d.box.x + d.box.width)), int(std::ceil(d.box.y + d.box.height)));
        if (std::fflush(wire) != 0)
            break;
    }
    std::fclose(wire);
    return 0;
}
