#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>
#include <unistd.h>

#if defined(FIELDOPS_YOLOV5)
#include "yolov5.h"
#define INIT_MODEL init_yolov5_model
#define RUN_MODEL inference_yolov5_model
#define RELEASE_MODEL release_yolov5_model
#elif defined(FIELDOPS_YOLOV6)
#include "yolov6.h"
#define INIT_MODEL init_yolov6_model
#define RUN_MODEL inference_yolov6_model
#define RELEASE_MODEL release_yolov6_model
#elif defined(FIELDOPS_YOLO26) || defined(FIELDOPS_YOLO26_SEGMENT) || defined(FIELDOPS_YOLO26_POSE)
#include "yolo26.h"
#define INIT_MODEL init_yolo26_model
#define RELEASE_MODEL release_yolo26_model
#elif defined(FIELDOPS_YOLOV8_SEG)
#include "yolov8_seg.h"
#define INIT_MODEL init_yolov8_seg_model
#define RUN_MODEL inference_yolov8_seg_model
#define RELEASE_MODEL release_yolov8_seg_model
#elif defined(FIELDOPS_YOLOV8_POSE)
#include "yolov8-pose.h"
#define INIT_MODEL init_yolov8_pose_model
#define RUN_MODEL inference_yolov8_pose_model
#define RELEASE_MODEL release_yolov8_pose_model
#else
#include "yolov8.h"
#define INIT_MODEL init_yolov8_model
#define RUN_MODEL inference_yolov8_model
#define RELEASE_MODEL release_yolov8_model
#endif

#ifdef FIELDOPS_RKNN3_BRIDGE
int init_rk1828_bridge(const char *, const char *, rknn_app_context_t *, uint32_t, const char *);
int run_rk1828_bridge(rknn_app_context_t *, image_buffer_t *, object_detect_result_list *);
int release_rk1828_bridge(rknn_app_context_t *);
#undef INIT_MODEL
#undef RUN_MODEL
#undef RELEASE_MODEL
#define INIT_MODEL init_rk1828_bridge
#define RUN_MODEL run_rk1828_bridge
#define RELEASE_MODEL release_rk1828_bridge
#endif

#if defined(FIELDOPS_RKNN2) || defined(FIELDOPS_FAST_LETTERBOX)
#include "image_utils.h"

extern "C" int __real_convert_image_with_letterbox(image_buffer_t *, image_buffer_t *, letterbox_t *, char);

extern "C" int __wrap_convert_image_with_letterbox(image_buffer_t *src, image_buffer_t *dst,
                                                  letterbox_t *letterbox, char color)
{
    // The live client has already resized landscape RGB frames to model width.
    // Preserve the SDK's padding and coordinates without resampling at 1:1.
    if (src->format != IMAGE_FORMAT_RGB888 || dst->format != IMAGE_FORMAT_RGB888
        || !src->virt_addr || !dst->virt_addr || src->width != dst->width
        || src->width % 4 != 0 || src->height % 2 != 0 || src->height > dst->height
        || (src->width_stride && src->width_stride != src->width)
        || (dst->width_stride && dst->width_stride != dst->width))
        return __real_convert_image_with_letterbox(src, dst, letterbox, color);

    const int top = ((dst->height - src->height) / 2) & ~1;
    const size_t rowBytes = static_cast<size_t>(src->width) * 3;
    const size_t first = top * rowBytes;
    const size_t bytes = src->height * rowBytes;
    const size_t last = (dst->height - src->height - top) * rowBytes;
    std::memset(dst->virt_addr, color, first);
    std::memcpy(dst->virt_addr + first, src->virt_addr, bytes);
    std::memset(dst->virt_addr + first + bytes, color, last);
    if (letterbox)
        *letterbox = {0, top, 1.0f};
    return 0;
}
#endif

namespace {
int infer(rknn_app_context_t &context, image_buffer_t &image, object_detect_result_list &results)
{
#if defined(FIELDOPS_YOLO26_SEGMENT) || defined(FIELDOPS_YOLO26_POSE)
    double npuTime = 0;
    return inference_yolo26_model(&context, &image, &results, &npuTime, NMS_THRESH, BOX_THRESH);
#elif defined(FIELDOPS_YOLO26)
    double npuTime = 0;
    return inference_yolo26_model(&context, &image, &results, &npuTime);
#else
    return RUN_MODEL(&context, &image, &results);
#endif
}

void copyMasks(const object_detect_result_list &results, int count, int width, int height,
               std::vector<unsigned char> &labels)
{
    labels.clear();
#if defined(FIELDOPS_YOLOV8_SEG)
    const auto *classes = results.results_seg[0].seg_mask;
    if (!count || !classes)
        return;
    labels.resize(static_cast<size_t>(width) * height, 0);
    // The SDK returns a merged class mask. Use its pixels and associate them
    // with the matching instance's box so the viewer can filter each result.
    for (int i = count - 1; i >= 0; --i) {
        const auto &row = results.results[i];
        const int left = std::max(0, row.box.left), top = std::max(0, row.box.top);
        const int right = std::min(width, row.box.right), bottom = std::min(height, row.box.bottom);
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
                if (classes[y * width + x] == row.cls_id + 1)
                    labels[y * width + x] = static_cast<unsigned char>(i + 1);
    }
#elif defined(FIELDOPS_YOLO26_SEGMENT)
    if (!count)
        return;
    labels.resize(static_cast<size_t>(width) * height, 0);
    for (int i = count - 1; i >= 0; --i) {
        const auto &row = results.results[i];
        const auto &mask = row.mask;
        const int left = std::max(0, row.box.left), top = std::max(0, row.box.top);
        const int right = std::min(width, row.box.right), bottom = std::min(height, row.box.bottom);
        const bool full = mask.width == width && mask.height == height;
        const bool local = mask.width == right - left && mask.height == bottom - top;
        if (!mask.data || (!full && !local))
            continue;
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x)
                if (mask.data[full ? y * width + x : (y - top) * mask.width + x - left])
                    labels[y * width + x] = static_cast<unsigned char>(i + 1);
    }
#endif
}
}

int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
#ifdef F_SETPIPE_SZ
    // Fit a 640×360 RGB frame and a segmentation reply into their pipes.
    // Other kernel limits can leave the pipes at their normal capacity.
    fcntl(STDIN_FILENO, F_SETPIPE_SZ, 1024 * 1024);
    fcntl(STDOUT_FILENO, F_SETPIPE_SZ, 512 * 1024);
#endif
    const int descriptor = dup(STDOUT_FILENO);
    if (descriptor < 0 || dup2(STDERR_FILENO, STDOUT_FILENO) < 0)
        return 3;
    auto *wire = fdopen(descriptor, "wb");
    if (!wire)
        return 3;

    rknn_app_context_t context{};
#ifdef FIELDOPS_RKNN2
    const bool initialized = init_post_process() == 0 && INIT_MODEL(argv[1], &context) == 0;
    if (initialized && rknn_set_core_mask(context.rknn_ctx, RKNN_NPU_CORE_0_1_2) != 0)
        std::fputs("Cannot select all RK3588 NPU cores; using runtime default\n", stderr);
#else
    const auto core = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 16));
    const bool initialized = init_post_process() == 0 && INIT_MODEL(argv[1], argv[2], &context, core, nullptr) == 0;
#endif
    if (!initialized) {
        std::fputs("ERROR model-init\n", wire);
        std::fflush(wire);
        return 4;
    }
    std::fputs("READY\n", wire);
    std::fflush(wire);

    std::string header;
    std::vector<unsigned char> pixels, labels;
    while (std::getline(std::cin, header)) {
        unsigned int sequence;
        int width, height;
        std::istringstream fields(header);
        if (!(fields >> sequence >> width >> height) || width < 16 || height < 16
            || width > 1280 || height > 1280)
            break;
        pixels.resize(static_cast<size_t>(width) * height * 3);
        if (!std::cin.read(reinterpret_cast<char *>(pixels.data()), pixels.size()))
            break;
        image_buffer_t image{};
        image.width = image.width_stride = width;
        image.height = image.height_stride = height;
        image.format = IMAGE_FORMAT_RGB888;
        image.virt_addr = pixels.data();
        image.size = static_cast<int>(pixels.size());
        image.fd = -1;

        object_detect_result_list results{};
        const auto started = std::chrono::steady_clock::now();
        const auto status = infer(context, image, results);
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        if (status != 0 || results.count < 0 || results.count > static_cast<int>(std::size(results.results))) {
            std::fputs("ERROR inference\n", wire);
        } else {
            const int count = std::min(results.count, 128);
            copyMasks(results, count, width, height, labels);
            std::fprintf(wire, "F %u %lld %d %zu\n", sequence,
                         static_cast<long long>(micros), count, labels.size());
            for (int i = 0; i < count; ++i) {
                const auto &row = results.results[i];
                std::fprintf(wire, "%d %.6f %d %d %d %d", row.cls_id, row.prop,
                             row.box.left, row.box.top, row.box.right, row.box.bottom);
#ifdef FIELDOPS_YOLOV8_POSE
                for (const auto &point : row.keypoints)
                    std::fprintf(wire, " %.3f %.3f %.4f", point[0], point[1], point[2]);
#elif defined(FIELDOPS_YOLO26_POSE)
                for (int keypoint = 0; keypoint < OBJ_KEYPOINT_NUM; ++keypoint) {
                    const auto &point = row.keypoints[keypoint];
                    std::fprintf(wire, " %.3f %.3f %.4f", point.x_float, point.y_float,
                                 keypoint < row.keypoint_count ? point.prop : 0.f);
                }
#endif
                std::fputc('\n', wire);
            }
            if (!labels.empty())
                std::fwrite(labels.data(), 1, labels.size(), wire);
        }
#ifdef FIELDOPS_YOLO26_SEGMENT
        release_object_detect_result_list(&results);
#elif defined(FIELDOPS_YOLOV8_SEG)
        std::free(results.results_seg[0].seg_mask);
#endif
        if (std::fflush(wire) != 0 || status != 0)
            break;
    }
    RELEASE_MODEL(&context);
    deinit_post_process();
    std::fclose(wire);
    return 0;
}
